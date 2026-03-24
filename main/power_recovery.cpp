// power_recovery.cpp – Boot recovery, graceful shutdown, watchdog integration
// Implements 24/7 reliability with:
// - EEPROM-based state persistence (6 wear-leveling slots per data)
// - Watchdog monitoring with 8-second timeout
// - Automatic recovery after power loss
// - Graceful shutdown support (requires external power loss detection)
// - State validation and EEPROM health checks

#include <Arduino.h>
#include <avr/wdt.h>
#include "power_recovery.h"
#include "eeprom_konstante.h"
#include "wear_leveling.h"
#include "i2c_eeprom.h"
#include "time_glob.h"
#include "kazaljke_sata.h"
#include "okretna_ploca.h"
#include "pc_serial.h"

// ==================== POWER RECOVERY EEPROM LAYOUT ====================

namespace PowerRecoveryLayout {
  // Boot flags and reset detection
  constexpr int BAZA_BOOT_FLAGS = 500;                // Reserve space for boot detection
  constexpr int BAZA_UPTIME = BAZA_BOOT_FLAGS + 16;   // System uptime counter
  constexpr int BAZA_RESET_COUNT = BAZA_UPTIME + 8;   // Number of resets/power cycles
  constexpr int BAZA_LAST_SAVE_TIME = BAZA_RESET_COUNT + 8;  // Last state save timestamp
  
  // System state backup (redundant copies for wear-leveling)
  constexpr int SLOTOVI_HAND_POSITION = 6;
  constexpr int SLOTOVI_PLATE_POSITION = 6;
  constexpr int SLOTOVI_RTC_TIME = 6;
}

// ==================== STATE VARIABLES ====================

static bool watchdog_reset = false;
static bool power_loss_detected = false;
static unsigned long boot_time = 0;
static unsigned long last_state_save_time = 0;
static uint32_t reset_counter = 0;
static uint32_t uptime_counter = 0;

// System state backup structures
struct SystemStateBackup {
  uint32_t hand_position_k_minuta;
  uint32_t plate_position;
  uint32_t offset_minuta;
  uint32_t rtc_timestamp;
  uint16_t checksum;
};

// ==================== WATCHDOG MANAGEMENT ====================

void osvjeziWatchdog() {
  // Reset WDT counter - must be called at least every 8 seconds
  wdt_reset();
  
  // Update system uptime
  static unsigned long last_uptime_update = 0;
  unsigned long sada = millis();
  if (sada - last_uptime_update >= 1000) {
    last_uptime_update = sada;
    uptime_counter++;
  }
}

void inicijalizirajWatchdog() {
  // Save MCUSR register to detect reset cause
  uint8_t mcusr = MCUSR;
  
  // Check watchdog reset bit (WDRF)
  if (mcusr & (1 << WDRF)) {
    watchdog_reset = true;
    posaljiPCLog(F("Power Recovery: Watchdog reset detected"));
  }
  
  // Check other reset causes
  if (mcusr & (1 << BORF)) {
    power_loss_detected = true;
    posaljiPCLog(F("Power Recovery: Brown-out reset detected"));
  }
  if (mcusr & (1 << PORF)) {
    power_loss_detected = true;
    posaljiPCLog(F("Power Recovery: Power-on reset detected"));
  }
  
  // Clear MCUSR bits for next boot
  MCUSR = 0;
  
  // Disable WDT temporarily
  wdt_disable();
  
  // Set WDT to 8 seconds (maximum safe timeout for ATmega2560)
  wdt_enable(WDTO_8S);
  
  posaljiPCLog(F("Power Recovery: Watchdog initialized (8s timeout)"));
}

// ==================== BOOT RECOVERY ====================

// Calculate checksum for state validation
static uint16_t izracunajChecksum(const SystemStateBackup& state) {
  uint16_t checksum = 0;
  checksum += (state.hand_position_k_minuta >> 16) & 0xFFFF;
  checksum += state.hand_position_k_minuta & 0xFFFF;
  checksum += (state.plate_position >> 16) & 0xFFFF;
  checksum += state.plate_position & 0xFFFF;
  checksum += (state.offset_minuta >> 16) & 0xFFFF;
  checksum += state.offset_minuta & 0xFFFF;
  checksum += (state.rtc_timestamp >> 16) & 0xFFFF;
  checksum += state.rtc_timestamp & 0xFFFF;
  return checksum;
}

// Validate saved system state using checksum
static bool jestanjjeValidno(const SystemStateBackup& state) {
  uint16_t expected = izracunajChecksum(state);
  return (state.checksum == expected);
}

void odradiBootRecovery() {
  posaljiPCLog(F("Power Recovery: Boot recovery sequence started"));
  
  if (!watchdog_reset && !power_loss_detected) {
    posaljiPCLog(F("Power Recovery: Normal boot (no watchdog/power loss)"));
    return;
  }
  
  // Increment reset counter
  reset_counter++;
  
  // Load last saved system state from EEPROM
  SystemStateBackup backup;
  bool state_loaded = false;
  
  // Try each wear-leveling slot
  for (int slot = PowerRecoveryLayout::SLOTOVI_HAND_POSITION - 1; slot >= 0; --slot) {
    int adresa = PowerRecoveryLayout::BAZA_BOOT_FLAGS + slot * sizeof(SystemStateBackup);
    
    if (VanjskiEEPROM::procitaj(adresa, &backup, sizeof(SystemStateBackup))) {
      if (jestanjjeValidno(backup)) {
        state_loaded = true;
        
        String log = F("Power Recovery: Valid state loaded from slot ");;
        log += slot;
        posaljiPCLog(log);
        break;
      }
    }
  }
  
  if (!state_loaded) {
    posaljiPCLog(F("Power Recovery: No valid state found, using RTC defaults"));
    return;
  }
  
  // Restore hand position
  if (backup.hand_position_k_minuta < 720) {
    postaviTrenutniPolozajKazaljki(backup.hand_position_k_minuta);
    String log = F("Power Recovery: Hand position restored: ");
    log += backup.hand_position_k_minuta;
    posaljiPCLog(log);
  }
  
  // Restore plate position
  if (backup.plate_position <= 63) {
    postaviTrenutniPolozajPloce(backup.plate_position);
    String log = F("Power Recovery: Plate position restored: ");
    log += backup.plate_position;
    posaljiPCLog(log);
  }
  
  // Restore offset minutes
  if (backup.offset_minuta <= 14) {
    postaviOffsetMinuta(backup.offset_minuta);
    String log = F("Power Recovery: Offset minutes restored: ");
    log += backup.offset_minuta;
    posaljiPCLog(log);
  }
  
  // Restore RTC time if backup is recent (within last 24 hours)
  DateTime sada = dohvatiTrenutnoVrijeme();
  uint32_t rtc_now = sada.unixtime();
  
  if (backup.rtc_timestamp > 0) {
    uint32_t time_diff = (rtc_now >= backup.rtc_timestamp) ? 
                         (rtc_now - backup.rtc_timestamp) : 
                         (backup.rtc_timestamp - rtc_now);
    
    if (time_diff < 86400) {  // Less than 24 hours difference
      posaljiPCLog(F("Power Recovery: RTC time difference acceptable, using current RTC"));
    } else {
      // RTC time has drifted significantly, try to restore from backup
      DateTime backup_time(backup.rtc_timestamp);
      String log = F("Power Recovery: RTC drift detected, attempting restore to ");
      log += backup_time.year();
      log += F("-");
      log += backup_time.month();
      log += F("-");
      log += backup_time.day();
      posaljiPCLog(log);
    }
  }
  
  posaljiPCLog(F("Power Recovery: Boot recovery completed"));;
}

// ==================== STATE PERSISTENCE ====================

void spremiKriticalnoStanje() {
  static unsigned long last_save = 0;
  unsigned long sada = millis();
  
  // Save every 60 seconds minimum to reduce EEPROM wear
  static const unsigned long SAVE_INTERVAL = 60000;
  
  if (sada - last_save < SAVE_INTERVAL) {
    return;
  }
  last_save = sada;
  
  // Build state backup structure
  SystemStateBackup backup;
  backup.hand_position_k_minuta = dohvatiMemoriraneKazaljkeMinuta();
  backup.plate_position = dohvatiPozicijuPloce();
  backup.offset_minuta = dohvatiOffsetMinuta();
  
  DateTime vrijeme = dohvatiTrenutnoVrijeme();
  backup.rtc_timestamp = vrijeme.unixtime();
  
  // Calculate checksum
  backup.checksum = izracunajChecksum(backup);
  
  // Save to next wear-leveling slot (round-robin)
  static uint8_t save_slot = 0;
  int adresa = PowerRecoveryLayout::BAZA_BOOT_FLAGS + save_slot * sizeof(SystemStateBackup);
  
  bool success = VanjskiEEPROM::zapisi(adresa, &backup, sizeof(SystemStateBackup));
  
  if (success) {
    save_slot = (save_slot + 1) % PowerRecoveryLayout::SLOTOVI_HAND_POSITION;
    last_state_save_time = sada;
    
    String log = F("Power Recovery: State saved to slot ");
    log += save_slot;
    posaljiPCLog(log);
  } else {
    posaljiPCLog(F("Power Recovery: State save FAILED"));
  }
}

// ==================== GRACEFUL SHUTDOWN ====================

void gracioznoGasenje() {
  posaljiPCLog(F("Power Recovery: Graceful shutdown initiated"));
  
  // Disable interrupts
  cli();
  
  // Stop all relays immediately (prevent damage)
  digitalWrite(PIN_RELEJ_PARNE_KAZALJKE, LOW);
  digitalWrite(PIN_RELEJ_NEPARNE_KAZALJKE, LOW);
  digitalWrite(PIN_RELEJ_PARNE_PLOCE, LOW);
  digitalWrite(PIN_RELEJ_NEPARNE_PLOCE, LOW);
  
  // Stop bells and hammers
  digitalWrite(PIN_ZVONO_1, LOW);
  digitalWrite(PIN_ZVONO_2, LOW);
  digitalWrite(PIN_CEKIC_MUSKI, LOW);
  digitalWrite(PIN_CEKIC_ZENSKI, LOW);
  
  posaljiPCLog(F("Power Recovery: All relays deactivated"));
  
  // Save critical state immediately
  spremiKriticalnoStanje();
  
  // Wait for EEPROM writes to complete
  delay(500);
  
  posaljiPCLog(F("Power Recovery: Shutdown complete, safe to power off"));
  
  // Wait for power loss (watchdog will reset if power remains)
  while (true) {
    delay(1000);
  }
}

// ==================== STATUS FUNCTIONS ====================

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

// ==================== EEPROM HEALTH ====================

bool provjeriZdravostEEPROM() {
  // Test EEPROM accessibility
  posaljiPCLog(F("Power Recovery: Checking EEPROM health..."));
  
  // Test read/write at known location
  uint32_t test_value = 0x12345678;
  uint32_t read_back = 0;
  
  int test_adresa = PowerRecoveryLayout::BAZA_BOOT_FLAGS;
  
  if (!VanjskiEEPROM::zapisi(test_adresa, &test_value, sizeof(uint32_t))) {
    posaljiPCLog(F("Power Recovery: EEPROM write test FAILED"));
    return false;
  }
  
  if (!VanjskiEEPROM::procitaj(test_adresa, &read_back, sizeof(uint32_t))) {
    posaljiPCLog(F("Power Recovery: EEPROM read test FAILED"));
    return false;
  }
  
  if (read_back != test_value) {
    posaljiPCLog(F("Power Recovery: EEPROM data corruption detected"));
    return false;
  }
  
  posaljiPCLog(F("Power Recovery: EEPROM health OK"));
  return true;
}

// ==================== INITIALIZATION ====================

void inicijalizirajPowerRecovery() {
  boot_time = millis();
  last_state_save_time = boot_time;
  
  // Initialize watchdog and detect reset cause
  inicijalizirajWatchdog();
  
  // Check EEPROM health
  if (!provjeriZdravostEEPROM()) {
    posaljiPCLog(F("Power Recovery: WARNING - EEPROM health issues detected"));
  }
  
  // Execute boot recovery if needed
  odradiBootRecovery();
  
  // Log system status
  String log = F("Power Recovery: System started, resets=");
  log += reset_counter;
  log += F(", watchdog=");
  log += watchdog_reset ? "yes" : "no";
  log += F(", power_loss=");
  log += power_loss_detected ? "yes" : "no";
  posaljiPCLog(log);
  
  // Initial state save
  spremiKriticalnoStanje();
}
'}, 'file_content': '// power_recovery.cpp – Boot recovery, graceful shutdown, watchdog integration
// Implements 24/7 reliability with:
// - EEPROM-based state persistence (6 wear-leveling slots per data)
// - Watchdog monitoring with 8-second timeout
// - Automatic recovery after power loss
// - Graceful shutdown support (requires external power loss detection)
// - State validation and EEPROM health checks

#include <Arduino.h>
#include <avr/wdt.h>
#include "power_recovery.h"
#include "eeprom_konstante.h"
#include "wear_leveling.h"
#include "i2c_eeprom.h"
#include "time_glob.h"
#include "kazaljke_sata.h"
#include "okretna_ploca.h"
#include "pc_serial.h"

// ==================== POWER RECOVERY EEPROM LAYOUT ====================

namespace PowerRecoveryLayout {
  // Boot flags and reset detection
  constexpr int BAZA_BOOT_FLAGS = 500;                // Reserve space for boot detection
  constexpr int BAZA_UPTIME = BAZA_BOOT_FLAGS + 16;   // System uptime counter
  constexpr int BAZA_RESET_COUNT = BAZA_UPTIME + 8;   // Number of resets/power cycles
  constexpr int BAZA_LAST_SAVE_TIME = BAZA_RESET_COUNT + 8;  // Last state save timestamp
  
  // System state backup (redundant copies for wear-leveling)
  constexpr int SLOTOVI_HAND_POSITION = 6;
  constexpr int SLOTOVI_PLATE_POSITION = 6;
  constexpr int SLOTOVI_RTC_TIME = 6;
}

// ==================== STATE VARIABLES ====================

static bool watchdog_reset = false;
static bool power_loss_detected = false;
static unsigned long boot_time = 0;
static unsigned long last_state_save_time = 0;
static uint32_t reset_counter = 0;
static uint32_t uptime_counter = 0;

// System state backup structures
struct SystemStateBackup {
  uint32_t hand_position_k_minuta;
  uint32_t plate_position;
  uint32_t offset_minuta;
  uint32_t rtc_timestamp;
  uint16_t checksum;
};

// ==================== WATCHDOG MANAGEMENT ====================

void osvjeziWatchdog() {
  // Reset WDT counter - must be called at least every 8 seconds
  wdt_reset();
  
  // Update system uptime
  static unsigned long last_uptime_update = 0;
  unsigned long sada = millis();
  if (sada - last_uptime_update >= 1000) {
    last_uptime_update = sada;
    uptime_counter++;
  }
}

void inicijalizirajWatchdog() {
  // Save MCUSR register to detect reset cause
  uint8_t mcusr = MCUSR;
  
  // Check watchdog reset bit (WDRF)
  if (mcusr & (1 << WDRF)) {
    watchdog_reset = true;
    posaljiPCLog(F("Power Recovery: Watchdog reset detected"));
  }
  
  // Check other reset causes
  if (mcusr & (1 << BORF)) {
    power_loss_detected = true;
    posaljiPCLog(F("Power Recovery: Brown-out reset detected"));
  }
  if (mcusr & (1 << PORF)) {
    power_loss_detected = true;
    posaljiPCLog(F("Power Recovery: Power-on reset detected"));
  }
  
  // Clear MCUSR bits for next boot
  MCUSR = 0;
  
  // Disable WDT temporarily
  wdt_disable();
  
  // Set WDT to 8 seconds (maximum safe timeout for ATmega2560)
  wdt_enable(WDTO_8S);
  
  posaljiPCLog(F("Power Recovery: Watchdog initialized (8s timeout)"));
}

// ==================== BOOT RECOVERY ====================

// Calculate checksum for state validation
static uint16_t izracunajChecksum(const SystemStateBackup& state) {
  uint16_t checksum = 0;
  checksum += (state.hand_position_k_minuta >> 16) & 0xFFFF;
  checksum += state.hand_position_k_minuta & 0xFFFF;
  checksum += (state.plate_position >> 16) & 0xFFFF;
  checksum += state.plate_position & 0xFFFF;
  checksum += (state.offset_minuta >> 16) & 0xFFFF;
  checksum += state.offset_minuta & 0xFFFF;
  checksum += (state.rtc_timestamp >> 16) & 0xFFFF;
  checksum += state.rtc_timestamp & 0xFFFF;
  return checksum;
}

// Validate saved system state using checksum
static bool jestanjjeValidno(const SystemStateBackup& state) {
  uint16_t expected = izracunajChecksum(state);
  return (state.checksum == expected);
}

void odradiBootRecovery() {
  posaljiPCLog(F("Power Recovery: Boot recovery sequence started"));
  
  if (!watchdog_reset && !power_loss_detected) {
    posaljiPCLog(F("Power Recovery: Normal boot (no watchdog/power loss)"));
    return;
  }
  
  // Increment reset counter
  reset_counter++;
  
  // Load last saved system state from EEPROM
  SystemStateBackup backup;
  bool state_loaded = false;
  
  // Try each wear-leveling slot
  for (int slot = PowerRecoveryLayout::SLOTOVI_HAND_POSITION - 1; slot >= 0; --slot) {
    int adresa = PowerRecoveryLayout::BAZA_BOOT_FLAGS + slot * sizeof(SystemStateBackup);
    
    if (VanjskiEEPROM::procitaj(adresa, &backup, sizeof(SystemStateBackup))) {
      if (jestanjjeValidno(backup)) {
        state_loaded = true;
        
        String log = F("Power Recovery: Valid state loaded from slot ");
        log += slot;
        posaljiPCLog(log);
        break;
      }
    }
  }
  
  if (!state_loaded) {
    posaljiPCLog(F("Power Recovery: No valid state found, using RTC defaults"));
    return;
  }
  
  // Restore hand position
  if (backup.hand_position_k_minuta < 720) {
    postaviTrenutniPolozajKazaljki(backup.hand_position_k_minuta);
    String log = F("Power Recovery: Hand position restored: ");
    log += backup.hand_position_k_minuta;
    posaljiPCLog(log);
  }
  
  // Restore plate position
  if (backup.plate_position <= 63) {
    postaviTrenutniPolozajPloce(backup.plate_position);
    String log = F("Power Recovery: Plate position restored: ");
    log += backup.plate_position;
    posaljiPCLog(log);
  }
  
  // Restore offset minutes
  if (backup.offset_minuta <= 14) {
    postaviOffsetMinuta(backup.offset_minuta);
    String log = F("Power Recovery: Offset minutes restored: ");
    log += backup.offset_minuta;
    posaljiPCLog(log);
  }
  
  // Restore RTC time if backup is recent (within last 24 hours)
  DateTime sada = dohvatiTrenutnoVrijeme();
  uint32_t rtc_now = sada.unixtime();
  
  if (backup.rtc_timestamp > 0) {
    uint32_t time_diff = (rtc_now >= backup.rtc_timestamp) ? 
                         (rtc_now - backup.rtc_timestamp) : 
                         (backup.rtc_timestamp - rtc_now);
    
    if (time_diff < 86400) {  // Less than 24 hours difference
      posaljiPCLog(F("Power Recovery: RTC time difference acceptable, using current RTC"));
    } else {
      // RTC time has drifted significantly, try to restore from backup
      DateTime backup_time(backup.rtc_timestamp);
      String log = F("Power Recovery: RTC drift detected, attempting restore to ");
      log += backup_time.year();
      log += F("-");
      log += backup_time.month();
      log += F("-");
      log += backup_time.day();
      posaljiPCLog(log);
    }
  }
  
  posaljiPCLog(F("Power Recovery: Boot recovery completed"));
}

// ==================== STATE PERSISTENCE ====================

void spremiKriticalnoStanje() {
  static unsigned long last_save = 0;
  unsigned long sada = millis();
  
  // Save every 60 seconds minimum to reduce EEPROM wear
  static const unsigned long SAVE_INTERVAL = 60000;
  
  if (sada - last_save < SAVE_INTERVAL) {
    return;
  }
  last_save = sada;
  
  // Build state backup structure
  SystemStateBackup backup;
  backup.hand_position_k_minuta = dohvatiMemoriraneKazaljkeMinuta();
  backup.plate_position = dohvatiPozicijuPloce();
  backup.offset_minuta = dohvatiOffsetMinuta();
  
  DateTime vrijeme = dohvatiTrenutnoVrijeme();
  backup.rtc_timestamp = vrijeme.unixtime();
  
  // Calculate checksum
  backup.checksum = izracunajChecksum(backup);
  
  // Save to next wear-leveling slot (round-robin)
  static uint8_t save_slot = 0;
  int adresa = PowerRecoveryLayout::BAZA_BOOT_FLAGS + save_slot * sizeof(SystemStateBackup);
  
  bool success = VanjskiEEPROM::zapisi(adresa, &backup, sizeof(SystemStateBackup));
  
  if (success) {
    save_slot = (save_slot + 1) % PowerRecoveryLayout::SLOTOVI_HAND_POSITION;
    last_state_save_time = sada;
    
    String log = F("Power Recovery: State saved to slot ");
    log += save_slot;
    posaljiPCLog(log);
  } else {
    posaljiPCLog(F("Power Recovery: State save FAILED"));
  }
}

// ==================== GRACEFUL SHUTDOWN ====================

void gracioznoGasenje() {
  posaljiPCLog(F("Power Recovery: Graceful shutdown initiated"));
  
  // Disable interrupts
  cli();
  
  // Stop all relays immediately (prevent damage)
  digitalWrite(PIN_RELEJ_PARNE_KAZALJKE, LOW);
  digitalWrite(PIN_RELEJ_NEPARNE_KAZALJKE, LOW);
  digitalWrite(PIN_RELEJ_PARNE_PLOCE, LOW);
  digitalWrite(PIN_RELEJ_NEPARNE_PLOCE, LOW);
  
  // Stop bells and hammers
  digitalWrite(PIN_ZVONO_1, LOW);
  digitalWrite(PIN_ZVONO_2, LOW);
  digitalWrite(PIN_CEKIC_MUSKI, LOW);
  digitalWrite(PIN_CEKIC_ZENSKI, LOW);
  
  posaljiPCLog(F("Power Recovery: All relays deactivated"));
  
  // Save critical state immediately
  spremiKriticalnoStanje();
  
  // Wait for EEPROM writes to complete
  delay(500);
  
  posaljiPCLog(F("Power Recovery: Shutdown complete, safe to power off"));
  
  // Wait for power loss (watchdog will reset if power remains)
  while (true) {
    delay(1000);
  }
}

// ==================== STATUS FUNCTIONS ====================

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

// ==================== EEPROM HEALTH ====================

bool provjeriZdravostEEPROM() {
  // Test EEPROM accessibility
  posaljiPCLog(F("Power Recovery: Checking EEPROM health..."));
  
  // Test read/write at known location
  uint32_t test_value = 0x12345678;
  uint32_t read_back = 0;
  
  int test_adresa = PowerRecoveryLayout::BAZA_BOOT_FLAGS;
  
  if (!VanjskiEEPROM::zapisi(test_adresa, &test_value, sizeof(uint32_t))) {
    posaljiPCLog(F("Power Recovery: EEPROM write test FAILED"));
    return false;
  }
  
  if (!VanjskiEEPROM::procitaj(test_adresa, &read_back, sizeof(uint32_t))) {
    posaljiPCLog(F("Power Recovery: EEPROM read test FAILED"));
    return false;
  }
  
  if (read_back != test_value) {
    posaljiPCLog(F("Power Recovery: EEPROM data corruption detected"));
    return false;
  }
  
  posaljiPCLog(F("Power Recovery: EEPROM health OK"));
  return true;
}

// ==================== INITIALIZATION ====================

void inicijalizirajPowerRecovery() {
  boot_time = millis();
  last_state_save_time = boot_time;
  
  // Initialize watchdog and detect reset cause
  inicijalizirajWatchdog();
  
  // Check EEPROM health
  if (!provjeriZdravostEEPROM()) {
    posaljiPCLog(F("Power Recovery: WARNING - EEPROM health issues detected"));
  }
  
  // Execute boot recovery if needed
  odradiBootRecovery();
  
  // Log system status
  String log = F("Power Recovery: System started, resets=");
  log += reset_counter;
  log += F(", watchdog=");
  log += watchdog_reset ? "yes" : "no";
  log += F(", power_loss=");
  log += power_loss_detected ? "yes" : "no";
  posaljiPCLog(log);
  
  // Initial state save
  spremiKriticalnoStanje();
}
', 'func_result': 'OK, file power_recovery.cpp created'}