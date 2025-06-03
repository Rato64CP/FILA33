// kazaljke_sata.cpp
#include <Arduino.h>
#include <RTClib.h>
#include <EEPROM.h>
#include "kazaljke_sata.h"
#include "podesavanja_piny.h"
#include "time_glob.h"

const unsigned long TRAJANJE_IMPULSA = 6000UL;
const int MAKS_PAMETNI_POMAK_MINUTA = 15;

static DateTime zadnjeVrijeme;
static unsigned long vrijemePocetkaImpulsa = 0;
static bool impulsUTijeku = false;

int memoriraneKazaljkeMinuta = 0;

void inicijalizirajKazaljke() {
  pinMode(PIN_RELEJ_PARNE_KAZALJKE, OUTPUT);
  pinMode(PIN_RELEJ_NEPARNE_KAZALJKE, OUTPUT);
  digitalWrite(PIN_RELEJ_PARNE_KAZALJKE, LOW);
  digitalWrite(PIN_RELEJ_NEPARNE_KAZALJKE, LOW);
  EEPROM.get(10, memoriraneKazaljkeMinuta);
  if (memoriraneKazaljkeMinuta < 0 || memoriraneKazaljkeMinuta > 1439) memoriraneKazaljkeMinuta = 0;
}

void upravljajKazaljkama() {
  DateTime now = rtc.now();
  int ukupnoMinuta = now.hour() * 60 + now.minute();
  if (!impulsUTijeku && now.second() == 0 && now != zadnjeVrijeme) {
    zadnjeVrijeme = now;
    int pin = (now.minute() % 2 == 0) ? PIN_RELEJ_PARNE_KAZALJKE : PIN_RELEJ_NEPARNE_KAZALJKE;
    digitalWrite(pin, HIGH);
    vrijemePocetkaImpulsa = millis();
    impulsUTijeku = true;
    memoriraneKazaljkeMinuta++;
    if (memoriraneKazaljkeMinuta > 1439) memoriraneKazaljkeMinuta = 0;
    EEPROM.put(10, memoriraneKazaljkeMinuta);
  }
  if (impulsUTijeku && millis() - vrijemePocetkaImpulsa >= TRAJANJE_IMPULSA) {
    digitalWrite(PIN_RELEJ_PARNE_KAZALJKE, LOW);
    digitalWrite(PIN_RELEJ_NEPARNE_KAZALJKE, LOW);
    impulsUTijeku = false;
  }
}

void postaviTrenutniPolozajKazaljki(int trenutnaMinuta) {
  memoriraneKazaljkeMinuta = constrain(trenutnaMinuta, 0, 1439);
  EEPROM.put(10, memoriraneKazaljkeMinuta);
}

void pomakniKazaljkeNaMinutu(int ciljMinuta, bool pametanMod) {
  ciljMinuta = constrain(ciljMinuta, 0, 1439);
  int razlika = ciljMinuta - memoriraneKazaljkeMinuta;
  if (razlika < 0) razlika += 1440;

  if (pametanMod && razlika <= MAKS_PAMETNI_POMAK_MINUTA) return;

  for (int i = 0; i < razlika; i++) {
    int pin = ((memoriraneKazaljkeMinuta + i) % 2 == 0) ? PIN_RELEJ_PARNE_KAZALJKE : PIN_RELEJ_NEPARNE_KAZALJKE;
    digitalWrite(pin, HIGH);
    delay(TRAJANJE_IMPULSA);
    digitalWrite(pin, LOW);
    delay(500);
  }
  memoriraneKazaljkeMinuta = ciljMinuta;
  EEPROM.put(10, memoriraneKazaljkeMinuta);
}

void kompenzirajKazaljke(bool pametanMod) {
  DateTime now = rtc.now();
  int trenutnaMinuta = now.hour() * 60 + now.minute();
  pomakniKazaljkeNaMinutu(trenutnaMinuta, pametanMod);
}

void pomakniKazaljkeZa(int brojMinuta) {
  int cilj = memoriraneKazaljkeMinuta + brojMinuta;
  if (cilj > 1439) cilj -= 1440;
  pomakniKazaljkeNaMinutu(cilj, false);
}
