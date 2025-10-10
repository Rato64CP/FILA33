#include "lcd_display.h"
#include <Arduino.h>

// Forward declarations of internal LCD helpers from lcd_prikaz.cpp
extern void azurirajLCDPrikaz();
extern void lcdPostaviNaSatPrikaz();
extern void lcdPrikaziPoruku(const char* redak1, const char* redak2);
extern void lcdPostaviBlinkanje(bool omoguci);
extern void lcdOdradiPauzu(unsigned long trajanjeMs);

// Jednostavna delegacija stvarnoj funkciji za prikaz sata
void prikaziSat() {
    lcdPostaviNaSatPrikaz();
    azurirajLCDPrikaz();
}

// Minimalna implementacija prikaza postavki na LCD-u
void prikaziPostavke() {
    lcdPostaviNaSatPrikaz();
    azurirajLCDPrikaz();
}

void prikaziPoruku(const char* redak1, const char* redak2) {
    lcdPrikaziPoruku(redak1, redak2);
}

void postaviLCDBlinkanje(bool omoguci) {
    lcdPostaviBlinkanje(omoguci);
}

void odradiPauzuSaLCD(unsigned long trajanjeMs) {
    lcdOdradiPauzu(trajanjeMs);
}
