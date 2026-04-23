// time_glob.cpp - Globalno rukovanje vremenom
#include <Arduino.h>
#include <avr/pgmspace.h>
#include <RTClib.h>
#include "time_glob.h"
#include "eeprom_konstante.h"
#include "podesavanja_piny.h"
#include "wear_leveling.h"
#include "pc_serial.h"
#include "kazaljke_sata.h"
#include "okretna_ploca.h"
#include "esp_serial.h"
#include "lcd_display.h"

// DS3231 RTC modul
static RTC_DS3231 rtc;

// Trenutno vrijeme (cache)
static DateTime trenutnoVrijeme;

// Izvor vremena toranjskog sata:
// - trenutniIzvor je ono sto sada stvarno prikazujemo i koristimo kao aktivni izvor rada
// - zadnjiPotvrdeniIzvor pamti odakle je dosla zadnja svjeza sinkronizacija
enum IzvorVremena {
  IZ_RTC,
  IZ_MAN,
  IZ_NTP
};

static IzvorVremena trenutniIzvor = IZ_RTC;
static IzvorVremena zadnjiPotvrdeniIzvor = IZ_RTC;

// RTC + NTP jezgra toranjskog sata namjerno je konzervativna.
// Pravila koja ne treba mijenjati bez stvarne potrebe:
// - nakon boota sat uvijek krece iz RTC-a, bez cekanja mreze
// - NTP samo potvrduje i korigira RTC, ne smije blokirati start sata
// - prikaz "NTP" smije se pojaviti tek nakon stvarno uspjesne nove sinkronizacije
// - ako vise od 24 sata nema nove sinkronizacije, prikaz i rad se vracaju na RTC
// - nakon povratka napajanja ESP/WiFi dobiva pocetnu odgodu prije prvog NTP pokusaja
// Svaka promjena ovdje mora cuvati taj tok rada.

// Zadnja sinkronizacija
static DateTime zadnjaSinkronizacija((uint32_t)0);
static unsigned long zadnjaSinkronizacijaMs = 0;
static const unsigned long TIMEOUT_SINKRONIZACIJE_MS = 86400000UL; // 24 sata

// Oznake
static char oznakaizvora[4] = "RTC";

// RTC pouzdanost
static bool rtcBaterijaOk = false;
static bool rtcSqwAktivan = false;
static bool rtcSqwGreskaPrijavljena = false;
static unsigned long rtcSqwZadnjiTickMs = 0;
static bool dstAktivan = false;
static bool dstStatusUcitan = false;
static bool vrijemePotvrdjenoZaAutomatiku = false;
static bool ntpSinkronizacijaZakazana = false;
static DateTime zakazanoNtpVrijeme((uint32_t)0);
static uint32_t zakazaniNtpRtcTick = 0;
static unsigned long zakazaniNtpMs = 0;
static bool zakazanoNtpImaEksplicitanDST = false;
static bool zakazanoNtpDstAktivan = false;
static bool sumnjivaSinkronizacijaNaCekanju = false;
static uint8_t sumnjiviIzvorSinkronizacije = IZ_RTC;
static DateTime sumnjivoVrijemeSinkronizacije((uint32_t)0);
static unsigned long sumnjivaSinkronizacijaMs = 0;

volatile uint32_t rtcSekundniBrojac = 0;

static const uint16_t MIN_VALJANA_GODINA = 2024;
static const uint16_t MAX_VALJANA_GODINA = 2099;
static const uint32_t MAX_SKOK_BEZ_DODATNE_POTVRDE_S = 300UL;
static const uint32_t MAX_RAZLIKA_IZMEDU_DVIJE_POTVRDE_S = 3UL;
static const unsigned long ROK_SUMNJIVE_SINKRONIZACIJE_MS = 15UL * 60UL * 1000UL;

void rtcSqwPrekid() {
  rtcSekundniBrojac++;
}

// -------------------- POMOCNE FUNKCIJE --------------------

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
    default:
      strncpy(oznakaizvora, "???", sizeof(oznakaizvora) - 1);
      break;
  }
  oznakaizvora[sizeof(oznakaizvora) - 1] = '\0';
}

static void primijeniVrijemeIzNTP(DateTime ntpVrijeme,
                                  bool imaEksplicitanDST,
                                  bool dstAktivanIzvori);

static bool jeVrijemeURasponuPouzdanosti(const DateTime& vrijeme) {
  return vrijeme.unixtime() != 0 &&
         vrijeme.year() >= MIN_VALJANA_GODINA &&
         vrijeme.year() <= MAX_VALJANA_GODINA;
}

static void ocistiSumnjivuSinkronizaciju() {
  sumnjivaSinkronizacijaNaCekanju = false;
  sumnjiviIzvorSinkronizacije = IZ_RTC;
  sumnjivoVrijemeSinkronizacije = DateTime((uint32_t)0);
  sumnjivaSinkronizacijaMs = 0;
}

static uint32_t apsolutnaRazlikaSekundi(const DateTime& a, const DateTime& b) {
  const uint32_t unixA = a.unixtime();
  const uint32_t unixB = b.unixtime();
  return (unixA >= unixB) ? (unixA - unixB) : (unixB - unixA);
}

static void kopirajFlashTekst(const __FlashStringHelper* tekst, char* odrediste, size_t velicina) {
  if (odrediste == nullptr || velicina == 0) {
    return;
  }

  if (tekst == nullptr) {
    odrediste[0] = '\0';
    return;
  }

  strncpy_P(odrediste, reinterpret_cast<const char*>(tekst), velicina - 1);
  odrediste[velicina - 1] = '\0';
}

static void formatirajDatumIVrijeme(const DateTime& vrijeme, char* odrediste, size_t velicina) {
  snprintf_P(odrediste, velicina,
             PSTR("%u-%02u-%02u %02u:%02u:%02u"),
             vrijeme.year(),
             vrijeme.month(),
             vrijeme.day(),
             vrijeme.hour(),
             vrijeme.minute(),
             vrijeme.second());
}

static RezultatProvjereSinkronizacije potvrdiIliOdbijSumnjivuSinkronizaciju(
    const DateTime& novoVrijeme,
    uint8_t izvor,
    const __FlashStringHelper* nazivIzvora) {
  if (!jeVrijemeURasponuPouzdanosti(novoVrijeme)) {
    char izvorTekst[8];
    char log[96];
    kopirajFlashTekst(nazivIzvora, izvorTekst, sizeof(izvorTekst));
    snprintf_P(log, sizeof(log), PSTR("%s: nevaljano vrijeme izvan dopustenog raspona, odbacujem"), izvorTekst);
    posaljiPCLog(log);
    ocistiSumnjivuSinkronizaciju();
    return SINKRONIZACIJA_ODBIJENA;
  }

  if (!vrijemePotvrdjenoZaAutomatiku || !jeVrijemeURasponuPouzdanosti(trenutnoVrijeme)) {
    ocistiSumnjivuSinkronizaciju();
    return SINKRONIZACIJA_PRIHVACENA;
  }

  const uint32_t razlikaSekundi = apsolutnaRazlikaSekundi(trenutnoVrijeme, novoVrijeme);
  if (razlikaSekundi <= MAX_SKOK_BEZ_DODATNE_POTVRDE_S) {
    ocistiSumnjivuSinkronizaciju();
    return SINKRONIZACIJA_PRIHVACENA;
  }

  const unsigned long sadaMs = millis();
  if (sumnjivaSinkronizacijaNaCekanju &&
      sumnjiviIzvorSinkronizacije == izvor &&
      jeVrijemeURasponuPouzdanosti(sumnjivoVrijemeSinkronizacije) &&
      (sadaMs - sumnjivaSinkronizacijaMs) <= ROK_SUMNJIVE_SINKRONIZACIJE_MS &&
      apsolutnaRazlikaSekundi(sumnjivoVrijemeSinkronizacije, novoVrijeme) <=
          MAX_RAZLIKA_IZMEDU_DVIJE_POTVRDE_S) {
    char izvorTekst[8];
    char log[80];
    kopirajFlashTekst(nazivIzvora, izvorTekst, sizeof(izvorTekst));
    snprintf_P(log, sizeof(log), PSTR("%s: potvrden sumnjiv skok vremena, prihvacam korekciju"), izvorTekst);
    posaljiPCLog(log);
    ocistiSumnjivuSinkronizaciju();
    return SINKRONIZACIJA_PRIHVACENA;
  }

  sumnjivaSinkronizacijaNaCekanju = true;
  sumnjiviIzvorSinkronizacije = izvor;
  sumnjivoVrijemeSinkronizacije = novoVrijeme;
  sumnjivaSinkronizacijaMs = sadaMs;

  char izvorTekst[8];
  char log[96];
  kopirajFlashTekst(nazivIzvora, izvorTekst, sizeof(izvorTekst));
  snprintf_P(log, sizeof(log),
             PSTR("%s: sumnjiv skok vremena od %lu s, cekam ponovljenu potvrdu prije upisa u RTC"),
             izvorTekst,
             razlikaSekundi);
  posaljiPCLog(log);

  if (izvor == IZ_NTP) {
    zatraziPrioritetnuNTPSinkronizaciju();
  }
  return SINKRONIZACIJA_CEKA_DODATNU_POTVRDU;
}

static void otkaziZakazanuNtpSinkronizaciju() {
  ntpSinkronizacijaZakazana = false;
  zakazanoNtpVrijeme = DateTime((uint32_t)0);
  zakazaniNtpRtcTick = 0;
  zakazaniNtpMs = 0;
  zakazanoNtpImaEksplicitanDST = false;
  zakazanoNtpDstAktivan = false;
}

static void spremiZadnjuSinkronizaciju() {
  EepromLayout::ZadnjaSinkronizacija zs;
  zs.izvor = static_cast<int>(zadnjiPotvrdeniIzvor);
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

static bool odrediDSTStatusSinkronizacije(const DateTime& vrijeme,
                                          bool imaEksplicitanDST,
                                          bool dstAktivanIzvori) {
  if (imaEksplicitanDST) {
    return dstAktivanIzvori;
  }

  return izracunajDSTIzKalendara(vrijeme);
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
    zadnjaSinkronizacijaMs = 0;
      if (zs.izvor == IZ_MAN) {
        zadnjiPotvrdeniIzvor = IZ_MAN;
      } else if (zs.izvor == IZ_NTP) {
        zadnjiPotvrdeniIzvor = IZ_NTP;
      } else {
        zadnjiPotvrdeniIzvor = IZ_RTC;
      }
  } else {
    zadnjaSinkronizacija = DateTime((uint32_t)0);
    zadnjaSinkronizacijaMs = 0;
    zadnjiPotvrdeniIzvor = IZ_RTC;
  }
  // Nakon boota sat realno radi iz RTC-a dok ne stigne nova potvrda izvora.
  trenutniIzvor = IZ_RTC;
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

DateTime dohvatiDatumUskrsaZaGodinu(int godina) {
  const int a = godina % 19;
  const int b = godina / 100;
  const int c = godina % 100;
  const int d = b / 4;
  const int e = b % 4;
  const int f = (b + 8) / 25;
  const int g = (b - f + 1) / 3;
  const int h = (19 * a + b - d - g + 15) % 30;
  const int i = c / 4;
  const int k = c % 4;
  const int l = (32 + (2 * e) + (2 * i) - h - k) % 7;
  const int m = (a + (11 * h) + (22 * l)) / 451;
  const int mjesec = (h + l - (7 * m) + 114) / 31;
  const int dan = ((h + l - (7 * m) + 114) % 31) + 1;
  return DateTime(godina, mjesec, dan, 0, 0, 0);
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
            F("Vrijeme: cekam NTP ili rucnu potvrdu prije aktivacije automatike"));
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
  posaljiPCLog(F("RTC: pocinjem ucitavanje DST statusa"));
  ucitajDSTStatus();
  posaljiPCLog(F("RTC: DST status ucitan"));
  posaljiPCLog(F("RTC: pocinjem ucitavanje zadnje sinkronizacije"));
  ucitajZadnjuSinkronizaciju();
  posaljiPCLog(F("RTC: zadnja sinkronizacija ucitana"));
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
    bool ntpPrimijenjenNaOvomTicku = false;
    if (ntpSinkronizacijaZakazana) {
      uint32_t protekliRtcTickovi = 1;
      if (zakazaniNtpRtcTick != 0 && lokalniRtcTick >= zakazaniNtpRtcTick) {
        protekliRtcTickovi = lokalniRtcTick - zakazaniNtpRtcTick;
        if (protekliRtcTickovi == 0) {
          protekliRtcTickovi = 1;
        }
      } else if (zakazaniNtpMs != 0) {
        protekliRtcTickovi = (sadaMs - zakazaniNtpMs) / 1000UL;
        if (protekliRtcTickovi == 0) {
          protekliRtcTickovi = 1;
        }
      }

      const DateTime vrijemeZaPrimjenu(
          zakazanoNtpVrijeme.unixtime() + protekliRtcTickovi);
      primijeniVrijemeIzNTP(vrijemeZaPrimjenu,
                            zakazanoNtpImaEksplicitanDST,
                            zakazanoNtpDstAktivan);
      ntpPrimijenjenNaOvomTicku = true;
      posaljiPCLog(F("NTP: sinkronizacija primijenjena na RTC SQW granici sekunde"));
    }
    if (!ntpPrimijenjenNaOvomTicku && rtc.begin()) {
      trenutnoVrijeme = rtc.now();
    } else if (!ntpPrimijenjenNaOvomTicku) {
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

  if (zadnjiPotvrdeniIzvor == IZ_NTP && jeSinkronizacijaZastarjela()) {
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

static void primijeniVrijemeIzNTP(DateTime ntpVrijeme,
                                  bool imaEksplicitanDST,
                                  bool dstAktivanIzvori) {
  if (!jeVrijemeURasponuPouzdanosti(ntpVrijeme)) {
    posaljiPCLog(F("NTP: odbijena interna primjena nevaljanog vremena"));
    otkaziZakazanuNtpSinkronizaciju();
    return;
  }

  otkaziZakazanuNtpSinkronizaciju();

  if (rtc.begin()) {
    rtc.adjust(ntpVrijeme);
    rtcBaterijaOk = true;
  } else {
    signalizirajError_RTC();
  }

  trenutnoVrijeme = ntpVrijeme;
  dstAktivan = odrediDSTStatusSinkronizacije(
      ntpVrijeme,
      imaEksplicitanDST,
      dstAktivanIzvori);
  spremiDSTStatus();
  zadnjaSinkronizacija = ntpVrijeme;
  trenutniIzvor = IZ_NTP;
  zadnjiPotvrdeniIzvor = IZ_NTP;
  ocistiSumnjivuSinkronizaciju();
  postaviVrijemePotvrdjenoZaAutomatiku(
      true,
      F("Vrijeme: potvrdeno iz NTP-a, automatika toranjskog sata je aktivna"));
  azurirajOznakuIzvora();
  spremiZadnjuSinkronizaciju();

  char vrijemeTekst[24];
  char log[64];
  formatirajDatumIVrijeme(ntpVrijeme, vrijemeTekst, sizeof(vrijemeTekst));
  snprintf_P(log, sizeof(log), PSTR("Vrijeme azurirano iz NTP: %s"), vrijemeTekst);
  posaljiPCLog(log);
}

void azurirajVrijemeIzNTP(const DateTime& ntpVrijeme,
                          bool imaEksplicitanDST,
                          bool dstAktivanIzvori) {
  // Provjera validnosti NTP vremena
  if (!jeVrijemeURasponuPouzdanosti(ntpVrijeme)) {
    posaljiPCLog(F("NTP: neispravno vrijeme, odbacujem"));
    return;
  }

  if (potvrdiIliOdbijSumnjivuSinkronizaciju(ntpVrijeme, IZ_NTP, F("NTP")) !=
      SINKRONIZACIJA_PRIHVACENA) {
    return;
  }

  // Za precizniji trenutak sinkronizacije NTP se poravnava na iduci RTC SQW tik.
  if (rtcSqwAktivan) {
    zakazanoNtpVrijeme = ntpVrijeme;
    ntpSinkronizacijaZakazana = true;
    zakazaniNtpRtcTick = dohvatiRtcSekundniBrojac();
    zakazaniNtpMs = millis();
    zakazanoNtpImaEksplicitanDST = imaEksplicitanDST;
    zakazanoNtpDstAktivan = dstAktivanIzvori;

    char vrijemeTekst[24];
    char log[96];
    formatirajDatumIVrijeme(ntpVrijeme, vrijemeTekst, sizeof(vrijemeTekst));
    snprintf_P(log, sizeof(log),
               PSTR("NTP: sinkronizacija zakazana na sljedeci RTC SQW tik od %s"),
               vrijemeTekst);
    posaljiPCLog(log);
    return;
  }

  // Ako RTC SQW trenutno nije dostupan, primijeni odmah kao fallback.
  primijeniVrijemeIzNTP(ntpVrijeme, imaEksplicitanDST, dstAktivanIzvori);
  return;
}

void azurirajVrijemeRucno(const DateTime& rucnoVrijeme) {
  otkaziZakazanuNtpSinkronizaciju();

  if (!jeVrijemeURasponuPouzdanosti(rucnoVrijeme)) {
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
  dstAktivan = izracunajDSTIzKalendara(rucnoVrijeme);
  spremiDSTStatus();
  zadnjaSinkronizacija = rucnoVrijeme;
  trenutniIzvor = IZ_MAN;
  zadnjiPotvrdeniIzvor = IZ_MAN;
  ocistiSumnjivuSinkronizaciju();
  postaviVrijemePotvrdjenoZaAutomatiku(
      true,
      F("Vrijeme: rucno potvrdeno, automatika toranjskog sata je aktivna"));
  azurirajOznakuIzvora();
  spremiZadnjuSinkronizaciju();

  char vrijemeTekst[24];
  char log[72];
  formatirajDatumIVrijeme(rucnoVrijeme, vrijemeTekst, sizeof(vrijemeTekst));
  snprintf_P(log, sizeof(log), PSTR("Vrijeme rucno postavljeno: %s"), vrijemeTekst);
  posaljiPCLog(log);
  zatraziPoravnanjeTaktaKazaljki();
  zatraziPoravnanjeTaktaPloce();
  zatraziPrioritetnuNTPSinkronizaciju();
}

const char* dohvatiOznakuIzvoraVremena() {
  return oznakaizvora;
}

bool jeZadnjaSvjezaSinkronizacijaIzNTP() {
  return zadnjiPotvrdeniIzvor == IZ_NTP && !jeSinkronizacijaZastarjela();
}

bool jeRtcSqwAktivan() {
  return rtcSqwAktivan;
}

bool jeVrijemePotvrdjenoZaAutomatiku() {
  return vrijemePotvrdjenoZaAutomatiku;
}

bool jeUskrsnaTisinaAktivna(const DateTime& vrijeme) {
  if (vrijeme.unixtime() == 0) {
    return false;
  }

  const DateTime uskrs = dohvatiDatumUskrsaZaGodinu(vrijeme.year());
  const DateTime pocetakTisine(uskrs.unixtime() - (3UL * 86400UL) + (19UL * 3600UL));
  const DateTime krajTisine(uskrs.unixtime() - 86400UL + (22UL * 3600UL));
  const uint32_t sada = vrijeme.unixtime();

  return sada >= pocetakTisine.unixtime() && sada < krajTisine.unixtime();
}

int dohvatiUTCOffsetMinuteZaLokalnoVrijeme(const DateTime& vrijeme) {
  return izracunajDSTIzKalendara(vrijeme) ? 120 : 60;
}

void oznaciPovratakNaRTC() {
  // Ako je sat radio iz potvrdenog NTP izvora, vrati prikaz i rad na RTC.
  if (trenutniIzvor != IZ_RTC) {
    trenutniIzvor = IZ_RTC;
    azurirajOznakuIzvora();
    posaljiPCLog(F("Povratak na RTC nakon vise od 24 sata bez nove NTP sinkronizacije"));
  }
}

void resetirajIzvorSinkronizacijeNaRTC() {
  zadnjaSinkronizacija = DateTime((uint32_t)0);
  zadnjaSinkronizacijaMs = 0;
  trenutniIzvor = IZ_RTC;
  zadnjiPotvrdeniIzvor = IZ_RTC;
  ocistiSumnjivuSinkronizaciju();
  azurirajOznakuIzvora();
  posaljiPCLog(F("Vrijeme: izvor sinkronizacije vracen na RTC"));
}

bool jeSinkronizacijaZastarjela() {
  if (!jeVrijemeURasponuPouzdanosti(zadnjaSinkronizacija)) {
    return true;
  }

  if (zadnjaSinkronizacijaMs == 0) {
    if (!jeVrijemeURasponuPouzdanosti(trenutnoVrijeme)) {
      return true;
    }

    uint32_t starostSekunde = 0;
    if (trenutnoVrijeme.unixtime() >= zadnjaSinkronizacija.unixtime()) {
      starostSekunde = trenutnoVrijeme.unixtime() - zadnjaSinkronizacija.unixtime();
    }

    return starostSekunde > (TIMEOUT_SINKRONIZACIJE_MS / 1000UL);
  }

  unsigned long sadaMs = millis();
  // Zastarjela ako je proslo vise od 24 sata
  return (sadaMs - zadnjaSinkronizacijaMs) > TIMEOUT_SINKRONIZACIJE_MS;
}

