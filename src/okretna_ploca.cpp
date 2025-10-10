// okretna_ploca.cpp
#include <Arduino.h>
#include <RTClib.h>
#include <EEPROM.h>
#include "okretna_ploca.h"
#include "podesavanja_piny.h"
#include "time_glob.h"
#include "lcd_display.h"
#include "zvonjenje.h"
#include "postavke.h"

const unsigned long POLA_OKRETA_MS = 6000UL;
const int MAKS_PAMETNI_POMAK_MINUTA = 15; // maksimalno za čekanje umjesto rotacije

static unsigned long vrijemeStarta = 0;
static bool ciklusUTijeku = false;
static bool drugaFaza = false;
static int zadnjaAktiviranaMinuta = -1;

static const uint8_t BROJ_ULAZA_PLOCE = 5;
static const uint8_t PIN_ULAZA_PLOCE[BROJ_ULAZA_PLOCE] = {
  PIN_PLOCA_ULAZ_1,
  PIN_PLOCA_ULAZ_2,
  PIN_PLOCA_ULAZ_3,
  PIN_PLOCA_ULAZ_4,
  PIN_PLOCA_ULAZ_5
};

static int zadnjiSlotUlaza = -1;
static bool autoZvonoAktivno[2] = {false, false};
static unsigned long autoZvonoKraj[2] = {0, 0};
static bool autoSlavljenjeZakazano = false;
static unsigned long autoSlavljenjeStart = 0;
static unsigned long autoSlavljenjeTrajanje = 0;
static bool autoSlavljenjeAktivno = false;
static unsigned long autoSlavljenjeKraj = 0;

int pozicijaPloce = 0; // 0-63
int offsetMinuta = 14; // podesivo u postavkama, 0–15

static int izracunajCiljnuPoziciju(const DateTime& now) {
  int ukupnoMinuta = now.hour() * 60 + now.minute();
  int pocetak = 5 * 60 + offsetMinuta;
  int diff = ukupnoMinuta - pocetak;
  if (diff < 0) return 0;
  int pozicija = diff / 15;
  if (pozicija > 63) pozicija = 63;
  if (pozicija < 0) pozicija = 0;
  return pozicija;
}

static void pokreniPrvuFazuPloce() {
  digitalWrite(PIN_RELEJ_PARNE_PLOCE, HIGH);
  digitalWrite(PIN_RELEJ_NEPARNE_PLOCE, LOW);
  vrijemeStarta = millis();
  ciklusUTijeku = true;
  drugaFaza = false;
}

static void zavrsiCiklusPloce() {
  digitalWrite(PIN_RELEJ_PARNE_PLOCE, LOW);
  digitalWrite(PIN_RELEJ_NEPARNE_PLOCE, LOW);
  ciklusUTijeku = false;
  drugaFaza = false;
  pozicijaPloce = (pozicijaPloce + 1) % 64;
  EEPROM.put(20, pozicijaPloce);
}

static void odradiJedanKorakPloceBlokirajuci() {
  digitalWrite(PIN_RELEJ_PARNE_PLOCE, HIGH);
  odradiPauzuSaLCD(POLA_OKRETA_MS);
  digitalWrite(PIN_RELEJ_PARNE_PLOCE, LOW);
  odradiPauzuSaLCD(200);
  digitalWrite(PIN_RELEJ_NEPARNE_PLOCE, HIGH);
  odradiPauzuSaLCD(POLA_OKRETA_MS);
  digitalWrite(PIN_RELEJ_NEPARNE_PLOCE, LOW);
  odradiPauzuSaLCD(400);
  pozicijaPloce = (pozicijaPloce + 1) % 64;
  EEPROM.put(20, pozicijaPloce);
}

static bool vrijemeProslo(unsigned long sada, unsigned long cilj) {
  return static_cast<long>(sada - cilj) >= 0;
}

static void azurirajAutomatskaZvonjenja(unsigned long sadaMs) {
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
    } else if (autoSlavljenjeAktivno) {
      autoSlavljenjeKraj = sadaMs + autoSlavljenjeTrajanje;
    }
  }

  if (autoSlavljenjeAktivno && vrijemeProslo(sadaMs, autoSlavljenjeKraj)) {
    zaustaviSlavljenje();
    autoSlavljenjeAktivno = false;
  }
}

static void pokreniAutomatskoZvonjenje(int index, unsigned long sadaMs, unsigned long trajanjeMs) {
  if (index < 0 || index > 1) return;
  if (!autoZvonoAktivno[index]) {
    aktivirajZvonjenje(index + 1);
  }
  autoZvonoAktivno[index] = true;
  autoZvonoKraj[index] = sadaMs + trajanjeMs;
}

static void zakaziSlavljenje(unsigned long startMs, unsigned long trajanjeMs) {
  autoSlavljenjeZakazano = true;
  autoSlavljenjeStart = startMs;
  autoSlavljenjeTrajanje = trajanjeMs;
}

static void pokreniSlavljenjeOdmah(unsigned long sadaMs, unsigned long trajanjeMs) {
  autoSlavljenjeZakazano = false;
  if (!jeSlavljenjeUTijeku()) {
    zapocniSlavljenje();
    autoSlavljenjeAktivno = true;
    autoSlavljenjeTrajanje = trajanjeMs;
    autoSlavljenjeKraj = sadaMs + trajanjeMs;
  } else if (autoSlavljenjeAktivno) {
    autoSlavljenjeTrajanje = trajanjeMs;
    autoSlavljenjeKraj = sadaMs + trajanjeMs;
  }
}

static void obradiUlazePloce(const DateTime& now, unsigned long sadaMs) {
  bool ulaziAktivni[BROJ_ULAZA_PLOCE];
  for (uint8_t i = 0; i < BROJ_ULAZA_PLOCE; ++i) {
    ulaziAktivni[i] = digitalRead(PIN_ULAZA_PLOCE[i]) == LOW;
  }

  bool jeNedjelja = now.dayOfTheWeek() == 0;
  bool pokreniMusko = jeNedjelja ? ulaziAktivni[2] : ulaziAktivni[0];
  bool pokreniZensko = jeNedjelja ? ulaziAktivni[3] : ulaziAktivni[1];
  bool pokreniSlavljenjePin = ulaziAktivni[4];

  unsigned long trajanjeMs = dohvatiTrajanjeZvonjenjaMs();
  if (trajanjeMs == 0) {
    trajanjeMs = 120000UL;
  }

  bool imaZvono = false;
  if (pokreniMusko) {
    pokreniAutomatskoZvonjenje(0, sadaMs, trajanjeMs);
    imaZvono = true;
  }
  if (pokreniZensko) {
    pokreniAutomatskoZvonjenje(1, sadaMs, trajanjeMs);
    imaZvono = true;
  }

  if (pokreniSlavljenjePin) {
    if (imaZvono) {
      unsigned long pocetak = sadaMs + trajanjeMs;
      for (int i = 0; i < 2; ++i) {
        if (autoZvonoAktivno[i] && autoZvonoKraj[i] > pocetak) {
          pocetak = autoZvonoKraj[i];
        }
      }
      zakaziSlavljenje(pocetak, trajanjeMs);
    } else {
      pokreniSlavljenjeOdmah(sadaMs, trajanjeMs);
    }
  }
}

void inicijalizirajPlocu() {
  pinMode(PIN_RELEJ_PARNE_PLOCE, OUTPUT);
  pinMode(PIN_RELEJ_NEPARNE_PLOCE, OUTPUT);
  digitalWrite(PIN_RELEJ_PARNE_PLOCE, LOW);
  digitalWrite(PIN_RELEJ_NEPARNE_PLOCE, LOW);

  for (uint8_t i = 0; i < BROJ_ULAZA_PLOCE; ++i) {
    pinMode(PIN_ULAZA_PLOCE[i], INPUT_PULLUP);
  }

  EEPROM.get(20, pozicijaPloce);
  if (pozicijaPloce < 0 || pozicijaPloce > 63) pozicijaPloce = 0;
  EEPROM.get(22, offsetMinuta);
  if (offsetMinuta < 0 || offsetMinuta > 15) offsetMinuta = 14;

  vrijemeStarta = 0;
  ciklusUTijeku = false;
  drugaFaza = false;
  zadnjaAktiviranaMinuta = -1;
  zadnjiSlotUlaza = -1;
  for (uint8_t i = 0; i < 2; ++i) {
    autoZvonoAktivno[i] = false;
    autoZvonoKraj[i] = 0;
  }
  autoSlavljenjeZakazano = false;
  autoSlavljenjeStart = 0;
  autoSlavljenjeTrajanje = 0;
  autoSlavljenjeAktivno = false;
  autoSlavljenjeKraj = 0;
}

void upravljajPlocom() {
  DateTime now = dohvatiTrenutnoVrijeme();
  unsigned long sadaMs = millis();

  azurirajAutomatskaZvonjenja(sadaMs);

  if (now.minute() % 15 == 0 && now.second() >= 30) {
    int slot = now.hour() * 4 + (now.minute() / 15);
    if (slot != zadnjiSlotUlaza) {
      zadnjiSlotUlaza = slot;
      obradiUlazePloce(now, sadaMs);
    }
  }

  int minuta = now.minute();

  int cilj = izracunajCiljnuPoziciju(now);
  int razlika = cilj - pozicijaPloce;
  if (razlika < 0) razlika += 64;

  bool trebaPokrenuti = (!ciklusUTijeku && now.second() <= 1 && minuta != zadnjaAktiviranaMinuta && (minuta % 15 == offsetMinuta) && razlika > 0);
  if (trebaPokrenuti) {
    pokreniPrvuFazuPloce();
    zadnjaAktiviranaMinuta = minuta;
  }

  if (!ciklusUTijeku) return;

  unsigned long proteklo = millis() - vrijemeStarta;
  if (!drugaFaza && proteklo >= POLA_OKRETA_MS) {
    digitalWrite(PIN_RELEJ_PARNE_PLOCE, LOW);
    digitalWrite(PIN_RELEJ_NEPARNE_PLOCE, HIGH);
    vrijemeStarta = millis();
    drugaFaza = true;
  } else if (drugaFaza && proteklo >= POLA_OKRETA_MS) {
    zavrsiCiklusPloce();
  }
}

void postaviTrenutniPolozajPloce(int pozicija) {
  pozicijaPloce = constrain(pozicija, 0, 63);
  EEPROM.put(20, pozicijaPloce);
}

void postaviOffsetMinuta(int offset) {
  offsetMinuta = constrain(offset, 0, 15);
  EEPROM.put(22, offsetMinuta);
}

int dohvatiOffsetMinuta() {
  return offsetMinuta;
}

int dohvatiPozicijuPloce() {
  return pozicijaPloce;
}

bool jePlocaUSinkronu() {
  DateTime now = dohvatiTrenutnoVrijeme();
  return pozicijaPloce == izracunajCiljnuPoziciju(now);
}

void kompenzirajPlocu(bool pametniMod) {
  DateTime now = dohvatiTrenutnoVrijeme();
  int ciljPozicija = izracunajCiljnuPoziciju(now);
  int razlika = ciljPozicija - pozicijaPloce;
  if (razlika < 0) razlika += 64;

  if (pametniMod && razlika <= 1) return; // čekaj sljedeći impuls ako je blizu

  for (int i = 0; i < razlika; i++) {
    odradiJedanKorakPloceBlokirajuci();
  }

  pozicijaPloce = ciljPozicija;
  EEPROM.put(20, pozicijaPloce);

  zadnjaAktiviranaMinuta = now.minute();
  ciklusUTijeku = false;
  drugaFaza = false;
}

void oznaciPlocuKaoSinkroniziranu() {
  DateTime now = dohvatiTrenutnoVrijeme();
  zadnjaAktiviranaMinuta = now.minute();
}
