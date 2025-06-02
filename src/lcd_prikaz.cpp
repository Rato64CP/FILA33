// lcd_prikaz.cpp
#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <RTClib.h>
#include "time_glob.h"
#include "vrijeme_izvor.h"

LiquidCrystal_I2C lcd(0x27, 16, 2); // prilagodi adresu po potrebi

const char* dani[7] = {"Ned", "Pon", "Uto", "Sri", "Cet", "Pet", "Sub"};
String izvor = "RTC"; // moze biti "RTC", "NTP", "DCF", "---"
char oznakaDana = 'R'; // 'R' ili 'N'
bool prikaziSekunde = true;
unsigned long zadnjiRefresh = 0;

void inicijalizirajLCD() {
  lcd.init();
  lcd.backlight();
  lcd.clear();
}

void azurirajLCDPrikaz() {
  if (millis() - zadnjiRefresh < 500) return;
  zadnjiRefresh = millis();

  DateTime now = RTC_DS3231().now();
  char red1[17];
  char red2[17];

  // Odredi izvor vremena za prikaz
  IzvorVremena izvorEnum = getZadnjiIzvor();
  if (jeSinkronizacijaZastarjela()) {
    izvor = "---";
  } else {
    switch (izvorEnum) {
      case RTC_VRIJEME: izvor = "RTC"; break;
      case NTP_VRIJEME: izvor = "NTP"; break;
      case DCF_VRIJEME: izvor = "DCF"; break;
      default: izvor = "---"; break;
    }
  }

  snprintf(red1, sizeof(red1), "%02d.%02d.%02d  %-3s %c",
    now.hour(), now.minute(), prikaziSekunde ? now.second() : 32,
    izvor.c_str(), oznakaDana);

  snprintf(red2, sizeof(red2), "%s, %02d:%02d:%04d.",
    dani[now.dayOfTheWeek()], now.day(), now.month(), now.year());

  lcd.setCursor(0, 0);
  lcd.print(red1);
  lcd.setCursor(0, 1);
  lcd.print(red2);

  prikaziSekunde = !prikaziSekunde;
}
