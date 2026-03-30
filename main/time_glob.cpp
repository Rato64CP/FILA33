// time_glob.cpp – Globalno rukovanje vremenom
#include <Arduino.h>
#include <RTClib.h>
#include "time_glob.h"
#include "i2c_eeprom.h"
#include "eeprom_konstante.h"
#include "podesavanja_piny.h"
#include "wear_leveling.h"
#include "pc_serial.h"
#include "lcd_display.h"
#include "kazaljke_sata.h"
#include "okretna_ploca.h"

// DS3231 RTC modul
static RTC_DS3231 rtc;

// Trenutno vrijeme (cache)
static DateTime trenutnoVrijeme;

// Izvor vremena
static enum {
  IZ_RTC,
  IZ_MAN,
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
static bool rtcSqwAktivan = false;
static bool rtcSqwGreskaPrijavljena = false;
static unsigned long rtcSqwZadnjiTickMs = 0;
static bool dstAktivan = false;
static bool dstStatusUcitan = false;
static bool vrijemePotvrdjenoZaAutomatiku = false;

// Fallback referencija (koristi zadnje poznato vrijeme ako RTC/NTP/DCF nisu dostupni)
static bool fallbackAktivan = false;
static DateTime fallbackVrijeme((uint32_t)0);
volatile uint32_t rtcSekundniBrojac = 0;

void rtcSqwPrekid() {
  rtcSekundniBrojac++;
}

// -------------------- POMOĆNE FUNKCIJE --------------------

static void azurirajOznakuIzvora() {
  switch (trenutniIzvor) {
    case IZ_RTC:
      strncpy(oznakaizvora, "RTC", sizeof(oznakaizvora) - 1);
      break;
    case IZ_MAN:
      strncpy(oznakaizvora, "MAN", sizeof(oznakaizvora) - 1);
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

static uint16_t izracunajDSTChecksum(const EepromLayout::DSTStatus& stanje) {
  uint16_t checksum = 0;
  checksum += stanje.potpis;
  checksum += stanje.dstAktivan;
  checksum += stanje.reserved;
  return checksum;
}

static bool jeDSTStatusValjan(const EepromLayout::DSTStatus& stanje) {
  return stanje.potpis == EepromLayout::DST_STATUS_POTPIS &&
         stanje.checksum == izracunajDSTChecksum(stanje) &&
         stanje.dstAktivan <= 1;
}

static uint8_t zadnjaNedjeljaUMjesecu(int godina, uint8_t mjesec) {
  DateTime zadnjiDan(godina, mjesec + 1, 1, 0, 0, 0);
  zadnjiDan = DateTime(zadnjiDan.unixtime() - 86400UL);
  while (zadnjiDan.dayOfTheWeek() != 0) {
    zadnjiDan = DateTime(zadnjiDan.unixtime() - 86400UL);
  }
  return zadnjiDan.day();
}

static bool izracunajDSTIzKalendara(const DateTime& vrijeme) {
  const uint8_t mjesec = vrijeme.month();
  if (mjesec < 3 || mjesec > 10) return false;
  if (mjesec > 3 && mjesec < 10) return true;

  const uint8_t zadnjaNedjelja = zadnjaNedjeljaUMjesecu(vrijeme.year(), mjesec);
  if (mjesec == 3) {
    if (vrijeme.day() < zadnjaNedjelja) return false;
    if (vrijeme.day() > zadnjaNedjelja) return true;
    return vrijeme.hour() >= 3;
  }

  if (vrijeme.day() < zadnjaNedjelja) return true;
  if (vrijeme.day() > zadnjaNedjelja) return false;
  return vrijeme.hour() < 3;
}

static void spremiDSTStatus() {
  EepromLayout::DSTStatus stanje{};
  stanje.potpis = EepromLayout::DST_STATUS_POTPIS;
  stanje.dstAktivan = dstAktivan ? 1 : 0;
  stanje.reserved = 0;
  stanje.checksum = izracunajDSTChecksum(stanje);
  WearLeveling::spremi(EepromLayout::BAZA_DST_STATUS,
                       EepromLayout::SLOTOVI_DST_STATUS,
                       stanje);
  dstStatusUcitan = true;
}

static void ucitajDSTStatus() {
  EepromLayout::DSTStatus stanje{};
  if (WearLeveling::ucitaj(EepromLayout::BAZA_DST_STATUS,
                           EepromLayout::SLOTOVI_DST_STATUS,
                           stanje) &&
      jeDSTStatusValjan(stanje)) {
    dstAktivan = stanje.dstAktivan != 0;
    dstStatusUcitan = true;
    return;
  }

  dstAktivan = izracunajDSTIzKalendara(trenutnoVrijeme);
  dstStatusUcitan = true;
  spremiDSTStatus();
}

static bool trebaProljetniPomak(const DateTime& vrijeme) {
  return !dstAktivan &&
         vrijeme.month() == 3 &&
         vrijeme.day() == zadnjaNedjeljaUMjesecu(vrijeme.year(), 3) &&
         vrijeme.hour() == 2;
}

static bool trebaJesenskiPomak(const DateTime& vrijeme) {
  return dstAktivan &&
         vrijeme.month() == 10 &&
         vrijeme.day() == zadnjaNedjeljaUMjesecu(vrijeme.year(), 10) &&
         vrijeme.hour() == 3;
}

static void primijeniDSTPomak(int pomakSekundi, bool noviDSTAktivan, const __FlashStringHelper* opis) {
  DateTime novoVrijeme(trenutnoVrijeme.unixtime() + pomakSekundi);
  if (rtc.begin()) {
    rtc.adjust(novoVrijeme);
    rtcBaterijaOk = true;
  } else {
    signalizirajError_RTC();
  }

  trenutnoVrijeme = novoVrijeme;
  fallbackVrijeme = novoVrijeme;
  fallbackAktivan = true;
  dstAktivan = noviDSTAktivan;
  spremiDSTStatus();

  posaljiPCLog(opis);
  obavijestiKazaljkeDSTPromjena(pomakSekundi / 60);
  zatraziPoravnanjeTaktaKazaljki();
  zatraziPoravnanjeTaktaPloce();
}

static void obradiAutomatskiDST() {
  if (!dstStatusUcitan) {
    ucitajDSTStatus();
  }

  if (trebaProljetniPomak(trenutnoVrijeme)) {
    primijeniDSTPomak(3600, true, F("DST: automatski prijelaz na CEST (+60 min)"));
    return;
  }

  if (trebaJesenskiPomak(trenutnoVrijeme)) {
    primijeniDSTPomak(-3600, false, F("DST: automatski prijelaz na CET (-60 min)"));
  }
}

static void ucitajZadnjuSinkronizaciju() {
  EepromLayout::ZadnjaSinkronizacija zs;
  if (WearLeveling::ucitaj(EepromLayout::BAZA_ZADNJA_SINKRONIZACIJA,
                          EepromLayout::SLOTOVI_ZADNJA_SINKRONIZACIJA,
                          zs)) {
    zadnjaSinkronizacija = DateTime(zs.timestamp);
    if (zs.izvor == IZ_MAN) {
      trenutniIzvor = IZ_MAN;
    } else if (zs.izvor == IZ_NTP) {
      trenutniIzvor = IZ_NTP;
    } else if (zs.izvor == IZ_DCF) {
      trenutniIzvor = IZ_DCF;
    } else {
      trenutniIzvor = IZ_RTC;
    }
  } else {
    zadnjaSinkronizacija = DateTime((uint32_t)0);
    trenutniIzvor = IZ_RTC;
  }
  azurirajOznakuIzvora();
}

static void postaviVrijemePotvrdjenoZaAutomatiku(bool potvrdeno, const __FlashStringHelper* razlog) {
  if (vrijemePotvrdjenoZaAutomatiku == potvrdeno && razlog == nullptr) {
    return;
  }

  vrijemePotvrdjenoZaAutomatiku = potvrdeno;
  if (razlog != nullptr) {
    posaljiPCLog(razlog);
  }
}

// -------------------- JAVNE FUNKCIJE --------------------

void inicijalizirajRTC() {
  if (!rtc.begin()) {
    posaljiPCLog(F("RTC: DS3231 nije dostupan"));
    rtcBaterijaOk = false;
    postaviVrijemePotvrdjenoZaAutomatiku(
        false,
        F("Vrijeme: nije potvrdeno, automatika toranjskog sata ostaje blokirana"));
    signalizirajError_RTC();
  } else {
    // Provjera valjanosti RTC baterije
    if (rtc.lostPower()) {
      posaljiPCLog(F("RTC: baterija je prazna, vrijeme nije pouzdano"));
      rtcBaterijaOk = false;
      postaviVrijemePotvrdjenoZaAutomatiku(
          false,
          F("Vrijeme: cekam NTP/DCF ili rucnu potvrdu prije aktivacije automatike"));
      signalizirajUpozorenjeRtcBaterije();
      // Postavi minimalnu vrijednost
      rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    } else {
      rtcBaterijaOk = true;
      postaviVrijemePotvrdjenoZaAutomatiku(true, nullptr);
      posaljiPCLog(F("RTC: baterija OK, vrijeme je pouzdano"));
    }
    trenutnoVrijeme = rtc.now();
    rtc.writeSqwPinMode(DS3231_SquareWave1Hz);
  }
  pinMode(PIN_RTC_SQW, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_RTC_SQW), rtcSqwPrekid, FALLING);
  rtcSqwAktivan = false;
  rtcSqwGreskaPrijavljena = false;
  rtcSqwZadnjiTickMs = millis();
  ucitajDSTStatus();
  ucitajZadnjuSinkronizaciju();
  if (!vrijemePotvrdjenoZaAutomatiku) {
    trenutniIzvor = IZ_RTC;
    azurirajOznakuIzvora();
  }
}

DateTime dohvatiTrenutnoVrijeme() {
  static uint32_t zadnjiObradeniRtcTick = 0xFFFFFFFFUL;
  static unsigned long zadnjiFallbackPollMs = 0;
  uint32_t lokalniRtcTick = 0;
  const unsigned long sadaMs = millis();
  noInterrupts();
  lokalniRtcTick = rtcSekundniBrojac;
  interrupts();

    if (lokalniRtcTick != zadnjiObradeniRtcTick || zadnjiObradeniRtcTick == 0xFFFFFFFFUL) {
    zadnjiObradeniRtcTick = lokalniRtcTick;
    rtcSqwAktivan = true;
    rtcSqwZadnjiTickMs = sadaMs;
    if (rtcSqwGreskaPrijavljena) {
      posaljiPCLog(F("RTC SQW: impulsi na D2 ponovno prisutni"));
      rtcSqwGreskaPrijavljena = false;
    }
    if (rtc.begin()) {
      trenutnoVrijeme = rtc.now();
    } else {
      signalizirajError_RTC();
    }
  } else if ((sadaMs - rtcSqwZadnjiTickMs) > 2500UL) {
    rtcSqwAktivan = false;
    if (!rtcSqwGreskaPrijavljena) {
      posaljiPCLog(F("RTC SQW: nema 1 Hz impulsa na D2, koristim RTC fallback citanje"));
      rtcSqwGreskaPrijavljena = true;
    }
    if ((sadaMs - zadnjiFallbackPollMs) >= 1000UL || zadnjiFallbackPollMs == 0) {
      zadnjiFallbackPollMs = sadaMs;
      if (rtc.begin()) {
        trenutnoVrijeme = rtc.now();
      }
    }
  }

  if ((trenutniIzvor == IZ_NTP || trenutniIzvor == IZ_DCF) && jeSinkronizacijaZastarjela()) {
    oznaciPovratakNaRTC();
  }

  obradiAutomatskiDST();

  return trenutnoVrijeme;
}

uint32_t dohvatiRtcSekundniBrojac() {
  uint32_t lokalniRtcTick = 0;
  noInterrupts();
  lokalniRtcTick = rtcSekundniBrojac;
  interrupts();
  return lokalniRtcTick;
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
  dstAktivan = izracunajDSTIzKalendara(ntpVrijeme);
  spremiDSTStatus();
  zadnjaSinkronizacija = ntpVrijeme;
  trenutniIzvor = IZ_NTP;
  postaviVrijemePotvrdjenoZaAutomatiku(
      true,
      F("Vrijeme: potvrdeno iz NTP-a, automatika toranjskog sata je aktivna"));
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
  dstAktivan = izracunajDSTIzKalendara(dcfVrijeme);
  spremiDSTStatus();
  zadnjaSinkronizacija = dcfVrijeme;
  trenutniIzvor = IZ_DCF;
  postaviVrijemePotvrdjenoZaAutomatiku(
      true,
      F("Vrijeme: potvrdeno iz DCF-a, automatika toranjskog sata je aktivna"));
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

void azurirajVrijemeRucno(const DateTime& rucnoVrijeme) {
  if (rucnoVrijeme.unixtime() == 0 || rucnoVrijeme.year() < 2024) {
    posaljiPCLog(F("Rucno vrijeme: neispravna vrijednost, odbacujem"));
    return;
  }

  if (rtc.begin()) {
    rtc.adjust(rucnoVrijeme);
    rtcBaterijaOk = true;
  } else {
    signalizirajError_RTC();
  }

  trenutnoVrijeme = rucnoVrijeme;
  fallbackVrijeme = rucnoVrijeme;
  fallbackAktivan = true;
  dstAktivan = izracunajDSTIzKalendara(rucnoVrijeme);
  spremiDSTStatus();
  zadnjaSinkronizacija = rucnoVrijeme;
  trenutniIzvor = IZ_MAN;
  postaviVrijemePotvrdjenoZaAutomatiku(
      true,
      F("Vrijeme: rucno potvrdeno, automatika toranjskog sata je aktivna"));
  azurirajOznakuIzvora();
  spremiZadnjuSinkronizaciju();

  String log = F("Vrijeme rucno postavljeno: ");
  log += rucnoVrijeme.year();
  log += F("-");
  log += rucnoVrijeme.month();
  log += F("-");
  log += rucnoVrijeme.day();
  log += F(" ");
  log += rucnoVrijeme.hour();
  log += F(":");
  if (rucnoVrijeme.minute() < 10) log += F("0");
  log += rucnoVrijeme.minute();
  log += F(":");
  if (rucnoVrijeme.second() < 10) log += F("0");
  log += rucnoVrijeme.second();
  posaljiPCLog(log);
}

String dohvatiIzvorVremena() {
  return String(dohvatiOznakuIzvoraVremena());
}

const char* dohvatiOznakuIzvoraVremena() {
  return oznakaizvora;
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

bool jeRtcSqwAktivan() {
  return rtcSqwAktivan;
}

bool fallbackImaPouzdanuReferencu() {
  return fallbackAktivan && fallbackVrijeme.unixtime() > 0;
}

bool jeVrijemePotvrdjenoZaAutomatiku() {
  return vrijemePotvrdjenoZaAutomatiku;
}

DateTime getZadnjeSinkroniziranoVrijeme() {
  return zadnjaSinkronizacija;
}

void oznaciPovratakNaRTC() {
  // Ako je bila NTP ili DCF, vrati se na RTC.
  if (trenutniIzvor == IZ_NTP || trenutniIzvor == IZ_DCF) {
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

