// esp_serial.cpp
#include <Arduino.h>
#include <RTClib.h>
#include <stdlib.h>
#include <string.h>
#include "esp_serial.h"
#include "time_glob.h"
#include "zvonjenje.h"
#include "podesavanja_piny.h"
#include "otkucavanje.h"
#include "pc_serial.h"
#include "kazaljke_sata.h"
#include "okretna_ploca.h"
#include "postavke.h"
#include "mqtt_handler.h"
#include "lcd_display.h"
#include "dcf_sync.h"

// Always use Serial3 (for Mega 2560 tower clock)
static HardwareSerial& espSerijskiPort = Serial3;

static const unsigned long ESP_BRZINA = 9600;
static const size_t ESP_ULAZNI_BUFFER_MAX = 256;

static char ulazniBuffer[ESP_ULAZNI_BUFFER_MAX + 1];
static size_t ulazniBufferDuljina = 0;
static bool mqttBrokerNijeKonfiguriranPrijavljen = false;
static bool ntpCekanjePrijavljeno = false;
static bool wifiPovezanNaESP = false;

void posaljiESPKomandu(const char* komanda);
void posaljiESPKomandu(const String& komanda);

static bool parsirajWebMQTTPayload(const char* payload,
                                   bool& omogucen,
                                   String& broker,
                                   uint16_t& port,
                                   String& korisnik,
                                   String& lozinka) {
  if (payload == nullptr) {
    return false;
  }

  String tekst = String(payload);
  const int g1 = tekst.indexOf('|');
  const int g2 = (g1 >= 0) ? tekst.indexOf('|', g1 + 1) : -1;
  const int g3 = (g2 >= 0) ? tekst.indexOf('|', g2 + 1) : -1;
  const int g4 = (g3 >= 0) ? tekst.indexOf('|', g3 + 1) : -1;
  if (g1 <= 0 || g2 <= g1 || g3 <= g2 || g4 <= g3) {
    return false;
  }

  const String omogucenTekst = tekst.substring(0, g1);
  broker = tekst.substring(g1 + 1, g2);
  const String portTekst = tekst.substring(g2 + 1, g3);
  korisnik = tekst.substring(g3 + 1, g4);
  lozinka = tekst.substring(g4 + 1);

  broker.trim();
  korisnik.trim();
  lozinka.trim();

  if (!(omogucenTekst == "0" || omogucenTekst == "1")) {
    return false;
  }
  omogucen = (omogucenTekst == "1");

  if (broker.length() == 0 || broker.length() >= 40) {
    return false;
  }
  if (korisnik.length() >= 33 || lozinka.length() >= 33) {
    return false;
  }

  const long procitaniPort = portTekst.toInt();
  if (procitaniPort <= 0 || procitaniPort > 65535) {
    return false;
  }
  port = static_cast<uint16_t>(procitaniPort);
  return true;
}

static void primijeniWebMQTTKonfiguraciju(const char* payload) {
  bool omogucen = false;
  uint16_t port = 1883;
  String broker = "";
  String korisnik = "";
  String lozinka = "";

  if (!parsirajWebMQTTPayload(payload, omogucen, broker, port, korisnik, lozinka)) {
    espSerijskiPort.println(F("ERR:WEBMQTT"));
    posaljiPCLog(F("WEB MQTT: neispravan payload iz ESP-a"));
    return;
  }

  if (lozinka.length() == 0) {
    lozinka = String(dohvatiMQTTLozinku());
  }

  postaviMQTTOmogucen(omogucen);
  postaviMQTTPodatke(broker.c_str(), port, korisnik.c_str(), lozinka.c_str());
  posaljiMQTTPostavkeESP();

  if (omogucen) {
    char komanda[160];
    snprintf(komanda,
             sizeof(komanda),
             "MQTT:CONNECT|%s|%u|%s|%s",
             dohvatiMQTTBroker(),
             dohvatiMQTTPort(),
             dohvatiMQTTKorisnika(),
             dohvatiMQTTLozinku());
    posaljiESPKomandu(komanda);
  } else {
    posaljiESPKomandu("MQTT:DISCONNECT");
  }

  espSerijskiPort.println(F("ACK:WEBMQTT"));
  char log[128];
  snprintf(log,
           sizeof(log),
           "WEB MQTT: spremljeno %s @%s:%u korisnik=%s",
           omogucen ? "ON" : "OFF",
           dohvatiMQTTBroker(),
           dohvatiMQTTPort(),
           dohvatiMQTTKorisnika());
  posaljiPCLog(log);
}

static void posaljiStatusESPU() {
  const DateTime sada = dohvatiTrenutnoVrijeme();
  char vrijemeIso[21];
  snprintf(vrijemeIso,
           sizeof(vrijemeIso),
           "%04d-%02d-%02dT%02d:%02d:%02d",
           sada.year(),
           sada.month(),
           sada.day(),
           sada.hour(),
           sada.minute(),
           sada.second());

  char statusLinija[256];
  snprintf(statusLinija,
           sizeof(statusLinija),
           "STATUS:time=%s|src=%s|ok=%d|wifi=%d|mq=%d|mqen=%d|ntp=%d|dcf=%d|dcfr=%d|hs=%d|hp=%d|ps=%d|pp=%d|sl=%d|mr=%d|ot=%d|b1=%d|b2=%d",
           vrijemeIso,
           dohvatiOznakuIzvoraVremena(),
           jeVrijemePotvrdjenoZaAutomatiku() ? 1 : 0,
           jeWiFiPovezanNaESP() ? 1 : 0,
           jeMQTTPovezan() ? 1 : 0,
           jeMQTTOmogucen() ? 1 : 0,
           jeNTPOmogucen() ? 1 : 0,
           jeDCFOmogucen() ? 1 : 0,
           jeDCFSinkronizacijaUTijeku() ? 1 : 0,
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

static bool jeBucnaMQTTStatusnaLinija(const char* linija) {
  return strcmp(linija, "MQTT:CONNECTED") == 0 ||
         strcmp(linija, "MQTT:DISCONNECTED") == 0 ||
         strncmp(linija, "MQTTLOG:", 8) == 0 ||
         strncmp(linija, "NTPLOG:", 7) == 0;
}

static bool jePrepoznataESPLinija(const char* linija) {
  return strcmp(linija, "WIFI:CONNECTED") == 0 ||
         strcmp(linija, "WIFI:DISCONNECTED") == 0 ||
         strncmp(linija, "WIFI:", 5) == 0 ||
         strcmp(linija, "CFGREQ") == 0 ||
         strncmp(linija, "STATUS:", 7) == 0 ||
         strcmp(linija, "STATUS?") == 0 ||
         strncmp(linija, "WEBMQTT:", 8) == 0 ||
         strncmp(linija, "NTP:", 4) == 0 ||
         strncmp(linija, "CMD:", 4) == 0 ||
         strncmp(linija, "MQTTLOG:", 8) == 0 ||
         strncmp(linija, "NTPLOG:", 7) == 0 ||
         strncmp(linija, "MQTT:", 5) == 0;
}

static void obradiMQTTLogLinijuESP(const char* linija) {
  const char* poruka = linija + 8;
  while (*poruka == ' ') {
    ++poruka;
  }

  if (strcmp(poruka, "broker jos nije konfiguriran") == 0) {
    if (!mqttBrokerNijeKonfiguriranPrijavljen) {
      posaljiPCLog(F("ESP MQTT: broker nije konfiguriran"));
      mqttBrokerNijeKonfiguriranPrijavljen = true;
    }
    return;
  }

  mqttBrokerNijeKonfiguriranPrijavljen = false;
  logirajLinijuESP("ESP MQTT: ", poruka);
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
      posaljiPCLog(F("ESP NTP: jos nije postavljeno vrijeme"));
      ntpCekanjePrijavljeno = true;
    }
    return;
  }

  ntpCekanjePrijavljeno = false;
  logirajLinijuESP("ESP NTP: ", poruka);
}

static void obradiWiFiLogLinijuESP(const char* linija) {
  const char* poruka = linija + 5;
  while (*poruka == ' ') {
    ++poruka;
  }

  logirajLinijuESP("ESP WiFi: ", poruka);
}

static void posaljiKonfiguracijuESPuNakonZahtjeva() {
  posaljiWifiPostavkeESP();
  posaljiWiFiStatusESP();
  posaljiNTPPostavkeESP();
  posaljiMQTTPostavkeESP();
  posaljiPCLog(F("ESP zatrazio osvjezavanje konfiguracije"));
}

void inicijalizirajESP() {
  espSerijskiPort.begin(ESP_BRZINA);
  resetirajUlazniBuffer();
  wifiPovezanNaESP = false;
  postaviWiFiStatus(false);
  posaljiPCLog(F("ESP serijska veza inicijalizirana"));
  delay(50);
  posaljiWifiPostavkeESP();
  posaljiWiFiStatusESP();
  posaljiNTPPostavkeESP();
  posaljiMQTTPostavkeESP();
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

  posaljiPCLog(F("Poslane WiFi postavke ESP-u"));
}

void posaljiWiFiStatusESP() {
  espSerijskiPort.print(F("WIFIEN:"));
  espSerijskiPort.println(jeWiFiOmogucen() ? '1' : '0');
  posaljiPCLog(jeWiFiOmogucen()
                   ? F("Poslana naredba ESP-u: WiFi ukljucen")
                   : F("Poslana naredba ESP-u: WiFi iskljucen"));
}

void posaljiNTPPostavkeESP() {
  espSerijskiPort.print(F("NTPCFG:"));
  espSerijskiPort.println(dohvatiNTPServer());

  char log[96];
  snprintf(log, sizeof(log), "Poslan NTP server ESP-u: %s", dohvatiNTPServer());
  posaljiPCLog(log);
}

void posaljiMQTTPostavkeESP() {
  char komanda[128];
  snprintf(komanda,
           sizeof(komanda),
           "MQTTCFG:%d|%s|%u|%s",
           jeMQTTOmogucen() ? 1 : 0,
           dohvatiMQTTBroker(),
           dohvatiMQTTPort(),
           dohvatiMQTTKorisnika());
  espSerijskiPort.println(komanda);
  posaljiPCLog(F("Poslane MQTT postavke ESP-u za web prikaz"));
}

void posaljiNTPZahtjevESP() {
  espSerijskiPort.println(F("NTPREQ:SYNC"));
  posaljiPCLog(F("Poslan zahtjev ESP-u za rucno NTP osvjezavanje toranjskog sata"));
}

void posaljiESPKomandu(const char* komanda) {
  if (strncmp(komanda, "MQTT:CONNECT|", 13) == 0 ||
      strcmp(komanda, "MQTT:DISCONNECT") == 0) {
    mqttBrokerNijeKonfiguriranPrijavljen = false;
  }
  espSerijskiPort.println(komanda);
}

void posaljiESPKomandu(const String& komanda) {
  espSerijskiPort.println(komanda);
}

bool jeWiFiPovezanNaESP() {
  return wifiPovezanNaESP;
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

static bool jeStrogiNTPPayload(const char* payload) {
  DateTime ignorirano;
  return parsirajISOVrijeme(payload, ignorirano);
}

static void obradiESPRedak() {
  trimBuffer();

  if (ulazniBufferDuljina == 0) {
    resetirajUlazniBuffer();
    return;
  }

  if (!jePrepoznataESPLinija(ulazniBuffer) && !jeBucnaMQTTStatusnaLinija(ulazniBuffer)) {
    logirajLinijuESP("ESP linija: ", ulazniBuffer);
  }

  if (strcmp(ulazniBuffer, "WIFI:CONNECTED") == 0) {
    wifiPovezanNaESP = true;
    postaviWiFiStatus(true);
    posaljiPCLog(F("ESP WiFi status: spojeno"));
    resetirajUlazniBuffer();
    return;
  }

  if (strcmp(ulazniBuffer, "WIFI:DISCONNECTED") == 0) {
    wifiPovezanNaESP = false;
    postaviWiFiStatus(false);
    posaljiPCLog(F("ESP WiFi status: odspojeno"));
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

  if (strcmp(ulazniBuffer, "STATUS?") == 0) {
    posaljiStatusESPU();
    resetirajUlazniBuffer();
    return;
  }

  if (strncmp(ulazniBuffer, "STATUS:", 7) == 0) {
    resetirajUlazniBuffer();
    return;
  }

  if (strncmp(ulazniBuffer, "WEBMQTT:", 8) == 0) {
    primijeniWebMQTTKonfiguraciju(ulazniBuffer + 8);
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
    if (parsirajISOVrijeme(iso, ntpVrijeme)) {
      if (jeNTPOmogucen()) {
        azurirajVrijemeIzNTP(ntpVrijeme);
        zatraziPoravnanjeTaktaKazaljki();
        zatraziPoravnanjeTaktaPloce();
      }
      espSerijskiPort.println(F("ACK:NTP"));
      if (jeNTPOmogucen()) {
        logirajLinijuESP("Primljen NTP iz ESP-a: ", iso);
      } else {
        logirajLinijuESP("Preskocen NTP iz ESP-a jer je NTP iskljucen: ", iso);
      }

      if (jeNTPOmogucen() && imaKazaljkeSata()) {
        if (!suKazaljkeUSinkronu()) {
          posaljiPCLog(F("Kazaljke nisu u sinkronu nakon NTP"));
          pokreniBudnoKorekciju();
          posaljiPCLog(F("Pokrenuta dinamicka korekcija kazaljki nakon NTP sinkronizacije"));
        } else {
          posaljiPCLog(F("Kazaljke su vec u sinkronu s vremenom nakon NTP"));
        }
      }
    }

    resetirajUlazniBuffer();
    return;
  }

  if (strncmp(ulazniBuffer, "CMD:", 4) == 0) {
    const char* komanda = ulazniBuffer + 4;
    bool uspjeh = true;

    if      (strcmp(komanda, "ZVONO1_ON") == 0)       ukljuciZvono(1);
    else if (strcmp(komanda, "ZVONO1_OFF") == 0)      iskljuciZvono(1);
    else if (strcmp(komanda, "ZVONO2_ON") == 0)       ukljuciZvono(2);
    else if (strcmp(komanda, "ZVONO2_OFF") == 0)      iskljuciZvono(2);
    else if (strcmp(komanda, "OTKUCAVANJE_OFF") == 0) postaviBlokaduOtkucavanja(true);
    else if (strcmp(komanda, "OTKUCAVANJE_ON") == 0)  postaviBlokaduOtkucavanja(false);
    else if (strcmp(komanda, "SLAVLJENJE_ON") == 0)   zapocniSlavljenje();
    else if (strcmp(komanda, "SLAVLJENJE_OFF") == 0)  zaustaviSlavljenje();
    else if (strcmp(komanda, "MRTVACKO_ON") == 0)     zapocniMrtvacko();
    else if (strcmp(komanda, "MRTVACKO_OFF") == 0)    zaustaviMrtvacko();
    else if (strcmp(komanda, "DCF_START") == 0)       pokreniRucniDCFPrijem();
    else uspjeh = false;

    if (uspjeh) {
      espSerijskiPort.println(F("ACK:CMD_OK"));
      logirajLinijuESP("Izvrsena CMD naredba: ", komanda);
    } else {
      espSerijskiPort.println(F("ERR:CMD"));
      logirajLinijuESP("Nepoznata CMD naredba: ", komanda);
    }

    resetirajUlazniBuffer();
    return;
  }

  if (strncmp(ulazniBuffer, "MQTTLOG:", 8) == 0) {
    obradiMQTTLogLinijuESP(ulazniBuffer);
    resetirajUlazniBuffer();
    return;
  }

  if (strncmp(ulazniBuffer, "NTPLOG:", 7) == 0) {
    obradiNTPLogLinijuESP(ulazniBuffer);
    resetirajUlazniBuffer();
    return;
  }

  if (strncmp(ulazniBuffer, "MQTT:", 5) == 0) {
    obradiMQTTLinijuIzESPa(ulazniBuffer);
    resetirajUlazniBuffer();
    return;
  }

  logirajLinijuESP("ESP LOG: ", ulazniBuffer);
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
      posaljiPCLog(F("ESP RX: preduga linija, odbacujem buffer"));
      resetirajUlazniBuffer();
    }
  }
}
