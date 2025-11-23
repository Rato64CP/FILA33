// watchdog.cpp
#include <Arduino.h>
#include "watchdog.h"

#if defined(__AVR__)
#include <avr/wdt.h>

static bool watchdogAktivan = false;

void inicijalizirajWatchdog() {
  // Osiguraj poznato stanje nakon potencijalnog WDT resetiranja.
  wdt_disable();
  delay(10);
  wdt_enable(WDTO_8S);
  watchdogAktivan = true;
}

void osvjeziWatchdog() {
  if (!watchdogAktivan) return;
  wdt_reset();
}

#else

void inicijalizirajWatchdog() {}

void osvjeziWatchdog() {}

#endif
