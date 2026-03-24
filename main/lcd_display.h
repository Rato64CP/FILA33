// lcd_display.h
#pragma once

void inicijalizirajLCD();
void prikaziSat();
void prikaziPostavke();
void prikaziPoruku(const char* redak1, const char* redak2 = "");
void postaviLCDBlinkanje(bool omoguci);
void odradiPauzuSaLCD(unsigned long trajanjeMs);

// Signalizacija statusa otkucavanja na LCD-u
void signalizirajHammer1_Active();
void signalizirajHammer2_Active();
void signalizirajCelebration_Mode();
void signalizirajFuneral_Mode();
