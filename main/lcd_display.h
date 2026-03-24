// lcd_display.h
#pragma once

#include <LiquidCrystal_I2C.h>

extern LiquidCrystal_I2C lcd;

void inicijalizirajLCD();
void prikaziSat();
void prikaziPostavke();
void prikaziPoruku(const char* redak1, const char* redak2 = "");
void postaviLCDBlinkanje(bool omoguci);
void odradiPauzuSaLCD(unsigned long trajanjeMs);

// Signalizacija statusa otkucavanja na LCD-u
void signalizirajBell1_Ringing();
void signalizirajBell2_Ringing();
void signalizirajHammer1_Active();
void signalizirajHammer2_Active();
void signalizirajCelebration_Mode();
void signalizirajFuneral_Mode();
