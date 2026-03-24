// debouncing.h – Software debouncing for keypad inputs and relay feedback
#pragma once

#include <stdint.h>

typedef enum {
  SWITCH_RELEASED = 0,
  SWITCH_PRESSED = 1
} SwitchState;

// Initialize debouncing system
void inicijalizirajDebouncing();

// Debounce a digital input
// Returns true if state has changed and is stable
// debounceTimeMs: time to wait for signal stability (typical: 20-50ms)
bool obradiDebouncedInput(uint8_t pinNumber, uint8_t debounceTimeMs, SwitchState* novoStanje);

// Debounce multiple pins in batch (optimized)
// pinMask: bitmask of pins to process (e.g., 0x3F for pins 0-5)
// Returns bitmap of pins that changed state
uint8_t obradiMultipleDebouncedInputs(uint8_t pinMask, uint8_t debounceTimeMs);

// Get current debounced state of a pin
SwitchState dohvatiDeboucedState(uint8_t pinNumber);

// Reset debouncing state for a specific pin
void resetDebounceState(uint8_t pinNumber);

// Reset all debouncing states
void resetAllDebounceStates();

// Get number of bounces detected (for diagnostics)
uint16_t dohvatiStatistikuOdskoka(uint8_t pinNumber);

// Clear bounce statistics
void resetStatistike();