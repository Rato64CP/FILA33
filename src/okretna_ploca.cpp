// okretna_ploca.cpp
#include <Arduino.h>
#include <RTClib.h>
#include <EEPROM.h>
#include "okretna_ploca.h"
#include "podesavanja_piny.h"
#include "time_glob.h"

const unsigned long POLA_OKRETA_MS = 6000UL;
const int MAKS_PAMETNI_POMAK_MINUTA = 15; // maksimalno za čekanje umjesto rotacije

static unsigned long vrijemeStarta = 0;
static bool uTijeku = false;
static bool drugaFaza = false;
static DateTime prosloVrijeme;

int pozicijaPloce = 0; // 0-63
int offsetMinuta = 14; // podesivo u postavkama, 0–15

void inicijalizirajPlocu() {
  pinMode(PIN_RELEJ_PARNE_PLOCE, OUTPUT);
  pinMode(PIN_RELEJ_NEPARNE_PLOCE, OUTPUT);
  digitalWrite(PIN_RELEJ_PARNE_PLOCE, LOW);
  digitalWrite(PIN_RELEJ_NEPARNE_PLOCE, LOW);

  EEPROM.get(20, pozicijaPloce);
  if (pozicijaPloce < 0 || pozicijaPloce > 63) pozicijaPloce = 0;
  EEPROM.get(22, offsetMinuta);
  if (offsetMinuta < 0 || offsetMinuta > 15) offsetMinuta = 14;

  prosloVrijeme = rtc.now();
}

void upravljajPločom() {
  DateTime now = rtc.now();
  int sati = now.hour();
  int minuta = now.minute();
  int sekunda = now.second();

  if ((sati < 5) || (sati > 20) || (sati == 20 && minuta > 45)) return;

  bool okidac = (minuta % 15 == offsetMinuta) && sekunda == 0;
  if (okidac && !uTijeku && now != prosloVrijeme) {
    prosloVrijeme = now;
    digitalWrite(PIN_RELEJ_PARNE_PLOCE, HIGH);
    vrijemeStarta = millis();
    uTijeku = true;
    drugaFaza = false;
  }

  if (uTijeku) {
    unsigned long trajanje = millis() - vrijemeStarta;

    if (!drugaFaza && trajanje >= POLA_OKRETA_MS) {
      digitalWrite(PIN_RELEJ_PARNE_PLOCE, LOW);
      digitalWrite(PIN_RELEJ_NEPARNE_PLOCE, HIGH);
      drugaFaza = true;
    }

    if (trajanje >= 2 * POLA_OKRETA_MS) {
      digitalWrite(PIN_RELEJ_NEPARNE_PLOCE, LOW);
      uTijeku = false;
      drugaFaza = false;
      pozicijaPloce = (pozicijaPloce + 1) % 64;
      EEPROM.put(20, pozicijaPloce);
    }
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

void kompenzirajPlocu(bool pametniMod) {
  DateTime now = rtc.now();
  int sati = now.hour();
  int minuta = now.minute();
  if (sati < 5 || sati > 20) return;

  int ciljPozicija = (sati - 5) * 4 + (minuta / 15);
  int razlika = ciljPozicija - pozicijaPloce;
  if (razlika < 0) razlika += 64;

  if (pametniMod && razlika <= 1) return; // čekaj sljedeći impuls ako je blizu

  for (int i = 0; i < razlika; i++) {
    digitalWrite(PIN_RELEJ_PARNE_PLOCE, HIGH);
    delay(POLA_OKRETA_MS);
    digitalWrite(PIN_RELEJ_PARNE_PLOCE, LOW);
    delay(200);
    digitalWrite(PIN_RELEJ_NEPARNE_PLOCE, HIGH);
    delay(POLA_OKRETA_MS);
    digitalWrite(PIN_RELEJ_NEPARNE_PLOCE, LOW);
    delay(500);
  }
  pozicijaPloce = ciljPozicija;
  EEPROM.put(20, pozicijaPloce);
}
