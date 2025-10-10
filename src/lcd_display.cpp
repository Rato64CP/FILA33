#include "lcd_display.h"
#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <RTClib.h>
#include <cstring>
#include "time_glob.h"
#include "vrijeme_izvor.h"

static LiquidCrystal_I2C lcd(0x27, 16, 2); // prilagodi adresu po potrebi

static const char* dani[7] = {"Ned", "Pon", "Uto", "Sri", "Cet", "Pet", "Sub"};
static bool prikazPoruke = false;
static char zadnjaPorukaRed1[17];
static char zadnjaPorukaRed2[17];
static bool prikaziSekunde = true;
static unsigned long zadnjiRefresh = 0;
static bool blinkanjeAktivno = false;
static bool lcdVidljiv = true;
static unsigned long zadnjeBlinkanje = 0;

static void postaviStandardniPrikaz() {
  prikazPoruke = false;
}

static void postaviPorukuNaLCD(const char* red1, const char* red2) {
  strncpy(zadnjaPorukaRed1, red1, sizeof(zadnjaPorukaRed1) - 1);
  zadnjaPorukaRed1[sizeof(zadnjaPorukaRed1) - 1] = '\0';
  strncpy(zadnjaPorukaRed2, red2, sizeof(zadnjaPorukaRed2) - 1);
  zadnjaPorukaRed2[sizeof(zadnjaPorukaRed2) - 1] = '\0';
  prikazPoruke = true;
  lcd.display();
  lcdVidljiv = true;
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(zadnjaPorukaRed1);
  lcd.setCursor(0, 1);
  lcd.print(zadnjaPorukaRed2);
}

static void upravljajBlinkanjem() {
  if (!blinkanjeAktivno) {
    if (!lcdVidljiv) {
      lcd.display();
      lcdVidljiv = true;
    }
    return;
  }

  unsigned long sada = millis();
  if (sada - zadnjeBlinkanje >= 500UL) {
    zadnjeBlinkanje = sada;
    if (lcdVidljiv) {
      lcd.noDisplay();
    } else {
      lcd.display();
    }
    lcdVidljiv = !lcdVidljiv;
  }
}

static void azurirajLCDPrikaz() {
  unsigned long sada = millis();
  if (!prikazPoruke) {
    if (sada - zadnjiRefresh >= 500UL) {
      zadnjiRefresh = sada;

      DateTime now = dohvatiTrenutnoVrijeme();
      char red1[17];
      char red2[17];

      static int zadnjiDan = -1;
      if (now.dayOfTheWeek() != zadnjiDan) {
        zadnjiDan = now.dayOfTheWeek();
        azurirajOznakuDana();
      }

      snprintf(red1, sizeof(red1), "%02d:%02d:%02d %-3s%c",
        now.hour(), now.minute(), prikaziSekunde ? now.second() : 32,
        dohvatiIzvorVremena().c_str(), dohvatiOznakuDana());

      snprintf(red2, sizeof(red2), "%s %02d.%02d.%04d",
        dani[now.dayOfTheWeek()], now.day(), now.month(), now.year());

      lcd.setCursor(0, 0);
      lcd.print("                ");
      lcd.setCursor(0, 0);
      lcd.print(red1);
      lcd.setCursor(0, 1);
      lcd.print("                ");
      lcd.setCursor(0, 1);
      lcd.print(red2);

      prikaziSekunde = !prikaziSekunde;
    }
  }

  upravljajBlinkanjem();
}

void inicijalizirajLCD() {
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.display();
  lcdVidljiv = true;
}

void prikaziSat() {
  postaviStandardniPrikaz();
  azurirajLCDPrikaz();
}

void prikaziPostavke() {
  postaviStandardniPrikaz();
  azurirajLCDPrikaz();
}

void prikaziPoruku(const char* redak1, const char* redak2) {
  postaviPorukuNaLCD(redak1, redak2);
}

void postaviLCDBlinkanje(bool omoguci) {
  blinkanjeAktivno = omoguci;
  zadnjeBlinkanje = millis();
  if (!omoguci && !lcdVidljiv) {
    lcd.display();
    lcdVidljiv = true;
  }
}

void odradiPauzuSaLCD(unsigned long trajanjeMs) {
  unsigned long start = millis();
  while (millis() - start < trajanjeMs) {
    azurirajLCDPrikaz();
    delay(10);
  }
}
