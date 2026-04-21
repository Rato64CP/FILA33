// watchdog.h - Watchdog nadzor za 24/7 pouzdanost
#pragma once

#include <stdint.h>

// Inicijalizacija watchdog timera
// Postavlja WDT na ~8 sekundi (maximum za ATmega2560)
void inicijalizirajWatchdog();

// Osvježi watchdog brojač (mora se pozivati najmanje svaki 8 sekundi)
void osvjeziWatchdog();

// Dohvati sirove MCU reset zastavice (MCUSR) očitane pri bootu.
uint8_t dohvatiResetFlags();

// True ako je zadnji reset uzrokovan watchdog timerom.
bool jeWatchdogResetDetektiran();

// True ako je zadnji reset najvjerojatnije povezan s gubitkom napajanja.
bool jePowerLossResetDetektiran();

// True ako zadnji reset treba tretirati kao boot recovery scenarij.
bool jeBootRecoveryResetDetektiran();
