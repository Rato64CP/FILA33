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

// ==================== POWER RECOVERY EEPROM LAYOUT ====================

namespace PowerRecoveryLayout {
constexpr int BAZA_BOOT_FLAGS = 500;
constexpr int SLOTOVI_HAND_POSITION = 6;
}

// ==================== STATE VARIABLES ====================

static bool watchdog_reset = false;
static bool power_loss_detected = false;
static unsigned long boot_time = 0;
static unsigned long last_state_save_time = 0;
static uint32_t reset_counter = 0;
static uint32_t uptime_counter = 0;

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

void oznaciWatchdogReset(bool resetiranWatchdog) {
  watchdog_reset = resetiranWatchdog;
}

void oznaciGubitakNapajanja(bool izgubljenoNapajanje) {
  power_loss_detected = izgubljenoNapajanje;
}

void povecajUptimeBrojac() {
  ++uptime_counter;
}

void odradiBootRecovery() {
  posaljiPCLog(F("Power Recovery: Boot recovery sequence started"));

  if (!watchdog_reset && !power_loss_detected) {
    posaljiPCLog(F("Power Recovery: Normal boot (no watchdog/power loss)"));
    return;
  }

  reset_counter++;

  SystemStateBackup backup;
  bool stanjeUcitano = false;

  for (int slot = PowerRecoveryLayout::SLOTOVI_HAND_POSITION - 1; slot >= 0; --slot) {
    int adresa = PowerRecoveryLayout::BAZA_BOOT_FLAGS + slot * static_cast<int>(sizeof(SystemStateBackup));
    if (VanjskiEEPROM::procitaj(adresa, &backup, sizeof(SystemStateBackup)) && jeStanjeValidno(backup)) {
      stanjeUcitano = true;
      String log = F("Power Recovery: Valid state loaded from slot ");
      log += slot;
      posaljiPCLog(log);
      break;
    }
  }

  if (!stanjeUcitano) {
    posaljiPCLog(F("Power Recovery: No valid state found, using RTC defaults"));
    return;
  }

  if (backup.hand_position_k_minuta < 720UL) {
    postaviTrenutniPolozajKazaljki(static_cast<int>(backup.hand_position_k_minuta));
  }
  if (backup.plate_position <= 63UL) {
    postaviTrenutniPolozajPloce(static_cast<int>(backup.plate_position));
  }
  if (backup.offset_minuta <= 14UL) {
    postaviOffsetMinuta(static_cast<int>(backup.offset_minuta));
  }

  posaljiPCLog(F("Power Recovery: Boot recovery completed"));
}

void spremiKriticalnoStanje() {
  static unsigned long last_save = 0;
  const unsigned long sada = millis();
  static const unsigned long SAVE_INTERVAL = 60000UL;

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

  static uint8_t save_slot = 0;
  const int adresa = PowerRecoveryLayout::BAZA_BOOT_FLAGS + save_slot * static_cast<int>(sizeof(SystemStateBackup));

  if (VanjskiEEPROM::zapisi(adresa, &backup, sizeof(SystemStateBackup))) {
    save_slot = (save_slot + 1) % PowerRecoveryLayout::SLOTOVI_HAND_POSITION;
    last_state_save_time = sada;
  }
}

void gracioznoGasenje() {
  posaljiPCLog(F("Power Recovery: Graceful shutdown initiated"));

  cli();

  digitalWrite(PIN_RELEJ_PARNE_KAZALJKE, LOW);
  digitalWrite(PIN_RELEJ_NEPARNE_KAZALJKE, LOW);
  digitalWrite(PIN_RELEJ_PARNE_PLOCE, LOW);
  digitalWrite(PIN_RELEJ_NEPARNE_PLOCE, LOW);
  digitalWrite(PIN_ZVONO_1, LOW);
  digitalWrite(PIN_ZVONO_2, LOW);
  digitalWrite(PIN_CEKIC_MUSKI, LOW);
  digitalWrite(PIN_CEKIC_ZENSKI, LOW);

  spremiKriticalnoStanje();
  delay(500);

  while (true) {
    delay(1000);
  }
}

bool jeSistemNakonWatchdogReseta() {
  return watchdog_reset;
}

bool jeSistemNakonGubickaNapajanja() {
  return power_loss_detected;
}

unsigned long dohvatiVrijemeZadnjegSpremanja() {
  return last_state_save_time;
}

unsigned long dohvatiSistUptimeSeconde() {
  return uptime_counter;
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

  if (!provjeriZdravostEEPROM()) {
    posaljiPCLog(F("Power Recovery: WARNING - EEPROM health issues detected"));
  }

  odradiBootRecovery();
  spremiKriticalnoStanje();
}
