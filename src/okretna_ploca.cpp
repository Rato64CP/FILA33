// okretna_ploca.cpp
#include <Arduino.h>
#include <RTClib.h>
#include <EEPROM.h>
#include "okretna_ploca.h"
#include "podesavanja_piny.h"
#include "time_glob.h"
#include "lcd_display.h"

const unsigned long POLA_OKRETA_MS = 6000UL;
const int MAKS_PAMETNI_POMAK_MINUTA = 15; // maksimalno za čekanje umjesto rotacije

static unsigned long vrijemeStarta = 0;
static bool ciklusUTijeku = false;
static bool drugaFaza = false;
static int zadnjaAktiviranaMinuta = -1;

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

void inicijalizirajPlocu() {
  pinMode(PIN_RELEJ_PARNE_PLOCE, OUTPUT);
  pinMode(PIN_RELEJ_NEPARNE_PLOCE, OUTPUT);
  digitalWrite(PIN_RELEJ_PARNE_PLOCE, LOW);
  digitalWrite(PIN_RELEJ_NEPARNE_PLOCE, LOW);

  EEPROM.get(20, pozicijaPloce);
  if (pozicijaPloce < 0 || pozicijaPloce > 63) pozicijaPloce = 0;
  EEPROM.get(22, offsetMinuta);
  if (offsetMinuta < 0 || offsetMinuta > 15) offsetMinuta = 14;

  vrijemeStarta = 0;
  ciklusUTijeku = false;
  drugaFaza = false;
  zadnjaAktiviranaMinuta = -1;
}

void upravljajPlocom() {
  DateTime now = dohvatiTrenutnoVrijeme();
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
