// time_glob.cpp
#include <Arduino.h>
#include <RTClib.h>
#include "time_glob.h"
#include "vrijeme_izvor.h"
#include "eeprom_konstante.h"
#include "wear_leveling.h"

RTC_DS3231 rtc;

static constexpr size_t MAKS_DULJINA_IZVORA = 4; // ukljucuje nul terminator

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

static void procitajIzvorVremena() {
  char spremljeniIzvor[MAKS_DULJINA_IZVORA] = {0};
  if (!WearLeveling::ucitaj(EepromLayout::BAZA_IZVOR_VREMENA, EepromLayout::SLOTOVI_IZVOR_VREMENA, spremljeniIzvor)) {
    spremljeniIzvor[0] = '\0';
  } else {
    spremljeniIzvor[MAKS_DULJINA_IZVORA - 1] = '\0';
  }

  String ucitani = String(spremljeniIzvor);
  if (ucitani == "NTP" || ucitani == "RU" || ucitani == "DCF") {
    izvorVremena = ucitani;
  } else {
    izvorVremena = "RTC";
  }
}

static void spremiIzvorVremena() {
  char spremi[MAKS_DULJINA_IZVORA] = {0};
  izvorVremena.toCharArray(spremi, MAKS_DULJINA_IZVORA);
  WearLeveling::spremi(EepromLayout::BAZA_IZVOR_VREMENA, EepromLayout::SLOTOVI_IZVOR_VREMENA, spremi);
}

void azurirajVrijemeIzNTP(const DateTime& dt) {
  postaviVrijemeIzNTP(dt);
  azurirajOznakuDana();
}

void inicijalizirajRTC() {
  if (!rtc.begin()) {
    rtcPouzdan = false;
    aktivirajFallbackVrijeme();
  } else {
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
  }

  procitajIzvorVremena();
  if (!rtcPouzdan) {
    izvorVremena = fallbackImaReferencu ? "CEK" : "ERR";
    spremiIzvorVremena();
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
  spremiIzvorVremena();
  setZadnjaSinkronizacija(NTP_VRIJEME, dt);
}

void postaviVrijemeIzDCF(const DateTime& dt) {
  rtc.adjust(dt);
  oznaciRTCPouzdanSaVremenom(dt);
  izvorVremena = "DCF";
  spremiIzvorVremena();
  setZadnjaSinkronizacija(DCF_VRIJEME, dt);
}

void postaviVrijemeRucno(const DateTime& dt) {
  rtc.adjust(dt);
  oznaciRTCPouzdanSaVremenom(dt);
  izvorVremena = "RU";
  spremiIzvorVremena();
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
    spremiIzvorVremena();
    return;
  }
  if (izvorVremena == "RTC") return;
  izvorVremena = "RTC";
  spremiIzvorVremena();
  setZadnjaSinkronizacija(RTC_VRIJEME, dohvatiTrenutnoVrijeme());
}

bool jeRTCPouzdan() {
  return rtcPouzdan;
}

bool fallbackImaPouzdanuReferencu() {
  return fallbackImaReferencu;
}
