// okretna_ploca.cpp
#include <Arduino.h>
#include <RTClib.h>
#include <EEPROM.h>
#include "okretna_ploca.h"

const int relejParniPloca = 8;
const int relejNeparniPloca = 9;
const unsigned long POLA_OKRETA_MS = 6000UL;

static unsigned long vrijemeStarta = 0;
static bool uTijeku = false;
static bool drugaFaza = false;
static DateTime prosloVrijeme;

int pozicijaPloce = 0; // 0-63
int offsetMinuta = 14; // podesivo u postavkama, 0–15

void inicijalizirajPlocu() {
  pinMode(relejParniPloca, OUTPUT);
  pinMode(relejNeparniPloca, OUTPUT);
  digitalWrite(relejParniPloca, LOW);
  digitalWrite(relejNeparniPloca, LOW);

  EEPROM.get(20, pozicijaPloce);
  if (pozicijaPloce < 0 || pozicijaPloce > 63) pozicijaPloce = 0;
  EEPROM.get(22, offsetMinuta);
  if (offsetMinuta < 0 || offsetMinuta > 15) offsetMinuta = 14;

  prosloVrijeme = RTC_DS3231().now();
}

void upravljajPločom() {
  DateTime now = RTC_DS3231().now();
  int sati = now.hour();
  int minuta = now.minute();
  int sekunda = now.second();

  // radno vrijeme ploce
  if ((sati < 5) || (sati > 20) || (sati == 20 && minuta > 45)) return;

  bool okidac = (minuta % 15 == offsetMinuta) && sekunda == 0;
  if (okidac && !uTijeku && now != prosloVrijeme) {
    prosloVrijeme = now;
    digitalWrite(relejParniPloca, HIGH);
    vrijemeStarta = millis();
    uTijeku = true;
    drugaFaza = false;
  }

  if (uTijeku) {
    unsigned long trajanje = millis() - vrijemeStarta;

    if (!drugaFaza && trajanje >= POLA_OKRETA_MS) {
      digitalWrite(relejParniPloca, LOW);
      digitalWrite(relejNeparniPloca, HIGH);
      drugaFaza = true;
    }

    if (trajanje >= 2 * POLA_OKRETA_MS) {
      digitalWrite(relejNeparniPloca, LOW);
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

void kompenzirajPlocu() {
  DateTime now = RTC_DS3231().now();
  int sati = now.hour();
  int minuta = now.minute();
  if (sati < 5 || sati > 20) return;

  int ciljPozicija = (sati - 5) * 4 + (minuta / 15);
  int razlika = ciljPozicija - pozicijaPloce;
  if (razlika < 0) razlika += 64;

  for (int i = 0; i < razlika; i++) {
    digitalWrite(relejParniPloca, HIGH);
    delay(POLA_OKRETA_MS);
    digitalWrite(relejParniPloca, LOW);
    delay(200);
    digitalWrite(relejNeparniPloca, HIGH);
    delay(POLA_OKRETA_MS);
    digitalWrite(relejNeparniPloca, LOW);
    delay(500);
  }
  pozicijaPloce = ciljPozicija;
  EEPROM.put(20, pozicijaPloce);
}
