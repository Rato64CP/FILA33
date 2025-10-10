// kazaljke_sata.cpp
#include <Arduino.h>
#include <RTClib.h>
#include <EEPROM.h>
#include "kazaljke_sata.h"
#include "podesavanja_piny.h"
#include "time_glob.h"
#include "lcd_display.h"

const unsigned long TRAJANJE_IMPULSA = 6000UL;
const int MAKS_PAMETNI_POMAK_MINUTA = 15;

static unsigned long vrijemePocetkaImpulsa = 0;
static bool impulsUTijeku = false;
static bool drugaFaza = false;
static int zadnjaAktiviranaMinuta = -1;

int memoriraneKazaljkeMinuta = 0;

static void zavrsiImpuls()
{
  digitalWrite(PIN_RELEJ_PARNE_KAZALJKE, LOW);
  digitalWrite(PIN_RELEJ_NEPARNE_KAZALJKE, LOW);
  impulsUTijeku = false;
  drugaFaza = false;
  memoriraneKazaljkeMinuta = (memoriraneKazaljkeMinuta + 1) % 1440;
  EEPROM.put(10, memoriraneKazaljkeMinuta);
}

static void pokreniPrvuFazu()
{
  digitalWrite(PIN_RELEJ_PARNE_KAZALJKE, HIGH);
  digitalWrite(PIN_RELEJ_NEPARNE_KAZALJKE, LOW);
  vrijemePocetkaImpulsa = millis();
  impulsUTijeku = true;
  drugaFaza = false;
}

void inicijalizirajKazaljke() {
  pinMode(PIN_RELEJ_PARNE_KAZALJKE, OUTPUT);
  pinMode(PIN_RELEJ_NEPARNE_KAZALJKE, OUTPUT);
  digitalWrite(PIN_RELEJ_PARNE_KAZALJKE, LOW);
  digitalWrite(PIN_RELEJ_NEPARNE_KAZALJKE, LOW);
  EEPROM.get(10, memoriraneKazaljkeMinuta);
  if (memoriraneKazaljkeMinuta < 0 || memoriraneKazaljkeMinuta > 1439) memoriraneKazaljkeMinuta = 0;
  impulsUTijeku = false;
  drugaFaza = false;
  zadnjaAktiviranaMinuta = -1;
}

void upravljajKazaljkama() {
  DateTime now = dohvatiTrenutnoVrijeme();
  int trenutnaMinuta = now.minute();

  if (!impulsUTijeku && now.second() <= 1 && trenutnaMinuta != zadnjaAktiviranaMinuta) {
    pokreniPrvuFazu();
    zadnjaAktiviranaMinuta = trenutnaMinuta;
  }

  if (!impulsUTijeku) return;

  unsigned long proteklo = millis() - vrijemePocetkaImpulsa;
  if (!drugaFaza && proteklo >= TRAJANJE_IMPULSA) {
    digitalWrite(PIN_RELEJ_PARNE_KAZALJKE, LOW);
    digitalWrite(PIN_RELEJ_NEPARNE_KAZALJKE, HIGH);
    vrijemePocetkaImpulsa = millis();
    drugaFaza = true;
  }
  else if (drugaFaza && proteklo >= TRAJANJE_IMPULSA) {
    zavrsiImpuls();
  }
}

void postaviTrenutniPolozajKazaljki(int trenutnaMinuta) {
  memoriraneKazaljkeMinuta = constrain(trenutnaMinuta, 0, 1439);
  EEPROM.put(10, memoriraneKazaljkeMinuta);
}

static void odradiJedanPomakBlokirajuci() {
  digitalWrite(PIN_RELEJ_PARNE_KAZALJKE, HIGH);
  odradiPauzuSaLCD(TRAJANJE_IMPULSA);
  digitalWrite(PIN_RELEJ_PARNE_KAZALJKE, LOW);
  odradiPauzuSaLCD(200);
  digitalWrite(PIN_RELEJ_NEPARNE_KAZALJKE, HIGH);
  odradiPauzuSaLCD(TRAJANJE_IMPULSA);
  digitalWrite(PIN_RELEJ_NEPARNE_KAZALJKE, LOW);
  odradiPauzuSaLCD(400);
  memoriraneKazaljkeMinuta = (memoriraneKazaljkeMinuta + 1) % 1440;
  EEPROM.put(10, memoriraneKazaljkeMinuta);
}

void pomakniKazaljkeNaMinutu(int ciljMinuta, bool pametanMod) {
  ciljMinuta = constrain(ciljMinuta, 0, 1439);
  int razlika = ciljMinuta - memoriraneKazaljkeMinuta;
  if (razlika < 0) razlika += 1440;

  if (pametanMod && razlika <= MAKS_PAMETNI_POMAK_MINUTA) return;

  for (int i = 0; i < razlika; i++) {
    odradiJedanPomakBlokirajuci();
  }

  memoriraneKazaljkeMinuta = ciljMinuta;
  EEPROM.put(10, memoriraneKazaljkeMinuta);
}

void kompenzirajKazaljke(bool pametanMod) {
  DateTime now = dohvatiTrenutnoVrijeme();
  int trenutnaMinuta = now.hour() * 60 + now.minute();
  pomakniKazaljkeNaMinutu(trenutnaMinuta, pametanMod);
  zadnjaAktiviranaMinuta = now.minute();
  impulsUTijeku = false;
  drugaFaza = false;
}

void pomakniKazaljkeZa(int brojMinuta) {
  int cilj = memoriraneKazaljkeMinuta + brojMinuta;
  while (cilj < 0) cilj += 1440;
  cilj %= 1440;
  pomakniKazaljkeNaMinutu(cilj, false);
}

bool suKazaljkeUSinkronu() {
  DateTime now = dohvatiTrenutnoVrijeme();
  int trenutnaMinuta = now.hour() * 60 + now.minute();
  return memoriraneKazaljkeMinuta == trenutnaMinuta;
}

int dohvatiMemoriraneKazaljkeMinuta() {
  return memoriraneKazaljkeMinuta;
}

void oznaciKazaljkeKaoSinkronizirane() {
  DateTime now = dohvatiTrenutnoVrijeme();
  zadnjaAktiviranaMinuta = now.minute();
}
