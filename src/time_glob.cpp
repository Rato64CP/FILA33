// time_glob.cpp
#include <RTClib.h>
#include <EEPROM.h>
#include "time_glob.h"

RTC_DS3231 rtc;

String izvorVremena = "RTC"; // moze biti "NTP", "DCF", "RU" ili "RTC"
char oznakaDana = 'R';

void inicijalizirajSat() {
  rtc.begin();
  if (rtc.lostPower()) {
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__))); // postavi na build vrijeme
  }
  EEPROM.get(30, izvorVremena);
  if (izvorVremena != "NTP" && izvorVremena != "DCF" && izvorVremena != "RU") izvorVremena = "RTC";
}

DateTime dohvatiTrenutnoVrijeme() {
  return rtc.now();
}

void postaviVrijemeIzNTP(const DateTime& dt) {
  rtc.adjust(dt);
  izvorVremena = "NTP";
  EEPROM.put(30, izvorVremena);
}

void postaviVrijemeIzDCF(const DateTime& dt) {
  rtc.adjust(dt);
  izvorVremena = "DCF";
  EEPROM.put(30, izvorVremena);
}

void postaviVrijemeRucno(const DateTime& dt) {
  rtc.adjust(dt);
  izvorVremena = "RU";
  EEPROM.put(30, izvorVremena);
}

void azurirajOznakuDana() {
  DateTime sada = rtc.now();
  int dan = sada.dayOfTheWeek();
  oznakaDana = (dan == 0) ? 'N' : 'R';
}

String dohvatiIzvorVremena() {
  return izvorVremena;
}

char dohvatiOznakuDana() {
  return oznakaDana;
}
