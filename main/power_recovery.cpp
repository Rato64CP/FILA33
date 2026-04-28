// power_recovery.cpp - Boot recovery, kontrolirano gasenje i watchdog integracija
// Pouzdanost toranjskog sata 24/7 ostvaruje se ovako:
// - EEPROM pamti stanje kroz rotirajuce wear-leveling slotove
// - nakon gubitka napajanja sustav pokusava automatski recovery
// - podrzano je kontrolirano gasenje uz vanjsku dojavu nestanka napajanja
// - provjera valjanosti stanja i osnovne provjere zdravlja EEPROM-a

#include <Arduino.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>
#include <string.h>
#include "power_recovery.h"
#include "podesavanja_piny.h"
#include "i2c_eeprom.h"
#include "time_glob.h"
#include "kazaljke_sata.h"
#include "okretna_ploca.h"
#include "pc_serial.h"
#include "watchdog.h"
#include "lcd_display.h"
#include "unified_motion_state.h"
#include "zvonjenje.h"
#include "otkucavanje.h"
#include "slavljenje_mrtvacko.h"

// ==================== POWER RECOVERY EEPROM LAYOUT ====================

namespace PowerRecoveryLayout {
constexpr int BAZA_BOOT_FLAGS = EepromLayout::BAZA_BOOT_FLAGS;
constexpr int SLOTOVI_BOOT_FLAGS = EepromLayout::SLOTOVI_BOOT_FLAGS;
constexpr int BAZA_EEPROM_DIJAGNOSTIKA = EepromLayout::BAZA_EEPROM_DIJAGNOSTIKA;
constexpr uint8_t HAND_NEAKTIVNO = 0;
constexpr uint8_t HAND_RELEJ_NIJEDAN = 0;
constexpr uint8_t PLATE_FAZA_STABILNO = 0;
constexpr uint8_t BROJ_POZICIJA_PLOCE = 64;
constexpr uint16_t BROJ_MINUTA_CIKLUS = 720;
constexpr uint32_t LEGACY_MIN_UNIX_TIMESTAMP = 1000000000UL;
constexpr uint32_t EEPROM_DIJAGNOSTICKI_POTPIS = 0x12345678UL;
constexpr int BAZA_LATCHED_FAULT = EepromLayout::BAZA_LATCHED_FAULT;
constexpr uint16_t LATCHED_FAULT_POTPIS = EepromLayout::LATCHED_FAULT_POTPIS;
constexpr uint8_t LATCHED_FAULT_NONE = EepromLayout::LATCHED_FAULT_NONE;
constexpr uint8_t LATCHED_FAULT_EEPROM = EepromLayout::LATCHED_FAULT_EEPROM;
constexpr int BAZA_WATCHDOG_SAFE_MODE = EepromLayout::BAZA_WATCHDOG_SAFE_MODE;
constexpr uint16_t WATCHDOG_SAFE_MODE_POTPIS = EepromLayout::WATCHDOG_SAFE_MODE_POTPIS;
constexpr uint8_t WATCHDOG_RESET_LOCKDOWN_PRAG = 3;
constexpr uint32_t WATCHDOG_RESET_PROZOR_SEKUNDE = 600UL;
constexpr unsigned long RUNTIME_EEPROM_HEALTH_INTERVAL_MS = 6UL * 60UL * 60UL * 1000UL;
constexpr uint16_t MIN_VALJANA_GODINA_SAFE_MODE = 2024;
constexpr uint16_t MAX_VALJANA_GODINA_SAFE_MODE = 2099;
}

// ==================== STATE VARIABLES ====================

static bool watchdog_reset = false;
static bool power_loss_detected = false;
static bool boot_recovery_odraden = false;
static uint32_t save_sequence = 1;
using SystemStateBackup = EepromLayout::SystemStateBackup;
using WatchdogSafeModeState = EepromLayout::WatchdogSafeModeState;
using LatchedFaultState = EepromLayout::LatchedFaultState;
static bool safe_mode_aktivan = false;
static bool safe_mode_primijenjen = false;
static bool latched_fault_aktivan = false;
static uint8_t latched_fault_kod = PowerRecoveryLayout::LATCHED_FAULT_NONE;
static bool eeprom_degraded_aktivan = false;
static unsigned long zadnja_runtime_eeprom_provjera_ms = 0;
static bool prijavljena_pauza_eeprom_degraded = false;

static uint16_t izracunajChecksum(const SystemStateBackup& stanje) {
  uint16_t checksum = 0;
  checksum += (stanje.hand_position_k_minuta >> 16) & 0xFFFF;
  checksum += stanje.hand_position_k_minuta & 0xFFFF;
  checksum += (stanje.plate_position >> 16) & 0xFFFF;
  checksum += stanje.plate_position & 0xFFFF;
  checksum += (stanje.rtc_timestamp >> 16) & 0xFFFF;
  checksum += stanje.rtc_timestamp & 0xFFFF;
  return checksum;
}

static bool jeStanjeValidno(const SystemStateBackup& stanje) {
  return stanje.checksum == izracunajChecksum(stanje);
}

static bool jeLegacyBackupPoVremenu(const SystemStateBackup& stanje) {
  return stanje.rtc_timestamp >= PowerRecoveryLayout::LEGACY_MIN_UNIX_TIMESTAMP;
}

static uint16_t izracunajChecksumWatchdogSafeMode(const WatchdogSafeModeState& stanje) {
  uint16_t checksum = stanje.potpis;
  checksum += stanje.brojWatchdogResetova;
  checksum += stanje.lockdownAktivan;
  checksum += static_cast<uint16_t>((stanje.pocetakProzoraUnix >> 16) & 0xFFFF);
  checksum += static_cast<uint16_t>(stanje.pocetakProzoraUnix & 0xFFFF);
  checksum += static_cast<uint16_t>((stanje.zadnjiWatchdogUnix >> 16) & 0xFFFF);
  checksum += static_cast<uint16_t>(stanje.zadnjiWatchdogUnix & 0xFFFF);
  return checksum;
}

static uint16_t izracunajChecksumLatchedFault(const LatchedFaultState& stanje) {
  uint16_t checksum = stanje.potpis;
  checksum += stanje.aktivan;
  checksum += stanje.kod;
  checksum += static_cast<uint16_t>((stanje.zadnjiFaultUnix >> 16) & 0xFFFF);
  checksum += static_cast<uint16_t>(stanje.zadnjiFaultUnix & 0xFFFF);
  return checksum;
}

static void postaviPocetnoWatchdogSafeModeStanje(WatchdogSafeModeState& stanje) {
  memset(&stanje, 0, sizeof(stanje));
  stanje.potpis = PowerRecoveryLayout::WATCHDOG_SAFE_MODE_POTPIS;
  stanje.checksum = izracunajChecksumWatchdogSafeMode(stanje);
}

static bool jeWatchdogSafeModeStanjeValjano(const WatchdogSafeModeState& stanje) {
  return stanje.potpis == PowerRecoveryLayout::WATCHDOG_SAFE_MODE_POTPIS &&
         stanje.checksum == izracunajChecksumWatchdogSafeMode(stanje);
}

static bool ucitajWatchdogSafeModeStanje(WatchdogSafeModeState& stanje) {
  if (!VanjskiEEPROM::procitaj(PowerRecoveryLayout::BAZA_WATCHDOG_SAFE_MODE,
                               &stanje,
                               sizeof(stanje))) {
    postaviPocetnoWatchdogSafeModeStanje(stanje);
    return false;
  }

  if (!jeWatchdogSafeModeStanjeValjano(stanje)) {
    postaviPocetnoWatchdogSafeModeStanje(stanje);
    return false;
  }

  return true;
}

static void postaviPocetnoLatchedFaultStanje(LatchedFaultState& stanje) {
  memset(&stanje, 0, sizeof(stanje));
  stanje.potpis = PowerRecoveryLayout::LATCHED_FAULT_POTPIS;
  stanje.kod = PowerRecoveryLayout::LATCHED_FAULT_NONE;
  stanje.checksum = izracunajChecksumLatchedFault(stanje);
}

static bool jeLatchedFaultStanjeValjano(const LatchedFaultState& stanje) {
  return stanje.potpis == PowerRecoveryLayout::LATCHED_FAULT_POTPIS &&
         stanje.checksum == izracunajChecksumLatchedFault(stanje);
}

static bool ucitajLatchedFaultStanje(LatchedFaultState& stanje) {
  if (!VanjskiEEPROM::procitaj(PowerRecoveryLayout::BAZA_LATCHED_FAULT,
                               &stanje,
                               sizeof(stanje))) {
    postaviPocetnoLatchedFaultStanje(stanje);
    return false;
  }

  if (!jeLatchedFaultStanjeValjano(stanje)) {
    postaviPocetnoLatchedFaultStanje(stanje);
    return false;
  }

  return true;
}

static bool spremiLatchedFaultStanje(LatchedFaultState& stanje) {
  stanje.potpis = PowerRecoveryLayout::LATCHED_FAULT_POTPIS;
  stanje.checksum = izracunajChecksumLatchedFault(stanje);
  return VanjskiEEPROM::zapisi(PowerRecoveryLayout::BAZA_LATCHED_FAULT,
                               &stanje,
                               sizeof(stanje));
}

static bool spremiWatchdogSafeModeStanje(WatchdogSafeModeState& stanje) {
  stanje.potpis = PowerRecoveryLayout::WATCHDOG_SAFE_MODE_POTPIS;
  stanje.checksum = izracunajChecksumWatchdogSafeMode(stanje);
  return VanjskiEEPROM::zapisi(PowerRecoveryLayout::BAZA_WATCHDOG_SAFE_MODE,
                               &stanje,
                               sizeof(stanje));
}

static bool jeRtcVrijemeValjanoZaSafeMode(const DateTime& vrijeme) {
  return vrijeme.unixtime() != 0 &&
         vrijeme.year() >= PowerRecoveryLayout::MIN_VALJANA_GODINA_SAFE_MODE &&
         vrijeme.year() <= PowerRecoveryLayout::MAX_VALJANA_GODINA_SAFE_MODE;
}

static uint32_t dohvatiTrenutniUnixSafeMode() {
  const DateTime trenutnoVrijeme = dohvatiTrenutnoVrijeme();
  return jeRtcVrijemeValjanoZaSafeMode(trenutnoVrijeme)
             ? trenutnoVrijeme.unixtime()
             : 0UL;
}

static uint32_t dohvatiValjaniUnixZaDijagnostiku() {
  return dohvatiTrenutniUnixSafeMode();
}

static void ucitajLatchedFaultPriBootu() {
  LatchedFaultState stanje{};
  const bool stanjeUcitano = ucitajLatchedFaultStanje(stanje);
  latched_fault_aktivan = stanje.aktivan != 0;
  latched_fault_kod = stanje.kod;
  eeprom_degraded_aktivan =
      latched_fault_aktivan &&
      latched_fault_kod == PowerRecoveryLayout::LATCHED_FAULT_EEPROM;

  if (!stanjeUcitano) {
    posaljiPCLog(F("Power Recovery: latched fault zapis inicijaliziran"));
    return;
  }

  if (latched_fault_aktivan &&
      latched_fault_kod == PowerRecoveryLayout::LATCHED_FAULT_EEPROM) {
    signalizirajLatchedFaultEEPROM();
    posaljiPCLog(F("Power Recovery: ucitan latched EEPROM fault iz EEPROM-a"));
  }
}

static void aktivirajLatchedFaultEEPROM(const __FlashStringHelper* razlog) {
  LatchedFaultState stanje{};
  ucitajLatchedFaultStanje(stanje);
  stanje.aktivan = 1;
  stanje.kod = PowerRecoveryLayout::LATCHED_FAULT_EEPROM;
  stanje.zadnjiFaultUnix = dohvatiValjaniUnixZaDijagnostiku();

  latched_fault_aktivan = true;
  latched_fault_kod = stanje.kod;
  eeprom_degraded_aktivan = true;
  signalizirajLatchedFaultEEPROM();

  if (!spremiLatchedFaultStanje(stanje)) {
    posaljiPCLog(F("Power Recovery: WARNING - latched fault nije spremljen u EEPROM"));
    signalizirajError_EEPROM();
    return;
  }

  posaljiPCLog(razlog);
}

static void obradiWatchdogSafeModePriBootu() {
  WatchdogSafeModeState stanje{};
  const bool stanjeUcitano = ucitajWatchdogSafeModeStanje(stanje);
  safe_mode_aktivan = stanje.lockdownAktivan != 0;

  if (safe_mode_aktivan) {
    posaljiPCLog(F("Power Recovery: Safe mode vec aktivan iz EEPROM-a"));
  } else if (!stanjeUcitano) {
    posaljiPCLog(F("Power Recovery: Watchdog safe-mode zapis inicijaliziran"));
  }

  if (!watchdog_reset) {
    return;
  }

  if (power_loss_detected) {
    posaljiPCLog(F("Power Recovery: Watchdog reset nije brojio za lockdown jer je prisutan i gubitak napajanja"));
    return;
  }

  const uint32_t sadaUnix = dohvatiTrenutniUnixSafeMode();
  if (sadaUnix == 0UL) {
    posaljiPCLog(F("Power Recovery: Watchdog reset nije brojio za lockdown jer RTC vrijeme nije valjano"));
    return;
  }

  const bool trebaNoviProzor =
      stanje.pocetakProzoraUnix == 0UL ||
      stanje.zadnjiWatchdogUnix == 0UL ||
      sadaUnix < stanje.pocetakProzoraUnix ||
      (sadaUnix - stanje.pocetakProzoraUnix) >
          PowerRecoveryLayout::WATCHDOG_RESET_PROZOR_SEKUNDE;

  if (trebaNoviProzor) {
    stanje.brojWatchdogResetova = 1;
    stanje.pocetakProzoraUnix = sadaUnix;
  } else if (stanje.brojWatchdogResetova < 255U) {
    ++stanje.brojWatchdogResetova;
  }

  stanje.zadnjiWatchdogUnix = sadaUnix;

  if (stanje.brojWatchdogResetova >
      PowerRecoveryLayout::WATCHDOG_RESET_LOCKDOWN_PRAG) {
    stanje.lockdownAktivan = 1;
    safe_mode_aktivan = true;
    posaljiPCLog(F("Power Recovery: LOCKDOWN aktiviran zbog previse watchdog resetova unutar 10 minuta"));
  }

  if (!spremiWatchdogSafeModeStanje(stanje)) {
    posaljiPCLog(F("Power Recovery: WARNING - watchdog safe-mode stanje nije spremljeno"));
    signalizirajError_EEPROM();
    return;
  }

  char logSafeMode[112];
  snprintf_P(
      logSafeMode,
      sizeof(logSafeMode),
      PSTR("Power Recovery: watchdog prozor start=%lu zadnji=%lu broj_resetova=%u lockdown=%s"),
      stanje.pocetakProzoraUnix,
      stanje.zadnjiWatchdogUnix,
      stanje.brojWatchdogResetova,
      stanje.lockdownAktivan ? "DA" : "NE");
  posaljiPCLog(logSafeMode);
}

bool ucitajNajnovijiBackup(SystemStateBackup& backup, int* slotNajnoviji = nullptr) {
  bool pronadeno = false;
  bool pronadenSekvencijskiBackup = false;
  uint32_t najboljaVrijednost = 0;
  int najboljiSlot = -1;

  for (int slot = 0; slot < PowerRecoveryLayout::SLOTOVI_BOOT_FLAGS; ++slot) {
    SystemStateBackup kandidat{};
    const int adresa = PowerRecoveryLayout::BAZA_BOOT_FLAGS + slot * static_cast<int>(sizeof(SystemStateBackup));
    if (!VanjskiEEPROM::procitaj(adresa, &kandidat, sizeof(SystemStateBackup)) || !jeStanjeValidno(kandidat)) {
      continue;
    }

    const bool kandidatJeLegacy = jeLegacyBackupPoVremenu(kandidat);
    if (kandidatJeLegacy && pronadenSekvencijskiBackup) {
      continue;
    }

    if (!kandidatJeLegacy && !pronadenSekvencijskiBackup) {
      pronadenSekvencijskiBackup = true;
      pronadeno = false;
      najboljaVrijednost = 0;
      najboljiSlot = -1;
    }

    if (!pronadeno || kandidat.rtc_timestamp >= najboljaVrijednost) {
      backup = kandidat;
      najboljaVrijednost = kandidat.rtc_timestamp;
      najboljiSlot = slot;
      pronadeno = true;
    }
  }

  if (pronadeno && slotNajnoviji != nullptr) {
    *slotNajnoviji = najboljiSlot;
  }
  return pronadeno;
}

void odradiBootRecovery() {
  if (boot_recovery_odraden) {
    posaljiPCLog(F("Power Recovery: Boot recovery već odrađen, preskačem"));
    return;
  }
  boot_recovery_odraden = true;

  posaljiPCLog(F("Power Recovery: Boot recovery sequence started"));

  if (!watchdog_reset && !power_loss_detected) {
    posaljiPCLog(F("Power Recovery: Normal boot (no watchdog/power loss)"));
    return;
  }

  SystemStateBackup backup{};
  int ucitaniSlot = -1;
  const bool stanjeUcitano = ucitajNajnovijiBackup(backup, &ucitaniSlot);
  if (stanjeUcitano) {
    char log[80];
    snprintf_P(log, sizeof(log), PSTR("Power Recovery: Valid state loaded from slot %d %s%lu"),
               ucitaniSlot,
               jeLegacyBackupPoVremenu(backup) ? "ts=" : "seq=",
               backup.rtc_timestamp);
    posaljiPCLog(log);
  }

  if (!stanjeUcitano) {
    posaljiPCLog(F("Power Recovery: No valid state found, using RTC defaults"));
    return;
  }

  EepromLayout::UnifiedMotionState jedinstvenoStanje{};
  if (UnifiedMotionStateStore::ucitaj(jedinstvenoStanje)) {
    if (jedinstvenoStanje.hand_active != PowerRecoveryLayout::HAND_NEAKTIVNO) {
      jedinstvenoStanje.hand_active = PowerRecoveryLayout::HAND_NEAKTIVNO;
      jedinstvenoStanje.hand_relay = PowerRecoveryLayout::HAND_RELEJ_NIJEDAN;
      jedinstvenoStanje.hand_start_ms = 0;
      UnifiedMotionStateStore::spremiAkoPromjena(jedinstvenoStanje);
      posaljiPCLog(F("Power Recovery: Prekinuti impuls kazaljki vracen u mirno stanje iz iste pozicije"));
    }
    if (jedinstvenoStanje.plate_phase != PowerRecoveryLayout::PLATE_FAZA_STABILNO) {
      jedinstvenoStanje.plate_phase = PowerRecoveryLayout::PLATE_FAZA_STABILNO;
      UnifiedMotionStateStore::spremiAkoPromjena(jedinstvenoStanje);
      posaljiPCLog(F("Power Recovery: Prekinuti korak ploce vracen u mirno stanje iz iste pozicije"));
    }
    posaljiPCLog(F("Power Recovery: Zadrzavam novije jedinstveno stanje kretanja"));
  } else {
    if (backup.hand_position_k_minuta < 720UL) {
      postaviTrenutniPolozajKazaljki(static_cast<int>(backup.hand_position_k_minuta));
    }
    if (backup.plate_position <= 63UL) {
      postaviTrenutniPolozajPloce(static_cast<int>(backup.plate_position));
    }
    posaljiPCLog(F("Power Recovery: Vraceno stanje iz periodickog backupa"));
  }

  posaljiPCLog(F("Power Recovery: Boot recovery completed"));
}

void spremiKriticalnoStanje() {
  static unsigned long last_save = 0;
  static bool inicijaliziranSaveSlot = false;
  static uint8_t save_slot = 0;
  static bool prijavljenaPauzaSpremanja = false;
  const unsigned long sada = millis();
  static const unsigned long SAVE_INTERVAL = 60000UL;

  if (!inicijaliziranSaveSlot) {
    SystemStateBackup zadnjiBackup{};
    int zadnjiSlot = -1;
    if (ucitajNajnovijiBackup(zadnjiBackup, &zadnjiSlot) && zadnjiSlot >= 0) {
      save_slot = static_cast<uint8_t>((zadnjiSlot + 1) % PowerRecoveryLayout::SLOTOVI_BOOT_FLAGS);
      if (!jeLegacyBackupPoVremenu(zadnjiBackup) && zadnjiBackup.rtc_timestamp > 0UL) {
        save_sequence = zadnjiBackup.rtc_timestamp + 1UL;
      }
    }
    inicijaliziranSaveSlot = true;
  }

  if ((sada - last_save) < SAVE_INTERVAL) {
    return;
  }

  if (eeprom_degraded_aktivan) {
    if (!prijavljena_pauza_eeprom_degraded) {
      posaljiPCLog(F("Power Recovery: spremanje pauzirano jer je EEPROM u degradiranom nacinu rada"));
      prijavljena_pauza_eeprom_degraded = true;
    }
    return;
  }

  if (!jeVrijemePotvrdjenoZaAutomatiku()) {
    if (!prijavljenaPauzaSpremanja) {
      posaljiPCLog(F("Power Recovery: spremanje pauzirano dok vrijeme toranjskog sata nije potvrdeno"));
      prijavljenaPauzaSpremanja = true;
    }
    return;
  }

  prijavljenaPauzaSpremanja = false;
  prijavljena_pauza_eeprom_degraded = false;
  last_save = sada;

  SystemStateBackup backup;
  backup.hand_position_k_minuta = static_cast<uint32_t>(dohvatiMemoriraneKazaljkeMinuta());
  backup.plate_position = static_cast<uint32_t>(dohvatiPozicijuPloce());
  // Novi backup koristi monotono rastucu sekvencu kako recovery toranjskog sata
  // ne bi ovisio o tome je li RTC/NTP vrijeme naknadno vraceno unatrag.
  backup.rtc_timestamp = save_sequence;
  backup.checksum = izracunajChecksum(backup);

  const int adresa = PowerRecoveryLayout::BAZA_BOOT_FLAGS + save_slot * static_cast<int>(sizeof(SystemStateBackup));

  if (VanjskiEEPROM::zapisi(adresa, &backup, sizeof(SystemStateBackup))) {
    save_slot = (save_slot + 1) % PowerRecoveryLayout::SLOTOVI_BOOT_FLAGS;
    save_sequence = (save_sequence == 0xFFFFFFFFUL) ? 1UL : (save_sequence + 1UL);
  }
}

bool provjeriZdravostEEPROM() {
  uint32_t read_back = 0;
  const uint32_t test_value = PowerRecoveryLayout::EEPROM_DIJAGNOSTICKI_POTPIS;
  const int test_adresa = PowerRecoveryLayout::BAZA_EEPROM_DIJAGNOSTIKA;

  if (!VanjskiEEPROM::procitaj(test_adresa, &read_back, sizeof(read_back))) {
    return false;
  }

  if (read_back == test_value) {
    return true;
  }

  if (!VanjskiEEPROM::zapisi(test_adresa, &test_value, sizeof(test_value))) {
    return false;
  }
  if (!VanjskiEEPROM::procitaj(test_adresa, &read_back, sizeof(read_back))) {
    return false;
  }
  return read_back == test_value;
}

void osvjeziPowerRecoveryDijagnostiku() {
  if (eeprom_degraded_aktivan) {
    return;
  }

  const unsigned long sadaMs = millis();
  if ((sadaMs - zadnja_runtime_eeprom_provjera_ms) <
      PowerRecoveryLayout::RUNTIME_EEPROM_HEALTH_INTERVAL_MS) {
    return;
  }

  zadnja_runtime_eeprom_provjera_ms = sadaMs;

  if (!provjeriZdravostEEPROM()) {
    aktivirajLatchedFaultEEPROM(
        F("Power Recovery: runtime EEPROM health-check nije prosao, fault latched"));
    return;
  }

  posaljiPCLog(F("Power Recovery: runtime EEPROM health-check uspjesan"));
}

bool jeLatchedFaultAktivan() {
  return latched_fault_aktivan;
}

bool potvrdiLatchedFault() {
  if (!latched_fault_aktivan) {
    return true;
  }

  LatchedFaultState stanje{};
  ucitajLatchedFaultStanje(stanje);
  stanje.aktivan = 0;
  stanje.kod = PowerRecoveryLayout::LATCHED_FAULT_NONE;
  stanje.zadnjiFaultUnix = dohvatiValjaniUnixZaDijagnostiku();

  if (!spremiLatchedFaultStanje(stanje)) {
    posaljiPCLog(F("Power Recovery: WARNING - nije moguce potvrditi latched fault"));
    signalizirajError_EEPROM();
    return false;
  }

  latched_fault_aktivan = false;
  latched_fault_kod = PowerRecoveryLayout::LATCHED_FAULT_NONE;
  eeprom_degraded_aktivan = false;
  prijavljena_pauza_eeprom_degraded = false;
  zadnja_runtime_eeprom_provjera_ms = millis();
  potvrdiLatchedFaultEEPROM();
  posaljiPCLog(F("Power Recovery: operater potvrdio latched fault"));
  return true;
}

bool jeEepromDegradiraniNacinAktivan() {
  return eeprom_degraded_aktivan;
}

bool jeSafeModeAktivan() {
  return safe_mode_aktivan;
}

static void primijeniSafeModeBlokadeMehanike(bool aktivno) {
  postaviGlobalnuBlokaduZvona(aktivno);
  postaviGlobalnuBlokaduOtkucavanja(aktivno);
  postaviRucnuBlokaduKazaljki(aktivno);
  postaviRucnuBlokaduPloce(aktivno);
}

void primijeniSafeModeAkoTreba() {
  if (!safe_mode_aktivan) {
    return;
  }

  primijeniSafeModeBlokadeMehanike(true);
  zaustaviSlavljenje();
  zaustaviMrtvacko();

  if (!safe_mode_primijenjen) {
    posaljiPCLog(F("Power Recovery: Safe mode blokade primijenjene na mehaniku toranjskog sata"));
    safe_mode_primijenjen = true;
  }
}

bool otkljucajSafeMode() {
  WatchdogSafeModeState stanje{};
  ucitajWatchdogSafeModeStanje(stanje);
  stanje.brojWatchdogResetova = 0;
  stanje.lockdownAktivan = 0;
  stanje.pocetakProzoraUnix = 0UL;
  stanje.zadnjiWatchdogUnix = 0UL;

  if (!spremiWatchdogSafeModeStanje(stanje)) {
    posaljiPCLog(F("Power Recovery: WARNING - neuspjelo servisno otkljucavanje safe mode-a"));
    signalizirajError_EEPROM();
    return false;
  }

  safe_mode_aktivan = false;
  safe_mode_primijenjen = false;
  watchdog_reset = false;

  primijeniSafeModeBlokadeMehanike(false);

  posaljiPCLog(F("Power Recovery: Safe mode servisno otkljucan"));
  return true;
}

void inicijalizirajPowerRecovery() {
  boot_recovery_odraden = false;
  safe_mode_primijenjen = false;
  watchdog_reset = jeWatchdogResetDetektiran();
  power_loss_detected = jePowerLossResetDetektiran();
  zadnja_runtime_eeprom_provjera_ms = millis();

  char logReset[96];
  snprintf_P(logReset, sizeof(logReset),
             PSTR("Power Recovery: reset flags MCUSR=0x%X watchdog=%s power_loss=%s"),
             dohvatiResetFlags(),
             watchdog_reset ? "DA" : "NE",
             power_loss_detected ? "DA" : "NE");
  posaljiPCLog(logReset);

  ucitajLatchedFaultPriBootu();

  if (!provjeriZdravostEEPROM()) {
    aktivirajLatchedFaultEEPROM(
        F("Power Recovery: boot EEPROM health-check nije prosao, fault latched"));
    signalizirajError_EEPROM();
  }

  obradiWatchdogSafeModePriBootu();

  spremiKriticalnoStanje();
}
