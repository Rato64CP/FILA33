// relay_control.cpp – UNIFIED RELAY CONTROL SYSTEM
// Single source for ALL relay impulse operations
// Handles both hand position (K-minuta) and rotating plate (position 0-63)
// Consolidated from previously scattered implementations

#include <Arduino.h>
#include <RTClib.h>
#include "relay_control.h"
#include "podesavanja_piny.h"
#include "time_glob.h"
#include "eeprom_konstante.h"
#include "wear_leveling.h"
#include "pc_serial.h"
#include "watchdog.h"

// ==================== RELAY STATE TRACKING ====================

// Current phase timing (used by all relay operations)
static unsigned long relay_start_time = 0;
static enum {
  RELAY_IDLE,
  RELAY_PHASE_ONE,
  RELAY_PHASE_TWO
} relay_phase = RELAY_IDLE;

// Relay type and context (identifies which relay operation is active)
static RelayType relay_type = RELAY_TYPE_NONE;

// Context-specific state
static struct {
  int current_k_minuta;             // For hand position relay
  int target_k_minuta;              // For hand position relay
  bool first_phase_complete;        // Track phase transitions
  
  int current_plate_position;       // For plate relay
  int target_plate_position;        // For plate relay
  bool plate_phase_one_done;        // Track plate phase transitions
} relay_context = {0, 0, false, 0, 0, false};

// ==================== RELAY HARDWARE ABSTRACTION ====================

// Get relay pair for current context and parity
// For K-minuta: even K → odd relay, odd K → even relay
// For plate: always use pair (PARNE first, NEPARNI second)
static void aktivirajPrvuFazu(bool je_ruka) {
  if (je_ruka) {
    // Hand position relay - select based on K-minuta parity
    if (relay_context.current_k_minuta % 2 == 0) {
      // Even K-minuta → activate odd relay (NEPARNI)
      digitalWrite(PIN_RELEJ_PARNE_KAZALJKE, LOW);
      digitalWrite(PIN_RELEJ_NEPARNE_KAZALJKE, HIGH);
    } else {
      // Odd K-minuta → activate even relay (PARNI)
      digitalWrite(PIN_RELEJ_PARNE_KAZALJKE, HIGH);
      digitalWrite(PIN_RELEJ_NEPARNE_KAZALJKE, LOW);
    }
  } else {
    // Plate position relay - always activate PARNI first
    digitalWrite(PIN_RELEJ_PARNE_PLOCE, HIGH);
    digitalWrite(PIN_RELEJ_NEPARNE_PLOCE, LOW);
  }
  
  relay_phase = RELAY_PHASE_ONE;
  relay_start_time = millis();
  relay_context.first_phase_complete = false;
  
  String log = je_ruka ? F("Relay: Hand phase 1, K=") : F("Relay: Plate phase 1, pos=");
  log += je_ruka ? relay_context.current_k_minuta : relay_context.current_plate_position;
  posaljiPCLog(log);
}

// Transition to second phase after 3 seconds
static void prelaziNaDruguFazu(bool je_ruka) {
  if (je_ruka) {
    // Hand position relay - switch polarity
    if (relay_context.current_k_minuta % 2 == 0) {
      // Was NEPARNI, now PARNI
      digitalWrite(PIN_RELEJ_PARNE_KAZALJKE, HIGH);
      digitalWrite(PIN_RELEJ_NEPARNE_KAZALJKE, LOW);
    } else {
      // Was PARNI, now NEPARNI
      digitalWrite(PIN_RELEJ_PARNE_KAZALJKE, LOW);
      digitalWrite(PIN_RELEJ_NEPARNE_KAZALJKE, HIGH);
    }
  } else {
    // Plate position relay - switch to NEPARNI
    digitalWrite(PIN_RELEJ_PARNE_PLOCE, LOW);
    digitalWrite(PIN_RELEJ_NEPARNE_PLOCE, HIGH);
  }
  
  relay_phase = RELAY_PHASE_TWO;
  relay_start_time = millis();
  relay_context.first_phase_complete = true;
  
  String log = je_ruka ? F("Relay: Hand phase 2, K=") : F("Relay: Plate phase 2, pos=");
  log += je_ruka ? relay_context.current_k_minuta : relay_context.current_plate_position;
  posaljiPCLog(log);
}

// Deactivate all relays and complete impulse
static void zavrsiImpuls(bool je_ruka) {
  // Deactivate both relay pins
  digitalWrite(PIN_RELEJ_PARNE_KAZALJKE, LOW);
  digitalWrite(PIN_RELEJ_NEPARNE_KAZALJKE, LOW);
  digitalWrite(PIN_RELEJ_PARNE_PLOCE, LOW);
  digitalWrite(PIN_RELEJ_NEPARNE_PLOCE, LOW);
  
  relay_phase = RELAY_IDLE;
  
  if (je_ruka) {
    // Increment K-minuta and save to EEPROM
    relay_context.current_k_minuta = (relay_context.current_k_minuta + 1) % 720;
    WearLeveling::spremi(EepromLayout::BAZA_KAZALJKE, 
                        EepromLayout::SLOTOVI_KAZALJKE,
                        relay_context.current_k_minuta);
    
    String log = F("Relay: Hand impulse complete, new K=");
    log += relay_context.current_k_minuta;
    posaljiPCLog(log);
  } else {
    // Increment plate position and save to EEPROM
    relay_context.current_plate_position = (relay_context.current_plate_position + 1) % 64;
    WearLeveling::spremi(EepromLayout::BAZA_POZICIJA_PLOCE,
                        EepromLayout::SLOTOVI_POZICIJA_PLOCE,
                        relay_context.current_plate_position);
    
    String log = F("Relay: Plate impulse complete, new pos=");
    log += relay_context.current_plate_position;
    posaljiPCLog(log);
  }
}

// ==================== PUBLIC HAND POSITION CONTROL ====================

// Send single relay impulse for hand position movement
// Implements 6-second impulse: 3s first phase + 3s second phase
void aktivirajRelej_Kazaljke(int trenutna_k_minuta, int ciljna_k_minuta) {
  // Prevent overlapping relay operations
  if (relay_phase != RELAY_IDLE) {
    posaljiPCLog(F("Relay: Already in progress, ignoring request"));
    return;
  }
  
  // Store context for this operation
  relay_type = RELAY_TYPE_HAND_POSITION;
  relay_context.current_k_minuta = trenutna_k_minuta;
  relay_context.target_k_minuta = ciljna_k_minuta;
  
  // Start first phase
  aktivirajPrvuFazu(true);
}

// ==================== PUBLIC PLATE POSITION CONTROL ====================

// Send single relay impulse for plate position movement
// Same 6-second impulse pattern as hand position
void aktivirajRelej_Ploca(int trenutna_pozicija, int ciljna_pozicija) {
  // Prevent overlapping relay operations
  if (relay_phase != RELAY_IDLE) {
    posaljiPCLog(F("Relay: Already in progress, ignoring request"));
    return;
  }
  
  // Store context for this operation
  relay_type = RELAY_TYPE_PLATE_POSITION;
  relay_context.current_plate_position = trenutna_pozicija;
  relay_context.target_plate_position = ciljna_pozicija;
  
  // Start first phase
  aktivirajPrvuFazu(false);
}

// ==================== PULSE MANAGEMENT ====================

// Non-blocking relay pulse manager
// Call this regularly from main loop() to handle phase transitions
void upravljajRelej() {
  // No active impulse
  if (relay_phase == RELAY_IDLE) {
    return;
  }
  
  unsigned long sada = millis();
  unsigned long proteklo = sada - relay_start_time;
  bool je_ruka = (relay_type == RELAY_TYPE_HAND_POSITION);
  
  // Transition from phase 1 to phase 2 after 3 seconds
  if (relay_phase == RELAY_PHASE_ONE && proteklo >= 3000UL) {
    prelaziNaDruguFazu(je_ruka);
    return;
  }
  
  // Complete impulse after phase 2 finishes (3 more seconds)
  if (relay_phase == RELAY_PHASE_TWO && proteklo >= 3000UL) {
    zavrsiImpuls(je_ruka);
    return;
  }
}

// ==================== BLOCKING IMPULSE (FOR SETUP/BOOT) ====================

// Execute single impulse synchronously (blocking)
// Used during boot synchronization and menu-driven correction
// Includes LCD updates and watchdog refresh
void odradiJedanImpulsBlokirajuci_Kazaljke(int trenutna_k_minuta) {
  // First phase: energize appropriate relay
  if (trenutna_k_minuta % 2 == 0) {
    // Even K-minuta → odd relay
    digitalWrite(PIN_RELEJ_PARNE_KAZALJKE, LOW);
    digitalWrite(PIN_RELEJ_NEPARNE_KAZALJKE, HIGH);
  } else {
    // Odd K-minuta → even relay
    digitalWrite(PIN_RELEJ_PARNE_KAZALJKE, HIGH);
    digitalWrite(PIN_RELEJ_NEPARNE_KAZALJKE, LOW);
  }
  
  String log = F("Relay (blk): Hand phase 1, K=");
  log += trenutna_k_minuta;
  posaljiPCLog(log);
  
  // Wait for first phase (3 seconds)
  delay(3000);
  osvjeziWatchdog();
  
  // Brief transition pause
  digitalWrite(PIN_RELEJ_PARNE_KAZALJKE, LOW);
  digitalWrite(PIN_RELEJ_NEPARNE_KAZALJKE, LOW);
  delay(200);
  osvjeziWatchdog();
  
  // Second phase: switch relay polarity
  if (trenutna_k_minuta % 2 == 0) {
    // Was odd, now even
    digitalWrite(PIN_RELEJ_PARNE_KAZALJKE, HIGH);
    digitalWrite(PIN_RELEJ_NEPARNE_KAZALJKE, LOW);
  } else {
    // Was even, now odd
    digitalWrite(PIN_RELEJ_PARNE_KAZALJKE, LOW);
    digitalWrite(PIN_RELEJ_NEPARNE_KAZALJKE, HIGH);
  }
  
  String log2 = F("Relay (blk): Hand phase 2, K=");
  log2 += trenutna_k_minuta;
  posaljiPCLog(log2);
  
  // Wait for second phase (3 seconds)
  delay(3000);
  osvjeziWatchdog();
  
  // Deactivate all relays
  digitalWrite(PIN_RELEJ_PARNE_KAZALJKE, LOW);
  digitalWrite(PIN_RELEJ_NEPARNE_KAZALJKE, LOW);
  
  // Brief settling time
  delay(400);
  osvjeziWatchdog();
}

// Blocking impulse for plate position
void odradiJedanImpulsBlokirajuci_Ploca(int trenutna_pozicija) {
  // First phase: activate PARNI relay
  digitalWrite(PIN_RELEJ_PARNE_PLOCE, HIGH);
  digitalWrite(PIN_RELEJ_NEPARNE_PLOCE, LOW);
  
  String log = F("Relay (blk): Plate phase 1, pos=");
  log += trenutna_pozicija;
  posaljiPCLog(log);
  
  // Wait for first phase (3 seconds)
  delay(3000);
  osvjeziWatchdog();
  
  // Brief transition pause
  digitalWrite(PIN_RELEJ_PARNE_PLOCE, LOW);
  delay(200);
  osvjeziWatchdog();
  
  // Second phase: activate NEPARNI relay
  digitalWrite(PIN_RELEJ_NEPARNE_PLOCE, HIGH);
  
  String log2 = F("Relay (blk): Plate phase 2, pos=");
  log2 += trenutna_pozicija;
  posaljiPCLog(log2);
  
  // Wait for second phase (3 seconds)
  delay(3000);
  osvjeziWatchdog();
  
  // Deactivate all relays
  digitalWrite(PIN_RELEJ_PARNE_PLOCE, LOW);
  digitalWrite(PIN_RELEJ_NEPARNE_PLOCE, LOW);
  
  // Brief settling time
  delay(400);
  osvjeziWatchdog();
}

// ==================== STATUS QUERIES ====================

// Check if any relay impulse is currently active
bool jeRelej_Aktivan() {
  return (relay_phase != RELAY_IDLE);
}

// Get current relay operation type
RelayType dohvatiTipReleja() {
  return relay_type;
}

// Get current relay phase (0=idle, 1=first, 2=second)
int dohvatiFazuReleja() {
  return (int)relay_phase;
}

// ==================== INITIALIZATION ====================

void inicijalizirajRelejeSistem() {
  // Configure all relay pins as outputs
  pinMode(PIN_RELEJ_PARNE_KAZALJKE, OUTPUT);
  pinMode(PIN_RELEJ_NEPARNE_KAZALJKE, OUTPUT);
  pinMode(PIN_RELEJ_PARNE_PLOCE, OUTPUT);
  pinMode(PIN_RELEJ_NEPARNE_PLOCE, OUTPUT);
  
  // Ensure all relays are deactivated initially
  digitalWrite(PIN_RELEJ_PARNE_KAZALJKE, LOW);
  digitalWrite(PIN_RELEJ_NEPARNE_KAZALJKE, LOW);
  digitalWrite(PIN_RELEJ_PARNE_PLOCE, LOW);
  digitalWrite(PIN_RELEJ_NEPARNE_PLOCE, LOW);
  
  // Reset state
  relay_phase = RELAY_IDLE;
  relay_type = RELAY_TYPE_NONE;
  relay_start_time = 0;
  
  posaljiPCLog(F("Relay system initialized: 4 pins configured"));
}

// Disable all relays immediately (emergency stop)
void deaktivirajSveReleje() {
  digitalWrite(PIN_RELEJ_PARNE_KAZALJKE, LOW);
  digitalWrite(PIN_RELEJ_NEPARNE_KAZALJKE, LOW);
  digitalWrite(PIN_RELEJ_PARNE_PLOCE, LOW);
  digitalWrite(PIN_RELEJ_NEPARNE_PLOCE, LOW);
  
  relay_phase = RELAY_IDLE;
  relay_type = RELAY_TYPE_NONE;
  
  posaljiPCLog(F("All relays deactivated (emergency stop)"));
}