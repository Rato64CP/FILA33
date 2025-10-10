#include "postavke.h"
#include <Arduino.h>
#include <EEPROM.h>

int satOd = 7;
int satDo = 20;
int pauzaIzmeduUdaraca = 1000;

void ucitajPostavke() {
    // Placeholder: load settings from EEPROM
}

void spremiPostavke() {
    // Placeholder: store settings to EEPROM
}

void resetPostavke() {
    // Placeholder: reset settings to defaults
    satOd = 7;
    satDo = 20;
    pauzaIzmeduUdaraca = 1000;
}
