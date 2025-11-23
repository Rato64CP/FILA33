#pragma once

#include <stddef.h>

// Adapter za 24C32 I2C EEPROM na RTC pločici korišten za toranjski sat.
namespace VanjskiEEPROM {

// Inicijaliziraj I2C sabirnicu i provjeri dostupnost EEPROM čipa.
bool inicijaliziraj();

// Čitanje sirovih bajtova s zadane adrese.
bool procitaj(int adresa, void* odrediste, size_t duljina);

// Zapis sirovih bajtova uz stranično adresiranje.
bool zapisi(int adresa, const void* izvor, size_t duljina);

// Pomoćni predlošci koji imitiraju EEPROM.get/put API.
template <typename T>
bool get(int adresa, T& cilj) {
  return procitaj(adresa, &cilj, sizeof(T));
}

template <typename T>
bool put(int adresa, const T& izvor) {
  return zapisi(adresa, &izvor, sizeof(T));
}

}  // namespace VanjskiEEPROM

