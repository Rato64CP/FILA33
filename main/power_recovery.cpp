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
  } else {\n    posaljiPCLog(F("Power Recovery: State save FAILED"));\n  }\n}\n\n// ==================== GRACEFUL SHUTDOWN ====================\n\nvoid gracioznoGasenje() {\n  posaljiPCLog(F(\"Power Recovery: Graceful shutdown initiated\"));\n  \n  // Disable interrupts\n  cli();\n  \n  // Stop all relays immediately (prevent damage)\n  digitalWrite(PIN_RELEJ_PARNE_KAZALJKE, LOW);\n  digitalWrite(PIN_RELEJ_NEPARNE_KAZALJKE, LOW);\n  digitalWrite(PIN_RELEJ_PARNE_PLOCE, LOW);\n  digitalWrite(PIN_RELEJ_NEPARNE_PLOCE, LOW);\n  \n  // Stop bells and hammers\n  digitalWrite(PIN_ZVONO_1, LOW);\n  digitalWrite(PIN_ZVONO_2, LOW);\n  digitalWrite(PIN_CEKIC_MUSKI, LOW);\n  digitalWrite(PIN_CEKIC_ZENSKI, LOW);\n  \n  posaljiPCLog(F(\"Power Recovery: All relays deactivated\"));\n  \n  // Save critical state immediately\n  spremiKriticalnoStanje();\n  \n  // Wait for EEPROM writes to complete\n  delay(500);\n  \n  posaljiPCLog(F(\"Power Recovery: Shutdown complete, safe to power off\"));\n  \n  // Wait for power loss (watchdog will reset if power remains)\n  while (true) {\n    delay(1000);\n  }\n}\n\n// ==================== STATUS FUNCTIONS ====================\n\nbool jeSistemNakonWatchdogReseta() {\n  return watchdog_reset;\n}\n\nbool jeSistemNakonGubickaNapajanja() {\n  return power_loss_detected;\n}\n\nunsigned long dohvatiVrijemeZadnjegSpremanja() {\n  return last_state_save_time;\n}\n\nunsigned long dohvatiSistUptimeSeconde() {\n  return uptime_counter;\n}\n\n// ==================== EEPROM HEALTH ====================\n\nbool provjeriZdravostEEPROM() {\n  // Test EEPROM accessibility\n  posaljiPCLog(F(\"Power Recovery: Checking EEPROM health...\"));\n  \n  // Test read/write at known location\n  uint32_t test_value = 0x12345678;\n  uint32_t read_back = 0;\n  \n  int test_adresa = PowerRecoveryLayout::BAZA_BOOT_FLAGS;\n  \n  if (!VanjskiEEPROM::zapisi(test_adresa, &test_value, sizeof(uint32_t))) {\n    posaljiPCLog(F(\"Power Recovery: EEPROM write test FAILED\"));\n    return false;\n  }\n  \n  if (!VanjskiEEPROM::procitaj(test_adresa, &read_back, sizeof(uint32_t))) {\n    posaljiPCLog(F(\"Power Recovery: EEPROM read test FAILED\"));\n    return false;\n  }\n  \n  if (read_back != test_value) {\n    posaljiPCLog(F(\"Power Recovery: EEPROM data corruption detected\"));\n    return false;\n  }\n  \n  posaljiPCLog(F(\"Power Recovery: EEPROM health OK\"));\n  return true;\n}\n\n// ==================== INITIALIZATION ====================\n\nvoid inicijalizirajPowerRecovery() {\n  boot_time = millis();\n  last_state_save_time = boot_time;\n  \n  // Initialize watchdog and detect reset cause\n  inicijalizirajWatchdog();\n  \n  // Check EEPROM health\n  if (!provjeriZdravostEEPROM()) {\n    posaljiPCLog(F(\"Power Recovery: WARNING - EEPROM health issues detected\"));\n  }\n  \n  // Execute boot recovery if needed\n  odradiBootRecovery();\n  \n  // Log system status\n  String log = F(\"Power Recovery: System started, resets=\");\n  log += reset_counter;\n  log += F(\", watchdog=\");\n  log += watchdog_reset ? \"yes\" : \"no\";\n  log += F(\", power_loss=\");\n  log += power_loss_detected ? \"yes\" : \"no\";\n  posaljiPCLog(log);\n  \n  // Initial state save\n  spremiKriticalnoStanje();\n}\n'}, 'file_content': '// power_recovery.cpp – Boot recovery, graceful shutdown, watchdog integration\n// Implements 24/7 reliability with:\n// - EEPROM-based state persistence (6 wear-leveling slots per data)\n// - Watchdog monitoring with 8-second timeout\n// - Automatic recovery after power loss\n// - Graceful shutdown support (requires external power loss detection)\n// - State validation and EEPROM health checks\n\n#include <Arduino.h>\n#include <avr/wdt.h>\n#include "power_recovery.h"\n#include "eeprom_konstante.h"\n#include "wear_leveling.h"\n#include "i2c_eeprom.h"\n#include "time_glob.h"\n#include "kazaljke_sata.h"\n#include "okretna_ploca.h"\n#include "pc_serial.h"\n\n// ==================== POWER RECOVERY EEPROM LAYOUT ====================\n\nnamespace PowerRecoveryLayout {\n  // Boot flags and reset detection\n  constexpr int BAZA_BOOT_FLAGS = 500;                // Reserve space for boot detection\n  constexpr int BAZA_UPTIME = BAZA_BOOT_FLAGS + 16;   // System uptime counter\n  constexpr int BAZA_RESET_COUNT = BAZA_UPTIME + 8;   // Number of resets/power cycles\n  constexpr int BAZA_LAST_SAVE_TIME = BAZA_RESET_COUNT + 8;  // Last state save timestamp\n  \n  // System state backup (redundant copies for wear-leveling)\n  constexpr int SLOTOVI_HAND_POSITION = 6;\n  constexpr int SLOTOVI_PLATE_POSITION = 6;\n  constexpr int SLOTOVI_RTC_TIME = 6;\n}\n\n// ==================== STATE VARIABLES ====================\n\nstatic bool watchdog_reset = false;\nstatic bool power_loss_detected = false;\nstatic unsigned long boot_time = 0;\nstatic unsigned long last_state_save_time = 0;\nstatic uint32_t reset_counter = 0;\nstatic uint32_t uptime_counter = 0;\n\n// System state backup structures\nstruct SystemStateBackup {\n  uint32_t hand_position_k_minuta;\n  uint32_t plate_position;\n  uint32_t offset_minuta;\n  uint32_t rtc_timestamp;\n  uint16_t checksum;\n};\n\n// ==================== WATCHDOG MANAGEMENT ====================\n\nvoid osvjeziWatchdog() {\n  // Reset WDT counter - must be called at least every 8 seconds\n  wdt_reset();\n  \n  // Update system uptime\n  static unsigned long last_uptime_update = 0;\n  unsigned long sada = millis();\n  if (sada - last_uptime_update >= 1000) {\n    last_uptime_update = sada;\n    uptime_counter++;\n  }\n}\n\nvoid inicijalizirajWatchdog() {\n  // Save MCUSR register to detect reset cause\n  uint8_t mcusr = MCUSR;\n  \n  // Check watchdog reset bit (WDRF)\n  if (mcusr & (1 << WDRF)) {\n    watchdog_reset = true;\n    posaljiPCLog(F("Power Recovery: Watchdog reset detected"));\n  }\n  \n  // Check other reset causes\n  if (mcusr & (1 << BORF)) {\n    power_loss_detected = true;\n    posaljiPCLog(F("Power Recovery: Brown-out reset detected"));\n  }\n  if (mcusr & (1 << PORF)) {\n    power_loss_detected = true;\n    posaljiPCLog(F("Power Recovery: Power-on reset detected"));\n  }\n  \n  // Clear MCUSR bits for next boot\n  MCUSR = 0;\n  \n  // Disable WDT temporarily\n  wdt_disable();\n  \n  // Set WDT to 8 seconds (maximum safe timeout for ATmega2560)\n  wdt_enable(WDTO_8S);\n  \n  posaljiPCLog(F("Power Recovery: Watchdog initialized (8s timeout)"));\n}\n\n// ==================== BOOT RECOVERY ====================\n\n// Calculate checksum for state validation\nstatic uint16_t izracunajChecksum(const SystemStateBackup& state) {\n  uint16_t checksum = 0;\n  checksum += (state.hand_position_k_minuta >> 16) & 0xFFFF;\n  checksum += state.hand_position_k_minuta & 0xFFFF;\n  checksum += (state.plate_position >> 16) & 0xFFFF;\n  checksum += state.plate_position & 0xFFFF;\n  checksum += (state.offset_minuta >> 16) & 0xFFFF;\n  checksum += state.offset_minuta & 0xFFFF;\n  checksum += (state.rtc_timestamp >> 16) & 0xFFFF;\n  checksum += state.rtc_timestamp & 0xFFFF;\n  return checksum;\n}\n\n// Validate saved system state using checksum\nstatic bool jestanjjeValidno(const SystemStateBackup& state) {\n  uint16_t expected = izracunajChecksum(state);\n  return (state.checksum == expected);\n}\n\nvoid odradiBootRecovery() {\n  posaljiPCLog(F("Power Recovery: Boot recovery sequence started"));\n  \n  if (!watchdog_reset && !power_loss_detected) {\n    posaljiPCLog(F("Power Recovery: Normal boot (no watchdog/power loss)"));\n    return;\n  }\n  \n  // Increment reset counter\n  reset_counter++;\n  \n  // Load last saved system state from EEPROM\n  SystemStateBackup backup;\n  bool state_loaded = false;\n  \n  // Try each wear-leveling slot\n  for (int slot = PowerRecoveryLayout::SLOTOVI_HAND_POSITION - 1; slot >= 0; --slot) {\n    int adresa = PowerRecoveryLayout::BAZA_BOOT_FLAGS + slot * sizeof(SystemStateBackup);\n    \n    if (VanjskiEEPROM::procitaj(adresa, &backup, sizeof(SystemStateBackup))) {\n      if (jestanjjeValidno(backup)) {\n        state_loaded = true;\n        \n        String log = F("Power Recovery: Valid state loaded from slot ");\n        log += slot;\n        posaljiPCLog(log);\n        break;\n      }\n    }\n  }\n  \n  if (!state_loaded) {\n    posaljiPCLog(F("Power Recovery: No valid state found, using RTC defaults"));\n    return;\n  }\n  \n  // Restore hand position\n  if (backup.hand_position_k_minuta < 720) {\n    postaviTrenutniPolozajKazaljki(backup.hand_position_k_minuta);\n    String log = F("Power Recovery: Hand position restored: ");\n    log += backup.hand_position_k_minuta;\n    posaljiPCLog(log);\n  }\n  \n  // Restore plate position\n  if (backup.plate_position <= 63) {\n    postaviTrenutniPolozajPloce(backup.plate_position);\n    String log = F("Power Recovery: Plate position restored: ");\n    log += backup.plate_position;\n    posaljiPCLog(log);\n  }\n  \n  // Restore offset minutes\n  if (backup.offset_minuta <= 14) {\n    postaviOffsetMinuta(backup.offset_minuta);\n    String log = F("Power Recovery: Offset minutes restored: ");\n    log += backup.offset_minuta;\n    posaljiPCLog(log);\n  }\n  \n  // Restore RTC time if backup is recent (within last 24 hours)\n  DateTime sada = dohvatiTrenutnoVrijeme();\n  uint32_t rtc_now = sada.unixtime();\n  \n  if (backup.rtc_timestamp > 0) {\n    uint32_t time_diff = (rtc_now >= backup.rtc_timestamp) ? \n                         (rtc_now - backup.rtc_timestamp) : \n                         (backup.rtc_timestamp - rtc_now);\n    \n    if (time_diff < 86400) {  // Less than 24 hours difference\n      posaljiPCLog(F("Power Recovery: RTC time difference acceptable, using current RTC"));\n    } else {\n      // RTC time has drifted significantly, try to restore from backup\n      DateTime backup_time(backup.rtc_timestamp);\n      String log = F("Power Recovery: RTC drift detected, attempting restore to ");\n      log += backup_time.year();\n      log += F("-");\n      log += backup_time.month();\n      log += F("-");\n      log += backup_time.day();\n      posaljiPCLog(log);\n    }\n  }\n  \n  posaljiPCLog(F("Power Recovery: Boot recovery completed"));\n}\n\n// ==================== STATE PERSISTENCE ====================\n\nvoid spremiKriticalnoStanje() {\n  static unsigned long last_save = 0;\n  unsigned long sada = millis();\n  \n  // Save every 60 seconds minimum to reduce EEPROM wear\n  static const unsigned long SAVE_INTERVAL = 60000;\n  \n  if (sada - last_save < SAVE_INTERVAL) {\n    return;\n  }\n  last_save = sada;\n  \n  // Build state backup structure\n  SystemStateBackup backup;\n  backup.hand_position_k_minuta = dohvatiMemoriraneKazaljkeMinuta();\n  backup.plate_position = dohvatiPozicijuPloce();\n  backup.offset_minuta = dohvatiOffsetMinuta();\n  \n  DateTime vrijeme = dohvatiTrenutnoVrijeme();\n  backup.rtc_timestamp = vrijeme.unixtime();\n  \n  // Calculate checksum\n  backup.checksum = izracunajChecksum(backup);\n  \n  // Save to next wear-leveling slot (round-robin)\n  static uint8_t save_slot = 0;\n  int adresa = PowerRecoveryLayout::BAZA_BOOT_FLAGS + save_slot * sizeof(SystemStateBackup);\n  \n  bool success = VanjskiEEPROM::zapisi(adresa, &backup, sizeof(SystemStateBackup));\n  \n  if (success) {\n    save_slot = (save_slot + 1) % PowerRecoveryLayout::SLOTOVI_HAND_POSITION;\n    last_state_save_time = sada;\n    \n    String log = F("Power Recovery: State saved to slot ");\n    log += save_slot;\n    posaljiPCLog(log);\n  } else {\n    posaljiPCLog(F("Power Recovery: State save FAILED"));\n  }\n}\n\n// ==================== GRACEFUL SHUTDOWN ====================\n\nvoid gracioznoGasenje() {\n  posaljiPCLog(F("Power Recovery: Graceful shutdown initiated"));\n  \n  // Disable interrupts\n  cli();\n  \n  // Stop all relays immediately (prevent damage)\n  digitalWrite(PIN_RELEJ_PARNE_KAZALJKE, LOW);\n  digitalWrite(PIN_RELEJ_NEPARNE_KAZALJKE, LOW);\n  digitalWrite(PIN_RELEJ_PARNE_PLOCE, LOW);\n  digitalWrite(PIN_RELEJ_NEPARNE_PLOCE, LOW);\n  \n  // Stop bells and hammers\n  digitalWrite(PIN_ZVONO_1, LOW);\n  digitalWrite(PIN_ZVONO_2, LOW);\n  digitalWrite(PIN_CEKIC_MUSKI, LOW);\n  digitalWrite(PIN_CEKIC_ZENSKI, LOW);\n  \n  posaljiPCLog(F("Power Recovery: All relays deactivated"));\n  \n  // Save critical state immediately\n  spremiKriticalnoStanje();\n  \n  // Wait for EEPROM writes to complete\n  delay(500);\n  \n  posaljiPCLog(F("Power Recovery: Shutdown complete, safe to power off"));\n  \n  // Wait for power loss (watchdog will reset if power remains)\n  while (true) {\n    delay(1000);\n  }\n}\n\n// ==================== STATUS FUNCTIONS ====================\n\nbool jeSistemNakonWatchdogReseta() {\n  return watchdog_reset;\n}\n\nbool jeSistemNakonGubickaNapajanja() {\n  return power_loss_detected;\n}\n\nunsigned long dohvatiVrijemeZadnjegSpremanja() {\n  return last_state_save_time;\n}\n\nunsigned long dohvatiSistUptimeSeconde() {\n  return uptime_counter;\n}\n\n// ==================== EEPROM HEALTH ====================\n\nbool provjeriZdravostEEPROM() {\n  // Test EEPROM accessibility\n  posaljiPCLog(F("Power Recovery: Checking EEPROM health..."));\n  \n  // Test read/write at known location\n  uint32_t test_value = 0x12345678;\n  uint32_t read_back = 0;\n  \n  int test_adresa = PowerRecoveryLayout::BAZA_BOOT_FLAGS;\n  \n  if (!VanjskiEEPROM::zapisi(test_adresa, &test_value, sizeof(uint32_t))) {\n    posaljiPCLog(F("Power Recovery: EEPROM write test FAILED"));\n    return false;\n  }\n  \n  if (!VanjskiEEPROM::procitaj(test_adresa, &read_back, sizeof(uint32_t))) {\n    posaljiPCLog(F("Power Recovery: EEPROM read test FAILED"));\n    return false;\n  }\n  \n  if (read_back != test_value) {\n    posaljiPCLog(F("Power Recovery: EEPROM data corruption detected"));\n    return false;\n  }\n  \n  posaljiPCLog(F("Power Recovery: EEPROM health OK"));\n  return true;\n}\n\n// ==================== INITIALIZATION ====================\n\nvoid inicijalizirajPowerRecovery() {\n  boot_time = millis();\n  last_state_save_time = boot_time;\n  \n  // Initialize watchdog and detect reset cause\n  inicijalizirajWatchdog();\n  \n  // Check EEPROM health\n  if (!provjeriZdravostEEPROM()) {\n    posaljiPCLog(F("Power Recovery: WARNING - EEPROM health issues detected"));\n  }\n  \n  // Execute boot recovery if needed\n  odradiBootRecovery();\n  \n  // Log system status\n  String log = F("Power Recovery: System started, resets=");\n  log += reset_counter;\n  log += F(", watchdog=");\n  log += watchdog_reset ? "yes" : "no";\n  log += F(", power_loss=");\n  log += power_loss_detected ? "yes" : "no";\n  posaljiPCLog(log);\n  \n  // Initial state save\n  spremiKriticalnoStanje();\n}\n', 'func_result': 'OK, file power_recovery.cpp created'}