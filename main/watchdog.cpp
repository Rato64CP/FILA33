// watchdog.cpp - Watchdog nadzor za 24/7 pouzdanost
#include <Arduino.h>
#include <avr/wdt.h>
#include "watchdog.h"
#include "pc_serial.h"

// ==================== WATCHDOG SETUP ====================

static uint8_t zadnje_reset_zastavice = 0;
static bool reset_zastavice_ucitane = false;

void pripremiResetFlagsMCU() {
  if (reset_zastavice_ucitane) {
    return;
  }

  zadnje_reset_zastavice = MCUSR;
  MCUSR = 0;
  wdt_disable();
  reset_zastavice_ucitane = true;
}

void inicijalizirajWatchdog() {
  // ATmega2560 ima WDT s timeout vrijednostima do 8 sekundi.
  // Ovdje samo logiramo vec spremljeni uzrok reseta i ponovno
  // ukljucujemo watchdog za redovni 24/7 rad.
  pripremiResetFlagsMCU();

  const uint8_t mcusr = zadnje_reset_zastavice;
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

  wdt_enable(WDTO_8S);
  posaljiPCLog(F("WDT: Inicijaliziran sa timeoutom od 8 sekundi"));
}

// ==================== WATCHDOG REFRESH ====================

void osvjeziWatchdog() {
  // Mora se pozivati barem jednom unutar 8 sekundi.
  wdt_reset();
}

uint8_t dohvatiResetFlags() {
  return zadnje_reset_zastavice;
}

bool jeWatchdogResetDetektiran() {
  return (zadnje_reset_zastavice & (1 << WDRF)) != 0;
}

bool jePowerLossResetDetektiran() {
  const bool imaBrownOutIliPowerOn =
      (zadnje_reset_zastavice & ((1 << BORF) | (1 << PORF))) != 0;
  const bool imaVanjskiReset = (zadnje_reset_zastavice & (1 << EXTRF)) != 0;
  return imaBrownOutIliPowerOn && !imaVanjskiReset;
}

bool jeBootRecoveryResetDetektiran() {
  return jeWatchdogResetDetektiran() || jePowerLossResetDetektiran();
}
