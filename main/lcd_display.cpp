#include "lcd_display.h"
#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <RTClib.h>
// #include <cstring>      // ovo makni
#include <string.h>        // za strncpy, strncmp
#include <stdio.h>         // za snprintf
#include "time_glob.h"
#include "tipke.h"
#include "watchdog.h"

static LiquidCrystal_I2C lcd(0x27, 16, 2); // prilagodi adresu po potrebi

static const char* dani[7] = {"NED", "PON", "UTO", "SRI", "CET", "PET", "SUB"};
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
  bool istaPoruka = prikazPoruke && strncmp(zadnjaPorukaRed1, red1, sizeof(zadnjaPorukaRed1)) == 0 &&
                    strncmp(zadnjaPorukaRed2, red2, sizeof(zadnjaPorukaRed2)) == 0;
  if (istaPoruka) {
    return;
  }

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
    if (!jeRTCPouzdan()) {
      if (!fallbackImaPouzdanuReferencu()) {
        postaviPorukuNaLCD("RTC baterija", "treba zamjenu");
      } else {
        postaviPorukuNaLCD("Cekam sinkron.", "RTC nije valjan");
      }
      upravljajBlinkanjem();
      return;
    }
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

      char sekunde[3];
      if (prikaziSekunde) {
        snprintf(sekunde, sizeof(sekunde), "%02d", now.second());
      } else {
        strncpy(sekunde, "  ", sizeof(sekunde));
      }

      snprintf(red1, sizeof(red1), "%02d:%02d:%s %-3s%c",
        now.hour(), now.minute(), sekunde,
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
  const char* red1 = dohvatiPostavkeRedak1();
  const char* red2 = dohvatiPostavkeRedak2();
  postaviPorukuNaLCD(red1, red2);
  upravljajBlinkanjem();
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
    osvjeziWatchdog();
    delay(10);
  }
}
