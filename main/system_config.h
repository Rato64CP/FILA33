// system_config.h – System-wide configuration and module integration
#pragma once

// ==================== COMPILE-TIME CONFIGURATION ====================

// Enable/disable major subsystems
#define ENABLE_MENU_SYSTEM              1   // 6-key LCD menu
#define ENABLE_BELL_STATE_MACHINE       1   // Advanced bell/hammer control
#define ENABLE_MQTT_FRAMEWORK          1   // MQTT integration via ESP8266
#define ENABLE_DEBOUNCING              1   // Software debouncing
#define ENABLE_I2C_AUTO_DETECT         1   // Automatic I2C device discovery
#define ENABLE_WATCHDOG                1   // Hardware watchdog timer
#define ENABLE_HAND_CORRECTION         1   // LCD menu hand position correction
#define ENABLE_TIME_SYNC               1   // RTC + NTP + DCF synchronization

// ==================== TIMING PARAMETERS ====================

// Menu system
#define MENU_TIMEOUT_MS                30000  // Auto-return to clock after 30s
#define MENU_REFRESH_RATE_MS           100    // Update LCD every 100ms

// Bell state machine
#define BELL_STRIKE_DURATION_MS        1000   // Bell rings for 1 second
#define BELL_STRIKE_PAUSE_MS           800    // Pause between strikes
#define INERTIAL_DELAY_MS              150    // Hammer settling time
#define BOTH_MODE_STAGGER_MS           400    // Stagger between bells in both-mode

// Debouncing
#define DEBOUNCE_TIME_MS               30     // 30ms debounce window
#define KEYPAD_POLL_RATE_MS            50     // Check keypad every 50ms

// MQTT
#define MQTT_HEARTBEAT_MS              5000   // MQTT heartbeat every 5s
#define MQTT_STATUS_PUBLISH_MS         30000  // Publish status every 30s
#define MQTT_RECONNECT_DELAY_MS        10000  // Wait 10s before reconnect attempt

// Watchdog
#define WATCHDOG_TIMEOUT_S             8      // 8 second timeout

// ==================== HARDWARE CONFIGURATION ====================

// Serial ports (Arduino Mega)
#define SERIAL_BAUD_PC                 115200 // PC debugging serial
#define SERIAL_BAUD_ESP                9600   // ESP8266 MQTT serial (Serial3)

// I2C
#define I2C_LCD_ADDRESS                0x27   // LiquidCrystal I2C address (or 0x3F)
#define I2C_RTC_ADDRESS                0x68   // DS3231 RTC address
#define I2C_EEPROM_ADDRESS             0x57   // 24C32 EEPROM address

// ==================== FEATURE FLAGS ====================

// Enable extended logging to PC serial
#define DEBUG_LOGGING                  1

// Enable bounce statistics tracking
#define ENABLE_BOUNCE_DIAGNOSTICS      1

// Enable HA discovery on startup
#define HA_DISCOVERY_ON_STARTUP        1

// Enable graceful shutdown on power loss detection
#define ENABLE_POWER_LOSS_DETECTION    0   // Requires external hardware

// ==================== MODULE INTEGRATION ====================

// These macros ensure all modules are properly initialized in sequence

#define INITIALIZE_ALL_SUBSYSTEMS() do { \
  inicijalizirajLCD(); \
  VanjskiEEPROM::inicijaliziraj(); \
  inicijalizirajRTC(); \
  inicijalizirajPCSerijsku(); \
  ucitajPostavke(); \
  inicijalizirajTipke(); \
  inicijalizirajESP(); \
  inicijalizirajDebouncing(); \
  inicijalizirajBellStateMachine(); \
  inicijalizirajMQTT(); \
  inicijalizirajMenuSistem(); \
  inicijalizirajZvona(); \
  inicijalizirajKazaljke(); \
  inicijalizirajPlocu(); \
  inicijalizirajDCF(); \
  inicijalizirajWatchdog(); \
} while(0)

#define UPDATE_ALL_SUBSYSTEMS() do { \
  osvjeziWatchdog(); \
  obradiESPSerijskuKomunikaciju(); \
  upravljajMenuSistemom(); \
  provjeriTipke(); \
  upravljajBellStateMachine(); \
  upravljajZvonom(); \
  upravljajOtkucavanjem(); \
  upravljajKazaljkama(); \
  upravljajPlocom(); \
  upravljajMQTT(); \
  osvjeziDCFSinkronizaciju(); \
  osvjeziWatchdog(); \
} while(0)

// ==================== SUBSYSTEM INCLUDES ====================

#ifdef ENABLE_MENU_SYSTEM
#include "menu_system.h"
#endif

#ifdef ENABLE_BELL_STATE_MACHINE
#include "bell_state_machine.h"
#endif

#ifdef ENABLE_MQTT_FRAMEWORK
#include "mqtt_framework.h"
#endif

#ifdef ENABLE_DEBOUNCING
#include "debouncing.h"
#endif

#ifdef ENABLE_WATCHDOG
#include "watchdog.h"
#endif

// ==================== SUBSYSTEM REQUIREMENTS ====================

/* Subsystem 1: RTC-primary timekeeping with NTP resync
   - DS3231 maintains time offline
   - On power recovery, correct hand position from software tracking
   - Resume all operations immediately without internet
   Dependencies: time_glob.h, kazaljke_sata.h
*/

/* Subsystem 2: Hour strikes with multiple modes
   - Bell 1 for hourly strikes, Bell 2 for half-hourly
   - Normal, Both (staggered), Celebration, Funeral modes
   - Hammers blocked during bell movement
   - Software debouncing on all inputs
   Dependencies: bell_state_machine.h, debouncing.h, zvonjenje.h
*/

/* Subsystem 3: Impulse relay control
   - 6-second pulses via ULN2803 optocoupler drivers
   - Noise immunity for 50m cables
   - Flyback protection present
   Dependencies: podesavanja_piny.h
*/

/* Subsystem 4: MQTT integration via ESP8266 serial
   - Publish: clock status, time source, last sync, bell/hammer states, mode
   - Subscribe: ring bell, set mode, strikes enable/disable, hand correction
   Dependencies: mqtt_framework.h, esp_serial.h
*/

/* Subsystem 5: Home Assistant MQTT discovery
   - Expose status entities: clock, time source, sync time, bells, hammers, mode, silent
   - Command services: ring bell, celebrate, funeral, strikes, hand correction, silent toggle
   Dependencies: mqtt_framework.h
*/

/* Subsystem 6: Watchdog monitoring with graceful shutdown
   - EEPROM wear-leveling
   - State persistence
   - 24/7 reliability with automatic recovery after restart
   Dependencies: watchdog.h, wear_leveling.h
*/

#endif // SYSTEM_CONFIG_H