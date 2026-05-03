#include "i2c_eeprom.h"

#include <Arduino.h>
#include <Wire.h>
#include "i2c_bus.h"
#include "watchdog.h"

namespace {
constexpr uint8_t EEPROM_ADRESA = 0x57;           // Tipicna adresa 24C32 na RTC plocici
constexpr size_t VELICINA_STRANICE = 32;          // 24C32 zapisuje po 32 bajta
constexpr size_t MAX_I2C_PODATAKA_PO_PAKETU = 30; // AVR Wire buffer: 32 bajta - 2 bajta adrese
constexpr size_t UKUPNI_KAPACITET = 4096;         // 32 kbit = 4096 bajtova
constexpr uint8_t BROJ_POKUSAJA_I2C = 3;
constexpr unsigned long CEKANJE_IZMEDU_POKUSAJA_MS = 2UL;
constexpr unsigned long TIMEOUT_ACK_POLLING_ZAPISA_MS = 25UL;
constexpr unsigned long PONOVNI_POKUSAJ_INICIJALIZACIJE_MS = 5000UL;

bool inicijaliziran = false;
unsigned long zadnjiPokusajInicijalizacijeMs = 0;

bool jeUnutarOpsega(int adresa, size_t duljina) {
  return adresa >= 0 && (static_cast<size_t>(adresa) + duljina) <= UKUPNI_KAPACITET;
}

bool provjeriDostupnostEEPROMa() {
  Wire.beginTransmission(EEPROM_ADRESA);
  Wire.write(0x00);
  Wire.write(0x00);
  return Wire.endTransmission() == 0;
}

bool inicijalizirajUnutarnje(bool preskociOdgodu) {
  if (inicijaliziran) {
    return true;
  }

  const unsigned long sadaMs = millis();
  if (!preskociOdgodu &&
      zadnjiPokusajInicijalizacijeMs != 0 &&
      (sadaMs - zadnjiPokusajInicijalizacijeMs) < PONOVNI_POKUSAJ_INICIJALIZACIJE_MS) {
    return false;
  }

  zadnjiPokusajInicijalizacijeMs = sadaMs;
  pripremiI2CSabirnicuSigurno();
  inicijaliziran = provjeriDostupnostEEPROMa();
  return inicijaliziran;
}

void oznaciI2CGresku() {
  inicijaliziran = false;
}

void kratkoCekajUzWatchdog(unsigned long trajanjeMs) {
  osvjeziWatchdogAkoJeAktivan();
  delay(trajanjeMs);
  osvjeziWatchdogAkoJeAktivan();
}

bool procitajBlokJedanPokusaj(int adresa, uint8_t* odrediste, size_t duljina) {
  Wire.beginTransmission(EEPROM_ADRESA);
  Wire.write(static_cast<uint8_t>((adresa >> 8) & 0xFF));
  Wire.write(static_cast<uint8_t>(adresa & 0xFF));
  if (Wire.endTransmission(false) != 0) {
    return false;
  }

  const size_t procitano =
      Wire.requestFrom(static_cast<int>(EEPROM_ADRESA), static_cast<int>(duljina));
  if (procitano != duljina) {
    return false;
  }

  for (size_t i = 0; i < duljina; ++i) {
    if (!Wire.available()) {
      return false;
    }
    odrediste[i] = Wire.read();
  }

  return true;
}

bool procitajBlokUzPokusaje(int adresa, uint8_t* odrediste, size_t duljina) {
  for (uint8_t pokusaj = 0; pokusaj < BROJ_POKUSAJA_I2C; ++pokusaj) {
    osvjeziWatchdogAkoJeAktivan();
    if (!inicijalizirajUnutarnje(pokusaj != 0)) {
      kratkoCekajUzWatchdog(CEKANJE_IZMEDU_POKUSAJA_MS);
      continue;
    }

    if (procitajBlokJedanPokusaj(adresa, odrediste, duljina)) {
      return true;
    }

    oznaciI2CGresku();
    kratkoCekajUzWatchdog(CEKANJE_IZMEDU_POKUSAJA_MS);
  }

  return false;
}

bool zapisiBlokJedanPokusaj(int adresa, const uint8_t* izvor, size_t duljina) {
  Wire.beginTransmission(EEPROM_ADRESA);
  Wire.write(static_cast<uint8_t>((adresa >> 8) & 0xFF));
  Wire.write(static_cast<uint8_t>(adresa & 0xFF));
  for (size_t i = 0; i < duljina; ++i) {
    Wire.write(izvor[i]);
  }

  return Wire.endTransmission() == 0;
}

bool cekajDovrsetakInternogZapisa() {
  const unsigned long pocetakMs = millis();
  while ((millis() - pocetakMs) <= TIMEOUT_ACK_POLLING_ZAPISA_MS) {
    osvjeziWatchdogAkoJeAktivan();
    Wire.beginTransmission(EEPROM_ADRESA);
    if (Wire.endTransmission() == 0) {
      return true;
    }
    kratkoCekajUzWatchdog(1);
  }
  return false;
}

bool provjeriZapisBloka(int adresa, const uint8_t* izvor, size_t duljina) {
  uint8_t procitano[MAX_I2C_PODATAKA_PO_PAKETU];
  if (duljina > sizeof(procitano)) {
    return false;
  }

  if (!procitajBlokUzPokusaje(adresa, procitano, duljina)) {
    return false;
  }

  for (size_t i = 0; i < duljina; ++i) {
    if (procitano[i] != izvor[i]) {
      return false;
    }
  }

  return true;
}

bool zapisiBlokUzPokusaje(int adresa, const uint8_t* izvor, size_t duljina) {
  for (uint8_t pokusaj = 0; pokusaj < BROJ_POKUSAJA_I2C; ++pokusaj) {
    osvjeziWatchdogAkoJeAktivan();
    if (!inicijalizirajUnutarnje(pokusaj != 0)) {
      kratkoCekajUzWatchdog(CEKANJE_IZMEDU_POKUSAJA_MS);
      continue;
    }

    if (!zapisiBlokJedanPokusaj(adresa, izvor, duljina)) {
      oznaciI2CGresku();
      kratkoCekajUzWatchdog(CEKANJE_IZMEDU_POKUSAJA_MS);
      continue;
    }

    if (!cekajDovrsetakInternogZapisa()) {
      oznaciI2CGresku();
      kratkoCekajUzWatchdog(CEKANJE_IZMEDU_POKUSAJA_MS);
      continue;
    }

    if (provjeriZapisBloka(adresa, izvor, duljina)) {
      return true;
    }

    oznaciI2CGresku();
    kratkoCekajUzWatchdog(CEKANJE_IZMEDU_POKUSAJA_MS);
  }

  return false;
}
}  // namespace

namespace VanjskiEEPROM {

bool inicijaliziraj() {
  return inicijalizirajUnutarnje(false);
}

bool procitaj(int adresa, void* odrediste, size_t duljina) {
  if (odrediste == nullptr || !jeUnutarOpsega(adresa, duljina)) {
    return false;
  }

  if (!inicijalizirajUnutarnje(false) && !inicijalizirajUnutarnje(true)) {
    return false;
  }

  uint8_t* cilj = reinterpret_cast<uint8_t*>(odrediste);
  size_t preostalo = duljina;

  while (preostalo > 0) {
    osvjeziWatchdogAkoJeAktivan();
    const size_t blok = (preostalo < MAX_I2C_PODATAKA_PO_PAKETU)
                            ? preostalo
                            : MAX_I2C_PODATAKA_PO_PAKETU;

    if (!procitajBlokUzPokusaje(adresa, cilj, blok)) {
      return false;
    }

    cilj += blok;
    adresa += static_cast<int>(blok);
    preostalo -= blok;
  }

  return true;
}

bool zapisi(int adresa, const void* izvor, size_t duljina) {
  if (izvor == nullptr || !jeUnutarOpsega(adresa, duljina)) {
    return false;
  }

  if (!inicijalizirajUnutarnje(false) && !inicijalizirajUnutarnje(true)) {
    return false;
  }

  const uint8_t* izvorBajtovi = reinterpret_cast<const uint8_t*>(izvor);
  size_t preostalo = duljina;

  while (preostalo > 0) {
    osvjeziWatchdogAkoJeAktivan();
    size_t offset = static_cast<size_t>(adresa % static_cast<int>(VELICINA_STRANICE));
    size_t prostorDoKrajaStranice = VELICINA_STRANICE - offset;
    size_t blok = (preostalo < prostorDoKrajaStranice) ? preostalo : prostorDoKrajaStranice;

    if (blok > MAX_I2C_PODATAKA_PO_PAKETU) {
      blok = MAX_I2C_PODATAKA_PO_PAKETU;
    }

    const size_t preostaliKapacitet = UKUPNI_KAPACITET - static_cast<size_t>(adresa);
    if (blok > preostaliKapacitet) {
      blok = preostaliKapacitet;
    }

    if (!zapisiBlokUzPokusaje(adresa, izvorBajtovi, blok)) {
      return false;
    }

    izvorBajtovi += blok;
    adresa += static_cast<int>(blok);
    preostalo -= blok;
  }

  return true;
}

}  // namespace VanjskiEEPROM
