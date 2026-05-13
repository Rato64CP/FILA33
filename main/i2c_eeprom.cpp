#include "i2c_eeprom.h"

#include <Arduino.h>
#include <Wire.h>
#include "i2c_bus.h"
#include "watchdog.h"

namespace {
constexpr uint8_t MEMORIJA_ADRESA_MIN = 0x50;     // 24C32 i FM24W256 koriste A0/A1/A2 pa mogu biti na 0x50-0x57
constexpr uint8_t MEMORIJA_ADRESA_MAX = 0x57;
constexpr uint8_t ZADANA_MEMORIJA_ADRESA = 0x57;  // Najcesca adresa na dosadasnjoj RTC/memorijskoj plocici toranjskog sata
constexpr size_t VELICINA_STRANICE = 32;          // Zadrzano radi kompatibilnog segmentiranja I2C zapisa
constexpr size_t MAX_I2C_PODATAKA_PO_PAKETU = 30; // AVR Wire buffer: 32 bajta - 2 bajta adrese
constexpr size_t FIZICKI_KAPACITET = 32768;       // FM24W256 = 256 kbit = 32768 bajtova
constexpr size_t LOGICKI_KAPACITET = 4096;        // Toranjski sat zadrzava postojeci 24C32 raspored prvih 4096 bajtova
constexpr uint8_t BROJ_POKUSAJA_I2C = 3;
constexpr unsigned long CEKANJE_IZMEDU_POKUSAJA_MS = 2UL;
constexpr unsigned long CEKANJE_DOVRSETKA_UPISA_MS = 12UL;
constexpr unsigned long PONOVNI_POKUSAJ_INICIJALIZACIJE_MS = 5000UL;

bool inicijaliziran = false;
uint8_t aktivnaAdresaMemorije = 0;
unsigned long zadnjiPokusajInicijalizacijeMs = 0;

static_assert(FIZICKI_KAPACITET >= LOGICKI_KAPACITET,
              "FM24W256 mora pokrivati zadrzani 4 KB kompatibilni raspored");

bool jeUnutarOpsega(int adresa, size_t duljina) {
  return adresa >= 0 && (static_cast<size_t>(adresa) + duljina) <= LOGICKI_KAPACITET;
}

bool jeValjanaAdresaMemorije(uint8_t adresa) {
  return adresa >= MEMORIJA_ADRESA_MIN && adresa <= MEMORIJA_ADRESA_MAX;
}

bool provjeriDostupnostMemorijeNaAdresi(uint8_t adresa) {
  Wire.beginTransmission(adresa);
  Wire.write(0x00);
  Wire.write(0x00);
  return Wire.endTransmission() == 0;
}

bool pronadiDostupnuAdresuMemorije(uint8_t* pronadjenaAdresa) {
  if (pronadjenaAdresa == nullptr) {
    return false;
  }

  if (jeValjanaAdresaMemorije(aktivnaAdresaMemorije) &&
      provjeriDostupnostMemorijeNaAdresi(aktivnaAdresaMemorije)) {
    *pronadjenaAdresa = aktivnaAdresaMemorije;
    return true;
  }

  if (aktivnaAdresaMemorije != ZADANA_MEMORIJA_ADRESA &&
      provjeriDostupnostMemorijeNaAdresi(ZADANA_MEMORIJA_ADRESA)) {
    *pronadjenaAdresa = ZADANA_MEMORIJA_ADRESA;
    return true;
  }

  for (uint8_t adresa = MEMORIJA_ADRESA_MIN; adresa <= MEMORIJA_ADRESA_MAX; ++adresa) {
    if (adresa == aktivnaAdresaMemorije || adresa == ZADANA_MEMORIJA_ADRESA) {
      continue;
    }

    if (provjeriDostupnostMemorijeNaAdresi(adresa)) {
      *pronadjenaAdresa = adresa;
      return true;
    }
  }

  return false;
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
  uint8_t pronadjenaAdresa = 0;
  inicijaliziran = pronadiDostupnuAdresuMemorije(&pronadjenaAdresa);
  if (inicijaliziran) {
    aktivnaAdresaMemorije = pronadjenaAdresa;
  } else {
    aktivnaAdresaMemorije = 0;
  }
  return inicijaliziran;
}

void oznaciI2CGresku() {
  inicijaliziran = false;
  aktivnaAdresaMemorije = 0;
}

void kratkoCekajUzWatchdog(unsigned long trajanjeMs) {
  osvjeziWatchdogAkoJeAktivan();
  delay(trajanjeMs);
  osvjeziWatchdogAkoJeAktivan();
}

bool procitajBlokJedanPokusaj(int adresa, uint8_t* odrediste, size_t duljina) {
  if (!jeValjanaAdresaMemorije(aktivnaAdresaMemorije)) {
    return false;
  }

  Wire.beginTransmission(aktivnaAdresaMemorije);
  Wire.write(static_cast<uint8_t>((adresa >> 8) & 0xFF));
  Wire.write(static_cast<uint8_t>(adresa & 0xFF));
  if (Wire.endTransmission(false) != 0) {
    return false;
  }

  const size_t procitano =
      Wire.requestFrom(static_cast<int>(aktivnaAdresaMemorije), static_cast<int>(duljina));
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
  if (!jeValjanaAdresaMemorije(aktivnaAdresaMemorije)) {
    return false;
  }

  Wire.beginTransmission(aktivnaAdresaMemorije);
  Wire.write(static_cast<uint8_t>((adresa >> 8) & 0xFF));
  Wire.write(static_cast<uint8_t>(adresa & 0xFF));
  for (size_t i = 0; i < duljina; ++i) {
    Wire.write(izvor[i]);
  }

  return Wire.endTransmission() == 0;
}

bool cekajDovrsetakInternogZapisa() {
  // 24C32 nakon upisa privremeno ne odgovara dok ne zavrsi interni ciklus.
  // FM24W256 odgovara prakticki odmah, pa isti polling ostaje kompatibilan
  // s oba tipa memorije na RTC plocici toranjskog sata.
  if (!jeValjanaAdresaMemorije(aktivnaAdresaMemorije)) {
    return false;
  }

  const unsigned long pocetakMs = millis();
  do {
    osvjeziWatchdogAkoJeAktivan();
    Wire.beginTransmission(aktivnaAdresaMemorije);
    if (Wire.endTransmission() == 0) {
      return true;
    }
    kratkoCekajUzWatchdog(1UL);
  } while ((millis() - pocetakMs) < CEKANJE_DOVRSETKA_UPISA_MS);

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
  uint8_t postojeciBlok[MAX_I2C_PODATAKA_PO_PAKETU];

  while (preostalo > 0) {
    osvjeziWatchdogAkoJeAktivan();
    size_t offset = static_cast<size_t>(adresa % static_cast<int>(VELICINA_STRANICE));
    size_t prostorDoKrajaStranice = VELICINA_STRANICE - offset;
    size_t blok = (preostalo < prostorDoKrajaStranice) ? preostalo : prostorDoKrajaStranice;

    if (blok > MAX_I2C_PODATAKA_PO_PAKETU) {
      blok = MAX_I2C_PODATAKA_PO_PAKETU;
    }

    const size_t preostaliKapacitet = LOGICKI_KAPACITET - static_cast<size_t>(adresa);
    if (blok > preostaliKapacitet) {
      blok = preostaliKapacitet;
    }

    bool procitanPostojeciBlok = false;
    bool trebaPisati = true;
    size_t prviRazliciti = 0;
    size_t zadnjiRazliciti = 0;

    if (procitajBlokUzPokusaje(adresa, postojeciBlok, blok)) {
      procitanPostojeciBlok = true;
      trebaPisati = false;
      for (size_t i = 0; i < blok; ++i) {
        if (postojeciBlok[i] != izvorBajtovi[i]) {
          if (!trebaPisati) {
            prviRazliciti = i;
            trebaPisati = true;
          }
          zadnjiRazliciti = i;
        }
      }
    }

    if (trebaPisati) {
      const int adresaZaUpis =
          procitanPostojeciBlok ? (adresa + static_cast<int>(prviRazliciti)) : adresa;
      const uint8_t* izvorZaUpis =
          procitanPostojeciBlok ? (izvorBajtovi + prviRazliciti) : izvorBajtovi;
      const size_t duljinaPromjene =
          procitanPostojeciBlok ? ((zadnjiRazliciti - prviRazliciti) + 1U) : blok;

      if (!zapisiBlokUzPokusaje(adresaZaUpis, izvorZaUpis, duljinaPromjene)) {
        return false;
      }
    }

    izvorBajtovi += blok;
    adresa += static_cast<int>(blok);
    preostalo -= blok;
  }

  return true;
}

}  // namespace VanjskiEEPROM
