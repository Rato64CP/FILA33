// power_recovery.cpp – Boot recovery, graceful shutdown, watchdog integration
// Implements 24/7 reliability with:
// - EEPROM-based state persistence (6 wear-leveling slots per data)
// - Automatic recovery after power loss
// - Graceful shutdown support (requires external power loss detection)
// - State validation and EEPROM health checks

#include <Arduino.h>
#include <avr/interrupt.h>
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

// ==================== POWER RECOVERY EEPROM LAYOUT ====================

namespace PowerRecoveryLayout {
constexpr int BAZA_BOOT_FLAGS = EepromLayout::BAZA_BOOT_FLAGS;
constexpr int SLOTOVI_BOOT_FLAGS = EepromLayout::SLOTOVI_BOOT_FLAGS;
constexpr uint8_t HAND_NEAKTIVNO = 0;
constexpr uint8_t HAND_RELEJ_NIJEDAN = 0;
constexpr uint16_t BROJ_MINUTA_CIKLUS = 720;
}

// ==================== STATE VARIABLES ====================

static bool watchdog_reset = false;
static bool power_loss_detected = false;
static unsigned long boot_time = 0;
static unsigned long last_state_save_time = 0;
static uint32_t reset_counter = 0;
static bool boot_recovery_odraden = false;

struct SystemStateBackup {
  uint32_t hand_position_k_minuta;
  uint32_t plate_position;
  uint32_t offset_minuta;
  uint32_t rtc_timestamp;
  uint16_t checksum;
};

static uint16_t izracunajChecksum(const SystemStateBackup& stanje) {
  uint16_t checksum = 0;
  checksum += (stanje.hand_position_k_minuta >> 16) & 0xFFFF;
  checksum += stanje.hand_position_k_minuta & 0xFFFF;
  checksum += (stanje.plate_position >> 16) & 0xFFFF;
  checksum += stanje.plate_position & 0xFFFF;
  checksum += (stanje.offset_minuta >> 16) & 0xFFFF;
  checksum += stanje.offset_minuta & 0xFFFF;
  checksum += (stanje.rtc_timestamp >> 16) & 0xFFFF;
  checksum += stanje.rtc_timestamp & 0xFFFF;
  return checksum;
}

static bool jeStanjeValidno(const SystemStateBackup& stanje) {
  return stanje.checksum == izracunajChecksum(stanje);
}

bool ucitajNajnovijiBackup(SystemStateBackup& backup, int* slotNajnoviji = nullptr) {
  bool pronadeno = false;
  uint32_t najnovijiTimestamp = 0;
  int najboljiSlot = -1;

  for (int slot = 0; slot < PowerRecoveryLayout::SLOTOVI_BOOT_FLAGS; ++slot) {
    SystemStateBackup kandidat{};
    const int adresa = PowerRecoveryLayout::BAZA_BOOT_FLAGS + slot * static_cast<int>(sizeof(SystemStateBackup));
    if (!VanjskiEEPROM::procitaj(adresa, &kandidat, sizeof(SystemStateBackup)) || !jeStanjeValidno(kandidat)) {
      continue;
    }

    if (!pronadeno || kandidat.rtc_timestamp >= najnovijiTimestamp) {
      backup = kandidat;
      najnovijiTimestamp = kandidat.rtc_timestamp;
      najboljiSlot = slot;
      pronadeno = true;
    }
  }

  if (pronadeno && slotNajnoviji != nullptr) {
    *slotNajnoviji = najboljiSlot;
  }
  return pronadeno;
}

void oznaciWatchdogReset(bool resetiranWatchdog) {
  watchdog_reset = resetiranWatchdog;
}

void oznaciGubitakNapajanja(bool izgubljenoNapajanje) {
  power_loss_detected = izgubljenoNapajanje;
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

  reset_counter++;

  SystemStateBackup backup{};
  int ucitaniSlot = -1;
  const bool stanjeUcitano = ucitajNajnovijiBackup(backup, &ucitaniSlot);
  if (stanjeUcitano) {
    String log = F("Power Recovery: Valid state loaded from slot ");
    log += ucitaniSlot;
    log += F(" ts=");
    log += backup.rtc_timestamp;
    posaljiPCLog(log);
  }

  if (!stanjeUcitano) {
    posaljiPCLog(F("Power Recovery: No valid state found, using RTC defaults"));
    return;
  }

  EepromLayout::UnifiedMotionState jedinstvenoStanje{};
  if (UnifiedMotionStateStore::ucitaj(jedinstvenoStanje)) {
    if (jedinstvenoStanje.hand_active != PowerRecoveryLayout::HAND_NEAKTIVNO) {
      jedinstvenoStanje.hand_position =
        static_cast<uint16_t>((jedinstvenoStanje.hand_position + 1) % PowerRecoveryLayout::BROJ_MINUTA_CIKLUS);
      jedinstvenoStanje.hand_active = PowerRecoveryLayout::HAND_NEAKTIVNO;
      jedinstvenoStanje.hand_relay = PowerRecoveryLayout::HAND_RELEJ_NIJEDAN;
      jedinstvenoStanje.hand_start_ms = 0;
      UnifiedMotionStateStore::spremiAkoPromjena(jedinstvenoStanje);
      posaljiPCLog(F("Power Recovery: Dovrsen prekinuti impuls kazaljki kao jedan korak"));
    }
    posaljiPCLog(F("Power Recovery: Zadrzavam novije jedinstveno stanje kretanja"));
  } else {
    if (backup.hand_position_k_minuta < 720UL) {
      postaviTrenutniPolozajKazaljki(static_cast<int>(backup.hand_position_k_minuta));
    }
    if (backup.plate_position <= 63UL) {
      postaviTrenutniPolozajPloce(static_cast<int>(backup.plate_position));
    }
    if (backup.offset_minuta <= 14UL) {
      postaviOffsetMinuta(static_cast<int>(backup.offset_minuta));
    }
    posaljiPCLog(F("Power Recovery: Vraceno stanje iz periodickog backupa"));
  }

  posaljiPCLog(F("Power Recovery: Boot recovery completed"));
}

void spremiKriticalnoStanje() {
  static unsigned long last_save = 0;
  static bool inicijaliziranSaveSlot = false;
  static uint8_t save_slot = 0;
  const unsigned long sada = millis();
  static const unsigned long SAVE_INTERVAL = 60000UL;

  if (!inicijaliziranSaveSlot) {
    SystemStateBackup zadnjiBackup{};
    int zadnjiSlot = -1;
    if (ucitajNajnovijiBackup(zadnjiBackup, &zadnjiSlot) && zadnjiSlot >= 0) {
      save_slot = static_cast<uint8_t>((zadnjiSlot + 1) % PowerRecoveryLayout::SLOTOVI_BOOT_FLAGS);
    }
    inicijaliziranSaveSlot = true;
  }

  if ((sada - last_save) < SAVE_INTERVAL) {
    return;
  }
  last_save = sada;

  SystemStateBackup backup;
  backup.hand_position_k_minuta = static_cast<uint32_t>(dohvatiMemoriraneKazaljkeMinuta());
  backup.plate_position = static_cast<uint32_t>(dohvatiPozicijuPloce());
  backup.offset_minuta = static_cast<uint32_t>(dohvatiOffsetMinuta());
  backup.rtc_timestamp = dohvatiTrenutnoVrijeme().unixtime();
  backup.checksum = izracunajChecksum(backup);

  const int adresa = PowerRecoveryLayout::BAZA_BOOT_FLAGS + save_slot * static_cast<int>(sizeof(SystemStateBackup));

  if (VanjskiEEPROM::zapisi(adresa, &backup, sizeof(SystemStateBackup))) {
    save_slot = (save_slot + 1) % PowerRecoveryLayout::SLOTOVI_BOOT_FLAGS;
    last_state_save_time = sada;
  }
}

bool provjeriZdravostEEPROM() {
  uint32_t test_value = 0x12345678UL;
  uint32_t read_back = 0;
  const int test_adresa = PowerRecoveryLayout::BAZA_BOOT_FLAGS;

  if (!VanjskiEEPROM::zapisi(test_adresa, &test_value, sizeof(test_value))) {
    return false;
  }
  if (!VanjskiEEPROM::procitaj(test_adresa, &read_back, sizeof(read_back))) {
    return false;
  }
  return read_back == test_value;
}

void inicijalizirajPowerRecovery() {
  boot_time = millis();
  last_state_save_time = boot_time;
  boot_recovery_odraden = false;
  watchdog_reset = jeWatchdogResetDetektiran();
  power_loss_detected = jePowerLossResetDetektiran();

  String logReset = F("Power Recovery: reset flags MCUSR=0x");
  logReset += String(dohvatiResetFlags(), HEX);
  logReset += F(" watchdog=");
  logReset += watchdog_reset ? F("DA") : F("NE");
  logReset += F(" power_loss=");
  logReset += power_loss_detected ? F("DA") : F("NE");
  posaljiPCLog(logReset);

  if (!provjeriZdravostEEPROM()) {
    posaljiPCLog(F("Power Recovery: WARNING - EEPROM health issues detected"));
    signalizirajError_EEPROM();
  }

  spremiKriticalnoStanje();
}
