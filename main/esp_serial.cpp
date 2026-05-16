// esp_serial.cpp - Serijska veza prema mreznom mostu toranjskog sata
#include <Arduino.h>
#include <avr/pgmspace.h>
#include <RTClib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_serial.h"
#include "time_glob.h"
#include "zvonjenje.h"
#include "podesavanja_piny.h"
#include "otkucavanje.h"
#include "slavljenje_mrtvacko.h"
#include "pc_serial.h"
#include "prekidac_tisine.h"
#include "kazaljke_sata.h"
#include "okretna_ploca.h"
#include "postavke.h"
#include "lcd_display.h"
#include "sunceva_automatika.h"
#include "watchdog.h"

// UART prema mreznom mostu toranjskog sata bira se kroz main/podesavanja_piny.h.
static HardwareSerial& espSerijskiPort = ESP_SERIJSKI_PORT;

static const unsigned long ESP_BRZINA = 9600;
// Nakon razdvajanja `MISE`, `BLAGDANI_NEP` i `BLAGDANI_POM` najveci serijski
// paketi vise ne trebaju 512 B. Time vracamo dio SRAM-a na `Megi`, a i dalje
// ostavljamo dovoljnu rezervu za najduzi pojedinacni web paket.
static const uint16_t ESP_ULAZNI_BUFFER_MAX = 256;
static const unsigned long WIFI_POCETNA_ODGODA_NAKON_NAPAJANJA_MS = 120000UL;
static const unsigned long WIFI_STATUS_DRUGI_UPIT_ODGODA_MS = 15000UL;
static const unsigned long WIFI_STATUS_RECOVERY_INTERVAL_MS = 300000UL;
static const uint8_t NTP_SIGURNA_SEKUNDA_MIN = 12;
static const uint8_t NTP_SIGURNA_SEKUNDA_MAX = 50;
static const uint16_t MIN_NTP_GODINA = 2000;
static const uint16_t MAX_NTP_GODINA = 2099;
static const uint32_t NTP_KLJUC_NEPOSTAVLJEN = 0xFFFFFFFFUL;

// RTC + NTP tok toranjskog sata mora ostati uskladen s time_glob.cpp:
// - boot krece iz RTC-a
// - NTP ide tek kad je mreza spremna i mehanika miruje
// - nakon povratka napajanja modem/WiFi dobivaju pocetnu odgodu
// - automatski NTP ne smije remetiti osnovni rad sata ni prikaz izvora vremena

static char ulazniBuffer[ESP_ULAZNI_BUFFER_MAX + 1];
// Duljina i dalje mora biti siri tip od `uint8_t` jer pojedini paketi i dalje
// mogu prijeci 255 znakova, iako je stalni ulazni buffer sada manji nego prije.
static uint16_t ulazniBufferDuljina = 0;
static bool ntpCekanjePrijavljeno = false;
static bool wifiPovezanNaESP = false;
static char zadnjaLokalnaWiFiIP[16] = "";
static char zadnjaWiFiMACAdresa[18] = "";
static bool wifiPocetnaOdgodaAktivna = false;
static unsigned long wifiPocetnaOdgodaDoMs = 0;
static unsigned long vrijemePrvogWiFiStatusUpitaMs = 0;
static unsigned long zadnjiWiFiStatusRecoveryUpitMs = 0;
static bool drugiWiFiStatusUpitPoslan = false;
static bool prioritetniNtpZahtjevNaCekanju = false;
static uint32_t zadnjiAutomatskiNtpZahtjevMinutniKljuc = NTP_KLJUC_NEPOSTAVLJEN;
static uint32_t zadnjiAutomatskiNtpZahtjevSatniKljuc = NTP_KLJUC_NEPOSTAVLJEN;

static bool vrijemeProslo(unsigned long sadaMs, unsigned long ciljMs) {
  return static_cast<long>(sadaMs - ciljMs) >= 0;
}

static bool jeResetNakonGubitkaNapajanja() {
  const uint8_t mcusr = dohvatiResetFlags();
  const bool imaBrownOutIliPowerOn = (mcusr & ((1 << BORF) | (1 << PORF))) != 0;
  const bool imaVanjskiReset = (mcusr & (1 << EXTRF)) != 0;
  return imaBrownOutIliPowerOn && !imaVanjskiReset;
}

static bool jeAktivnaPocetnaOdgodaWiFi() {
  if (!wifiPocetnaOdgodaAktivna) {
    return false;
  }

  if (!vrijemeProslo(millis(), wifiPocetnaOdgodaDoMs)) {
    return true;
  }

  wifiPocetnaOdgodaAktivna = false;
  wifiPocetnaOdgodaDoMs = 0;
  vrijemePrvogWiFiStatusUpitaMs = 0;
  zadnjiWiFiStatusRecoveryUpitMs = 0;
  drugiWiFiStatusUpitPoslan = false;
  posaljiPCLog(F("WiFi/NTP: istekla pocetna odgoda nakon povratka napajanja, krecem s provjerom mreze"));
  return false;
}

static bool jeValjanaIPv4AdresaZaLCD(const char* tekst) {
  if (tekst == nullptr || tekst[0] == '\0') {
    return false;
  }

  uint8_t brojSegmenata = 0;
  const char* pokazivac = tekst;

  while (*pokazivac != '\0') {
    if (brojSegmenata >= 4 || !isDigit(*pokazivac)) {
      return false;
    }

    int segment = 0;
    uint8_t brojZnamenki = 0;
    while (isDigit(*pokazivac)) {
      segment = segment * 10 + (*pokazivac - '0');
      if (segment > 255) {
        return false;
      }
      ++pokazivac;
      ++brojZnamenki;
      if (brojZnamenki > 3) {
        return false;
      }
    }

    if (brojZnamenki == 0) {
      return false;
    }

    ++brojSegmenata;
    if (*pokazivac == '.') {
      ++pokazivac;
      if (*pokazivac == '\0') {
        return false;
      }
    } else if (*pokazivac != '\0') {
      return false;
    }
  }

  return brojSegmenata == 4;
}

static void potvrdiWiFiPovezanostAkoTreba(const __FlashStringHelper* razlog) {
  if (wifiPovezanNaESP) {
    return;
  }

  wifiPovezanNaESP = true;
  postaviWiFiStatus(true);
  prioritetniNtpZahtjevNaCekanju = true;
  zadnjiAutomatskiNtpZahtjevMinutniKljuc = NTP_KLJUC_NEPOSTAVLJEN;
  zadnjiAutomatskiNtpZahtjevSatniKljuc = NTP_KLJUC_NEPOSTAVLJEN;
  posaljiPCLog(razlog);
}

void posaljiESPKomandu(const char* komanda);
static void zatraziWiFiStatusESP();
static bool spremiSetupWiFiPostavkeIzESPa(const char* payload);
static void posaljiSustavskePostavkeESPu();
static bool spremiSustavskePostavkeIzESPa(char* payload);
static void posaljiPostavkeStapicaESPu();
static bool spremiPostavkeStapicaIzESPa(char* payload);
static void posaljiBATPostavkeESPu();
static bool spremiBATPostavkeIzESPa(char* payload);
static void posaljiSuncevePostavkeESPu();
static bool spremiSuncevePostavkeIzESPa(char* payload);
static void posaljiMisePostavkeESPu();
static bool spremiMisePostavkeIzESPa(char* payload);
static void posaljiNepomicneBlagdaneESPu();
static bool spremiNepomicneBlagdaneIzESPa(char* payload);
static void posaljiPomicneBlagdaneESPu();
static bool spremiPomicneBlagdaneIzESPa(char* payload);

static void posaljiStatusESPU() {
  const DateTime sada = dohvatiTrenutnoVrijeme();
  char vrijemeIso[21];
  snprintf_P(vrijemeIso,
             sizeof(vrijemeIso),
             PSTR("%04d-%02d-%02dT%02d:%02d:%02d"),
             sada.year(),
             sada.month(),
             sada.day(),
             sada.hour(),
             sada.minute(),
             sada.second());

  char statusLinija[176];
  snprintf_P(statusLinija,
             sizeof(statusLinija),
             PSTR("STATUS:time=%s|src=%s|ok=%d|wifi=%d|mq=%d|mqen=%d|ntp=%d|hs=%d|hp=%d|ps=%d|pp=%d|sl=%d|mr=%d|ot=%d|b1=%d|b2=%d|sj=%d|sp=%d|sv=%d|tm=%d"),
             vrijemeIso,
             dohvatiOznakuIzvoraVremena(),
             jeVrijemePotvrdjenoZaAutomatiku() ? 1 : 0,
             jeWiFiPovezanNaESP() ? 1 : 0,
             0,
             0,
             jeNTPOmogucen() ? 1 : 0,
             suKazaljkeUSinkronu() ? 1 : 0,
             dohvatiMemoriraneKazaljkeMinuta(),
             jePlocaUSinkronu() ? 1 : 0,
             dohvatiPozicijuPloce(),
             jeSlavljenjeUTijeku() ? 1 : 0,
             jeMrtvackoUTijeku() ? 1 : 0,
             jeOtkucavanjeUTijeku() ? 1 : 0,
             jeZvonoAktivno(1) ? 1 : 0,
             jeZvonoAktivno(2) ? 1 : 0,
             jeSuncevDogadajOmogucen(SUNCEVI_DOGADAJ_JUTRO) ? 1 : 0,
             jeSuncevDogadajOmogucen(SUNCEVI_DOGADAJ_PODNE) ? 1 : 0,
             jeSuncevDogadajOmogucen(SUNCEVI_DOGADAJ_VECER) ? 1 : 0,
             jePrekidacTisineAktivan() ? 1 : 0);
  espSerijskiPort.println(statusLinija);
}

static void posaljiSustavskePostavkeESPu() {
  char linija[112];
  snprintf_P(linija,
             sizeof(linija),
             PSTR("SET:SUSTAV|lcd=%d|log=%d|rs=%d|ups=%d|koc=%d|inr1=%u|inr2=%u|imp=%u"),
             jeLCDPozadinskoOsvjetljenjeUkljuceno() ? 1 : 0,
             jePCLogiranjeOmoguceno() ? 1 : 0,
             jeRS485Omogucen() ? 1 : 0,
             jeUPSModOmogucen() ? 1 : 0,
             jeKocnicaZvonaOmogucena() ? 1 : 0,
             static_cast<unsigned>(dohvatiInercijuZvona1Sekunde()),
             static_cast<unsigned>(dohvatiInercijuZvona2Sekunde()),
             static_cast<unsigned>(dohvatiTrajanjeImpulsaCekica()));
  espSerijskiPort.println(linija);
}

static void posaljiPostavkeStapicaESPu() {
  char linija[64];
  snprintf_P(linija,
             sizeof(linija),
             PSTR("SET:STAPICI|tr=%u|tn=%u|ts=%u|odg=%u"),
             static_cast<unsigned>(dohvatiTrajanjeZvonjenjaRadniMin()),
             static_cast<unsigned>(dohvatiTrajanjeZvonjenjaNedjeljaMin()),
             static_cast<unsigned>(dohvatiTrajanjeSlavljenjaMin()),
             static_cast<unsigned>(dohvatiOdgoduSlavljenjaSekunde()));
  espSerijskiPort.println(linija);
}

static void posaljiBATPostavkeESPu() {
  char linija[72];
  snprintf_P(linija,
             sizeof(linija),
             PSTR("SET:BAT|od=%d|do=%d|otk=%u|sl=%u|mr=%u"),
             dohvatiBATPeriodOdSata(),
             dohvatiBATPeriodDoSata(),
             static_cast<unsigned>(dohvatiModOtkucavanja()),
             static_cast<unsigned>(dohvatiModSlavljenja()),
             static_cast<unsigned>(dohvatiModMrtvackog()));
  espSerijskiPort.println(linija);
}

static void posaljiSuncevePostavkeESPu() {
  char linija[128];
  snprintf_P(linija,
             sizeof(linija),
             PSTR("SET:SUNCE|ju=%d|jb=%u|jo=%d|pu=%d|pb=%u|vu=%d|vb=%u|vo=%d|nr=%d"),
             jeSuncevDogadajOmogucen(SUNCEVI_DOGADAJ_JUTRO) ? 1 : 0,
             static_cast<unsigned>(dohvatiZvonoZaSuncevDogadaj(SUNCEVI_DOGADAJ_JUTRO)),
             dohvatiOdgoduSuncevogDogadajaMin(SUNCEVI_DOGADAJ_JUTRO),
             jeSuncevDogadajOmogucen(SUNCEVI_DOGADAJ_PODNE) ? 1 : 0,
             static_cast<unsigned>(dohvatiZvonoZaSuncevDogadaj(SUNCEVI_DOGADAJ_PODNE)),
             jeSuncevDogadajOmogucen(SUNCEVI_DOGADAJ_VECER) ? 1 : 0,
             static_cast<unsigned>(dohvatiZvonoZaSuncevDogadaj(SUNCEVI_DOGADAJ_VECER)),
             dohvatiOdgoduSuncevogDogadajaMin(SUNCEVI_DOGADAJ_VECER),
             jeNocnaRasvjetaOmogucena() ? 1 : 0);
  espSerijskiPort.println(linija);
}

static void posaljiMisePostavkeESPu() {
  RedoviteMisePostavke redoviteMise;
  dohvatiRedoviteMise(redoviteMise);

  char linija[80];
  snprintf_P(linija,
             sizeof(linija),
             PSTR("SET:MISE|rd=%u,%u,%u|nd=%u,%u,%u"),
             redoviteMise.dnevnaOmogucena ? 1U : 0U,
             static_cast<unsigned>(redoviteMise.dnevnaSatMise),
             static_cast<unsigned>(redoviteMise.dnevnaMinutaMise),
             redoviteMise.nedjeljnaOmogucena ? 1U : 0U,
             static_cast<unsigned>(redoviteMise.nedjeljnaSatMise),
             static_cast<unsigned>(redoviteMise.nedjeljnaMinutaMise));
  espSerijskiPort.println(linija);
}

static void posaljiNepomicneBlagdaneESPu() {
  char linija[320];
  int duljina = snprintf_P(linija, sizeof(linija), PSTR("SET:BLAGDANI_NEP"));
  if (duljina < 0) {
    return;
  }

  for (uint8_t i = 0; i < BROJ_NEPOMICNIH_BLAGDANA && duljina < static_cast<int>(sizeof(linija)); ++i) {
    NepomicniBlagdanPostavka blagdan;
    dohvatiNepomicniBlagdan(i, blagdan);
    duljina += snprintf_P(linija + duljina,
                          sizeof(linija) - static_cast<size_t>(duljina),
                          PSTR("|f%u=%u,%u,%u"),
                          static_cast<unsigned>(i),
                          blagdan.omogucen ? 1U : 0U,
                          static_cast<unsigned>(blagdan.satMise),
                          static_cast<unsigned>(blagdan.minutaMise));
  }

  espSerijskiPort.println(linija);
}

static void posaljiPomicneBlagdaneESPu() {
  char linija[192];
  int duljina = snprintf_P(linija, sizeof(linija), PSTR("SET:BLAGDANI_POM"));
  if (duljina < 0) {
    return;
  }

  for (uint8_t i = 0; i < BROJ_POMICNIH_BLAGDANA && duljina < static_cast<int>(sizeof(linija)); ++i) {
    PomicniBlagdanPostavka blagdan;
    dohvatiPomicniBlagdan(i, blagdan);
    duljina += snprintf_P(linija + duljina,
                          sizeof(linija) - static_cast<size_t>(duljina),
                          PSTR("|p%u=%u,%u,%u"),
                          static_cast<unsigned>(i),
                          blagdan.omogucen ? 1U : 0U,
                          static_cast<unsigned>(blagdan.satMise),
                          static_cast<unsigned>(blagdan.minutaMise));
  }

  espSerijskiPort.println(linija);
}

static void resetirajUlazniBuffer() {
  ulazniBuffer[0] = '\0';
  ulazniBufferDuljina = 0;
}

static void trimBuffer() {
  while (ulazniBufferDuljina > 0 &&
         (ulazniBuffer[ulazniBufferDuljina - 1] == ' ' ||
          ulazniBuffer[ulazniBufferDuljina - 1] == '\t')) {
    ulazniBuffer[--ulazniBufferDuljina] = '\0';
  }

  size_t pocetak = 0;
  while (pocetak < ulazniBufferDuljina &&
         (ulazniBuffer[pocetak] == ' ' || ulazniBuffer[pocetak] == '\t')) {
    ++pocetak;
  }

  if (pocetak > 0) {
    memmove(ulazniBuffer, ulazniBuffer + pocetak, ulazniBufferDuljina - pocetak + 1);
    ulazniBufferDuljina -= pocetak;
  }
}

static void trimJednolinijskiTekstESP(char* tekst) {
  if (tekst == nullptr) {
    return;
  }

  size_t duljina = strlen(tekst);
  while (duljina > 0 &&
         (tekst[duljina - 1] == ' ' || tekst[duljina - 1] == '\t' ||
          tekst[duljina - 1] == '\r' || tekst[duljina - 1] == '\n')) {
    tekst[--duljina] = '\0';
  }

  size_t pocetak = 0;
  while (tekst[pocetak] == ' ' || tekst[pocetak] == '\t') {
    ++pocetak;
  }

  if (pocetak > 0) {
    memmove(tekst, tekst + pocetak, duljina - pocetak + 1);
  }
}

static void logirajLinijuESP(const __FlashStringHelper* prefiks, const char* sadrzaj) {
  char log[128];
  strncpy_P(log, reinterpret_cast<PGM_P>(prefiks), sizeof(log) - 1);
  log[sizeof(log) - 1] = '\0';
  strncat(log, sadrzaj, sizeof(log) - strlen(log) - 1);
  posaljiPCLog(log);
}

static bool jePrepoznataESPLinija(const char* linija) {
  return strcmp(linija, "WIFI:CONNECTED") == 0 ||
         strcmp(linija, "WIFI:DISCONNECTED") == 0 ||
         strncmp(linija, "WIFI:", 5) == 0 ||
         strcmp(linija, "CFGREQ") == 0 ||
         strncmp(linija, "SETUPWIFI:", 10) == 0 ||
         strcmp(linija, "SETREQ:SUSTAV") == 0 ||
         strcmp(linija, "SETREQ:STAPICI") == 0 ||
         strcmp(linija, "SETREQ:BAT") == 0 ||
         strcmp(linija, "SETREQ:SUNCE") == 0 ||
         strcmp(linija, "SETREQ:MISE") == 0 ||
         strcmp(linija, "SETREQ:BLAGDANI_NEP") == 0 ||
         strcmp(linija, "SETREQ:BLAGDANI_POM") == 0 ||
         strncmp(linija, "SETCFG:SUSTAV|", 14) == 0 ||
         strncmp(linija, "SETCFG:STAPICI|", 15) == 0 ||
         strncmp(linija, "SETCFG:BAT|", 11) == 0 ||
         strncmp(linija, "SETCFG:SUNCE|", 13) == 0 ||
         strncmp(linija, "SETCFG:MISE|", 12) == 0 ||
         strncmp(linija, "SETCFG:BLAGDANI_NEP|", 20) == 0 ||
         strncmp(linija, "SETCFG:BLAGDANI_POM|", 20) == 0 ||
         strncmp(linija, "STATUS:", 7) == 0 ||
         strcmp(linija, "STATUS?") == 0 ||
         strncmp(linija, "NTP:", 4) == 0 ||
         strncmp(linija, "CMD:", 4) == 0 ||
         strncmp(linija, "NTPLOG:", 7) == 0;
}

static void obradiNTPLogLinijuESP(const char* linija) {
  const char* poruka = linija + 7;
  while (*poruka == ' ') {
    ++poruka;
  }

  if (strncmp(poruka, "osvjezeno, epoch=", 17) == 0) {
    ntpCekanjePrijavljeno = false;
    return;
  }

  if (strcmp(poruka, "jos nije postavljeno vrijeme, cekam...") == 0) {
    if (!ntpCekanjePrijavljeno) {
      posaljiPCLog(F("Mrezni most NTP: jos nije postavljeno vrijeme"));
      ntpCekanjePrijavljeno = true;
    }
    return;
  }

  ntpCekanjePrijavljeno = false;
  logirajLinijuESP(F("Mrezni most NTP: "), poruka);
}

static void obradiWiFiLogLinijuESP(const char* linija) {
  const char* poruka = linija + 5;
  while (*poruka == ' ') {
    ++poruka;
  }

  logirajLinijuESP(F("Mrezni most WiFi: "), poruka);
}

static void posaljiKonfiguracijuESPuNakonZahtjeva() {
  posaljiWifiPostavkeESP();
  posaljiWiFiStatusESP();
  posaljiNTPPostavkeESP();
  posaljiSustavskePostavkeESPu();
  posaljiPostavkeStapicaESPu();
  posaljiBATPostavkeESPu();
  posaljiSuncevePostavkeESPu();
  posaljiMisePostavkeESPu();
  posaljiNepomicneBlagdaneESPu();
  posaljiPomicneBlagdaneESPu();
  zatraziWiFiStatusESP();
  posaljiPCLog(F("Mrezni most zatrazio osvjezavanje konfiguracije"));
}

static bool parsirajBoolZastavicuSustava(const char* vrijednost, bool& izlaz) {
  if (vrijednost == nullptr || vrijednost[0] == '\0' || vrijednost[1] != '\0') {
    return false;
  }

  if (vrijednost[0] == '0') {
    izlaz = false;
    return true;
  }

  if (vrijednost[0] == '1') {
    izlaz = true;
    return true;
  }

  return false;
}

static bool parsirajUIntPoljeSustava(const char* vrijednost, unsigned int& izlaz) {
  if (vrijednost == nullptr || vrijednost[0] == '\0') {
    return false;
  }

  unsigned long akumulator = 0;
  for (const char* pokazivac = vrijednost; *pokazivac != '\0'; ++pokazivac) {
    if (!isDigit(*pokazivac)) {
      return false;
    }

    akumulator = akumulator * 10UL + static_cast<unsigned long>(*pokazivac - '0');
    if (akumulator > 60000UL) {
      return false;
    }
  }

  izlaz = static_cast<unsigned int>(akumulator);
  return true;
}

static bool parsirajIntPoljeSustava(const char* vrijednost, int& izlaz) {
  if (vrijednost == nullptr || vrijednost[0] == '\0') {
    return false;
  }

  bool negativan = false;
  const char* pokazivac = vrijednost;
  if (*pokazivac == '-') {
    negativan = true;
    ++pokazivac;
  }

  if (*pokazivac == '\0') {
    return false;
  }

  long akumulator = 0L;
  while (*pokazivac != '\0') {
    if (!isDigit(*pokazivac)) {
      return false;
    }

    akumulator = akumulator * 10L + static_cast<long>(*pokazivac - '0');
    if (akumulator > 60000L) {
      return false;
    }
    ++pokazivac;
  }

  izlaz = static_cast<int>(negativan ? -akumulator : akumulator);
  return true;
}

static bool spremiSustavskePostavkeIzESPa(char* payload) {
  if (payload == nullptr || payload[0] == '\0') {
    return false;
  }

  bool lcdPoznato = false;
  bool logPoznato = false;
  bool rsPoznato = false;
  bool upsPoznato = false;
  bool kocPoznato = false;
  bool lcdUkljuceno = false;
  bool logOmogucen = false;
  bool rsOmogucen = false;
  bool upsOmogucen = false;
  bool kocOmogucena = false;
  unsigned int inr1 = 0;
  unsigned int inr2 = 0;
  unsigned int impuls = 0;
  bool inr1Poznat = false;
  bool inr2Poznat = false;
  bool impulsPoznat = false;

  char* context = nullptr;
  for (char* polje = strtok_r(payload, "|", &context);
       polje != nullptr;
       polje = strtok_r(nullptr, "|", &context)) {
    trimJednolinijskiTekstESP(polje);

    char* jednako = strchr(polje, '=');
    if (jednako == nullptr) {
      return false;
    }

    *jednako = '\0';
    char* kljuc = polje;
    char* vrijednost = jednako + 1;
    trimJednolinijskiTekstESP(kljuc);
    trimJednolinijskiTekstESP(vrijednost);

    if (strcmp(kljuc, "lcd") == 0) {
      if (!parsirajBoolZastavicuSustava(vrijednost, lcdUkljuceno)) return false;
      lcdPoznato = true;
    } else if (strcmp(kljuc, "log") == 0) {
      if (!parsirajBoolZastavicuSustava(vrijednost, logOmogucen)) return false;
      logPoznato = true;
    } else if (strcmp(kljuc, "rs") == 0) {
      if (!parsirajBoolZastavicuSustava(vrijednost, rsOmogucen)) return false;
      rsPoznato = true;
    } else if (strcmp(kljuc, "ups") == 0) {
      if (!parsirajBoolZastavicuSustava(vrijednost, upsOmogucen)) return false;
      upsPoznato = true;
    } else if (strcmp(kljuc, "koc") == 0) {
      if (!parsirajBoolZastavicuSustava(vrijednost, kocOmogucena)) return false;
      kocPoznato = true;
    } else if (strcmp(kljuc, "inr1") == 0) {
      if (!parsirajUIntPoljeSustava(vrijednost, inr1)) return false;
      inr1Poznat = true;
    } else if (strcmp(kljuc, "inr2") == 0) {
      if (!parsirajUIntPoljeSustava(vrijednost, inr2)) return false;
      inr2Poznat = true;
    } else if (strcmp(kljuc, "imp") == 0) {
      if (!parsirajUIntPoljeSustava(vrijednost, impuls)) return false;
      impulsPoznat = true;
    } else {
      return false;
    }
  }

  if (!lcdPoznato || !logPoznato || !rsPoznato || !upsPoznato || !kocPoznato ||
      !inr1Poznat || !inr2Poznat || !impulsPoznat) {
    return false;
  }

  postaviLCDPozadinskoOsvjetljenje(lcdUkljuceno);
  postaviPCLogiranjeOmoguceno(logOmogucen);
  postaviRS485Omogucen(rsOmogucen);
  postaviUPSModOmogucen(upsOmogucen);
  postaviKocnicuZvonaOmoguceno(kocOmogucena);
  postaviInercijeZvona(static_cast<uint8_t>(inr1), static_cast<uint8_t>(inr2));
  postaviTrajanjeImpulsaCekica(impuls);

  char log[128];
  snprintf_P(log,
             sizeof(log),
             PSTR("Mrezni most je spremio sustavske postavke: lcd=%d log=%d rs=%d ups=%d koc=%d inr1=%u inr2=%u imp=%u"),
             lcdUkljuceno ? 1 : 0,
             logOmogucen ? 1 : 0,
             rsOmogucen ? 1 : 0,
             upsOmogucen ? 1 : 0,
             kocOmogucena ? 1 : 0,
             inr1,
             inr2,
             impuls);
  posaljiPCLog(log);
  return true;
}

static bool spremiPostavkeStapicaIzESPa(char* payload) {
  if (payload == nullptr || payload[0] == '\0') {
    return false;
  }

  unsigned int trajanjeRadni = 0;
  unsigned int trajanjeNedjelja = 0;
  unsigned int trajanjeSlavljenja = 0;
  unsigned int odgoda = 0;
  bool trajanjeRadniPoznato = false;
  bool trajanjeNedjeljaPoznato = false;
  bool trajanjeSlavljenjaPoznato = false;
  bool odgodaPoznata = false;

  char* context = nullptr;
  for (char* polje = strtok_r(payload, "|", &context);
       polje != nullptr;
       polje = strtok_r(nullptr, "|", &context)) {
    trimJednolinijskiTekstESP(polje);

    char* jednako = strchr(polje, '=');
    if (jednako == nullptr) {
      return false;
    }

    *jednako = '\0';
    char* kljuc = polje;
    char* vrijednost = jednako + 1;
    trimJednolinijskiTekstESP(kljuc);
    trimJednolinijskiTekstESP(vrijednost);

    if (strcmp(kljuc, "tr") == 0) {
      if (!parsirajUIntPoljeSustava(vrijednost, trajanjeRadni)) return false;
      trajanjeRadniPoznato = true;
    } else if (strcmp(kljuc, "tn") == 0) {
      if (!parsirajUIntPoljeSustava(vrijednost, trajanjeNedjelja)) return false;
      trajanjeNedjeljaPoznato = true;
    } else if (strcmp(kljuc, "ts") == 0) {
      if (!parsirajUIntPoljeSustava(vrijednost, trajanjeSlavljenja)) return false;
      trajanjeSlavljenjaPoznato = true;
    } else if (strcmp(kljuc, "odg") == 0) {
      if (!parsirajUIntPoljeSustava(vrijednost, odgoda)) return false;
      odgodaPoznata = true;
    } else {
      return false;
    }
  }

  if (!trajanjeRadniPoznato || !trajanjeNedjeljaPoznato ||
      !trajanjeSlavljenjaPoznato || !odgodaPoznata) {
    return false;
  }

  postaviPostavkeCavala(static_cast<uint8_t>(trajanjeRadni),
                        static_cast<uint8_t>(trajanjeNedjelja),
                        static_cast<uint8_t>(trajanjeSlavljenja),
                        static_cast<uint8_t>(odgoda));

  char log[96];
  snprintf_P(log,
             sizeof(log),
             PSTR("Mrezni most je spremio postavke stapica: TR=%u TN=%u TS=%u S=%u"),
             trajanjeRadni,
             trajanjeNedjelja,
             trajanjeSlavljenja,
             odgoda);
  posaljiPCLog(log);
  return true;
}

static bool spremiBATPostavkeIzESPa(char* payload) {
  if (payload == nullptr || payload[0] == '\0') {
    return false;
  }

  int satOd = 0;
  int satDo = 0;
  unsigned int modOtkucavanja = 0;
  unsigned int modSlavljenja = 0;
  unsigned int modMrtvackog = 0;
  bool satOdPoznat = false;
  bool satDoPoznat = false;
  bool modOtkucavanjaPoznat = false;
  bool modSlavljenjaPoznat = false;
  bool modMrtvackogPoznat = false;

  char* context = nullptr;
  for (char* polje = strtok_r(payload, "|", &context);
       polje != nullptr;
       polje = strtok_r(nullptr, "|", &context)) {
    trimJednolinijskiTekstESP(polje);

    char* jednako = strchr(polje, '=');
    if (jednako == nullptr) {
      return false;
    }

    *jednako = '\0';
    char* kljuc = polje;
    char* vrijednost = jednako + 1;
    trimJednolinijskiTekstESP(kljuc);
    trimJednolinijskiTekstESP(vrijednost);

    if (strcmp(kljuc, "od") == 0) {
      if (!parsirajIntPoljeSustava(vrijednost, satOd)) return false;
      satOdPoznat = true;
    } else if (strcmp(kljuc, "do") == 0) {
      if (!parsirajIntPoljeSustava(vrijednost, satDo)) return false;
      satDoPoznat = true;
    } else if (strcmp(kljuc, "otk") == 0) {
      if (!parsirajUIntPoljeSustava(vrijednost, modOtkucavanja)) return false;
      modOtkucavanjaPoznat = true;
    } else if (strcmp(kljuc, "sl") == 0) {
      if (!parsirajUIntPoljeSustava(vrijednost, modSlavljenja)) return false;
      modSlavljenjaPoznat = true;
    } else if (strcmp(kljuc, "mr") == 0) {
      if (!parsirajUIntPoljeSustava(vrijednost, modMrtvackog)) return false;
      modMrtvackogPoznat = true;
    } else {
      return false;
    }
  }

  if (!satOdPoznat || !satDoPoznat || !modOtkucavanjaPoznat ||
      !modSlavljenjaPoznat || !modMrtvackogPoznat) {
    return false;
  }

  postaviKompaktnePostavkeOtkucavanja(
      satOd,
      satDo,
      static_cast<uint8_t>(modOtkucavanja),
      static_cast<uint8_t>(modSlavljenja),
      static_cast<uint8_t>(modMrtvackog));

  char log[112];
  snprintf_P(log,
             sizeof(log),
             PSTR("Mrezni most je spremio BAT postavke: od=%d do=%d OTK=%u S=%u M=%u"),
             satOd,
             satDo,
             modOtkucavanja,
             modSlavljenja,
             modMrtvackog);
  posaljiPCLog(log);
  return true;
}

static bool spremiSuncevePostavkeIzESPa(char* payload) {
  if (payload == nullptr || payload[0] == '\0') {
    return false;
  }

  bool jutroOmoguceno = false;
  bool podneOmoguceno = false;
  bool vecerOmoguceno = false;
  bool nocnaRasvjeta = false;
  unsigned int jutroZvono = 0;
  unsigned int podneZvono = 0;
  unsigned int vecerZvono = 0;
  int jutroOdgoda = 0;
  int vecerOdgoda = 0;
  bool jutroOmogucenoPoznato = false;
  bool podneOmogucenoPoznato = false;
  bool vecerOmogucenoPoznato = false;
  bool nocnaRasvjetaPoznata = false;
  bool jutroZvonoPoznato = false;
  bool podneZvonoPoznato = false;
  bool vecerZvonoPoznato = false;
  bool jutroOdgodaPoznata = false;
  bool vecerOdgodaPoznata = false;

  char* context = nullptr;
  for (char* polje = strtok_r(payload, "|", &context);
       polje != nullptr;
       polje = strtok_r(nullptr, "|", &context)) {
    trimJednolinijskiTekstESP(polje);

    char* jednako = strchr(polje, '=');
    if (jednako == nullptr) {
      return false;
    }

    *jednako = '\0';
    char* kljuc = polje;
    char* vrijednost = jednako + 1;
    trimJednolinijskiTekstESP(kljuc);
    trimJednolinijskiTekstESP(vrijednost);

    if (strcmp(kljuc, "ju") == 0) {
      if (!parsirajBoolZastavicuSustava(vrijednost, jutroOmoguceno)) return false;
      jutroOmogucenoPoznato = true;
    } else if (strcmp(kljuc, "jb") == 0) {
      if (!parsirajUIntPoljeSustava(vrijednost, jutroZvono)) return false;
      jutroZvonoPoznato = true;
    } else if (strcmp(kljuc, "jo") == 0) {
      if (!parsirajIntPoljeSustava(vrijednost, jutroOdgoda)) return false;
      jutroOdgodaPoznata = true;
    } else if (strcmp(kljuc, "pu") == 0) {
      if (!parsirajBoolZastavicuSustava(vrijednost, podneOmoguceno)) return false;
      podneOmogucenoPoznato = true;
    } else if (strcmp(kljuc, "pb") == 0) {
      if (!parsirajUIntPoljeSustava(vrijednost, podneZvono)) return false;
      podneZvonoPoznato = true;
    } else if (strcmp(kljuc, "vu") == 0) {
      if (!parsirajBoolZastavicuSustava(vrijednost, vecerOmoguceno)) return false;
      vecerOmogucenoPoznato = true;
    } else if (strcmp(kljuc, "vb") == 0) {
      if (!parsirajUIntPoljeSustava(vrijednost, vecerZvono)) return false;
      vecerZvonoPoznato = true;
    } else if (strcmp(kljuc, "vo") == 0) {
      if (!parsirajIntPoljeSustava(vrijednost, vecerOdgoda)) return false;
      vecerOdgodaPoznata = true;
    } else if (strcmp(kljuc, "nr") == 0) {
      if (!parsirajBoolZastavicuSustava(vrijednost, nocnaRasvjeta)) return false;
      nocnaRasvjetaPoznata = true;
    } else {
      return false;
    }
  }

  if (!jutroOmogucenoPoznato || !jutroZvonoPoznato || !jutroOdgodaPoznata ||
      !podneOmogucenoPoznato || !podneZvonoPoznato ||
      !vecerOmogucenoPoznato || !vecerZvonoPoznato || !vecerOdgodaPoznata ||
      !nocnaRasvjetaPoznata) {
    return false;
  }

  postaviSuncevDogadaj(
      SUNCEVI_DOGADAJ_JUTRO,
      jutroOmoguceno,
      static_cast<uint8_t>(jutroZvono),
      jutroOdgoda);
  postaviSuncevDogadaj(
      SUNCEVI_DOGADAJ_PODNE,
      podneOmoguceno,
      static_cast<uint8_t>(podneZvono),
      0);
  postaviSuncevDogadaj(
      SUNCEVI_DOGADAJ_VECER,
      vecerOmoguceno,
      static_cast<uint8_t>(vecerZvono),
      vecerOdgoda);
  postaviNocnuRasvjetuOmoguceno(nocnaRasvjeta);

  char log[128];
  snprintf_P(log,
             sizeof(log),
             PSTR("Mrezni most je spremio sunceve postavke: J=%d/%u/%d P=%d/%u V=%d/%u/%d NR=%d"),
             jutroOmoguceno ? 1 : 0,
             jutroZvono,
             jutroOdgoda,
             podneOmoguceno ? 1 : 0,
             podneZvono,
             vecerOmoguceno ? 1 : 0,
             vecerZvono,
             vecerOdgoda,
             nocnaRasvjeta ? 1 : 0);
  posaljiPCLog(log);
  return true;
}

static bool spremiMisePostavkeIzESPa(char* payload) {
  if (payload == nullptr || payload[0] == '\0') {
    return false;
  }

  RedoviteMisePostavke redoviteMise = {false, 0, 0, false, 0, 0};
  bool dnevnaMisaPoznata = false;
  bool nedjeljnaMisaPoznata = false;

  char* context = nullptr;
  for (char* polje = strtok_r(payload, "|", &context);
       polje != nullptr;
       polje = strtok_r(nullptr, "|", &context)) {
    trimJednolinijskiTekstESP(polje);

    char* jednako = strchr(polje, '=');
    if (jednako == nullptr) {
      return false;
    }

    *jednako = '\0';
    char* kljuc = polje;
    char* vrijednost = jednako + 1;
    trimJednolinijskiTekstESP(kljuc);
    trimJednolinijskiTekstESP(vrijednost);

    unsigned int omogucena = 0;
    unsigned int sat = 0;
    unsigned int minuta = 0;
    if (sscanf_P(vrijednost, PSTR("%u,%u,%u"), &omogucena, &sat, &minuta) != 3) {
      return false;
    }

    if (strcmp(kljuc, "rd") == 0) {
      redoviteMise.dnevnaOmogucena = omogucena != 0;
      redoviteMise.dnevnaSatMise = static_cast<uint8_t>(sat);
      redoviteMise.dnevnaMinutaMise = static_cast<uint8_t>(minuta);
      dnevnaMisaPoznata = true;
    } else if (strcmp(kljuc, "nd") == 0) {
      redoviteMise.nedjeljnaOmogucena = omogucena != 0;
      redoviteMise.nedjeljnaSatMise = static_cast<uint8_t>(sat);
      redoviteMise.nedjeljnaMinutaMise = static_cast<uint8_t>(minuta);
      nedjeljnaMisaPoznata = true;
    } else {
      return false;
    }
  }

  if (!dnevnaMisaPoznata || !nedjeljnaMisaPoznata) {
    return false;
  }

  postaviRedoviteMise(redoviteMise);
  posaljiPCLog(F("Mrezni most je spremio misne postavke toranjskog sata"));
  return true;
}

static bool spremiNepomicneBlagdaneIzESPa(char* payload) {
  if (payload == nullptr || payload[0] == '\0') {
    return false;
  }

  NepomicniBlagdanPostavka nepomicni[BROJ_NEPOMICNIH_BLAGDANA] = {};
  PomicniBlagdanPostavka pomicni[BROJ_POMICNIH_BLAGDANA] = {};
  bool nepomicniPoznati[BROJ_NEPOMICNIH_BLAGDANA] = {};

  for (uint8_t i = 0; i < BROJ_POMICNIH_BLAGDANA; ++i) {
    dohvatiPomicniBlagdan(i, pomicni[i]);
  }

  char* context = nullptr;
  for (char* polje = strtok_r(payload, "|", &context);
       polje != nullptr;
       polje = strtok_r(nullptr, "|", &context)) {
    trimJednolinijskiTekstESP(polje);

    char* jednako = strchr(polje, '=');
    if (jednako == nullptr) {
      return false;
    }

    *jednako = '\0';
    char* kljuc = polje;
    char* vrijednost = jednako + 1;
    trimJednolinijskiTekstESP(kljuc);
    trimJednolinijskiTekstESP(vrijednost);

    if (kljuc[0] != 'f' || kljuc[1] == '\0') {
      return false;
    }

    char* krajIndeksa = nullptr;
    const unsigned long indeksBroj = strtoul(kljuc + 1, &krajIndeksa, 10);
    if (krajIndeksa == nullptr || *krajIndeksa != '\0') {
      return false;
    }

    const uint8_t indeks = static_cast<uint8_t>(indeksBroj);
    if (indeks >= BROJ_NEPOMICNIH_BLAGDANA) {
      return false;
    }

    unsigned int omogucen = 0;
    unsigned int sat = 0;
    unsigned int minuta = 0;
    if (sscanf_P(vrijednost, PSTR("%u,%u,%u"), &omogucen, &sat, &minuta) != 3) {
      return false;
    }

    nepomicni[indeks].omogucen = omogucen != 0;
    nepomicni[indeks].satMise = static_cast<uint8_t>(sat);
    nepomicni[indeks].minutaMise = static_cast<uint8_t>(minuta);
    nepomicniPoznati[indeks] = true;
  }

  for (uint8_t i = 0; i < BROJ_NEPOMICNIH_BLAGDANA; ++i) {
    if (!nepomicniPoznati[i]) {
      return false;
    }
  }

  postaviBlagdanskeMise(nepomicni,
                        BROJ_NEPOMICNIH_BLAGDANA,
                        pomicni,
                        BROJ_POMICNIH_BLAGDANA);
  posaljiPCLog(F("Mrezni most je spremio nepomicne blagdane toranjskog sata"));
  return true;
}

static bool spremiPomicneBlagdaneIzESPa(char* payload) {
  if (payload == nullptr || payload[0] == '\0') {
    return false;
  }

  NepomicniBlagdanPostavka nepomicni[BROJ_NEPOMICNIH_BLAGDANA] = {};
  PomicniBlagdanPostavka pomicni[BROJ_POMICNIH_BLAGDANA] = {};
  bool pomicniPoznati[BROJ_POMICNIH_BLAGDANA] = {};

  for (uint8_t i = 0; i < BROJ_NEPOMICNIH_BLAGDANA; ++i) {
    dohvatiNepomicniBlagdan(i, nepomicni[i]);
  }

  char* context = nullptr;
  for (char* polje = strtok_r(payload, "|", &context);
       polje != nullptr;
       polje = strtok_r(nullptr, "|", &context)) {
    trimJednolinijskiTekstESP(polje);

    char* jednako = strchr(polje, '=');
    if (jednako == nullptr) {
      return false;
    }

    *jednako = '\0';
    char* kljuc = polje;
    char* vrijednost = jednako + 1;
    trimJednolinijskiTekstESP(kljuc);
    trimJednolinijskiTekstESP(vrijednost);

    if (kljuc[0] != 'p' || kljuc[1] == '\0') {
      return false;
    }

    char* krajIndeksa = nullptr;
    const unsigned long indeksBroj = strtoul(kljuc + 1, &krajIndeksa, 10);
    if (krajIndeksa == nullptr || *krajIndeksa != '\0') {
      return false;
    }

    const uint8_t indeks = static_cast<uint8_t>(indeksBroj);
    if (indeks >= BROJ_POMICNIH_BLAGDANA) {
      return false;
    }

    unsigned int omogucen = 0;
    unsigned int sat = 0;
    unsigned int minuta = 0;
    if (sscanf_P(vrijednost, PSTR("%u,%u,%u"), &omogucen, &sat, &minuta) != 3) {
      return false;
    }

    pomicni[indeks].omogucen = omogucen != 0;
    pomicni[indeks].satMise = static_cast<uint8_t>(sat);
    pomicni[indeks].minutaMise = static_cast<uint8_t>(minuta);
    pomicniPoznati[indeks] = true;
  }

  for (uint8_t i = 0; i < BROJ_POMICNIH_BLAGDANA; ++i) {
    if (!pomicniPoznati[i]) {
      return false;
    }
  }

  postaviBlagdanskeMise(nepomicni,
                        BROJ_NEPOMICNIH_BLAGDANA,
                        pomicni,
                        BROJ_POMICNIH_BLAGDANA);
  posaljiPCLog(F("Mrezni most je spremio pomicne blagdane toranjskog sata"));
  return true;
}

static bool spremiSetupWiFiPostavkeIzESPa(const char* payload) {
  if (payload == nullptr || payload[0] == '\0') {
    return false;
  }

  const char* granica = strchr(payload, '|');
  if (granica == nullptr) {
    return false;
  }

  const size_t duljinaSsid = static_cast<size_t>(granica - payload);
  const char* lozinka = granica + 1;
  const size_t duljinaLozinke = strlen(lozinka);

  if (duljinaSsid == 0 || duljinaSsid > 32 || duljinaLozinke == 0 || duljinaLozinke > 32) {
    return false;
  }

  char ssid[33];
  char lozinkaBuffer[33];
  memcpy(ssid, payload, duljinaSsid);
  ssid[duljinaSsid] = '\0';
  memcpy(lozinkaBuffer, lozinka, duljinaLozinke);
  lozinkaBuffer[duljinaLozinke] = '\0';

  for (size_t i = 0; i < duljinaSsid; ++i) {
    const char znak = ssid[i];
    if (znak == '|' || znak == '\r' || znak == '\n') {
      return false;
    }
  }

  for (size_t i = 0; i < duljinaLozinke; ++i) {
    const char znak = lozinkaBuffer[i];
    if (znak == '|' || znak == '\r' || znak == '\n') {
      return false;
    }
  }

  postaviWiFiPodatkeZaSetup(ssid, lozinkaBuffer);
  postaviWiFiOmogucen(true);
  posaljiWifiPostavkeESP();
  posaljiWiFiStatusESP();
  zatraziWiFiStatusESP();

  char log[88];
  snprintf_P(log,
             sizeof(log),
             PSTR("Setup WiFi: spremljen novi SSID=%s preko mreznog mosta"),
             ssid);
  posaljiPCLog(log);
  return true;
}

void inicijalizirajESP() {
  espSerijskiPort.begin(ESP_BRZINA);
  resetirajUlazniBuffer();
  wifiPovezanNaESP = false;
  zadnjaLokalnaWiFiIP[0] = '\0';
  zadnjaWiFiMACAdresa[0] = '\0';
  wifiPocetnaOdgodaAktivna = false;
  wifiPocetnaOdgodaDoMs = 0;
  vrijemePrvogWiFiStatusUpitaMs = 0;
  zadnjiWiFiStatusRecoveryUpitMs = 0;
  drugiWiFiStatusUpitPoslan = false;
  prioritetniNtpZahtjevNaCekanju = false;
  zadnjiAutomatskiNtpZahtjevMinutniKljuc = NTP_KLJUC_NEPOSTAVLJEN;
  zadnjiAutomatskiNtpZahtjevSatniKljuc = NTP_KLJUC_NEPOSTAVLJEN;
  postaviWiFiStatus(false);
  posaljiPCLog(F("Serijska veza prema mreznom mostu inicijalizirana"));
  // Kratka pauza je samo za stabilizaciju serijske veze tijekom boot-a.
  // Ovdje jos nismo usli u glavnu loop petlju niti su mehanike aktivirane,
  // pa ovih 50 ms ne remeti vremenski kriticne dijelove toranjskog sata.
  delay(50);
  posaljiWifiPostavkeESP();
  posaljiWiFiStatusESP();
  posaljiNTPPostavkeESP();

  if (jeWiFiOmogucen() && jeResetNakonGubitkaNapajanja()) {
    wifiPocetnaOdgodaAktivna = true;
    wifiPocetnaOdgodaDoMs = millis() + WIFI_POCETNA_ODGODA_NAKON_NAPAJANJA_MS;
    posaljiPCLog(F("WiFi/NTP: povratak napajanja detektiran, cekam 120 s prije prve provjere mreze"));
    return;
  }

  zatraziWiFiStatusESP();
}

static void zatraziWiFiStatusESP() {
  espSerijskiPort.println(F("WIFISTATUS?"));
}

void posaljiWifiPostavkeESP() {
  espSerijskiPort.print(F("WIFI:"));
  espSerijskiPort.print(dohvatiWifiSsid());
  espSerijskiPort.print('|');
  espSerijskiPort.print(dohvatiWifiLozinku());
  espSerijskiPort.print('|');
  espSerijskiPort.print(koristiDhcpMreza() ? '1' : '0');
  espSerijskiPort.print('|');
  espSerijskiPort.print(dohvatiStatickuIP());
  espSerijskiPort.print('|');
  espSerijskiPort.print(dohvatiMreznuMasku());
  espSerijskiPort.print('|');
  espSerijskiPort.println(dohvatiZadaniGateway());

  posaljiPCLog(F("Poslane WiFi postavke mreznom mostu"));
}

void posaljiWiFiStatusESP() {
  espSerijskiPort.print(F("WIFIEN:"));
  espSerijskiPort.println(jeWiFiOmogucen() ? '1' : '0');
  posaljiPCLog(jeWiFiOmogucen()
                   ? F("Poslana naredba mreznom mostu: mreza ukljucena")
                   : F("Poslana naredba mreznom mostu: mreza iskljucena"));
}

void posaljiNTPPostavkeESP() {
  espSerijskiPort.print(F("NTPCFG:"));
  espSerijskiPort.println(dohvatiNTPServer());

  char log[80];
  snprintf_P(log,
             sizeof(log),
             PSTR("Poslan NTP server mreznom mostu: %s"),
             dohvatiNTPServer());
  posaljiPCLog(log);
}

static bool jeSiguranProzorZaNTPZahtjev(const DateTime& sada) {
  return sada.second() >= NTP_SIGURNA_SEKUNDA_MIN &&
         sada.second() <= NTP_SIGURNA_SEKUNDA_MAX;
}

static bool jeSigurnaMinutaZaNTPZahtjev(const DateTime& sada) {
  return sada.minute() != 0;
}

static bool mehanikaTornjskogSataMirujeZaNTP() {
  if (jeOtkucavanjeUTijeku()) {
    return false;
  }

  if (jeRucnaBlokadaKazaljkiAktivna() || jeRucnaBlokadaPloceAktivna()) {
    return false;
  }

  if (!mozeSeRucnoNamjestatiKazaljke() || !mozeSeRucnoNamjestatiPloca()) {
    return false;
  }

  if (jeVrijemePotvrdjenoZaAutomatiku() &&
      (!suKazaljkeUSinkronu() || !jePlocaUSinkronu())) {
    return false;
  }

  return true;
}

static bool jePouzdanoVrijemeDostupnoZaNTP(const DateTime& sada) {
  return jeVrijemePotvrdjenoZaAutomatiku() && sada.unixtime() != 0;
}

static uint32_t odrediMinutniKljucNTPZahtjeva(const DateTime& sada) {
  if (jePouzdanoVrijemeDostupnoZaNTP(sada)) {
    return sada.unixtime() / 60UL;
  }

  // Kad RTC nakon prazne baterije vrati nevaljano vrijeme, toranjski sat ne smije
  // zauvijek ostati na 00:00:00 i blokirati prvi NTP zahtjev. U tom stanju
  // koristimo lokalni millis ritam samo za ogranicenje ucestalosti pokusaja.
  return millis() / 60000UL;
}

static uint32_t odrediSatniKljucNTPZahtjeva(const DateTime& sada) {
  if (jePouzdanoVrijemeDostupnoZaNTP(sada)) {
    return sada.unixtime() / 3600UL;
  }

  return millis() / 3600000UL;
}

void posaljiNTPZahtjevESP() {
  const DateTime sada = dohvatiTrenutnoVrijeme();
  zadnjiAutomatskiNtpZahtjevMinutniKljuc = odrediMinutniKljucNTPZahtjeva(sada);
  zadnjiAutomatskiNtpZahtjevSatniKljuc = odrediSatniKljucNTPZahtjeva(sada);
  espSerijskiPort.println(F("NTPREQ:SYNC"));
  posaljiPCLog(F("Poslan zahtjev mreznom mostu za NTP osvjezavanje toranjskog sata"));
}

void zatraziPrioritetnuNTPSinkronizaciju() {
  prioritetniNtpZahtjevNaCekanju = true;
  zadnjiAutomatskiNtpZahtjevMinutniKljuc = NTP_KLJUC_NEPOSTAVLJEN;
  zadnjiAutomatskiNtpZahtjevSatniKljuc = NTP_KLJUC_NEPOSTAVLJEN;
  posaljiPCLog(F("NTP: zabiljezen prioritetni zahtjev nakon rucne promjene vremena"));
}

void obradiAutomatskiNTPZahtjevESP() {
  if (!jeNTPOmogucen() || !jeWiFiPovezanNaESP() || jeAktivnaPocetnaOdgodaWiFi()) {
    return;
  }

  const DateTime sada = dohvatiTrenutnoVrijeme();
  const bool vrijemePouzdanoZaRaspored = jePouzdanoVrijemeDostupnoZaNTP(sada);
  if (!mehanikaTornjskogSataMirujeZaNTP()) {
    return;
  }

  if (vrijemePouzdanoZaRaspored &&
      (!jeSigurnaMinutaZaNTPZahtjev(sada) ||
       !jeSiguranProzorZaNTPZahtjev(sada))) {
    return;
  }

  const uint32_t minutniKljuc = odrediMinutniKljucNTPZahtjeva(sada);
  const uint32_t satniKljuc = odrediSatniKljucNTPZahtjeva(sada);

  if (!jeVrijemePotvrdjenoZaAutomatiku() || jeSinkronizacijaZastarjela()) {
    prioritetniNtpZahtjevNaCekanju = true;
  }

  if (prioritetniNtpZahtjevNaCekanju) {
    if (minutniKljuc == zadnjiAutomatskiNtpZahtjevMinutniKljuc) {
      return;
    }

    posaljiNTPZahtjevESP();
    if (!jeVrijemePotvrdjenoZaAutomatiku()) {
      posaljiPCLog(F("Automatski NTP zahtjev: cekam prvu potvrdu vremena za toranjski sat"));
    } else {
      posaljiPCLog(F("Automatski NTP zahtjev: obnavljam svjezu NTP sinkronizaciju toranjskog sata"));
    }
    return;
  }

  if (satniKljuc == zadnjiAutomatskiNtpZahtjevSatniKljuc) {
    return;
  }

  posaljiNTPZahtjevESP();
  posaljiPCLog(F("Automatski NTP zahtjev: siguran prozor za obnovu vremena toranjskog sata"));
}

void posaljiESPKomandu(const char* komanda) {
  espSerijskiPort.println(komanda);
}

bool jeWiFiPovezanNaESP() {
  return wifiPovezanNaESP;
}

const char* dohvatiESPWiFiLokalnuIP() {
  return zadnjaLokalnaWiFiIP;
}

const char* dohvatiESPWiFiMACAdresu() {
  return zadnjaWiFiMACAdresa;
}

static bool parsirajISOVrijeme(const char* iso, DateTime& dt, uint16_t& milisekunde) {
  const int osnovnaDuljina = 19;
  const size_t duljina = strlen(iso);
  const bool imaZuluneSufiks = (duljina == 20 && iso[19] == 'Z') ||
                               (duljina == 24 && iso[23] == 'Z');
  const size_t osnovnaDuljinaBezZ =
      imaZuluneSufiks ? (duljina - 1U) : duljina;

  if (!(osnovnaDuljinaBezZ == osnovnaDuljina || osnovnaDuljinaBezZ == 23U)) return false;
  if (iso[4] != '-' || iso[7] != '-' || iso[10] != 'T' ||
      iso[13] != ':' || iso[16] != ':') {
    return false;
  }

  const bool imaMilisekunde = (osnovnaDuljinaBezZ == 23U);
  if (imaMilisekunde && iso[19] != '.') {
    return false;
  }

  for (int i = 0; i < osnovnaDuljina; ++i) {
    if (i == 4 || i == 7 || i == 10 || i == 13 || i == 16) continue;
    if (!isDigit(iso[i])) return false;
  }

  if (imaMilisekunde) {
    if (!isDigit(iso[20]) || !isDigit(iso[21]) || !isDigit(iso[22])) {
      return false;
    }
  }

  char broj[5];
  memcpy(broj, iso, 4);
  broj[4] = '\0';
  const int godina = atoi(broj);

  broj[0] = iso[5];
  broj[1] = iso[6];
  broj[2] = '\0';
  const int mjesec = atoi(broj);

  broj[0] = iso[8];
  broj[1] = iso[9];
  const int dan = atoi(broj);

  broj[0] = iso[11];
  broj[1] = iso[12];
  const int sat = atoi(broj);

  broj[0] = iso[14];
  broj[1] = iso[15];
  const int minuta = atoi(broj);

  broj[0] = iso[17];
  broj[1] = iso[18];
  const int sekunda = atoi(broj);

  milisekunde = 0;
  if (imaMilisekunde) {
    char msBroj[4];
    msBroj[0] = iso[20];
    msBroj[1] = iso[21];
    msBroj[2] = iso[22];
    msBroj[3] = '\0';
    milisekunde = static_cast<uint16_t>(atoi(msBroj));
  }

  // Ovdje parsiramo iskljucivo NTP payload koji dolazi s mreznog mosta.
  // Donju granicu drzimo na 2000 jer RTClib::DateTime podrzava raspon 2000-2099,
  // a ne zelimo da oporavak nakon loseg RTC stanja padne zbog proizvoljno
  // previsoke godine u parseru.
  if (godina < MIN_NTP_GODINA || godina > MAX_NTP_GODINA ||
      mjesec < 1 || mjesec > 12) {
    return false;
  }

  static const uint8_t daniUMjesecu[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  const bool prijestupnaGodina =
      ((godina % 4 == 0) && (godina % 100 != 0)) || (godina % 400 == 0);
  int maxDana = daniUMjesecu[mjesec - 1];
  if (mjesec == 2 && prijestupnaGodina) {
    maxDana = 29;
  }

  const bool poljaIspravna =
      dan >= 1 && dan <= maxDana &&
      sat >= 0 && sat <= 23 &&
      minuta >= 0 && minuta <= 59 &&
      sekunda >= 0 && sekunda <= 59;
  if (!poljaIspravna) return false;

  dt = DateTime(godina, mjesec, dan, sat, minuta, sekunda);
  return true;
}

static bool parsirajNTPPayload(const char* payload,
                               DateTime& dt,
                               uint16_t& milisekunde,
                               bool& imaEksplicitanDST,
                               bool& dstAktivanIzvori) {
  imaEksplicitanDST = false;
  dstAktivanIzvori = false;
  milisekunde = 0;

  const char* dstSuffix = strstr(payload, ";DST=");
  if (dstSuffix == nullptr) {
    return parsirajISOVrijeme(payload, dt, milisekunde);
  }

  const size_t isoDuljina = static_cast<size_t>(dstSuffix - payload);
  const bool valjanaOsnovnaDuljina =
      isoDuljina == 19 || isoDuljina == 23 ||
      (isoDuljina == 20 && payload[19] == 'Z') ||
      (isoDuljina == 24 && payload[23] == 'Z');
  if (!valjanaOsnovnaDuljina) {
    return false;
  }

  if ((dstSuffix[5] != '0' && dstSuffix[5] != '1') || dstSuffix[6] != '\0') {
    return false;
  }

  char isoBuffer[25];
  if (isoDuljina >= sizeof(isoBuffer)) {
    return false;
  }

  memcpy(isoBuffer, payload, isoDuljina);
  isoBuffer[isoDuljina] = '\0';
  if (!parsirajISOVrijeme(isoBuffer, dt, milisekunde)) {
    return false;
  }

  imaEksplicitanDST = true;
  dstAktivanIzvori = (dstSuffix[5] == '1');
  return true;
}

static void obradiESPRedak() {
  trimBuffer();

  if (ulazniBufferDuljina == 0) {
    resetirajUlazniBuffer();
    return;
  }

  if (!jePrepoznataESPLinija(ulazniBuffer)) {
    logirajLinijuESP(F("Mrezni most linija: "), ulazniBuffer);
  }

  if (strcmp(ulazniBuffer, "WIFI:CONNECTED") == 0) {
    const bool bioSpojen = wifiPovezanNaESP;
    wifiPovezanNaESP = true;
    postaviWiFiStatus(true);
    if (!bioSpojen) {
      prioritetniNtpZahtjevNaCekanju = true;
      zadnjiAutomatskiNtpZahtjevMinutniKljuc = NTP_KLJUC_NEPOSTAVLJEN;
      zadnjiAutomatskiNtpZahtjevSatniKljuc = NTP_KLJUC_NEPOSTAVLJEN;
    }
    posaljiPCLog(F("Mrezni most status mreze: spojeno"));
    resetirajUlazniBuffer();
    return;
  }

  if (strcmp(ulazniBuffer, "WIFI:DISCONNECTED") == 0) {
    wifiPovezanNaESP = false;
    postaviWiFiStatus(false);
    prioritetniNtpZahtjevNaCekanju = false;
    zadnjiAutomatskiNtpZahtjevMinutniKljuc = NTP_KLJUC_NEPOSTAVLJEN;
    zadnjiAutomatskiNtpZahtjevSatniKljuc = NTP_KLJUC_NEPOSTAVLJEN;
    posaljiPCLog(F("Mrezni most status mreze: odspojeno"));
    resetirajUlazniBuffer();
    return;
  }

  if (strncmp(ulazniBuffer, "WIFI:LOCAL_IP:", 14) == 0) {
    const char* ipAdresa = ulazniBuffer + 14;
    potvrdiWiFiPovezanostAkoTreba(F("Mrezni most status mreze: spojeno (potvrda preko lokalne IP)"));

    if (jeValjanaIPv4AdresaZaLCD(ipAdresa)) {
      strncpy(zadnjaLokalnaWiFiIP, ipAdresa, sizeof(zadnjaLokalnaWiFiIP) - 1);
      zadnjaLokalnaWiFiIP[sizeof(zadnjaLokalnaWiFiIP) - 1] = '\0';

      char log[48];
      snprintf_P(log, sizeof(log), PSTR("Mrezni most lokalna IP: %s"), ipAdresa);
      posaljiPCLog(log);
    } else {
      logirajLinijuESP(F("Mrezni most: neispravna lokalna IP: "), ipAdresa);
    }
    resetirajUlazniBuffer();
    return;
  }

  if (strncmp(ulazniBuffer, "WIFI:LCD:", 9) == 0) {
    const char* payload = ulazniBuffer + 9;
    if (payload[0] != '\0') {
      prikaziWiFiDijagnostiku(payload);

      char log[48];
      snprintf_P(log,
                 sizeof(log),
                 PSTR("Mrezni most WiFi LCD sazetak: %s"),
                 payload);
      posaljiPCLog(log);
    }
    resetirajUlazniBuffer();
    return;
  }

  if (strncmp(ulazniBuffer, "WIFI:MAC:", 9) == 0) {
    const char* macAdresa = ulazniBuffer + 9;
    if (strlen(macAdresa) == 17) {
      strncpy(zadnjaWiFiMACAdresa, macAdresa, sizeof(zadnjaWiFiMACAdresa) - 1);
      zadnjaWiFiMACAdresa[sizeof(zadnjaWiFiMACAdresa) - 1] = '\0';

      char log[48];
      snprintf_P(log,
                 sizeof(log),
                 PSTR("Mrezni most MAC adresa: %s"),
                 zadnjaWiFiMACAdresa);
      posaljiPCLog(log);
    } else {
      logirajLinijuESP(F("Mrezni most: neispravna MAC adresa: "), macAdresa);
    }
    resetirajUlazniBuffer();
    return;
  }

  if (strncmp(ulazniBuffer, "WIFI:", 5) == 0) {
    obradiWiFiLogLinijuESP(ulazniBuffer);
    resetirajUlazniBuffer();
    return;
  }

  if (strcmp(ulazniBuffer, "CFGREQ") == 0) {
    posaljiKonfiguracijuESPuNakonZahtjeva();
    resetirajUlazniBuffer();
    return;
  }

  if (strncmp(ulazniBuffer, "SETUPWIFI:", 10) == 0) {
    if (spremiSetupWiFiPostavkeIzESPa(ulazniBuffer + 10)) {
      espSerijskiPort.println(F("ACK:SETUPWIFI"));
    } else {
      espSerijskiPort.println(F("ERR:SETUPWIFI"));
    }
    resetirajUlazniBuffer();
    return;
  }

  if (strcmp(ulazniBuffer, "SETREQ:SUSTAV") == 0) {
    posaljiSustavskePostavkeESPu();
    posaljiPCLog(F("Mrezni most je zatrazio sustavske postavke toranjskog sata"));
    resetirajUlazniBuffer();
    return;
  }

  if (strcmp(ulazniBuffer, "SETREQ:STAPICI") == 0) {
    posaljiPostavkeStapicaESPu();
    posaljiPCLog(F("Mrezni most je zatrazio postavke stapica toranjskog sata"));
    resetirajUlazniBuffer();
    return;
  }

  if (strcmp(ulazniBuffer, "SETREQ:BAT") == 0) {
    posaljiBATPostavkeESPu();
    posaljiPCLog(F("Mrezni most je zatrazio BAT postavke toranjskog sata"));
    resetirajUlazniBuffer();
    return;
  }

  if (strcmp(ulazniBuffer, "SETREQ:SUNCE") == 0) {
    posaljiSuncevePostavkeESPu();
    posaljiPCLog(F("Mrezni most je zatrazio sunceve postavke toranjskog sata"));
    resetirajUlazniBuffer();
    return;
  }

  if (strcmp(ulazniBuffer, "SETREQ:MISE") == 0) {
    posaljiMisePostavkeESPu();
    posaljiPCLog(F("Mrezni most je zatrazio misne postavke toranjskog sata"));
    resetirajUlazniBuffer();
    return;
  }

  if (strcmp(ulazniBuffer, "SETREQ:BLAGDANI_NEP") == 0) {
    posaljiNepomicneBlagdaneESPu();
    posaljiPCLog(F("Mrezni most je zatrazio nepomicne blagdane toranjskog sata"));
    resetirajUlazniBuffer();
    return;
  }

  if (strcmp(ulazniBuffer, "SETREQ:BLAGDANI_POM") == 0) {
    posaljiPomicneBlagdaneESPu();
    posaljiPCLog(F("Mrezni most je zatrazio pomicne blagdane toranjskog sata"));
    resetirajUlazniBuffer();
    return;
  }

  if (strncmp(ulazniBuffer, "SETCFG:SUSTAV|", 14) == 0) {
    if (spremiSustavskePostavkeIzESPa(ulazniBuffer + 14)) {
      posaljiSustavskePostavkeESPu();
      espSerijskiPort.println(F("ACK:SETCFG"));
    } else {
      espSerijskiPort.println(F("ERR:SETCFG"));
      posaljiPCLog(F("Mrezni most je poslao neispravne sustavske postavke toranjskog sata"));
    }
    resetirajUlazniBuffer();
    return;
  }

  if (strncmp(ulazniBuffer, "SETCFG:STAPICI|", 15) == 0) {
    if (spremiPostavkeStapicaIzESPa(ulazniBuffer + 15)) {
      posaljiPostavkeStapicaESPu();
      espSerijskiPort.println(F("ACK:SETCFG"));
    } else {
      espSerijskiPort.println(F("ERR:SETCFG"));
      posaljiPCLog(F("Mrezni most je poslao neispravne postavke stapica toranjskog sata"));
    }
    resetirajUlazniBuffer();
    return;
  }

  if (strncmp(ulazniBuffer, "SETCFG:BAT|", 11) == 0) {
    if (spremiBATPostavkeIzESPa(ulazniBuffer + 11)) {
      posaljiBATPostavkeESPu();
      espSerijskiPort.println(F("ACK:SETCFG"));
    } else {
      espSerijskiPort.println(F("ERR:SETCFG"));
      posaljiPCLog(F("Mrezni most je poslao neispravne BAT postavke toranjskog sata"));
    }
    resetirajUlazniBuffer();
    return;
  }

  if (strncmp(ulazniBuffer, "SETCFG:SUNCE|", 13) == 0) {
    if (spremiSuncevePostavkeIzESPa(ulazniBuffer + 13)) {
      posaljiSuncevePostavkeESPu();
      espSerijskiPort.println(F("ACK:SETCFG"));
    } else {
      espSerijskiPort.println(F("ERR:SETCFG"));
      posaljiPCLog(F("Mrezni most je poslao neispravne sunceve postavke toranjskog sata"));
    }
    resetirajUlazniBuffer();
    return;
  }

  if (strncmp(ulazniBuffer, "SETCFG:MISE|", 12) == 0) {
    if (spremiMisePostavkeIzESPa(ulazniBuffer + 12)) {
      posaljiMisePostavkeESPu();
      espSerijskiPort.println(F("ACK:SETCFG"));
    } else {
      espSerijskiPort.println(F("ERR:SETCFG"));
      posaljiPCLog(F("Mrezni most je poslao neispravne misne postavke toranjskog sata"));
    }
    resetirajUlazniBuffer();
    return;
  }

  if (strncmp(ulazniBuffer, "SETCFG:BLAGDANI_NEP|", 20) == 0) {
    if (spremiNepomicneBlagdaneIzESPa(ulazniBuffer + 20)) {
      posaljiNepomicneBlagdaneESPu();
      espSerijskiPort.println(F("ACK:SETCFG"));
    } else {
      espSerijskiPort.println(F("ERR:SETCFG"));
      posaljiPCLog(F("Mrezni most je poslao neispravne nepomicne blagdane toranjskog sata"));
    }
    resetirajUlazniBuffer();
    return;
  }

  if (strncmp(ulazniBuffer, "SETCFG:BLAGDANI_POM|", 20) == 0) {
    if (spremiPomicneBlagdaneIzESPa(ulazniBuffer + 20)) {
      posaljiPomicneBlagdaneESPu();
      espSerijskiPort.println(F("ACK:SETCFG"));
    } else {
      espSerijskiPort.println(F("ERR:SETCFG"));
      posaljiPCLog(F("Mrezni most je poslao neispravne pomicne blagdane toranjskog sata"));
    }
    resetirajUlazniBuffer();
    return;
  }

  if (strcmp(ulazniBuffer, "WEBCFG?") == 0) {
    espSerijskiPort.println(F("ERR:WEBCFGDISABLED"));
      posaljiPCLog(F("Web konfiguracija na mreznom mostu je onemogucena; postavke toranjskog sata uredjuju se na Megi"));
    resetirajUlazniBuffer();
    return;
  }

  if (strncmp(ulazniBuffer, "WEBCFGSET:", 10) == 0) {
    espSerijskiPort.println(F("ERR:WEBCFGDISABLED"));
      posaljiPCLog(F("Mrezni most je pokusao spremiti web postavke sata, ali je konfiguracija vracena na Megu"));
    resetirajUlazniBuffer();
    return;
  }

  if (strcmp(ulazniBuffer, "STATUS?") == 0) {
    posaljiStatusESPU();
    resetirajUlazniBuffer();
    return;
  }

  if (strncmp(ulazniBuffer, "STATUS:", 7) == 0) {
    resetirajUlazniBuffer();
    return;
  }

  if (strncmp(ulazniBuffer, "NTP:", 4) == 0) {
    const char* iso = ulazniBuffer + 4;
    DateTime ntpVrijeme;
    uint16_t ntpMilisekunde = 0;
    bool imaEksplicitanDST = false;
    bool dstAktivanIzvori = false;
    if (!parsirajNTPPayload(iso,
                            ntpVrijeme,
                            ntpMilisekunde,
                            imaEksplicitanDST,
                            dstAktivanIzvori)) {
      espSerijskiPort.println(F("ERR:NTP"));
      logirajLinijuESP(F("Neispravan NTP format: "), iso);
      resetirajUlazniBuffer();
      return;
    }

    potvrdiWiFiPovezanostAkoTreba(F("Mrezni most status mreze: spojeno (potvrda preko NTP sinkronizacije)"));

    if (jeNTPOmogucen()) {
      prioritetniNtpZahtjevNaCekanju = false;
      azurirajVrijemeIzNTP(ntpVrijeme,
                          ntpMilisekunde,
                          imaEksplicitanDST,
                          dstAktivanIzvori);
    }
    espSerijskiPort.println(F("ACK:NTP"));
    if (jeNTPOmogucen()) {
      logirajLinijuESP(F("Primljen NTP iz ESP-a: "), iso);
    } else {
      logirajLinijuESP(F("Preskocen NTP iz ESP-a jer je NTP iskljucen: "), iso);
    }

    resetirajUlazniBuffer();
    return;
  }

  if (strncmp(ulazniBuffer, "CMD:", 4) == 0) {
    const char* komanda = ulazniBuffer + 4;
    bool uspjeh = true;
    bool prepoznataKomanda = true;

    if      (strcmp(komanda, "ZVONO1_ON") == 0)       ukljuciZvono(1);
    else if (strcmp(komanda, "ZVONO1_OFF") == 0)      iskljuciZvono(1);
    else if (strcmp(komanda, "ZVONO2_ON") == 0)       ukljuciZvono(2);
    else if (strcmp(komanda, "ZVONO2_OFF") == 0)      iskljuciZvono(2);
    else if (strcmp(komanda, "GASI_SVE") == 0) {
      iskljuciObaZvonaSinkronizirano();
      zaustaviSlavljenje();
      zaustaviMrtvacko();
    }
    else if (strcmp(komanda, "OTKUCAVANJE_OFF") == 0) postaviBlokaduOtkucavanja(true);
    else if (strcmp(komanda, "OTKUCAVANJE_ON") == 0)  postaviBlokaduOtkucavanja(false);
    else if (strcmp(komanda, "SLAVLJENJE_ON") == 0)   uspjeh = pokusajZapocetiSlavljenjeBezCekanja();
    else if (strcmp(komanda, "SLAVLJENJE_OFF") == 0)  zaustaviSlavljenje();
    else if (strcmp(komanda, "MRTVACKO_ON") == 0)     uspjeh = pokusajZapocetiMrtvackoBezCekanja();
    else if (strcmp(komanda, "MRTVACKO_OFF") == 0)    zaustaviMrtvacko();
    else if (strcmp(komanda, "TIHI_ON") == 0)         postaviWebTihiRezim(true);
    else if (strcmp(komanda, "TIHI_OFF") == 0)        postaviWebTihiRezim(false);
    else if (strcmp(komanda, "SUNCE_JUTRO_ON") == 0)
      postaviSuncevDogadaj(
          SUNCEVI_DOGADAJ_JUTRO,
          true,
          dohvatiZvonoZaSuncevDogadaj(SUNCEVI_DOGADAJ_JUTRO),
          dohvatiOdgoduSuncevogDogadajaMin(SUNCEVI_DOGADAJ_JUTRO));
    else if (strcmp(komanda, "SUNCE_JUTRO_OFF") == 0)
      postaviSuncevDogadaj(
          SUNCEVI_DOGADAJ_JUTRO,
          false,
          dohvatiZvonoZaSuncevDogadaj(SUNCEVI_DOGADAJ_JUTRO),
          dohvatiOdgoduSuncevogDogadajaMin(SUNCEVI_DOGADAJ_JUTRO));
    else if (strcmp(komanda, "SUNCE_PODNE_ON") == 0)
      postaviSuncevDogadaj(
          SUNCEVI_DOGADAJ_PODNE,
          true,
          dohvatiZvonoZaSuncevDogadaj(SUNCEVI_DOGADAJ_PODNE),
          dohvatiOdgoduSuncevogDogadajaMin(SUNCEVI_DOGADAJ_PODNE));
    else if (strcmp(komanda, "SUNCE_PODNE_OFF") == 0)
      postaviSuncevDogadaj(
          SUNCEVI_DOGADAJ_PODNE,
          false,
          dohvatiZvonoZaSuncevDogadaj(SUNCEVI_DOGADAJ_PODNE),
          dohvatiOdgoduSuncevogDogadajaMin(SUNCEVI_DOGADAJ_PODNE));
    else if (strcmp(komanda, "SUNCE_VECER_ON") == 0)
      postaviSuncevDogadaj(
          SUNCEVI_DOGADAJ_VECER,
          true,
          dohvatiZvonoZaSuncevDogadaj(SUNCEVI_DOGADAJ_VECER),
          dohvatiOdgoduSuncevogDogadajaMin(SUNCEVI_DOGADAJ_VECER));
    else if (strcmp(komanda, "SUNCE_VECER_OFF") == 0)
      postaviSuncevDogadaj(
          SUNCEVI_DOGADAJ_VECER,
          false,
          dohvatiZvonoZaSuncevDogadaj(SUNCEVI_DOGADAJ_VECER),
          dohvatiOdgoduSuncevogDogadajaMin(SUNCEVI_DOGADAJ_VECER));
    else prepoznataKomanda = false;

    if (!prepoznataKomanda) {
      espSerijskiPort.println(F("ERR:CMD"));
      logirajLinijuESP(F("Nepoznata CMD naredba: "), komanda);
    } else if (uspjeh) {
      espSerijskiPort.println(F("ACK:CMD_OK"));
      logirajLinijuESP(F("Izvrsena CMD naredba: "), komanda);
    } else {
      espSerijskiPort.println(F("ERR:CMD_BUSY"));
      logirajLinijuESP(F("CMD naredba odbijena jer je toranjski sat zauzet: "), komanda);
    }

    resetirajUlazniBuffer();
    return;
  }

  if (strncmp(ulazniBuffer, "NTPLOG:", 7) == 0) {
    obradiNTPLogLinijuESP(ulazniBuffer);
    resetirajUlazniBuffer();
    return;
  }

    logirajLinijuESP(F("Mrezni most log: "), ulazniBuffer);
  resetirajUlazniBuffer();
}

void obradiESPSerijskuKomunikaciju() {
  while (espSerijskiPort.available()) {
    const char znak = static_cast<char>(espSerijskiPort.read());

    if (znak == '\r') {
      continue;
    }

    if (znak == '\n') {
      obradiESPRedak();
      continue;
    }

    if (ulazniBufferDuljina < ESP_ULAZNI_BUFFER_MAX) {
      ulazniBuffer[ulazniBufferDuljina++] = znak;
      ulazniBuffer[ulazniBufferDuljina] = '\0';
    } else {
    posaljiPCLog(F("Mrezni most RX: preduga linija, odbacujem buffer"));
      resetirajUlazniBuffer();
    }
  }

  if (wifiPovezanNaESP) {
    vrijemePrvogWiFiStatusUpitaMs = 0;
    zadnjiWiFiStatusRecoveryUpitMs = 0;
    drugiWiFiStatusUpitPoslan = false;
    return;
  }

  if (!jeWiFiOmogucen()) {
    return;
  }

  if (jeAktivnaPocetnaOdgodaWiFi()) {
    return;
  }

  const unsigned long sadaMs = millis();
  if (vrijemePrvogWiFiStatusUpitaMs == 0) {
    zatraziWiFiStatusESP();
    vrijemePrvogWiFiStatusUpitaMs = sadaMs;
    zadnjiWiFiStatusRecoveryUpitMs = sadaMs;
    return;
  }

  if (!drugiWiFiStatusUpitPoslan &&
      (sadaMs - vrijemePrvogWiFiStatusUpitaMs) >= WIFI_STATUS_DRUGI_UPIT_ODGODA_MS) {
    zatraziWiFiStatusESP();
    drugiWiFiStatusUpitPoslan = true;
    zadnjiWiFiStatusRecoveryUpitMs = sadaMs;
    return;
  }

  if ((sadaMs - zadnjiWiFiStatusRecoveryUpitMs) >= WIFI_STATUS_RECOVERY_INTERVAL_MS) {
    zatraziWiFiStatusESP();
    zadnjiWiFiStatusRecoveryUpitMs = sadaMs;
  }
}
