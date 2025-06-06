// lcd_prikaz.cpp
#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <RTClib.h>
#include "time_glob.h"
#include "vrijeme_izvor.h"

LiquidCrystal_I2C lcd(0x27, 16, 2); // prilagodi adresu po potrebi

const char* dani[7] = {"Ned", "Pon", "Uto", "Sri", "Cet", "Pet", "Sub"};
String izvor = "RTC"; // moze biti "RTC", "NTP", "DCF"
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

  DateTime now = dohvatiTrenutnoVrijeme();
  char red1[17];
  char red2[17];

  static int zadnjiDan = -1;
  if (now.dayOfTheWeek() != zadnjiDan) {
    zadnjiDan = now.dayOfTheWeek();
    azurirajOznakuDana();
  }

  snprintf(red1, sizeof(red1), "%02d.%02d.%02d  %-3s %c",
    now.hour(), now.minute(), prikaziSekunde ? now.second() : 32,
    dohvatiIzvorVremena().c_str(), dohvatiOznakuDana());

  snprintf(red2, sizeof(red2), "%s, %02d:%02d:%04d.",
    dani[now.dayOfTheWeek()], now.day(), now.month(), now.year());

  lcd.setCursor(0, 0);
  lcd.print(red1);
  lcd.setCursor(0, 1);
  lcd.print(red2);

  prikaziSekunde = !prikaziSekunde;
}
