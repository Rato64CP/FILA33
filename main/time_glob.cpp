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

// ---------------------------------------------------------------------------
// Pomoćne funkcije za DST (CET/CEST, Hrvatska / EU)
// ---------------------------------------------------------------------------

// Određuje je li zadani UTC trenutak unutar razdoblja ljetnog računanja vremena
static bool jeLjetnoVrijeme(const DateTime& t) {
  int godina = t.year();

  // Zadnja nedjelja u ožujku u 2:00 (po UTC-u)
  DateTime zadnjaNedOzu(godina, 3, 31, 2, 0, 0);
  while (zadnjaNedOzu.dayOfTheWeek() != 0) { // 0 = nedjelja
    zadnjaNedOzu = zadnjaNedOzu - TimeSpan(24 * 3600);
  }

  // Zadnja nedjelja u listopadu u 3:00 (po UTC-u)
  DateTime zadnjaNedLis(godina, 10, 31, 3, 0, 0);
  while (zadnjaNedLis.dayOfTheWeek() != 0) {
    zadnjaNedLis = zadnjaNedLis - TimeSpan(24 * 3600);
  }

  uint32_t unixT = t.unixtime();
  return (unixT >= zadnjaNedOzu.unixtime() && unixT < zadnjaNedLis.unixtime());
}

// Pretvara UTC DateTime u lokalno vrijeme (CET/CEST)
static DateTime pretvoriUTCULokalno(const DateTime& utc) {
  // CET = UTC+1, CEST = UTC+2
  if (jeLjetnoVrijeme(utc)) {
    return utc + TimeSpan(2 * 3600);
  } else {
    return utc + TimeSpan(1 * 3600);
  }
}

// ---------------------------------------------------------------------------

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

void azurirajVrijemeIzNTP(const DateTime& dtUTC) {
  // dtUTC je *UTC* vrijeme koje dolazi s ESP-a
  postaviVrijemeIzNTP(dtUTC);
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

// dt (NTP) dolazi u UTC formatu; ovdje ga prevodimo u lokalno vrijeme i spremamo u RTC
void postaviVrijemeIzNTP(const DateTime& dtUTC) {
  // 1) Pretvori UTC → lokalno (CET/CEST)
  DateTime lokalno = pretvoriUTCULokalno(dtUTC);

  // 2) Spremaj u RTC lokalno vrijeme
  rtc.adjust(lokalno);
  oznaciRTCPouzdanSaVremenom(lokalno);
  izvorVremena = "NTP";
  spremiIzvorVremena();
  setZadnjaSinkronizacija(NTP_VRIJEME, lokalno);
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