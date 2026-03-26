#include <Arduino.h>
#include <RTClib.h>
#include "okretna_ploca.h"
#include "podesavanja_piny.h"
#include "time_glob.h"
#include "zvonjenje.h"
#include "postavke.h"
#include "eeprom_konstante.h"
#include "wear_leveling.h"
#include "pc_serial.h"
#include "unified_motion_state.h"

namespace {
constexpr unsigned long TRAJANJE_FAZE_MS = 6000UL;
constexpr int BROJ_POZICIJA = 64;
constexpr int VRIJEME_POCETKA_OPERACIJE_ZADANO = 299;
constexpr int VRIJEME_KRAJA_OPERACIJE_ZADANO = 1244;
constexpr int POZICIJA_NOCI = 63;
constexpr int MINUTNI_BLOK = 15;

constexpr uint8_t FAZA_STABILNO = 0;
constexpr uint8_t FAZA_PRVI_RELEJ = 1;
constexpr uint8_t FAZA_DRUGI_RELEJ = 2;

unsigned long pocetakFazeMs = 0;
unsigned long zadnjaProvjeraMs = 0;
bool poravnanjeTaktaNaCekanju = true;

int offsetMinuta = 14;
int zadnjiSlotUlaza = -1;

static const uint8_t BROJ_ULAZA_PLOCE = 5;
static const uint8_t PIN_ULAZA_PLOCE[BROJ_ULAZA_PLOCE] = {
  PIN_ULAZA_PLOCE_1,
  PIN_ULAZA_PLOCE_2,
  PIN_ULAZA_PLOCE_3,
  PIN_ULAZA_PLOCE_4,
  PIN_ULAZA_PLOCE_5
};

bool autoZvonoAktivno[2] = {false, false};
unsigned long autoZvonoKraj[2] = {0, 0};
bool autoSlavljenjeZakazano = false;
unsigned long autoSlavljenjeStart = 0;
unsigned long autoSlavljenjeTrajanje = 0;
bool autoSlavljenjeAktivno = false;
unsigned long autoSlavljenjeKraj = 0;

int izracunajCiljnuPoziciju(const DateTime& now) {
  if (!jePlocaKonfigurirana()) {
    return POZICIJA_NOCI;
  }

  const int ukupnoMinuta = now.hour() * 60 + now.minute();
  const int diff = ukupnoMinuta - VRIJEME_POCETKA_OPERACIJE_ZADANO;
  if (diff < 0 || ukupnoMinuta > VRIJEME_KRAJA_OPERACIJE_ZADANO) {
    return POZICIJA_NOCI;
  }

  int pozicija = diff / MINUTNI_BLOK;
  if (pozicija < 0) pozicija = 0;
  if (pozicija > POZICIJA_NOCI) pozicija = POZICIJA_NOCI;
  return pozicija;
}

bool poravnajTaktPloceAkoTreba(unsigned long sadaMs,
                               const EepromLayout::UnifiedMotionState& stanje) {
  if (!poravnanjeTaktaNaCekanju) {
    return true;
  }
  if (stanje.plate_phase != FAZA_STABILNO) {
    return false;
  }

  const DateTime rtcVrijeme = dohvatiTrenutnoVrijeme();
  if ((rtcVrijeme.second() % 6) != 0) {
    return false;
  }

  zadnjaProvjeraMs = sadaMs - TRAJANJE_FAZE_MS;
  poravnanjeTaktaNaCekanju = false;
  posaljiPCLog(F("Ploca: takt poravnat na RTC 6s granicu"));
  return true;
}

void aktivirajRelejePoFazi(const EepromLayout::UnifiedMotionState& stanje) {
  if (stanje.plate_phase == FAZA_STABILNO) {
    digitalWrite(PIN_RELEJ_PARNE_PLOCE, LOW);
    digitalWrite(PIN_RELEJ_NEPARNE_PLOCE, LOW);
  } else if (stanje.plate_phase == FAZA_PRVI_RELEJ) {
    digitalWrite(PIN_RELEJ_PARNE_PLOCE, HIGH);
    digitalWrite(PIN_RELEJ_NEPARNE_PLOCE, LOW);
  } else {
    digitalWrite(PIN_RELEJ_PARNE_PLOCE, LOW);
    digitalWrite(PIN_RELEJ_NEPARNE_PLOCE, HIGH);
  }
}

void obradiKorak(EepromLayout::UnifiedMotionState& stanje, unsigned long sadaMs) {
  if (stanje.plate_phase == FAZA_PRVI_RELEJ && (sadaMs - pocetakFazeMs) >= TRAJANJE_FAZE_MS) {
    stanje.plate_phase = FAZA_DRUGI_RELEJ;
    pocetakFazeMs = sadaMs;
    aktivirajRelejePoFazi(stanje);
    UnifiedMotionStateStore::spremiAkoPromjena(stanje);
    posaljiPCLog(F("Ploca: faza 2"));
    UnifiedMotionStateStore::logirajStanje(stanje);
    return;
  }

  if (stanje.plate_phase == FAZA_DRUGI_RELEJ && (sadaMs - pocetakFazeMs) >= TRAJANJE_FAZE_MS) {
    stanje.plate_position = static_cast<uint8_t>((stanje.plate_position + 1) % BROJ_POZICIJA);
    stanje.plate_phase = FAZA_STABILNO;
    aktivirajRelejePoFazi(stanje);
    UnifiedMotionStateStore::spremiAkoPromjena(stanje);
    posaljiPCLog(F("Ploca: korak dovrsen"));
    UnifiedMotionStateStore::logirajStanje(stanje);
  }
}

void pokreniKorakAkoTreba(EepromLayout::UnifiedMotionState& stanje, unsigned long sadaMs) {
  if (stanje.plate_phase != FAZA_STABILNO || (sadaMs - zadnjaProvjeraMs) < TRAJANJE_FAZE_MS) {
    return;
  }
  zadnjaProvjeraMs = sadaMs;

  const int cilj = izracunajCiljnuPoziciju(dohvatiTrenutnoVrijeme());
  if (stanje.plate_position == cilj) {
    return;
  }

  stanje.plate_phase = FAZA_PRVI_RELEJ;
  pocetakFazeMs = sadaMs;
  aktivirajRelejePoFazi(stanje);
  UnifiedMotionStateStore::spremiAkoPromjena(stanje);
  posaljiPCLog(F("Ploca: start"));
  UnifiedMotionStateStore::logirajStanje(stanje);
}

bool vrijemeProslo(unsigned long sada, unsigned long cilj) {
  return static_cast<long>(sada - cilj) >= 0;
}

void azurirajAutomatskaZvonjenja(unsigned long sadaMs) {
  for (int i = 0; i < 2; ++i) {
    if (autoZvonoAktivno[i] && vrijemeProslo(sadaMs, autoZvonoKraj[i])) {
      deaktivirajZvonjenje(i + 1);
      autoZvonoAktivno[i] = false;
    }
  }

  if (autoSlavljenjeAktivno && !jeSlavljenjeUTijeku()) {
    autoSlavljenjeAktivno = false;
  }
  if (autoSlavljenjeZakazano && vrijemeProslo(sadaMs, autoSlavljenjeStart)) {
    autoSlavljenjeZakazano = false;
    if (!jeSlavljenjeUTijeku()) {
      zapocniSlavljenje();
      autoSlavljenjeAktivno = true;
      autoSlavljenjeKraj = sadaMs + autoSlavljenjeTrajanje;
    }
  }
  if (autoSlavljenjeAktivno && vrijemeProslo(sadaMs, autoSlavljenjeKraj)) {
    zaustaviSlavljenje();
    autoSlavljenjeAktivno = false;
  }
}

void pokreniAutomatskoZvonjenje(int index, unsigned long sadaMs, unsigned long trajanjeMs) {
  if (index < 0 || index > 1) return;
  if (!autoZvonoAktivno[index]) aktivirajZvonjenje(index + 1);
  autoZvonoAktivno[index] = true;
  autoZvonoKraj[index] = sadaMs + trajanjeMs;
}

void obradiUlazePloce(const DateTime& now, unsigned long sadaMs) {
  bool ulazi[BROJ_ULAZA_PLOCE];
  for (uint8_t i = 0; i < BROJ_ULAZA_PLOCE; ++i) {
    ulazi[i] = (digitalRead(PIN_ULAZA_PLOCE[i]) == LOW);
  }

  bool jeNedjelja = (now.dayOfTheWeek() == 0);
  bool zvono1 = jeNedjelja ? ulazi[2] : ulazi[0];
  bool zvono2 = jeNedjelja ? ulazi[3] : ulazi[1];
  bool slavljenje = ulazi[4];

  unsigned long trajanjeZvona = jeNedjelja ? dohvatiTrajanjeZvonjenjaNedjeljaMs()
                                           : dohvatiTrajanjeZvonjenjaRadniMs();
  if (trajanjeZvona == 0) trajanjeZvona = jeNedjelja ? 180000UL : 120000UL;
  unsigned long trajanjeSlavljenja = dohvatiTrajanjeSlavljenjaMs();
  if (trajanjeSlavljenja == 0) trajanjeSlavljenja = 120000UL;

  bool imaZvono = false;
  if (zvono1) { pokreniAutomatskoZvonjenje(0, sadaMs, trajanjeZvona); imaZvono = true; }
  if (zvono2) { pokreniAutomatskoZvonjenje(1, sadaMs, trajanjeZvona); imaZvono = true; }

  if (slavljenje) {
    if (!imaZvono) {
      zapocniSlavljenje();
      autoSlavljenjeAktivno = true;
      autoSlavljenjeKraj = sadaMs + trajanjeSlavljenja;
      autoSlavljenjeZakazano = false;
    } else {
      autoSlavljenjeZakazano = true;
      autoSlavljenjeTrajanje = trajanjeSlavljenja;
      unsigned long start = sadaMs + trajanjeZvona;
      for (int i = 0; i < 2; ++i) {
        if (autoZvonoAktivno[i] && autoZvonoKraj[i] > start) start = autoZvonoKraj[i];
      }
      autoSlavljenjeStart = start;
    }
  }
}

}  // namespace

void inicijalizirajPlocu() {
  pinMode(PIN_RELEJ_PARNE_PLOCE, OUTPUT);
  pinMode(PIN_RELEJ_NEPARNE_PLOCE, OUTPUT);
  digitalWrite(PIN_RELEJ_PARNE_PLOCE, LOW);
  digitalWrite(PIN_RELEJ_NEPARNE_PLOCE, LOW);

  for (uint8_t i = 0; i < BROJ_ULAZA_PLOCE; ++i) pinMode(PIN_ULAZA_PLOCE[i], INPUT_PULLUP);

  WearLeveling::ucitaj(EepromLayout::BAZA_OFFSET_MINUTA,
                       EepromLayout::SLOTOVI_OFFSET_MINUTA,
                       offsetMinuta);
  offsetMinuta = constrain(offsetMinuta, 0, 14);

  EepromLayout::UnifiedMotionState stanje = UnifiedMotionStateStore::dohvatiIliMigriraj();
  aktivirajRelejePoFazi(stanje);

  pocetakFazeMs = millis();
  zadnjaProvjeraMs = millis();
  poravnanjeTaktaNaCekanju = true;
  zadnjiSlotUlaza = -1;
  UnifiedMotionStateStore::logirajStanje(stanje);
  posaljiPCLog(F("Ploca: inicijalizirana kroz jedinstveni model stanja"));
}

void upravljajPlocom() {
  const DateTime now = dohvatiTrenutnoVrijeme();
  const unsigned long sadaMs = millis();
  azurirajAutomatskaZvonjenja(sadaMs);

  if (jePlocaKonfigurirana() && now.minute() % 15 == 0 && now.second() >= 30) {
    int slot = now.hour() * 4 + (now.minute() / 15);
    if (slot != zadnjiSlotUlaza) {
      zadnjiSlotUlaza = slot;
      obradiUlazePloce(now, sadaMs);
    }
  }

  EepromLayout::UnifiedMotionState stanje = UnifiedMotionStateStore::dohvatiIliMigriraj();
  aktivirajRelejePoFazi(stanje);

  if (stanje.plate_phase != FAZA_STABILNO) {
    obradiKorak(stanje, sadaMs);
    return;
  }
  if (!poravnajTaktPloceAkoTreba(sadaMs, stanje)) {
    return;
  }

  pokreniKorakAkoTreba(stanje, sadaMs);
}

void postaviTrenutniPolozajPloce(int pozicija) {
  EepromLayout::UnifiedMotionState stanje = UnifiedMotionStateStore::dohvatiIliMigriraj();
  stanje.plate_position = static_cast<uint8_t>(constrain(pozicija, 0, BROJ_POZICIJA - 1));
  stanje.plate_phase = FAZA_STABILNO;
  UnifiedMotionStateStore::spremiAkoPromjena(stanje);
}

void postaviOffsetMinuta(int offset) {
  offsetMinuta = constrain(offset, 0, 14);
  WearLeveling::spremi(EepromLayout::BAZA_OFFSET_MINUTA,
                       EepromLayout::SLOTOVI_OFFSET_MINUTA,
                       offsetMinuta);
}

int dohvatiPozicijuPloce() {
  return UnifiedMotionStateStore::dohvatiIliMigriraj().plate_position;
}

int dohvatiOffsetMinuta() {
  return offsetMinuta;
}

void kompenzirajPlocu(bool) {
  zadnjaProvjeraMs = 0;
}

bool jePlocaUSinkronu() {
  EepromLayout::UnifiedMotionState stanje = UnifiedMotionStateStore::dohvatiIliMigriraj();
  return stanje.plate_phase == FAZA_STABILNO &&
         stanje.plate_position == izracunajCiljnuPoziciju(dohvatiTrenutnoVrijeme());
}

void oznaciPlocuKaoSinkroniziranu() {
  zadnjaProvjeraMs = 0;
}

void zatraziPoravnanjeTaktaPloce() {
  poravnanjeTaktaNaCekanju = true;
}
