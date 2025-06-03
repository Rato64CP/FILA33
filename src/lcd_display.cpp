#include "lcd_display.h"
#include <Arduino.h>

// Forward declaration of internal LCD update function from lcd_prikaz.cpp
extern void azurirajLCDPrikaz();

// Jednostavna delegacija stvarnoj funkciji za prikaz sata
void prikaziSat() {
    azurirajLCDPrikaz();
}

// Minimalna implementacija prikaza postavki na LCD-u
void prikaziPostavke() {
    // Trenutno samo prikazi staticki tekst
    azurirajLCDPrikaz();
}
