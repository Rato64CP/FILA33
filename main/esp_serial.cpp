// esp_serial.cpp - Serijska veza prema mreznom mostu toranjskog sata
#include <Arduino.h>
#include <RTClib.h>
#include <stdlib.h>
#include <string.h>
#include "esp_serial.h"
#include "time_glob.h"
#include "zvonjenje.h"
#include "podesavanja_piny.h"
#include "otkucavanje.h"
#include "slavljenje_mrtvacko.h"
#include "pc_serial.h"
#include "kazaljke_sata.h"
#include "okretna_ploca.h"
#include "postavke.h"
#include "lcd_display.h"
#include "misna_automatika.h"
#include "sunceva_automatika.h"

// UART prema mreznom mostu toranjskog sata bira se kroz main/podesavanja_piny.h.
static HardwareSerial& espSerijskiPort = ESP_SERIJSKI_PORT;

static const unsigned long ESP_BRZINA = 9600;
static const size_t ESP_ULAZNI_BUFFER_MAX = 160;
static const unsigned long WIFI_POCETNA_ODGODA_NAKON_NAPAJANJA_MS = 120000UL;
static const unsigned long WIFI_STATUS_DRUGI_UPIT_ODGODA_MS = 15000UL;
static const unsigned long WIFI_STATUS_RECOVERY_INTERVAL_MS = 300000UL;
static const uint8_t NTP_SIGURNA_SEKUNDA_MIN = 12;
static const uint8_t NTP_SIGURNA_SEKUNDA_MAX = 50;

// RTC + NTP tok toranjskog sata mora ostati uskladen s time_glob.cpp:
// - boot krece iz RTC-a
// - NTP ide tek kad je mreza spremna i mehanika miruje
// - nakon povratka napajanja modem/WiFi dobivaju pocetnu odgodu
// - automatski NTP ne smije remetiti osnovni rad sata ni prikaz izvora vremena

static char ulazniBuffer[ESP_ULAZNI_BUFFER_MAX + 1];
static size_t ulazniBufferDuljina = 0;
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
static uint32_t zadnjiAutomatskiNtpZahtjevMinutniKljuc = 0;
static uint32_t zadnjiAutomatskiNtpZahtjevSatniKljuc = 0;

static bool vrijemeProslo(unsigned long sadaMs, unsigned long ciljMs) {
  return static_cast<long>(sadaMs - ciljMs) >= 0;
}

static bool jeResetNakonGubitkaNapajanja() {
  const uint8_t mcusr = MCUSR;
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

void posaljiESPKomandu(const char* komanda);
static void zatraziWiFiStatusESP();
static bool spremiSetupWiFiPostavkeIzESPa(const char* payload);
static void posaljiVrijemeESPU();

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

  char statusLinija[256];
  snprintf_P(statusLinija,
             sizeof(statusLinija),
             PSTR("STATUS:time=%s|src=%s|ok=%d|wifi=%d|mq=%d|mqen=%d|ntp=%d|hs=%d|hp=%d|ps=%d|pp=%d|sl=%d|mr=%d|ot=%d|b1=%d|b2=%d"),
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
             jeZvonoAktivno(2) ? 1 : 0);
  espSerijskiPort.println(statusLinija);
}

static void posaljiVrijemeESPU() {
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
  espSerijskiPort.print(F("TIME:"));
  espSerijskiPort.println(vrijemeIso);
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

static void logirajLinijuESP(const char* prefiks, const char* sadrzaj) {
  char log[320];
  snprintf(log, sizeof(log), "%s%s", prefiks, sadrzaj);
  posaljiPCLog(log);
}

static bool jeStatusnaLogLinijaESP(const char* linija) {
  return strncmp(linija, "NTPLOG:", 7) == 0;
}

static bool jePrepoznataESPLinija(const char* linija) {
  return strcmp(linija, "WIFI:CONNECTED") == 0 ||
         strcmp(linija, "WIFI:DISCONNECTED") == 0 ||
         strncmp(linija, "WIFI:", 5) == 0 ||
         strcmp(linija, "CFGREQ") == 0 ||
         strncmp(linija, "SETUPWIFI:", 10) == 0 ||
         strncmp(linija, "STATUS:", 7) == 0 ||
         strcmp(linija, "STATUS?") == 0 ||
         strcmp(linija, "TIME?") == 0 ||
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
  logirajLinijuESP("Mrezni most NTP: ", poruka);
}

static void obradiWiFiLogLinijuESP(const char* linija) {
  const char* poruka = linija + 5;
  while (*poruka == ' ') {
    ++poruka;
  }

  logirajLinijuESP("Mrezni most WiFi: ", poruka);
}

static void posaljiKonfiguracijuESPuNakonZahtjeva() {
  posaljiWifiPostavkeESP();
  posaljiWiFiStatusESP();
  posaljiNTPPostavkeESP();
  zatraziWiFiStatusESP();
  posaljiPCLog(F("Mrezni most zatrazio osvjezavanje konfiguracije"));
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

  char log[96];
  snprintf(log, sizeof(log), "Setup WiFi: spremljen novi SSID=%s preko mreznog mosta", ssid);
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
  zadnjiAutomatskiNtpZahtjevMinutniKljuc = 0;
  zadnjiAutomatskiNtpZahtjevSatniKljuc = 0;
  postaviWiFiStatus(false);
  posaljiPCLog(F("Serijska veza prema mreznom mostu inicijalizirana"));
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

  char log[96];
  snprintf(log, sizeof(log), "Poslan NTP server mreznom mostu: %s", dohvatiNTPServer());
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

void posaljiNTPZahtjevESP() {
  const DateTime sada = dohvatiTrenutnoVrijeme();
  zadnjiAutomatskiNtpZahtjevMinutniKljuc = sada.unixtime() / 60UL;
  zadnjiAutomatskiNtpZahtjevSatniKljuc = sada.unixtime() / 3600UL;
  espSerijskiPort.println(F("NTPREQ:SYNC"));
  posaljiPCLog(F("Poslan zahtjev mreznom mostu za NTP osvjezavanje toranjskog sata"));
}

void zatraziPrioritetnuNTPSinkronizaciju() {
  prioritetniNtpZahtjevNaCekanju = true;
  zadnjiAutomatskiNtpZahtjevMinutniKljuc = 0;
  zadnjiAutomatskiNtpZahtjevSatniKljuc = 0;
  posaljiPCLog(F("NTP: zabiljezen prioritetni zahtjev nakon rucne promjene vremena"));
}

void obradiAutomatskiNTPZahtjevESP() {
  if (!jeNTPOmogucen() || !jeWiFiPovezanNaESP() || jeAktivnaPocetnaOdgodaWiFi()) {
    return;
  }

  const DateTime sada = dohvatiTrenutnoVrijeme();
  if (!jeSigurnaMinutaZaNTPZahtjev(sada) ||
      !jeSiguranProzorZaNTPZahtjev(sada) ||
      !mehanikaTornjskogSataMirujeZaNTP()) {
    return;
  }

  const uint32_t minutniKljuc = sada.unixtime() / 60UL;
  const uint32_t satniKljuc = sada.unixtime() / 3600UL;

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

static bool parsirajISOVrijeme(const char* iso, DateTime& dt) {
  const int osnovnaDuljina = 19;
  const size_t duljina = strlen(iso);
  const bool imaZuluneSufiks = (duljina == 20 && iso[19] == 'Z');

  if (!(duljina == osnovnaDuljina || imaZuluneSufiks)) return false;
  if (iso[4] != '-' || iso[7] != '-' || iso[10] != 'T' ||
      iso[13] != ':' || iso[16] != ':') {
    return false;
  }

  for (int i = 0; i < osnovnaDuljina; ++i) {
    if (i == 4 || i == 7 || i == 10 || i == 13 || i == 16) continue;
    if (!isDigit(iso[i])) return false;
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

  if (godina < 2024 || mjesec < 1 || mjesec > 12) return false;

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
                               bool& imaEksplicitanDST,
                               bool& dstAktivanIzvori) {
  imaEksplicitanDST = false;
  dstAktivanIzvori = false;

  const char* dstSuffix = strstr(payload, ";DST=");
  if (dstSuffix == nullptr) {
    return parsirajISOVrijeme(payload, dt);
  }

  const size_t isoDuljina = static_cast<size_t>(dstSuffix - payload);
  if (!(isoDuljina == 19 || (isoDuljina == 20 && payload[19] == 'Z'))) {
    return false;
  }

  if ((dstSuffix[5] != '0' && dstSuffix[5] != '1') || dstSuffix[6] != '\0') {
    return false;
  }

  char isoBuffer[21];
  if (isoDuljina >= sizeof(isoBuffer)) {
    return false;
  }

  memcpy(isoBuffer, payload, isoDuljina);
  isoBuffer[isoDuljina] = '\0';
  if (!parsirajISOVrijeme(isoBuffer, dt)) {
    return false;
  }

  imaEksplicitanDST = true;
  dstAktivanIzvori = (dstSuffix[5] == '1');
  return true;
}

static bool jeStrogiNTPPayload(const char* payload) {
  DateTime ignorirano;
  bool imaEksplicitanDST = false;
  bool dstAktivanIzvori = false;
  return parsirajNTPPayload(payload, ignorirano, imaEksplicitanDST, dstAktivanIzvori);
}

static void obradiESPRedak() {
  trimBuffer();

  if (ulazniBufferDuljina == 0) {
    resetirajUlazniBuffer();
    return;
  }

  if (!jePrepoznataESPLinija(ulazniBuffer) && !jeStatusnaLogLinijaESP(ulazniBuffer)) {
    logirajLinijuESP("Mrezni most linija: ", ulazniBuffer);
  }

  if (strcmp(ulazniBuffer, "WIFI:CONNECTED") == 0) {
    const bool bioSpojen = wifiPovezanNaESP;
    wifiPovezanNaESP = true;
    postaviWiFiStatus(true);
    if (!bioSpojen) {
      prioritetniNtpZahtjevNaCekanju = true;
      zadnjiAutomatskiNtpZahtjevMinutniKljuc = 0;
      zadnjiAutomatskiNtpZahtjevSatniKljuc = 0;
    }
    posaljiPCLog(F("Mrezni most status mreze: spojeno"));
    resetirajUlazniBuffer();
    return;
  }

  if (strcmp(ulazniBuffer, "WIFI:DISCONNECTED") == 0) {
    wifiPovezanNaESP = false;
    postaviWiFiStatus(false);
    prioritetniNtpZahtjevNaCekanju = false;
    zadnjiAutomatskiNtpZahtjevMinutniKljuc = 0;
    zadnjiAutomatskiNtpZahtjevSatniKljuc = 0;
    posaljiPCLog(F("Mrezni most status mreze: odspojeno"));
    resetirajUlazniBuffer();
    return;
  }

  if (strncmp(ulazniBuffer, "WIFI:LOCAL_IP:", 14) == 0) {
    const char* ipAdresa = ulazniBuffer + 14;
    if (!wifiPovezanNaESP) {
      wifiPovezanNaESP = true;
      postaviWiFiStatus(true);
      prioritetniNtpZahtjevNaCekanju = true;
      zadnjiAutomatskiNtpZahtjevMinutniKljuc = 0;
      zadnjiAutomatskiNtpZahtjevSatniKljuc = 0;
      posaljiPCLog(F("Mrezni most status mreze: spojeno (potvrda preko lokalne IP)"));
    }

    if (jeValjanaIPv4AdresaZaLCD(ipAdresa)) {
      strncpy(zadnjaLokalnaWiFiIP, ipAdresa, sizeof(zadnjaLokalnaWiFiIP) - 1);
      zadnjaLokalnaWiFiIP[sizeof(zadnjaLokalnaWiFiIP) - 1] = '\0';
      prikaziLokalnuWiFiIP(ipAdresa);

      char log[64];
      snprintf(log, sizeof(log), "Mrezni most lokalna IP: %s", ipAdresa);
      posaljiPCLog(log);
    } else {
      logirajLinijuESP("Mrezni most: neispravna lokalna IP: ", ipAdresa);
    }
    resetirajUlazniBuffer();
    return;
  }

  if (strncmp(ulazniBuffer, "WIFI:MAC:", 9) == 0) {
    const char* macAdresa = ulazniBuffer + 9;
    if (strlen(macAdresa) == 17) {
      strncpy(zadnjaWiFiMACAdresa, macAdresa, sizeof(zadnjaWiFiMACAdresa) - 1);
      zadnjaWiFiMACAdresa[sizeof(zadnjaWiFiMACAdresa) - 1] = '\0';

      char log[64];
      snprintf(log, sizeof(log), "Mrezni most MAC adresa: %s", zadnjaWiFiMACAdresa);
      posaljiPCLog(log);
    } else {
      logirajLinijuESP("Mrezni most: neispravna MAC adresa: ", macAdresa);
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

  if (strcmp(ulazniBuffer, "TIME?") == 0) {
    posaljiVrijemeESPU();
    resetirajUlazniBuffer();
    return;
  }

  if (strncmp(ulazniBuffer, "STATUS:", 7) == 0) {
    resetirajUlazniBuffer();
    return;
  }

  if (strncmp(ulazniBuffer, "NTP:", 4) == 0) {
    const char* iso = ulazniBuffer + 4;
    if (!jeStrogiNTPPayload(iso)) {
      espSerijskiPort.println(F("ERR:NTP"));
      logirajLinijuESP("Neispravan NTP format: ", iso);
      resetirajUlazniBuffer();
      return;
    }

    DateTime ntpVrijeme;
    bool imaEksplicitanDST = false;
    bool dstAktivanIzvori = false;
    if (parsirajNTPPayload(iso, ntpVrijeme, imaEksplicitanDST, dstAktivanIzvori)) {
      if (!wifiPovezanNaESP) {
        wifiPovezanNaESP = true;
        postaviWiFiStatus(true);
        prioritetniNtpZahtjevNaCekanju = true;
        zadnjiAutomatskiNtpZahtjevMinutniKljuc = 0;
        zadnjiAutomatskiNtpZahtjevSatniKljuc = 0;
        posaljiPCLog(F("Mrezni most status mreze: spojeno (potvrda preko NTP sinkronizacije)"));
      }

      if (jeNTPOmogucen()) {
        prioritetniNtpZahtjevNaCekanju = false;
        azurirajVrijemeIzNTP(ntpVrijeme, imaEksplicitanDST, dstAktivanIzvori);
      }
      espSerijskiPort.println(F("ACK:NTP"));
      if (jeNTPOmogucen()) {
        logirajLinijuESP("Primljen NTP iz ESP-a: ", iso);
      } else {
        logirajLinijuESP("Preskocen NTP iz ESP-a jer je NTP iskljucen: ", iso);
      }
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
      iskljuciZvono(1);
      iskljuciZvono(2);
      zaustaviSlavljenje();
      zaustaviMrtvacko();
    }
    else if (strcmp(komanda, "OTKUCAVANJE_OFF") == 0) postaviBlokaduOtkucavanja(true);
    else if (strcmp(komanda, "OTKUCAVANJE_ON") == 0)  postaviBlokaduOtkucavanja(false);
    else if (strcmp(komanda, "SLAVLJENJE_ON") == 0)   zapocniSlavljenje();
    else if (strcmp(komanda, "SLAVLJENJE_OFF") == 0)  zaustaviSlavljenje();
    else if (strcmp(komanda, "MRTVACKO_ON") == 0)     zapocniMrtvacko();
    else if (strcmp(komanda, "MRTVACKO_OFF") == 0)    zaustaviMrtvacko();
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
    else if (strcmp(komanda, "MISA_RADNA") == 0)     uspjeh = pokreniESPMisnuNajavuRadniDan();
    else if (strcmp(komanda, "MISA_NEDJELJA") == 0)  uspjeh = pokreniESPMisnuNajavuNedjelja();
    else if (strcmp(komanda, "MISA_BLAGDAN") == 0)   uspjeh = pokreniESPMisnuNajavuBlagdan();
    else prepoznataKomanda = false;

    if (!prepoznataKomanda) {
      espSerijskiPort.println(F("ERR:CMD"));
      logirajLinijuESP("Nepoznata CMD naredba: ", komanda);
    } else if (uspjeh) {
      espSerijskiPort.println(F("ACK:CMD_OK"));
      logirajLinijuESP("Izvrsena CMD naredba: ", komanda);
    } else {
      espSerijskiPort.println(F("ERR:CMD_BUSY"));
      logirajLinijuESP("CMD naredba odbijena jer je toranjski sat zauzet: ", komanda);
    }

    resetirajUlazniBuffer();
    return;
  }

  if (strncmp(ulazniBuffer, "NTPLOG:", 7) == 0) {
    obradiNTPLogLinijuESP(ulazniBuffer);
    resetirajUlazniBuffer();
    return;
  }

    logirajLinijuESP("Mrezni most log: ", ulazniBuffer);
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
