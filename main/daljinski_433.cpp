// daljinski_433.cpp - 433 MHz prijemnik SRX882 za toranjski sat

#include <Arduino.h>
#include <avr/pgmspace.h>
#include <stdio.h>

#include "daljinski_433.h"

#include "pc_serial.h"
#include "podesavanja_piny.h"
#include "slavljenje_mrtvacko.h"

namespace {

const unsigned long DALJINSKI_433_MIN_TRAJANJE_IMPULSA_US = 80UL;
const unsigned long DALJINSKI_433_GAP_OKVIRA_US = 5000UL;
const unsigned long DALJINSKI_433_MAKS_RAZMAK_PONAVLJANJA_MS = 350UL;
const uint8_t DALJINSKI_433_EV1527_BROJ_BITOVA = 24;
const uint8_t DALJINSKI_433_MAKS_IMPULSA = 80;
const uint8_t DALJINSKI_433_MIN_IMPULSA_ZA_OKVIR = 20;
const uint32_t DALJINSKI_433_KOD_TIPKE_C = 0x000000UL;

volatile uint16_t siroviImpulsi[DALJINSKI_433_MAKS_IMPULSA];
volatile uint8_t brojSirovihImpulsa = 0;
volatile unsigned long zadnjiRubUs = 0UL;
volatile bool okvirSpremanZaObradu = false;
bool daljinski433Inicijaliziran = false;
uint32_t zadnjiPrihvaceniKod = 0UL;
unsigned long zadnjePrihvacanjeKodaMs = 0UL;

void prekidDaljinskog433() {
  const unsigned long sadaUs = micros();
  const unsigned long razmakUs = sadaUs - zadnjiRubUs;
  zadnjiRubUs = sadaUs;

  if (okvirSpremanZaObradu) {
    return;
  }

  if (razmakUs < DALJINSKI_433_MIN_TRAJANJE_IMPULSA_US) {
    return;
  }

  if (razmakUs > DALJINSKI_433_GAP_OKVIRA_US) {
    if (brojSirovihImpulsa >= DALJINSKI_433_MIN_IMPULSA_ZA_OKVIR) {
      okvirSpremanZaObradu = true;
    } else {
      brojSirovihImpulsa = 0;
    }
    return;
  }

  if (brojSirovihImpulsa < DALJINSKI_433_MAKS_IMPULSA) {
    siroviImpulsi[brojSirovihImpulsa++] = static_cast<uint16_t>(razmakUs);
  } else {
    brojSirovihImpulsa = 0;
  }
}

bool jeUnutarTolerancije(unsigned long vrijednost, unsigned long cilj, unsigned long tolerancijaPosto) {
  const unsigned long tolerancija = (cilj * tolerancijaPosto) / 100UL;
  return vrijednost >= (cilj - tolerancija) && vrijednost <= (cilj + tolerancija);
}

bool dekodirajEv1527Okvir(const uint16_t* impulsi,
                          uint8_t brojImpulsa,
                          uint32_t* kod) {
  if (impulsi == nullptr || kod == nullptr || brojImpulsa < 50) {
    return false;
  }

  uint16_t bazniImpulsUs = 0xFFFFU;
  for (uint8_t i = 0; i < brojImpulsa; ++i) {
    const uint16_t impuls = impulsi[i];
    if (impuls >= 150U && impuls < bazniImpulsUs) {
      bazniImpulsUs = impuls;
    }
  }

  if (bazniImpulsUs == 0xFFFFU) {
    return false;
  }

  int indeksSinkroPocetka = -1;
  for (uint8_t i = 0; i + 1 < brojImpulsa; ++i) {
    const uint16_t prvi = impulsi[i];
    const uint16_t drugi = impulsi[i + 1];
    const bool prviKratki = jeUnutarTolerancije(prvi, bazniImpulsUs, 70UL);
    const bool drugiDugi = drugi >= static_cast<uint16_t>(bazniImpulsUs * 8UL);
    if (prviKratki && drugiDugi) {
      indeksSinkroPocetka = i;
      break;
    }
  }

  if (indeksSinkroPocetka < 0) {
    return false;
  }

  const int indeksPodataka = indeksSinkroPocetka + 2;
  const int potrebniImpulsi = DALJINSKI_433_EV1527_BROJ_BITOVA * 2;
  if ((indeksPodataka + potrebniImpulsi) > brojImpulsa) {
    return false;
  }

  uint32_t procitaniKod = 0UL;
  for (uint8_t bit = 0; bit < DALJINSKI_433_EV1527_BROJ_BITOVA; ++bit) {
    const uint16_t prvi = impulsi[indeksPodataka + (bit * 2)];
    const uint16_t drugi = impulsi[indeksPodataka + (bit * 2) + 1];
    const bool prviKratki = jeUnutarTolerancije(prvi, bazniImpulsUs, 70UL);
    const bool prviDugi = jeUnutarTolerancije(prvi, bazniImpulsUs * 3UL, 45UL);
    const bool drugiKratki = jeUnutarTolerancije(drugi, bazniImpulsUs, 70UL);
    const bool drugiDugi = jeUnutarTolerancije(drugi, bazniImpulsUs * 3UL, 45UL);

    procitaniKod <<= 1;
    if (prviKratki && drugiDugi) {
      // bit 0
    } else if (prviDugi && drugiKratki) {
      procitaniKod |= 1UL;
    } else {
      return false;
    }
  }

  *kod = procitaniKod;
  return true;
}

void obradiKodDaljinskog(uint32_t kod) {
  const unsigned long sadaMs = millis();
  if (kod == zadnjiPrihvaceniKod &&
      (sadaMs - zadnjePrihvacanjeKodaMs) < DALJINSKI_433_MAKS_RAZMAK_PONAVLJANJA_MS) {
    return;
  }

  zadnjiPrihvaceniKod = kod;
  zadnjePrihvacanjeKodaMs = sadaMs;

  if (DALJINSKI_433_KOD_TIPKE_C == 0UL) {
    char log[112];
    snprintf_P(log,
               sizeof(log),
               PSTR("433 MHz: procitan kod 0x%06lX, upisi ga kao kod tipke C u daljinski_433.cpp"),
               kod);
    posaljiPCLog(log);
    return;
  }

  if (kod == DALJINSKI_433_KOD_TIPKE_C) {
    preklopiSlavljenjeDaljinskimUpravljacem();
    return;
  }

  char log[88];
  snprintf_P(log,
             sizeof(log),
             PSTR("433 MHz: ignoriram nepoznat kod 0x%06lX"),
             kod);
  posaljiPCLog(log);
}

}  // namespace

void inicijalizirajDaljinski433() {
  pinMode(PIN_DALJINSKI_433_DATA, INPUT);
  brojSirovihImpulsa = 0;
  zadnjiRubUs = micros();
  okvirSpremanZaObradu = false;
  zadnjiPrihvaceniKod = 0UL;
  zadnjePrihvacanjeKodaMs = 0UL;

  attachInterrupt(digitalPinToInterrupt(PIN_DALJINSKI_433_DATA), prekidDaljinskog433, CHANGE);

  daljinski433Inicijaliziran = true;
  posaljiPCLog(F("433 MHz: inicijaliziran SRX882 na D3, cekam nauceni kod tipke C za slavljenje"));
}

void obradiDaljinski433() {
  if (!daljinski433Inicijaliziran) {
    return;
  }

  const unsigned long sadaUs = micros();
  noInterrupts();
  if (!okvirSpremanZaObradu &&
      brojSirovihImpulsa >= DALJINSKI_433_MIN_IMPULSA_ZA_OKVIR &&
      (sadaUs - zadnjiRubUs) > DALJINSKI_433_GAP_OKVIRA_US) {
    okvirSpremanZaObradu = true;
  }

  if (!okvirSpremanZaObradu) {
    interrupts();
    return;
  }

  uint16_t lokalniImpulsi[DALJINSKI_433_MAKS_IMPULSA];
  const uint8_t lokalniBrojImpulsa = brojSirovihImpulsa;
  for (uint8_t i = 0; i < lokalniBrojImpulsa; ++i) {
    lokalniImpulsi[i] = siroviImpulsi[i];
  }
  brojSirovihImpulsa = 0;
  okvirSpremanZaObradu = false;
  interrupts();

  uint32_t kod = 0UL;
  if (dekodirajEv1527Okvir(lokalniImpulsi, lokalniBrojImpulsa, &kod)) {
    obradiKodDaljinskog(kod);
    return;
  }

  char log[72];
  snprintf_P(log,
             sizeof(log),
             PSTR("433 MHz: ne mogu dekodirati okvir, broj impulsa=%u"),
             static_cast<unsigned>(lokalniBrojImpulsa));
  posaljiPCLog(log);
}
