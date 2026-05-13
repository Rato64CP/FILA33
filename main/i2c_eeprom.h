#pragma once

#include <stddef.h>

// Adapter za vanjski I2C spremnik na RTC plocici.
// Toranjski sat podrzava i `24C32 EEPROM` i `FM24W256 FRAM`,
// uz zadrzani 24C32-kompatibilni raspored prvih 4096 bajtova.
namespace VanjskiEEPROM {

// Inicijaliziraj I2C sabirnicu i provjeri dostupnost vanjske memorije.
bool inicijaliziraj();

// Citanje sirovih bajtova s zadane adrese.
bool procitaj(int adresa, void* odrediste, size_t duljina);

// Zapis sirovih bajtova uz stranicno adresiranje.
bool zapisi(int adresa, const void* izvor, size_t duljina);

// Pomocni predlosci koji imitiraju EEPROM.get/put API.
template <typename T>
bool get(int adresa, T& cilj) {
  return procitaj(adresa, &cilj, sizeof(T));
}

template <typename T>
bool put(int adresa, const T& izvor) {
  return zapisi(adresa, &izvor, sizeof(T));
}

}  // namespace VanjskiEEPROM
