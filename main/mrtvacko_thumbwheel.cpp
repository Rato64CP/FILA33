// mrtvacko_thumbwheel.cpp - Stabilno ocitavanje timera mrtvackog zvona s dvije BCD znamenke
#include <Arduino.h>

#include "mrtvacko_thumbwheel.h"
#include "pc_serial.h"
#include "podesavanja_piny.h"

namespace {

constexpr unsigned long PERIOD_OCITANJA_MS = 1000UL;

struct BCDOcitavanje {
  uint8_t desetice;
  uint8_t jedinice;
  bool valjano;
};

struct StanjeThumbwheela {
  BCDOcitavanje zadnjeSirovo;
  BCDOcitavanje stabilno;
  bool imaZadnjeSirovo;
  bool imaStabilno;
  bool prijavljenaNevaljanaKombinacija;
  unsigned long zadnjeOcitavanjeMs;
};

StanjeThumbwheela stanje = {{0, 0, false}, {0, 0, false}, false, false, false, 0};

constexpr uint8_t PINOVI_DESETICA[4] = {
  PIN_MRTVACKO_TIMER_DESETICE_BIT0,
  PIN_MRTVACKO_TIMER_DESETICE_BIT1,
  PIN_MRTVACKO_TIMER_DESETICE_BIT2,
  PIN_MRTVACKO_TIMER_DESETICE_BIT3
};

constexpr uint8_t PINOVI_JEDINICA[4] = {
  PIN_MRTVACKO_TIMER_JEDINICE_BIT0,
  PIN_MRTVACKO_TIMER_JEDINICE_BIT1,
  PIN_MRTVACKO_TIMER_JEDINICE_BIT2,
  PIN_MRTVACKO_TIMER_JEDINICE_BIT3
};

uint8_t procitajBCDZnamenku(const uint8_t pinovi[4]) {
  uint8_t vrijednost = 0;

  for (uint8_t i = 0; i < 4; ++i) {
    // Thumbwheel je planiran kao kontakt prema GND pa LOW predstavlja aktivan bit.
    if (digitalRead(pinovi[i]) == LOW) {
      vrijednost |= static_cast<uint8_t>(1U << i);
    }
  }

  return vrijednost;
}

BCDOcitavanje procitajSirovoStanje() {
  const uint8_t desetice = procitajBCDZnamenku(PINOVI_DESETICA);
  const uint8_t jedinice = procitajBCDZnamenku(PINOVI_JEDINICA);

  return {desetice, jedinice, desetice <= 9 && jedinice <= 9};
}

bool istaOcitavanja(const BCDOcitavanje& lhs, const BCDOcitavanje& rhs) {
  return lhs.desetice == rhs.desetice &&
         lhs.jedinice == rhs.jedinice &&
         lhs.valjano == rhs.valjano;
}

void prijaviStabilnoOcitavanje(const BCDOcitavanje& ocitanje) {
  String log = F("Mrtvacko thumbwheel: stabilno ocitanje ");
  if (ocitanje.desetice < 10) log += static_cast<char>('0' + ocitanje.desetice);
  if (ocitanje.jedinice < 10) log += static_cast<char>('0' + ocitanje.jedinice);
  log += F(" za timer mrtvackog zvona");
  posaljiPCLog(log);
}

void prijaviNevaljanoOcitavanje(const BCDOcitavanje& ocitanje) {
  String log = F("Mrtvacko thumbwheel: nevaljana BCD kombinacija D=");
  log += String(ocitanje.desetice);
  log += F(" J=");
  log += String(ocitanje.jedinice);
  log += F(" - zadrzavam zadnji stabilni timer mrtvackog zvona");
  posaljiPCLog(log);
}

}  // namespace

void inicijalizirajMrtvackoThumbwheel() {
  for (uint8_t i = 0; i < 4; ++i) {
    pinMode(PINOVI_DESETICA[i], INPUT_PULLUP);
    pinMode(PINOVI_JEDINICA[i], INPUT_PULLUP);
  }

  stanje = {{0, 0, false}, {0, 0, false}, false, false, false, 0};

  const BCDOcitavanje pocetno = procitajSirovoStanje();
  stanje.zadnjeSirovo = pocetno;
  stanje.imaZadnjeSirovo = true;
  stanje.zadnjeOcitavanjeMs = millis();

  if (pocetno.valjano) {
    stanje.stabilno = pocetno;
    stanje.imaStabilno = true;
    prijaviStabilnoOcitavanje(pocetno);
  } else {
    stanje.prijavljenaNevaljanaKombinacija = true;
    prijaviNevaljanoOcitavanje(pocetno);
  }
}

void osvjeziMrtvackoThumbwheel() {
  const unsigned long sadaMs = millis();
  if (sadaMs - stanje.zadnjeOcitavanjeMs < PERIOD_OCITANJA_MS) {
    return;
  }

  stanje.zadnjeOcitavanjeMs = sadaMs;
  const BCDOcitavanje sirovo = procitajSirovoStanje();

  if (!stanje.imaZadnjeSirovo) {
    stanje.zadnjeSirovo = sirovo;
    stanje.imaZadnjeSirovo = true;
    return;
  }

  if (!istaOcitavanja(sirovo, stanje.zadnjeSirovo)) {
    stanje.zadnjeSirovo = sirovo;
    return;
  }

  if (!sirovo.valjano) {
    if (!stanje.prijavljenaNevaljanaKombinacija) {
      stanje.prijavljenaNevaljanaKombinacija = true;
      prijaviNevaljanoOcitavanje(sirovo);
    }
    return;
  }

  stanje.prijavljenaNevaljanaKombinacija = false;

  if (!stanje.imaStabilno || !istaOcitavanja(sirovo, stanje.stabilno)) {
    stanje.stabilno = sirovo;
    stanje.imaStabilno = true;
    prijaviStabilnoOcitavanje(sirovo);
  }
}

bool jeMrtvackoThumbwheelValjan() {
  return stanje.imaStabilno && stanje.stabilno.valjano;
}

uint8_t dohvatiMrtvackoThumbwheelVrijednost() {
  if (!jeMrtvackoThumbwheelValjan()) {
    return 0;
  }

  return static_cast<uint8_t>(stanje.stabilno.desetice * 10U + stanje.stabilno.jedinice);
}

uint8_t dohvatiMrtvackoThumbwheelDesetice() {
  return jeMrtvackoThumbwheelValjan() ? stanje.stabilno.desetice : 0;
}

uint8_t dohvatiMrtvackoThumbwheelJedinice() {
  return jeMrtvackoThumbwheelValjan() ? stanje.stabilno.jedinice : 0;
}
