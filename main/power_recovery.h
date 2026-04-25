#pragma once

#include <Arduino.h>

// Inicijalizacija recovery sustava toranjskog sata.
void inicijalizirajPowerRecovery();

// Pokretanje boot recovery postupka nakon watchdog/power-loss reseta.
void odradiBootRecovery();

// Periodicko spremanje kriticnog stanja kazaljki i okretne ploce.
void spremiKriticalnoStanje();

// Periodicki health-check i latched fault obrada za toranjski sat.
void osvjeziPowerRecoveryDijagnostiku();

// Provjera zdravlja vanjskog EEPROM-a za recovery toranjskog sata.
bool provjeriZdravostEEPROM();
bool jeLatchedFaultAktivan();
bool potvrdiLatchedFault();
bool jeEepromDegradiraniNacinAktivan();

// True ako je toranjski sat zakljucan zbog vise watchdog reset petlji.
bool jeSafeModeAktivan();

// Primjenjuje lockdown blokade na mehaniku toranjskog sata.
void primijeniSafeModeAkoTreba();

// Servisno otkljucavanje toranjskog sata iz watchdog lockdowna.
bool otkljucajSafeMode();
