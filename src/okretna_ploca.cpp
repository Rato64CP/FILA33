// okretna_ploca.cpp
#include "okretna_ploca.h"
#include <Arduino.h>
#include <EEPROM.h>
#include <RTClib.h>

const int relejPloca1 = 12;
const int relejPloca2 = 13;
const unsigned long TRAJANJE_IMPULSA_PLOCE = 6000UL;
int memoriranaPozicijaPloce = 0;
int offsetMinuta = 14; // podesivo u postavkama, 0–15

static unsigned long vrijemeStarta = 0;
static bool uTijeku = false;
static bool drugaFaza = false;
static DateTime prosloVrijeme;

void inicijalizirajPlocu() {
  pinMode(relejPloca1, OUTPUT);
  pinMode(relejPloca2, OUTPUT);
  digitalWrite(relejPloca1, LOW);
  digitalWrite(relejPloca2, LOW);
  EEPROM.get(20, memoriranaPozicijaPloce);
  if (memoriranaPozicijaPloce < 0 || memoriranaPozicijaPloce > 63) memoriranaPozicijaPloce = 0;
  EEPROM.get(22, offsetMinuta);
  if (offsetMinuta < 0 || offsetMinuta > 15) offsetMinuta = 14;

  prosloVrijeme = RTC_DS3231().now();
}

void upravljajPločom() {
  DateTime now = RTC_DS3231().now();
  int sati = now.hour();
  int minuta = now.minute();
  int sekunda = now.second();

  if ((sati < 5) || (sati > 20) || (sati == 20 && minuta > 45)) return;

  bool okidac = (minuta % 15 == offsetMinuta) && sekunda == 0;
  if (okidac && !uTijeku && now != prosloVrijeme) {
    prosloVrijeme = now;
    digitalWrite(relejPloca1, HIGH);
    vrijemeStarta = millis();
    uTijeku = true;
    drugaFaza = false;
  }

  if (uTijeku) {
    unsigned long trajanje = millis() - vrijemeStarta;

    if (!drugaFaza && trajanje >= TRAJANJE_IMPULSA_PLOCE) {
      digitalWrite(relejPloca1, LOW);
      digitalWrite(relejPloca2, HIGH);
      drugaFaza = true;
    }

    if (trajanje >= 2 * TRAJANJE_IMPULSA_PLOCE) {
      digitalWrite(relejPloca2, LOW);
      uTijeku = false;
      drugaFaza = false;
      memoriranaPozicijaPloce = (memoriranaPozicijaPloce + 1) % 64;
      EEPROM.put(20, memoriranaPozicijaPloce);
    }
  }
}

void postaviTrenutniPolozajPloce(int pozicija) {
  memoriranaPozicijaPloce = constrain(pozicija, 0, 63);
  EEPROM.put(20, memoriranaPozicijaPloce);
}

void postaviOffsetMinuta(int offset) {
  offsetMinuta = constrain(offset, 0, 15);
  EEPROM.put(22, offsetMinuta);
}

int dohvatiTrenutnuPozicijuPloce() {
  return memoriranaPozicijaPloce;
}

int dohvatiOffsetMinuta() {
  return offsetMinuta;
}

void pomakniPlocuZa(int brojKvadranata) {
  for (int i = 0; i < brojKvadranata; ++i) {
    digitalWrite(relejPloca1, HIGH);
    delay(TRAJANJE_IMPULSA_PLOCE);
    digitalWrite(relejPloca1, LOW);
    delay(200);
    digitalWrite(relejPloca2, HIGH);
    delay(TRAJANJE_IMPULSA_PLOCE);
    digitalWrite(relejPloca2, LOW);
    delay(500);

    memoriranaPozicijaPloce++;
    if (memoriranaPozicijaPloce > 63) memoriranaPozicijaPloce = 0;
  }
  EEPROM.put(20, memoriranaPozicijaPloce);
}

void kompenzirajPlocu() {
  DateTime now = RTC_DS3231().now();
  int sati = now.hour();
  int minuta = now.minute();
  if (sati < 5 || sati > 20) return;

  int ciljPozicija = (sati - 5) * 4 + (minuta / 15);
  int razlika = ciljPozicija - memoriranaPozicijaPloce;
  if (razlika < 0) razlika += 64;

  pomakniPlocuZa(razlika);
}
