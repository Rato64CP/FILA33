// tipke.cpp – Upravljanje tipkama i menijom za postavke
#include <Arduino.h>
#include "tipke.h"
#include "lcd_display.h"
#include "pc_serial.h"
#include "debouncing.h"
#include "menu_system.h"
#include "podesavanja_piny.h"

// ==================== KEYPAD CONFIGURATION ====================

// Key state tracking
static struct {
  uint8_t pin;
  KeyEvent event;
} keypadMapping[6] = {
  {PIN_KEY_UP, KEY_UP},
  {PIN_KEY_DOWN, KEY_DOWN},
  {PIN_KEY_LEFT, KEY_LEFT},
  {PIN_KEY_RIGHT, KEY_RIGHT},
  {PIN_KEY_SELECT, KEY_SELECT},
  {PIN_KEY_BACK, KEY_BACK}
};

static bool meniJeAktivan = false;
static unsigned long zadnjaDjetelnost = 0;
static const unsigned long TIMEOUT_MENIJA = 30000; // 30 seconds

// ==================== KEYPAD INITIALIZATION ====================

void inicijalizirajTipke() {
  // Initialize debouncing system
  inicijalizirajDebouncing();
  
  // Configure keypad pins as inputs with pull-ups
  for (int i = 0; i < 6; i++) {
    pinMode(keypadMapping[i].pin, INPUT_PULLUP);
  }
  
  posaljiPCLog(F("Tipke: inicijalizirane sa 6 kljuceva"));
}

// ==================== KEY SCANNING ====================

void provjeriTipke() {
  unsigned long sadaMs = millis();
  
  // Timeout menija ako nema aktivnosti
  if (meniJeAktivan && (sadaMs - zadnjaDjetelnost) > TIMEOUT_MENIJA) {
    meniJeAktivan = false;
    posaljiPCLog(F("Meni: timeout, povratak na prikaz sata"));
  }
  
  // Scan all 6 keys with debouncing
  for (int i = 0; i < 6; i++) {
    uint8_t pin = keypadMapping[i].pin;
    KeyEvent event = keypadMapping[i].event;
    
    SwitchState novoStanje;
    bool promjena = obradiDebouncedInput(pin, 30, &novoStanje);
    
    if (promjena && novoStanje == SWITCH_PRESSED) {
      // Key was pressed
      zadnjaDjetelnost = sadaMs;
      meniJeAktivan = true;
      
      String log = F("Tipka: ");
      switch (event) {
        case KEY_UP:
          log += F("UP");
          break;
        case KEY_DOWN:
          log += F("DOWN");
          break;
        case KEY_LEFT:
          log += F("LEFT");
          break;
        case KEY_RIGHT:
          log += F("RIGHT");
          break;
        case KEY_SELECT:
          log += F("SELECT");
          break;
        case KEY_BACK:
          log += F("BACK");
          break;
        default:
          log += F("UNKNOWN");
          break;
      }
      posaljiPCLog(log);
      
      // Send key event to menu system
      obradiKluc(event);
    }
  }
}

// ==================== MENU STATE ====================

bool uPostavkama() {
  return meniJeAktivan && dohvatiMenuState() != MENU_STATE_DISPLAY_TIME;
}
