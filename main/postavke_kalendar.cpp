// postavke_kalendar.cpp - Liturgijski kalendar i satnice misa toranjskog sata
#include <Arduino.h>

#include "postavke_kalendar.h"

namespace {

constexpr uint8_t SVI_SVETI_ZADANI_POCETAK_SAT = 15;
constexpr uint8_t SVI_SVETI_ZADANI_ZAVRSETAK_SAT = 8;
constexpr uint8_t ZADANA_MINUTA_BLAGDANSKE_MISE = 0;

struct ZadaniNepomicniBlagdanInfo {
  uint8_t mjesec;
  uint8_t dan;
};

struct ZadaniPomicniBlagdanInfo {
  int8_t pomakOdUskrsaDana;
};

constexpr ZadaniNepomicniBlagdanInfo ZADANI_NEPOMICNI_BLAGDANI[BROJ_NEPOMICNIH_BLAGDANA] = {
  {1, 1},   // Nova Godina
  {1, 6},   // Bogojavljenje
  {2, 2},   // Svjecnica
  {2, 10},  // Alojzije Stepinac
  {3, 19},  // Sveti Josip
  {3, 25},  // Blagovijest
  {6, 13},  // Sveti Ante
  {6, 29},  // Sveti Petar
  {8, 15},  // Velika Gospa
  {11, 1},  // Svi Sveti
  {11, 2},  // Dusni dan
  {12, 24}, // Badnjak
  {12, 25}, // Bozic
  {12, 26}, // Sveti Stjepan
  {12, 27}  // Sveti Ivan
};

constexpr ZadaniPomicniBlagdanInfo ZADANI_POMICNI_BLAGDANI[BROJ_POMICNIH_BLAGDANA] = {
  {-46}, // Pepelnica
  {-3},  // Veliki cetvrtak
  {0},   // Uskrs
  {1},   // Uskrsni ponedjeljak
  {39},  // Uzasasce
  {60},  // Tijelovo
  {68}   // Srce Isusovo
};

uint8_t ograniceniSatBlagdanskeMise(uint8_t sat) {
  return static_cast<uint8_t>(constrain(static_cast<int>(sat), 0, 23));
}

uint8_t ogranicenaMinutaBlagdanskeMise(uint8_t minuta) {
  return static_cast<uint8_t>(constrain(static_cast<int>(minuta), 0, 59));
}

void postaviZadaniNepomicniBlagdan(uint8_t indeks, NepomicniBlagdanPostavka& blagdan) {
  if (indeks >= BROJ_NEPOMICNIH_BLAGDANA) {
    blagdan.omogucen = false;
    blagdan.mjesec = 0;
    blagdan.dan = 0;
    blagdan.satMise = 0;
    blagdan.minutaMise = 0;
    return;
  }

  blagdan.mjesec = ZADANI_NEPOMICNI_BLAGDANI[indeks].mjesec;
  blagdan.dan = ZADANI_NEPOMICNI_BLAGDANI[indeks].dan;
}

void postaviZadaniPomicniBlagdan(uint8_t indeks, PomicniBlagdanPostavka& blagdan) {
  if (indeks >= BROJ_POMICNIH_BLAGDANA) {
    blagdan.omogucen = false;
    blagdan.pomakOdUskrsaDana = 0;
    blagdan.satMise = 0;
    blagdan.minutaMise = 0;
    return;
  }

  blagdan.pomakOdUskrsaDana = ZADANI_POMICNI_BLAGDANI[indeks].pomakOdUskrsaDana;
}

}  // namespace

EepromLayout::BlagdaniSpremnik napraviZadaneBlagdane() {
  EepromLayout::BlagdaniSpremnik zadani = {};
  zadani.potpis = EepromLayout::BLAGDANI_POTPIS;
  zadani.verzija = EepromLayout::BLAGDANI_VERZIJA;
  zadani.reserved = 0;
  for (uint8_t i = 0; i < BROJ_NEPOMICNIH_BLAGDANA; ++i) {
    zadani.nepomicni[i].omogucen = 0;
    zadani.nepomicni[i].mjesec = ZADANI_NEPOMICNI_BLAGDANI[i].mjesec;
    zadani.nepomicni[i].dan = ZADANI_NEPOMICNI_BLAGDANI[i].dan;
    zadani.nepomicni[i].satMise = 0;
    zadani.nepomicni[i].minutaMise = ZADANA_MINUTA_BLAGDANSKE_MISE;
  }
  for (uint8_t i = 0; i < BROJ_POMICNIH_BLAGDANA; ++i) {
    zadani.pomicni[i].omogucen = 0;
    zadani.pomicni[i].pomakOdUskrsaDana = ZADANI_POMICNI_BLAGDANI[i].pomakOdUskrsaDana;
    zadani.pomicni[i].satMise = 0;
    zadani.pomicni[i].minutaMise = ZADANA_MINUTA_BLAGDANSKE_MISE;
  }
  return zadani;
}

EepromLayout::MiseSpremnik napraviZadaneMise() {
  EepromLayout::MiseSpremnik zadane = {};
  zadane.potpis = EepromLayout::MISE_POTPIS;
  zadane.verzija = EepromLayout::MISE_VERZIJA;
  zadane.reserved = 0;
  zadane.dnevna.omogucena = 0;
  zadane.dnevna.satMise = 10;
  zadane.dnevna.minutaMise = 0;
  zadane.dnevna.reserved = 0;
  zadane.nedjeljna.omogucena = 0;
  zadane.nedjeljna.satMise = 10;
  zadane.nedjeljna.minutaMise = 0;
  zadane.nedjeljna.reserved = 0;
  return zadane;
}

uint8_t ograniceniSviSvetiPocetakSat(uint8_t sat) {
  return (sat <= 20) ? sat : SVI_SVETI_ZADANI_POCETAK_SAT;
}

uint8_t ograniceniSviSvetiZavrsetakSat(uint8_t sat) {
  return (sat >= 6 && sat <= 23) ? sat : SVI_SVETI_ZADANI_ZAVRSETAK_SAT;
}

void sanitizirajRedovituMisuPostavku(bool& omogucena, uint8_t& satMise, uint8_t& minutaMise) {
  if (!omogucena) {
    satMise = 0;
    minutaMise = 0;
    return;
  }

  satMise = ograniceniSatBlagdanskeMise(satMise);
  minutaMise = ogranicenaMinutaBlagdanskeMise(minutaMise);
}

void sanitizirajNepomicniBlagdan(uint8_t indeks, NepomicniBlagdanPostavka& blagdan) {
  postaviZadaniNepomicniBlagdan(indeks, blagdan);
  if (!blagdan.omogucen) {
    blagdan.satMise = 0;
    blagdan.minutaMise = 0;
    return;
  }

  blagdan.satMise = ograniceniSatBlagdanskeMise(blagdan.satMise);
  blagdan.minutaMise = ogranicenaMinutaBlagdanskeMise(blagdan.minutaMise);
}

void sanitizirajPomicniBlagdan(uint8_t indeks, PomicniBlagdanPostavka& blagdan) {
  postaviZadaniPomicniBlagdan(indeks, blagdan);
  if (!blagdan.omogucen) {
    blagdan.satMise = 0;
    blagdan.minutaMise = 0;
    return;
  }

  blagdan.satMise = ograniceniSatBlagdanskeMise(blagdan.satMise);
  blagdan.minutaMise = ogranicenaMinutaBlagdanskeMise(blagdan.minutaMise);
}
