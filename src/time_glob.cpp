// time_glob.cpp
#include <Arduino.h>
#include <RTClib.h>
#include <EEPROM.h>
#include "time_glob.h"
#include "vrijeme_izvor.h"

RTC_DS3231 rtc;

String izvorVremena = "RTC"; // moze biti "NTP", "DCF", "RU" ili "RTC"
char oznakaDana = 'R';

static bool rtcPouzdan = true;
static bool fallbackAktivan = false;
static bool fallbackImaReferencu = false;
static DateTime fallbackVrijeme = DateTime((uint32_t)0);
static unsigned long fallbackMillis = 0;

static void aktivirajFallbackVrijeme() {
  fallbackVrijeme = getZadnjeSinkroniziranoVrijeme();
  fallbackImaReferencu = fallbackVrijeme.unixtime() > 0;
  if (!fallbackImaReferencu) {
    fallbackVrijeme = DateTime(F(__DATE__), F(__TIME__));
  }
  fallbackMillis = millis();
  fallbackAktivan = true;
}

static DateTime izracunajFallbackVrijeme() {
  if (!fallbackAktivan) {
    aktivirajFallbackVrijeme();
  }
  unsigned long proteklo = millis() - fallbackMillis;
  return fallbackVrijeme + TimeSpan(proteklo / 1000);
}

static void oznaciRTCPouzdanSaVremenom(const DateTime& referenca) {
  rtcPouzdan = true;
  fallbackAktivan = false;
  fallbackImaReferencu = true;
  fallbackVrijeme = referenca;
  fallbackMillis = millis();
}

void azurirajVrijemeIzNTP(const DateTime& dt) {
  postaviVrijemeIzNTP(dt);
  azurirajOznakuDana();
}

void inicijalizirajRTC() {
  rtc.begin();
  bool trebaSinkronizaciju = rtc.lostPower();
  DateTime trenutno = rtc.now();
  if (trenutno.year() < 2024) {
    trebaSinkronizaciju = true;
  }
  if (trebaSinkronizaciju) {
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__))); // privremeno vrijeme dok cekamo sinkronizaciju
    rtcPouzdan = false;
    aktivirajFallbackVrijeme();
  } else {
    oznaciRTCPouzdanSaVremenom(trenutno);
  }
  EEPROM.get(30, izvorVremena);
  if (izvorVremena != "NTP" && izvorVremena != "RU" && izvorVremena != "DCF") izvorVremena = "RTC";
  if (!rtcPouzdan) {
    izvorVremena = fallbackImaReferencu ? "CEK" : "ERR";
    EEPROM.put(30, izvorVremena);
  }
}

DateTime dohvatiTrenutnoVrijeme() {
  if (rtcPouzdan) {
    return rtc.now();
  }
  return izracunajFallbackVrijeme();
}

void postaviVrijemeIzNTP(const DateTime& dt) {
  rtc.adjust(dt);
  oznaciRTCPouzdanSaVremenom(dt);
  izvorVremena = "NTP";
  EEPROM.put(30, izvorVremena);
  setZadnjaSinkronizacija(NTP_VRIJEME, dt);
}

void postaviVrijemeIzDCF(const DateTime& dt) {
  rtc.adjust(dt);
  oznaciRTCPouzdanSaVremenom(dt);
  izvorVremena = "DCF";
  EEPROM.put(30, izvorVremena);
  setZadnjaSinkronizacija(DCF_VRIJEME, dt);
}

void postaviVrijemeRucno(const DateTime& dt) {
  rtc.adjust(dt);
  oznaciRTCPouzdanSaVremenom(dt);
  izvorVremena = "RU";
  EEPROM.put(30, izvorVremena);
  setZadnjaSinkronizacija(RTC_VRIJEME, dt);
}

void azurirajVrijemeIzDCF(const DateTime& dt) {
  postaviVrijemeIzDCF(dt);
  azurirajOznakuDana();
}

void azurirajOznakuDana() {
  DateTime sada = dohvatiTrenutnoVrijeme();
  int dan = sada.dayOfTheWeek();
  oznakaDana = (dan == 0) ? 'N' : 'R';
}

String dohvatiIzvorVremena() {
  return izvorVremena;
}

char dohvatiOznakuDana() {
  return oznakaDana;
}

void oznaciPovratakNaRTC() {
  if (!rtcPouzdan) {
    izvorVremena = fallbackImaReferencu ? "CEK" : "ERR";
    EEPROM.put(30, izvorVremena);
    return;
  }
  if (izvorVremena == "RTC") return;
  izvorVremena = "RTC";
  EEPROM.put(30, izvorVremena);
  setZadnjaSinkronizacija(RTC_VRIJEME, dohvatiTrenutnoVrijeme());
}

bool jeRTCPouzdan() {
  return rtcPouzdan;
}

bool fallbackImaPouzdanuReferencu() {
  return fallbackImaReferencu;
}
