// time_glob.cpp – Globalno rukovanje vremenom
#include <Arduino.h>
#include <RTClib.h>
#include "time_glob.h"
#include "i2c_eeprom.h"
#include "eeprom_konstante.h"
#include "wear_leveling.h"
#include "pc_serial.h"

// DS3231 RTC modul
static RTC_DS3231 rtc;

// Trenutno vrijeme (cache)
static DateTime trenutnoVrijeme;

// Izvor vremena
static enum {
  IZ_RTC,
  IZ_NTP,
  IZ_DCF
} trenutniIzvor = IZ_RTC;

// Zadnja sinkronizacija
static DateTime zadnjaSinkronizacija((uint32_t)0);
static unsigned long zadnjaSinkronizacijaMs = 0;
static const unsigned long TIMEOUT_SINKRONIZACIJE_MS = 3600000; // 1 sat

// Oznake
static char oznakaDana = 'N';
static char oznakaizvora[4] = "RTC";

// RTC pouzdanost
static bool rtcBaterijaOk = false;

// Fallback referencija (koristi zadnje poznato vrijeme ako RTC/NTP/DCF nisu dostupni)
static bool fallbackAktivan = false;
static DateTime fallbackVrijeme((uint32_t)0);

// -------------------- POMOĆNE FUNKCIJE --------------------

static void azurirajOznakuIzvora() {
  switch (trenutniIzvor) {
    case IZ_RTC:
      strncpy(oznakaizvora, "RTC", sizeof(oznakaizvora) - 1);
      break;
    case IZ_NTP:
      strncpy(oznakaizvora, "NTP", sizeof(oznakaizvora) - 1);
      break;
    case IZ_DCF:
      strncpy(oznakaizvora, "DCF", sizeof(oznakaizvora) - 1);
      break;
    default:
      strncpy(oznakaizvora, "???", sizeof(oznakaizvora) - 1);
      break;
  }
  oznakaizvora[sizeof(oznakaizvora) - 1] = '\0';
}

static void spremiZadnjuSinkronizaciju() {
  EepromLayout::ZadnjaSinkronizacija zs;
  zs.izvor = (int)trenutniIzvor;
  zs.timestamp = zadnjaSinkronizacija.unixtime();
  WearLeveling::spremi(EepromLayout::BAZA_ZADNJA_SINKRONIZACIJA,
                       EepromLayout::SLOTOVI_ZADNJA_SINKRONIZACIJA,
                       zs);
  zadnjaSinkronizacijaMs = millis();
}

static void ucitajZadnjuSinkronizaciju() {
  EepromLayout::ZadnjaSinkronizacija zs;
  if (WearLeveling::ucitaj(EepromLayout::BAZA_ZADNJA_SINKRONIZACIJA,
                          EepromLayout::SLOTOVI_ZADNJA_SINKRONIZACIJA,
                          zs)) {
    zadnjaSinkronizacija = DateTime(zs.timestamp);
    trenutniIzvor = (zs.izvor == IZ_NTP ? IZ_NTP : (zs.izvor == IZ_DCF ? IZ_DCF : IZ_RTC));
  } else {
    zadnjaSinkronizacija = DateTime((uint32_t)0);
    trenutniIzvor = IZ_RTC;
  }
  azurirajOznakuIzvora();
}

// -------------------- JAVNE FUNKCIJE --------------------

void inicijalizirajRTC() {
  if (!rtc.begin()) {
    posaljiPCLog(F("RTC: DS3231 nije dostupan"));
    rtcBaterijaOk = false;
  } else {
    // Provjera valjanosti RTC baterije
    if (rtc.lostPower()) {
      posaljiPCLog(F("RTC: baterija je prazna, vrijeme nije pouzdano"));
      rtcBaterijaOk = false;
      // Postavi minimalnu vrijednost
      rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    } else {
      rtcBaterijaOk = true;
      posaljiPCLog(F("RTC: baterija OK, vrijeme je pouzdano"));
    }
    trenutnoVrijeme = rtc.now();
  }
  ucitajZadnjuSinkronizaciju();
  oznaciPovratakNaRTC();
}

DateTime dohvatiTrenutnoVrijeme() {
  // Osvježi vrijeme svakih 100 ms ako je RTC dostupan
  static unsigned long zadnjaProvjeraMs = 0;
  unsigned long sadaMs = millis();
  
  if (sadaMs - zadnjaProvjeraMs >= 100 || zadnjaProvjeraMs == 0) {
    zadnjaProvjeraMs = sadaMs;
    if (rtc.begin()) {
      trenutnoVrijeme = rtc.now();
    }
  }

  if (trenutniIzvor != IZ_RTC && jeSinkronizacijaZastarjela()) {
    oznaciPovratakNaRTC();
  }
  
  return trenutnoVrijeme;
}

void azurirajVrijemeIzNTP(const DateTime& ntpVrijeme) {
  // Provjera validnosti NTP vremena
  if (ntpVrijeme.unixtime() == 0 || ntpVrijeme.year() < 2024) {
    posaljiPCLog(F("NTP: neispravno vrijeme, odbacujem"));
    return;
  }
  
  // Ažuriranje RTC-a
  if (rtc.begin()) {
    rtc.adjust(ntpVrijeme);
    rtcBaterijaOk = true;
  }
  
  trenutnoVrijeme = ntpVrijeme;
  zadnjaSinkronizacija = ntpVrijeme;
  trenutniIzvor = IZ_NTP;
  azurirajOznakuIzvora();
  spremiZadnjuSinkronizaciju();
  
  String log = F("Vrijeme ažurirano iz NTP: ");
  log += ntpVrijeme.year();
  log += F("-");
  log += ntpVrijeme.month();
  log += F("-");
  log += ntpVrijeme.day();
  log += F(" ");
  log += ntpVrijeme.hour();
  log += F(":");
  log += ntpVrijeme.minute();
  posaljiPCLog(log);
}

void azurirajVrijemeIzDCF(const DateTime& dcfVrijeme) {
  // Provjera validnosti DCF vremena
  if (dcfVrijeme.unixtime() == 0 || dcfVrijeme.year() < 2024) {
    posaljiPCLog(F("DCF: neispravno vrijeme, odbacujem"));
    return;
  }
  
  // Ako je NTP najnoviji izvor, ne mijenjaj
  if (trenutniIzvor == IZ_NTP && !jeSinkronizacijaZastarjela()) {
    return;
  }
  
  // Ažuriranje RTC-a
  if (rtc.begin()) {
    rtc.adjust(dcfVrijeme);
    rtcBaterijaOk = true;
  }
  
  trenutnoVrijeme = dcfVrijeme;
  zadnjaSinkronizacija = dcfVrijeme;
  trenutniIzvor = IZ_DCF;
  azurirajOznakuIzvora();
  spremiZadnjuSinkronizaciju();
  
  String log = F("Vrijeme ažurirano iz DCF: ");
  log += dcfVrijeme.year();
  log += F("-");
  log += dcfVrijeme.month();
  log += F("-");
  log += dcfVrijeme.day();
  log += F(" ");
  log += dcfVrijeme.hour();
  log += F(":");
  log += dcfVrijeme.minute();
  posaljiPCLog(log);
}

String dohvatiIzvorVremena() {
  return String(oznakaizvora);
}

char dohvatiOznakuDana() {
  return oznakaDana;
}

void azurirajOznakuDana() {
  DateTime sada = dohvatiTrenutnoVrijeme();
  uint8_t dan = sada.dayOfTheWeek();
  const char znakovi[] = {'N', 'P', 'U', 'S', 'C', 'P', 'S'};
  if (dan < 7) {
    oznakaDana = znakovi[dan];
  }
}

bool jeRTCPouzdan() {
  return rtcBaterijaOk;
}

bool fallbackImaPouzdanuReferencu() {
  return fallbackAktivan && fallbackVrijeme.unixtime() > 0;
}

DateTime getZadnjeSinkroniziranoVrijeme() {
  return zadnjaSinkronizacija;
}

void oznaciPovratakNaRTC() {
  // Ako je bila NTP/DCF, vrati se na RTC
  if (trenutniIzvor != IZ_RTC) {
    trenutniIzvor = IZ_RTC;
    azurirajOznakuIzvora();
    posaljiPCLog(F("Povratak na RTC nakon gubitka NTP/DCF sinkronizacije"));
  }
}

bool jeSinkronizacijaZastarjela() {
  if (zadnjaSinkronizacijaMs == 0) return true;
  unsigned long sadaMs = millis();
  // Zastarjela ako je prošlo više od 1 sata
  return (sadaMs - zadnjaSinkronizacijaMs) > TIMEOUT_SINKRONIZACIJE_MS;
}
