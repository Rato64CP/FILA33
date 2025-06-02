// kazaljke_sata.cpp
#include <Arduino.h>
#include <RTClib.h>
#include <EEPROM.h>
#include "kazaljke_sata.h"

const int relejParniPin = 10;
const int relejNeparniPin = 11;
const unsigned long TRAJANJE_IMPULSA = 6000UL;

static DateTime zadnjeVrijeme;
static unsigned long vrijemePocetkaImpulsa = 0;
static bool impulsUTijeku = false;

int memoriraneKazaljkeMinuta = 0;

void inicijalizirajKazaljke() {
  pinMode(relejParniPin, OUTPUT);
  pinMode(relejNeparniPin, OUTPUT);
  digitalWrite(relejParniPin, LOW);
  digitalWrite(relejNeparniPin, LOW);
  EEPROM.get(10, memoriraneKazaljkeMinuta);
  if (memoriraneKazaljkeMinuta < 0 || memoriraneKazaljkeMinuta > 1439) memoriraneKazaljkeMinuta = 0;
}

void upravljajKazaljkama() {
  DateTime now = RTC_DS3231().now();
  int ukupnoMinuta = now.hour() * 60 + now.minute();
  if (!impulsUTijeku && now.second() == 0 && now != zadnjeVrijeme) {
    zadnjeVrijeme = now;
    int pin = (now.minute() % 2 == 0) ? relejParniPin : relejNeparniPin;
    digitalWrite(pin, HIGH);
    vrijemePocetkaImpulsa = millis();
    impulsUTijeku = true;
    memoriraneKazaljkeMinuta++;
    if (memoriraneKazaljkeMinuta > 1439) memoriraneKazaljkeMinuta = 0;
    EEPROM.put(10, memoriraneKazaljkeMinuta);
  }
  if (impulsUTijeku && millis() - vrijemePocetkaImpulsa >= TRAJANJE_IMPULSA) {
    digitalWrite(relejParniPin, LOW);
    digitalWrite(relejNeparniPin, LOW);
    impulsUTijeku = false;
  }
}

void postaviTrenutniPolozajKazaljki(int trenutnaMinuta) {
  memoriraneKazaljkeMinuta = constrain(trenutnaMinuta, 0, 1439);
  EEPROM.put(10, memoriraneKazaljkeMinuta);
}

void pomakniKazaljkeNaMinutu(int ciljMinuta) {
  ciljMinuta = constrain(ciljMinuta, 0, 1439);
  int razlika = ciljMinuta - memoriraneKazaljkeMinuta;
  if (razlika < 0) razlika += 1440;

  for (int i = 0; i < razlika; i++) {
    int pin = ((memoriraneKazaljkeMinuta + i) % 2 == 0) ? relejParniPin : relejNeparniPin;
    digitalWrite(pin, HIGH);
    delay(TRAJANJE_IMPULSA);
    digitalWrite(pin, LOW);
    delay(500);
  }
  memoriraneKazaljkeMinuta = ciljMinuta;
  EEPROM.put(10, memoriraneKazaljkeMinuta);
}

void kompenzirajKazaljke() {
  DateTime now = RTC_DS3231().now();
  int trenutnaMinuta = now.hour() * 60 + now.minute();
  pomakniKazaljkeNaMinutu(trenutnaMinuta);
}
