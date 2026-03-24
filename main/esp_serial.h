// esp_serial.h
#pragma once
#include <Arduino.h>

void inicijalizirajESP();
void obradiESPSerijskuKomunikaciju();
void posaljiWifiPostavkeESP();
void posaljiESPKomandu(const String& komanda);
