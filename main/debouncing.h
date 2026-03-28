// debouncing.h – Software debouncing for keypad inputs and relay feedback
#pragma once

#include <stdint.h>

typedef enum {
  SWITCH_RELEASED = 0,
  SWITCH_PRESSED = 1
} SwitchState;

// Inicijalizacija debounce sustava
void inicijalizirajDebouncing();

// Obrada jednog digitalnog ulaza kroz debounce
// Vraca true ako se stabilno stanje promijenilo
bool obradiDebouncedInput(uint8_t pinNumber, uint8_t debounceTimeMs, SwitchState* novoStanje);
