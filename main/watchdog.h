#pragma once

// watchdog.h – Upravljanje ugrađenim WDT-om za toranjski sat
// Funkcije omogućuju da se glavna petlja i blokirajuće operacije redovito javlja
// watchdogu kako bi se Arduino resetirao u slučaju pada napona ili zastoja koda.

void inicijalizirajWatchdog();
void osvjeziWatchdog();
