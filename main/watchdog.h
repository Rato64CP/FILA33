// watchdog.h – Watchdog monitoring za 24/7 pouzdanost
#pragma once

// Inicijalizacija watchdog timera
// Postavlja WDT na ~8 sekundi (maximum za ATmega2560)
void inicijalizirajWatchdog();

// Osvježi watchdog brojač (mora se pozivati najmanje svaki 8 sekundi)
void osvjeziWatchdog();
