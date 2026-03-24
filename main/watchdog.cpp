// watchdog.cpp – Watchdog monitoring za 24/7 pouzdanost
#include <Arduino.h>
#include <avr/wdt.h>
#include "watchdog.h"
#include "pc_serial.h"

// ==================== WATCHDOG SETUP ====================

static uint8_t zadnje_reset_zastavice = 0;

void inicijalizirajWatchdog() {
  // ATmega2560 ima WDT s tim timeout vrijednostima:
  // 16 ms, 32 ms, 64 ms, 125 ms, 250 ms, 500 ms, 1s, 2s, 4s, 8s
  // Postavljamo na 8 sekundi kao maximum sigurnu vrijednost
  
  // Provjera razloga restart-a
  uint8_t mcusr = MCUSR;
  zadnje_reset_zastavice = mcusr;
  if (mcusr & (1 << WDRF)) {
    posaljiPCLog(F("WDT: Recovery nakon watchdog reset-a"));
  }
  if (mcusr & (1 << BORF)) {
    posaljiPCLog(F("WDT: Recovery nakon Brown-out reset-a"));
  }
  if (mcusr & (1 << EXTRF)) {
    posaljiPCLog(F("WDT: Recovery nakon vanjskog reset-a"));
  }
  if (mcusr & (1 << PORF)) {
    posaljiPCLog(F("WDT: Recovery nakon Power-on reset-a"));
  }
  
  MCUSR = 0; // Očisti sve zastavice
  
  // Disable WDT privremeno (osiguraj da se može prepisati)
  wdt_disable();
  
  // Postavi na 8 sekundi (WDTO_8S)
  // Ovo je sigurni timeout koji permet dovoljno vremena za sve operacije
  wdt_enable(WDTO_8S);
  
  posaljiPCLog(F("WDT: Inicijaliziran sa timeoutom od 8 sekundi"));
}

// ==================== WATCHDOG REFRESH ====================

void osvjeziWatchdog() {
  // Resetiraj WDT brojač
  // Mora se pozivati najmanje svakih 8 sekundi kako bi se izbjeglo resetiranje
  wdt_reset();
}

uint8_t dohvatiResetFlags() {
  return zadnje_reset_zastavice;
}

bool jeWatchdogResetDetektiran() {
  return (zadnje_reset_zastavice & (1 << WDRF)) != 0;
}

bool jePowerLossResetDetektiran() {
  return (zadnje_reset_zastavice & ((1 << BORF) | (1 << PORF))) != 0;
}
