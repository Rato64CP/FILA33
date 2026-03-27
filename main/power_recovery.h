#pragma once

#include <Arduino.h>

// Inicijalizacija recovery sustava toranjskog sata.
void inicijalizirajPowerRecovery();

// Pokretanje boot recovery postupka nakon watchdog/power-loss reseta.
void odradiBootRecovery();

// Periodicko spremanje kriticnog stanja kazaljki i okretne ploce.
void spremiKriticalnoStanje();

// Provjera zdravlja vanjskog EEPROM-a za recovery toranjskog sata.
bool provjeriZdravostEEPROM();

// Oznake uzroka reseta za recovery logiku.
void oznaciWatchdogReset(bool resetiranWatchdog);
void oznaciGubitakNapajanja(bool izgubljenoNapajanje);
