// esp_serial.h
#pragma once
#include <Arduino.h>

void inicijalizirajESP();
void obradiESPSerijskuKomunikaciju();
void posaljiWifiPostavkeESP();
void posaljiNTPPostavkeESP();
void posaljiESPKomandu(const char* komanda);
void posaljiESPKomandu(const String& komanda);
