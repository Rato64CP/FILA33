// relay_control.h – UNIFIED RELAY CONTROL SYSTEM HEADER
// Single source for ALL relay impulse operations
// Handles both hand position (K-minuta) and rotating plate (position 0-63)

#pragma once

#include <stdint.h>

// Relay operation types
enum RelayType {
  RELAY_TYPE_NONE = 0,
  RELAY_TYPE_HAND_POSITION = 1,   // K-minuta tracking (hand movement)
  RELAY_TYPE_PLATE_POSITION = 2   // Position 0-63 tracking (plate rotation)
};

// ==================== INITIALIZATION ====================

// Initialize relay control system and configure pins
void inicijalizirajRelejeSistem();

// ==================== HAND POSITION RELAY CONTROL ====================

// Send single non-blocking relay impulse for hand position movement
// Implements 6-second impulse: 3s first phase + 3s second phase
// Uses upravljajRelej() in main loop to handle phase transitions
void aktivirajRelej_Kazaljke(int trenutna_k_minuta, int ciljna_k_minuta);

// Execute single blocking impulse for hand position
// Used during boot synchronization and menu-driven correction
// Includes watchdog refresh and LCD updates
void odradiJedanImpulsBlokirajuci_Kazaljke(int trenutna_k_minuta);

// ==================== PLATE POSITION RELAY CONTROL ====================

// Send single non-blocking relay impulse for plate position movement
// Same 6-second impulse pattern as hand position
void aktivirajRelej_Ploca(int trenutna_pozicija, int ciljna_pozicija);

// Execute single blocking impulse for plate position
// Used during boot synchronization and menu-driven correction
void odradiJedanImpulsBlokirajuci_Ploca(int trenutna_pozicija);

// ==================== PULSE MANAGEMENT ====================

// Non-blocking relay pulse manager
// Must be called regularly from main loop() to handle phase transitions
// Automatically manages:
// - 3-second first phase (appropriate relay based on context)
// - 3-second second phase (reverse polarity)
// - EEPROM persistence after impulse completion
// - Relay deactivation and state reset
void upravljajRelej();

// ==================== STATUS QUERIES ====================

// Check if any relay impulse is currently active
bool jeRelej_Aktivan();

// Get current relay operation type (NONE, HAND_POSITION, or PLATE_POSITION)
enum RelayType dohvatiTipReleja();

// Get current relay phase (0=idle, 1=first phase, 2=second phase)
int dohvatiFazuReleja();

// ==================== EMERGENCY CONTROL ====================

// Disable all relays immediately (emergency stop)
// Used during graceful shutdown or error recovery
void deaktivirajSveReleje();

// ==================== OPERATION NOTES ====================

/*
 * Relay Operation Sequence (6 seconds total):
 * 
 * NON-BLOCKING MODE (for normal operation):
 * 1. Call aktivirajRelej_Kazaljke() or aktivirajRelej_Ploca()
 * 2. Call upravljajRelej() every 10-50ms from main loop
 * 3. System automatically handles phase transitions and completion
 * 4. Use jeRelej_Aktivan() to check completion status
 * 
 * BLOCKING MODE (for boot/menu correction):
 * 1. Call odradiJedanImpulsBlokirajuci_Kazaljke() or _Ploca()
 * 2. Function blocks for ~7 seconds (6s pulse + 1s settling)
 * 3. Automatic EEPROM persistence and watchdog refresh
 * 4. Returns when impulse complete
 * 
 * Parity Enforcement (Hand Position):
 * - Even K-minuta (current position) → activate ODD relay next
 * - Odd K-minuta → activate EVEN relay next
 * - Ensures proper hand movement via alternating relay pairs
 * - P-suffix tracking in EEPROM: even→P, odd→N
 * 
 * Phase Timing:
 * - First phase: 3 seconds (energize one relay)
 * - Transition: 200ms pause (for mechanical settling)
 * - Second phase: 3 seconds (energize alternate relay/polarity)
 * - Settling: 400ms post-pulse (for lock engagement on plate)
 * - Total: ~6.6 seconds per impulse
 * 
 * EEPROM Persistence:
 * - K-minuta incremented and saved after every hand impulse
 * - Plate position incremented and saved after every plate impulse
 * - Uses wear-leveling (6 slots) to extend EEPROM lifespan
 * - Critical for power-loss recovery
 * 
 * Safety:
 * - Prevents overlapping relay operations (only one active at a time)
 * - Deactivates all relays on error
 * - Includes watchdog refresh in blocking mode
 * - Automatic relay deactivation on phase completion
 */

#endif // RELAY_CONTROL_H