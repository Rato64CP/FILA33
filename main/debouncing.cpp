// debouncing.cpp – Software debouncing for keypad inputs and relay feedback
#include <Arduino.h>
#include "debouncing.h"
#include "pc_serial.h"

// ==================== STATE TRACKING ====================

#define MAX_PINS 48  // Arduino Mega has up to 48 pins (though not all digital)

// Debounce state for each pin
static struct {
  SwitchState trenutnoStanje;
  SwitchState prethodnoStanje;
  unsigned long vremePocetkaOdskoka;
  uint16_t brojOdskoka;
  bool u_odskoku;
} pinStanja[MAX_PINS];

// ==================== INITIALIZATION ====================

void inicijalizirajDebouncing() {
  for (uint8_t i = 0; i < MAX_PINS; i++) {
    pinStanja[i].trenutnoStanje = SWITCH_RELEASED;
    pinStanja[i].prethodnoStanje = SWITCH_RELEASED;
    pinStanja[i].vremePocetkaOdskoka = 0;
    pinStanja[i].brojOdskoka = 0;
    pinStanja[i].u_odskoku = false;
  }
  
  posaljiPCLog(F("Debouncing sistem inicijaliziran"));
}

// ==================== SINGLE PIN DEBOUNCING ====================

bool obradiDebouncedInput(uint8_t pinNumber, uint8_t debounceTimeMs, SwitchState* novoStanje) {
  if (pinNumber >= MAX_PINS || novoStanje == NULL) {
    return false;
  }
  
  // Read current physical state
  SwitchState fizickoStanje = digitalRead(pinNumber) == LOW ? SWITCH_PRESSED : SWITCH_RELEASED;
  unsigned long sada = millis();
  
  // If state matches current debounced state, no change
  if (fizickoStanje == pinStanja[pinNumber].trenutnoStanje) {
    if (pinStanja[pinNumber].u_odskoku) {
      // Debouncing was in progress, but signal stabilized
      pinStanja[pinNumber].u_odskoku = false;
      pinStanja[pinNumber].vremePocetkaOdskoka = 0;
    }
    *novoStanje = pinStanja[pinNumber].trenutnoStanje;
    return false;
  }
  
  // State doesn't match – check if it's a bounce or real change
  if (!pinStanja[pinNumber].u_odskoku) {
    // Start debouncing timer
    pinStanja[pinNumber].vremePocetkaOdskoka = sada;
    pinStanja[pinNumber].u_odskoku = true;
    pinStanja[pinNumber].brojOdskoka++;
    *novoStanje = pinStanja[pinNumber].trenutnoStanje;
    return false;
  }
  
  // Check if debounce time has elapsed
  unsigned long vremeProslo = sada - pinStanja[pinNumber].vremePocetkaOdskoka;
  if (vremeProslo >= debounceTimeMs) {
    // Debounce time passed – accept the new state
    pinStanja[pinNumber].prethodnoStanje = pinStanja[pinNumber].trenutnoStanje;
    pinStanja[pinNumber].trenutnoStanje = fizickoStanje;
    pinStanja[pinNumber].u_odskoku = false;
    pinStanja[pinNumber].vremePocetkaOdskoka = 0;
    
    *novoStanje = pinStanja[pinNumber].trenutnoStanje;
    return true;  // State changed!
  }
  
  // Still debouncing
  *novoStanje = pinStanja[pinNumber].trenutnoStanje;
  return false;
}

// ==================== MULTIPLE PIN DEBOUNCING ====================

uint8_t obradiMultipleDebouncedInputs(uint8_t pinMask, uint8_t debounceTimeMs) {
  uint8_t izmijenjeniPinovi = 0;
  SwitchState novoStanje;
  
  for (uint8_t i = 0; i < 8; i++) {
    if ((pinMask & (1 << i)) == 0) {
      continue;  // Skip pins not in mask
    }
    
    if (obradiDebouncedInput(i, debounceTimeMs, &novoStanje)) {
      izmijenjeniPinovi |= (1 << i);
    }
  }
  
  return izmijenjeniPinovi;
}

// ==================== STATE QUERIES ====================

SwitchState dohvatiDeboucedState(uint8_t pinNumber) {
  if (pinNumber >= MAX_PINS) {
    return SWITCH_RELEASED;
  }
  return pinStanja[pinNumber].trenutnoStanje;
}

void resetDebounceState(uint8_t pinNumber) {
  if (pinNumber >= MAX_PINS) {
    return;
  }
  
  pinStanja[pinNumber].trenutnoStanje = SWITCH_RELEASED;
  pinStanja[pinNumber].prethodnoStanje = SWITCH_RELEASED;
  pinStanja[pinNumber].vremePocetkaOdskoka = 0;
  pinStanja[pinNumber].u_odskoku = false;
}

void resetAllDebounceStates() {
  for (uint8_t i = 0; i < MAX_PINS; i++) {
    resetDebounceState(i);
  }
  
  posaljiPCLog(F("Svi debounce statusi resetirani"));
}

// ==================== STATISTICS ====================

uint16_t dohvatiStatistikuOdskoka(uint8_t pinNumber) {
  if (pinNumber >= MAX_PINS) {
    return 0;
  }
  return pinStanja[pinNumber].brojOdskoka;
}

void resetStatistike() {
  for (uint8_t i = 0; i < MAX_PINS; i++) {
    pinStanja[i].brojOdskoka = 0;
  }
  
  posaljiPCLog(F("Debounce statistike resetirane"));
}

// ==================== DIAGNOSTICS ====================

void ispisiBounceStatistics() {
  // Debug function to print bounce statistics
  // Useful for detecting electrical noise issues
  
  String log = F("Bounce statistics:\n");
  bool imaOdskoka = false;
  
  for (uint8_t i = 0; i < MAX_PINS; i++) {
    if (pinStanja[i].brojOdskoka > 0) {
      imaOdskoka = true;
      log += F("Pin ");
      log += i;
      log += F(": ");
      log += pinStanja[i].brojOdskoka;
      log += F(" bounces\n");
    }
  }
  
  if (!imaOdskoka) {
    log = F("Nema detektovanih odskoka");
  }
  
  posaljiPCLog(log);
}