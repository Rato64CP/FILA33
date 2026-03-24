// power_recovery.h – Boot recovery, graceful shutdown, watchdog integration
#pragma once

#include <Arduino.h>

// Initialize power recovery system (call during setup)
// Checks for previous crash/power loss and recovers state from EEPROM
void inicijalizirajPowerRecovery();

// Execute boot recovery sequence after power loss
// Restores system state from EEPROM (hand position, plate position, settings)
void odradiBootRecovery();

// Save critical system state to EEPROM before power loss
// Called periodically to ensure state is always persisted
void spremiKriticalnoStanje();

// Graceful shutdown routine - called when power loss is detected
// Saves all state, closes open operations, prepares for safe shutdown
void gracioznoGasenje();

// Watchdog ping - must be called regularly to prevent system reset
void osvjeziWatchdog();

// Check if system recovered from watchdog reset
bool jeSistemNakonWatchdogReseta();

// Check if system recovered from power loss
bool jeSistemNakonGubickaNapajanja();

// Get last saved state timestamp
unsigned long dohvatiVrijemeZadnjegSpremanja();

// Check EEPROM health and repair if needed
bool provjeriZdravostEEPROM();

// Get system uptime in seconds
unsigned long dohvatiSistUptimeSeconde();