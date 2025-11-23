#include "i2c_eeprom.h"

#include <Arduino.h>
#include <Wire.h>
// #include <SparkFun_External_EEPROM.h>  // nije ti potreban, slobodno izbriši

namespace {
constexpr uint8_t EEPROM_ADRESA = 0x57;           // Tipična adresa 24C32 na RTC pločici
constexpr size_t VELICINA_STRANICE = 32;          // 24C32 zapisuje po 32 bajta
constexpr size_t UKUPNI_KAPACITET = 4096;         // 32 kbit = 4096 bajtova
constexpr unsigned long CEKANJE_ZAPISA_MS = 5UL;  // Vrijeme interne pohrane nakon page write

bool inicijaliziran = false;

bool jeUnutarOpsega(int adresa, size_t duljina) {
  return adresa >= 0 && (static_cast<size_t>(adresa) + duljina) <= UKUPNI_KAPACITET;
}

bool provjeriDostupnost() {
  Wire.beginTransmission(EEPROM_ADRESA);
  Wire.write(0x00);
  Wire.write(0x00);
  return Wire.endTransmission() == 0;
}
}  // namespace

namespace VanjskiEEPROM {

bool inicijaliziraj() {
  if (!inicijaliziran) {
    Wire.begin();
    inicijaliziran = provjeriDostupnost();
  }
  return inicijaliziran;
}

bool procitaj(int adresa, void* odrediste, size_t duljina) {
  if (!inicijaliziraj() || !jeUnutarOpsega(adresa, duljina)) {
    return false;
  }

  uint8_t* cilj = reinterpret_cast<uint8_t*>(odrediste);
  size_t preostalo = duljina;

  while (preostalo > 0) {
    // umjesto std::min
    size_t blok = (preostalo < VELICINA_STRANICE) ? preostalo : VELICINA_STRANICE;

    Wire.beginTransmission(EEPROM_ADRESA);
    Wire.write(static_cast<uint8_t>((adresa >> 8) & 0xFF));
    Wire.write(static_cast<uint8_t>(adresa & 0xFF));
    if (Wire.endTransmission(false) != 0) {
      return false;
    }

    size_t procitano = Wire.requestFrom(static_cast<int>(EEPROM_ADRESA), static_cast<int>(blok));
    if (procitano != blok) {
      return false;
    }

    for (size_t i = 0; i < blok; ++i) {
      if (Wire.available()) {
        cilj[i] = Wire.read();
      } else {
        return false;
      }
    }

    cilj += blok;
    adresa += static_cast<int>(blok);
    preostalo -= blok;
  }

  return true;
}

bool zapisi(int adresa, const void* izvor, size_t duljina) {
  if (!inicijaliziraj() || !jeUnutarOpsega(adresa, duljina)) {
    return false;
  }

  const uint8_t* izvorBajtovi = reinterpret_cast<const uint8_t*>(izvor);
  size_t preostalo = duljina;

  while (preostalo > 0) {
    size_t offset = static_cast<size_t>(adresa % static_cast<int>(VELICINA_STRANICE));
    size_t prostor = VELICINA_STRANICE - offset;

    // prvo ograničimo blok na kraj stranice
    size_t blok = (preostalo < prostor) ? preostalo : prostor;

    // dodatno ograničenje da ne pređemo kraj EEPROM-a
    size_t preostaliKapacitet = UKUPNI_KAPACITET - static_cast<size_t>(adresa);
    if (blok > preostaliKapacitet) {
      blok = preostaliKapacitet;
    }

    Wire.beginTransmission(EEPROM_ADRESA);
    Wire.write(static_cast<uint8_t>((adresa >> 8) & 0xFF));
    Wire.write(static_cast<uint8_t>(adresa & 0xFF));
    for (size_t i = 0; i < blok; ++i) {
      Wire.write(izvorBajtovi[i]);
    }

    if (Wire.endTransmission() != 0) {
      return false;
    }

    delay(CEKANJE_ZAPISA_MS);

    izvorBajtovi += blok;
    adresa += static_cast<int>(blok);
    preostalo -= blok;
  }

  return true;
}

}  // namespace VanjskiEEPROM
