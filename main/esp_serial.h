// esp_serial.h
#pragma once
#include <Arduino.h>

void inicijalizirajESP();
void obradiESPSerijskuKomunikaciju();
void posaljiWifiPostavkeESP();
void posaljiWiFiStatusESP();
void posaljiNTPPostavkeESP();
void posaljiMQTTPostavkeESP();
void posaljiNTPZahtjevESP();
void posaljiESPKomandu(const char* komanda);
void posaljiESPKomandu(const String& komanda);
bool jeWiFiPovezanNaESP();
