#include <Arduino.h>
#include <RTClib.h>
#include <string.h>

#include "kazaljke_sata.h"
#include "podesavanja_piny.h"
#include "time_glob.h"
#include "eeprom_konstante.h"
#include "wear_leveling.h"
#include "pc_serial.h"

namespace {
constexpr unsigned long TRAJANJE_FAZE_MS = 6000UL;
constexpr int BROJ_MINUTA_CIKLUS = 720;
constexpr uint8_t HAND_NEAKTIVNO = 0;
constexpr uint8_t HAND_AKTIVNO = 1;
constexpr uint8_t HAND_RELEJ_NIJEDAN = 0;
constexpr uint8_t HAND_RELEJ_PARNI = 1;
constexpr uint8_t HAND_RELEJ_NEPARNI = 2;

unsigned long zadnjaProvjeraMs = 0;

int izracunajDvanaestSatneMinute(const DateTime& vrijeme) {
  return (vrijeme.hour() % 12) * 60 + vrijeme.minute();
}

bool jeValjanoStanje(const EepromLayout::UnifiedMotionState& stanje) {
  return stanje.hand_position < BROJ_MINUTA_CIKLUS &&
         stanje.hand_active <= HAND_AKTIVNO &&
         stanje.hand_relay <= HAND_RELEJ_NEPARNI &&
         stanje.plate_position < 64 &&
         stanje.plate_phase <= 2 &&
         stanje.version == EepromLayout::UNIFIED_STANJE_VERZIJA;
}

bool ucitajStanje(EepromLayout::UnifiedMotionState& stanje) {
  if (!WearLeveling::ucitaj(EepromLayout::BAZA_UNIFIED_STANJE,
                            EepromLayout::SLOTOVI_UNIFIED_STANJE,
                            stanje)) {
    return false;
  }
  return jeValjanoStanje(stanje);
}

void spremiStanjeAkoPromjena(const EepromLayout::UnifiedMotionState& novoStanje) {
  EepromLayout::UnifiedMotionState trenutno{};
  if (ucitajStanje(trenutno) && memcmp(&trenutno, &novoStanje, sizeof(novoStanje)) == 0) {
    return;
  }
  WearLeveling::spremi(EepromLayout::BAZA_UNIFIED_STANJE,
                       EepromLayout::SLOTOVI_UNIFIED_STANJE,
                       novoStanje);
}

EepromLayout::UnifiedMotionState dohvatiIliMigrirajStanje() {
  EepromLayout::UnifiedMotionState stanje{};
  if (ucitajStanje(stanje)) {
    return stanje;
  }

  int legacyK = 0;
  if (!WearLeveling::ucitaj(EepromLayout::BAZA_KAZALJKE,
                            EepromLayout::SLOTOVI_KAZALJKE,
                            legacyK)) {
    legacyK = izracunajDvanaestSatneMinute(dohvatiTrenutnoVrijeme());
  }

  stanje.hand_position = static_cast<uint16_t>((legacyK % BROJ_MINUTA_CIKLUS + BROJ_MINUTA_CIKLUS) % BROJ_MINUTA_CIKLUS);
  stanje.hand_active = HAND_NEAKTIVNO;
  stanje.hand_relay = HAND_RELEJ_NIJEDAN;
  stanje.hand_start_ms = 0;
  stanje.plate_position = 63;
  stanje.plate_phase = 0;
  stanje.version = EepromLayout::UNIFIED_STANJE_VERZIJA;
  stanje.reserved = 0;
  spremiStanjeAkoPromjena(stanje);
  return stanje;
}

uint8_t odrediRelejKazaljki(const EepromLayout::UnifiedMotionState& stanje) {
  return (stanje.hand_position % 2 == 0) ? HAND_RELEJ_PARNI : HAND_RELEJ_NEPARNI;
}

void aktivirajRelejeKazaljki(const EepromLayout::UnifiedMotionState& stanje) {
  if (stanje.hand_active != HAND_AKTIVNO) {
    digitalWrite(PIN_RELEJ_PARNE_KAZALJKE, LOW);
    digitalWrite(PIN_RELEJ_NEPARNE_KAZALJKE, LOW);
    return;
  }

  if (stanje.hand_relay == HAND_RELEJ_PARNI) {
    digitalWrite(PIN_RELEJ_PARNE_KAZALJKE, HIGH);
    digitalWrite(PIN_RELEJ_NEPARNE_KAZALJKE, LOW);
  } else if (stanje.hand_relay == HAND_RELEJ_NEPARNI) {
    digitalWrite(PIN_RELEJ_PARNE_KAZALJKE, LOW);
    digitalWrite(PIN_RELEJ_NEPARNE_KAZALJKE, HIGH);
  } else {
    digitalWrite(PIN_RELEJ_PARNE_KAZALJKE, LOW);
    digitalWrite(PIN_RELEJ_NEPARNE_KAZALJKE, LOW);
  }
}

String formatUnifiedState(const EepromLayout::UnifiedMotionState& stanje) {
  String log = F("hand=");
  log += stanje.hand_position;
  log += F(" active=");
  log += stanje.hand_active;
  log += F(" relay=");
  log += stanje.hand_relay;
  log += F(" start=");
  log += stanje.hand_start_ms;
  log += F(" plate=");
  log += stanje.plate_position;
  log += F(" phase=");
  log += stanje.plate_phase;
  return log;
}

void obradiJedanKorak(EepromLayout::UnifiedMotionState& stanje, unsigned long sadaMs) {
  if (stanje.hand_active == HAND_AKTIVNO &&
      (sadaMs - stanje.hand_start_ms) >= TRAJANJE_FAZE_MS) {
    stanje.hand_position = static_cast<uint16_t>((stanje.hand_position + 1) % BROJ_MINUTA_CIKLUS);
    stanje.hand_active = HAND_NEAKTIVNO;
    stanje.hand_relay = HAND_RELEJ_NIJEDAN;
    stanje.hand_start_ms = 0;
    aktivirajRelejeKazaljki(stanje);
    spremiStanjeAkoPromjena(stanje);
    posaljiPCLog(String(F("Kazaljke: korak dovrsen, ")) + formatUnifiedState(stanje));
  }
}

void pokreniKorakAkoTreba(EepromLayout::UnifiedMotionState& stanje, unsigned long sadaMs) {
  if (stanje.hand_active != HAND_NEAKTIVNO) {
    return;
  }
  if ((sadaMs - zadnjaProvjeraMs) < TRAJANJE_FAZE_MS) {
    return;
  }
  zadnjaProvjeraMs = sadaMs;

  const int cilj = izracunajDvanaestSatneMinute(dohvatiTrenutnoVrijeme());
  if (stanje.hand_position == cilj) {
    return;
  }

  stanje.hand_active = HAND_AKTIVNO;
  stanje.hand_relay = odrediRelejKazaljki(stanje);
  stanje.hand_start_ms = sadaMs;
  aktivirajRelejeKazaljki(stanje);
  spremiStanjeAkoPromjena(stanje);
  posaljiPCLog(String(F("Kazaljke: start koraka, ")) + formatUnifiedState(stanje));
}

}  // namespace

void inicijalizirajKazaljke() {
  pinMode(PIN_RELEJ_PARNE_KAZALJKE, OUTPUT);
  pinMode(PIN_RELEJ_NEPARNE_KAZALJKE, OUTPUT);

  EepromLayout::UnifiedMotionState stanje = dohvatiIliMigrirajStanje();
  stanje.hand_active = HAND_NEAKTIVNO;
  stanje.hand_relay = HAND_RELEJ_NIJEDAN;
  stanje.hand_start_ms = 0;
  aktivirajRelejeKazaljki(stanje);
  spremiStanjeAkoPromjena(stanje);

  zadnjaProvjeraMs = millis();
  posaljiPCLog(String(F("STATE: ")) + formatUnifiedState(stanje));
  posaljiPCLog(F("Kazaljke: inicijalizirane kroz jedinstveni model stanja"));
}

void upravljajKazaljkama() {
  upravljajKorekcijomKazaljki();
}

void upravljajKorekcijomKazaljki() {
  EepromLayout::UnifiedMotionState stanje = dohvatiIliMigrirajStanje();
  const unsigned long sadaMs = millis();

  if (stanje.hand_active != HAND_NEAKTIVNO) {
    aktivirajRelejeKazaljki(stanje);
    obradiJedanKorak(stanje, sadaMs);
    return;
  }

  aktivirajRelejeKazaljki(stanje);
  pokreniKorakAkoTreba(stanje, sadaMs);
}

void pokreniBudnoKorekciju() {
  zadnjaProvjeraMs = 0;
}

void postaviRucnuPozicijuKazaljki(int satKazaljke, int minutaKazaljke) {
  satKazaljke = constrain(satKazaljke, 0, 11);
  minutaKazaljke = constrain(minutaKazaljke, 0, 59);

  EepromLayout::UnifiedMotionState stanje = dohvatiIliMigrirajStanje();
  stanje.hand_position = static_cast<uint16_t>((satKazaljke * 60 + minutaKazaljke) % BROJ_MINUTA_CIKLUS);
  stanje.hand_active = HAND_NEAKTIVNO;
  stanje.hand_relay = HAND_RELEJ_NIJEDAN;
  stanje.hand_start_ms = 0;
  aktivirajRelejeKazaljki(stanje);
  spremiStanjeAkoPromjena(stanje);
  pokreniBudnoKorekciju();
}

void pomakniKazaljkeZa(int brojMinuta) {
  EepromLayout::UnifiedMotionState stanje = dohvatiIliMigrirajStanje();
  int nova = static_cast<int>(stanje.hand_position) + brojMinuta;
  while (nova < 0) nova += BROJ_MINUTA_CIKLUS;
  stanje.hand_position = static_cast<uint16_t>(nova % BROJ_MINUTA_CIKLUS);
  stanje.hand_active = HAND_NEAKTIVNO;
  stanje.hand_relay = HAND_RELEJ_NIJEDAN;
  stanje.hand_start_ms = 0;
  spremiStanjeAkoPromjena(stanje);
}

void kompenzirajKazaljke(bool) {
  pokreniBudnoKorekciju();
}

bool suKazaljkeUSinkronu() {
  const EepromLayout::UnifiedMotionState stanje = dohvatiIliMigrirajStanje();
  return stanje.hand_active == HAND_NEAKTIVNO &&
         stanje.hand_position == izracunajDvanaestSatneMinute(dohvatiTrenutnoVrijeme());
}

int dohvatiMemoriraneKazaljkeMinuta() {
  return dohvatiIliMigrirajStanje().hand_position;
}

void oznaciKazaljkeKaoSinkronizirane() {
  pokreniBudnoKorekciju();
}

void obavijestiKazaljkeDSTPromjena(int) {
  pokreniBudnoKorekciju();
}

void postaviTrenutniPolozajKazaljki(int trenutnaMinuta) {
  trenutnaMinuta = constrain(trenutnaMinuta, 0, BROJ_MINUTA_CIKLUS - 1);
  EepromLayout::UnifiedMotionState stanje = dohvatiIliMigrirajStanje();
  stanje.hand_position = static_cast<uint16_t>(trenutnaMinuta);
  stanje.hand_active = HAND_NEAKTIVNO;
  stanje.hand_relay = HAND_RELEJ_NIJEDAN;
  stanje.hand_start_ms = 0;
  spremiStanjeAkoPromjena(stanje);
}

void pomakniKazaljkeNaMinutu(int ciljMinuta, bool) {
  postaviTrenutniPolozajKazaljki(ciljMinuta);
}
