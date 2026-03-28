// debouncing.cpp - softverski debounce za odabrane ulaze toranjskog sata
#include <Arduino.h>
#include "debouncing.h"
#include "pc_serial.h"
#include "podesavanja_piny.h"

namespace {

static const uint8_t PINOVI_DEBOUNCEA[] = {
  PIN_ULAZA_PLOCE_1,
  PIN_ULAZA_PLOCE_2,
  PIN_ULAZA_PLOCE_3,
  PIN_ULAZA_PLOCE_4,
  PIN_ULAZA_PLOCE_5,
  PIN_ULAZA_PLOCE_6,
  PIN_ULAZA_PLOCE_7,
  PIN_ULAZA_PLOCE_8,
  PIN_ULAZA_PLOCE_9,
  PIN_ULAZA_PLOCE_10,
  PIN_BELL1_SWITCH,
  PIN_BELL2_SWITCH,
  PIN_KEY_CELEBRATION,
  PIN_KEY_FUNERAL,
  PIN_KEY_UP,
  PIN_KEY_DOWN,
  PIN_KEY_LEFT,
  PIN_KEY_RIGHT,
  PIN_KEY_SELECT,
  PIN_KEY_BACK
};

static const uint8_t BROJ_DEBOUNCE_PINOVA =
    static_cast<uint8_t>(sizeof(PINOVI_DEBOUNCEA) / sizeof(PINOVI_DEBOUNCEA[0]));

struct DebounceStanje {
  SwitchState trenutnoStanje;
  unsigned long vrijemePocetkaOdskoka;
  bool uOdskoku;
};

static DebounceStanje pinStanja[BROJ_DEBOUNCE_PINOVA];
static bool debouncingInicijaliziran = false;

static int pronadiIndeksPina(uint8_t pinNumber) {
  for (uint8_t i = 0; i < BROJ_DEBOUNCE_PINOVA; i++) {
    if (PINOVI_DEBOUNCEA[i] == pinNumber) {
      return i;
    }
  }
  return -1;
}

}  // namespace

void inicijalizirajDebouncing() {
  if (debouncingInicijaliziran) {
    posaljiPCLog(F("Debouncing sistem vec inicijaliziran, preskacem"));
    return;
  }

  for (uint8_t i = 0; i < BROJ_DEBOUNCE_PINOVA; i++) {
    pinStanja[i].trenutnoStanje = SWITCH_RELEASED;
    pinStanja[i].vrijemePocetkaOdskoka = 0;
    pinStanja[i].uOdskoku = false;
  }

  debouncingInicijaliziran = true;
  posaljiPCLog(F("Debouncing sistem inicijaliziran za odabrane ulaze"));
}

bool obradiDebouncedInput(uint8_t pinNumber, uint8_t debounceTimeMs, SwitchState* novoStanje) {
  if (novoStanje == NULL) {
    return false;
  }

  const int indeks = pronadiIndeksPina(pinNumber);
  if (indeks < 0) {
    *novoStanje = SWITCH_RELEASED;
    return false;
  }

  const SwitchState fizickoStanje =
      (digitalRead(pinNumber) == LOW) ? SWITCH_PRESSED : SWITCH_RELEASED;
  const unsigned long sada = millis();

  if (fizickoStanje == pinStanja[indeks].trenutnoStanje) {
    if (pinStanja[indeks].uOdskoku) {
      pinStanja[indeks].uOdskoku = false;
      pinStanja[indeks].vrijemePocetkaOdskoka = 0;
    }
    *novoStanje = pinStanja[indeks].trenutnoStanje;
    return false;
  }

  if (!pinStanja[indeks].uOdskoku) {
    pinStanja[indeks].vrijemePocetkaOdskoka = sada;
    pinStanja[indeks].uOdskoku = true;
    *novoStanje = pinStanja[indeks].trenutnoStanje;
    return false;
  }

  const unsigned long vrijemeProslo = sada - pinStanja[indeks].vrijemePocetkaOdskoka;
  if (vrijemeProslo >= debounceTimeMs) {
    pinStanja[indeks].trenutnoStanje = fizickoStanje;
    pinStanja[indeks].uOdskoku = false;
    pinStanja[indeks].vrijemePocetkaOdskoka = 0;
    *novoStanje = pinStanja[indeks].trenutnoStanje;
    return true;
  }

  *novoStanje = pinStanja[indeks].trenutnoStanje;
  return false;
}
