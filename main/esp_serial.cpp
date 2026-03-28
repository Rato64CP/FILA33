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

// Always use Serial3 (for Mega 2560 tower clock)
static HardwareSerial& espSerijskiPort = Serial3;

static const unsigned long ESP_BRZINA = 9600;
static const size_t ESP_ULAZNI_BUFFER_MAX = 256;

static char ulazniBuffer[ESP_ULAZNI_BUFFER_MAX + 1];
static size_t ulazniBufferDuljina = 0;
static bool mqttBrokerNijeKonfiguriranPrijavljen = false;
static bool ntpCekanjePrijavljeno = false;

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

void inicijalizirajESP() {
  espSerijskiPort.begin(ESP_BRZINA);
  resetirajUlazniBuffer();
  posaljiPCLog(F("ESP serijska veza inicijalizirana"));
  delay(50);
  posaljiWifiPostavkeESP();
  posaljiNTPPostavkeESP();
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

void posaljiNTPPostavkeESP() {
  espSerijskiPort.print(F("NTPCFG:"));
  espSerijskiPort.println(dohvatiNTPServer());

  char log[96];
  snprintf(log, sizeof(log), "Poslan NTP server ESP-u: %s", dohvatiNTPServer());
  posaljiPCLog(log);
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
    postaviWiFiStatus(true);
    posaljiPCLog(F("ESP WiFi status: spojeno"));
    resetirajUlazniBuffer();
    return;
  }

  if (strcmp(ulazniBuffer, "WIFI:DISCONNECTED") == 0) {
    postaviWiFiStatus(false);
    posaljiPCLog(F("ESP WiFi status: odspojeno"));
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
      azurirajVrijemeIzNTP(ntpVrijeme);
      zatraziPoravnanjeTaktaKazaljki();
      zatraziPoravnanjeTaktaPloce();
      espSerijskiPort.println(F("ACK:NTP"));
      logirajLinijuESP("Primljen NTP iz ESP-a: ", iso);

      if (!suKazaljkeUSinkronu()) {
        posaljiPCLog(F("Kazaljke nisu u sinkronu nakon NTP"));
        pokreniBudnoKorekciju();
        posaljiPCLog(F("Pokrenuta dinamicka korekcija kazaljki nakon NTP sinkronizacije"));
      } else {
        posaljiPCLog(F("Kazaljke su vec u sinkronu s vremenom nakon NTP"));
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
    else if (strcmp(komanda, "ZVONO3_ON") == 0)       ukljuciZvono(3);
    else if (strcmp(komanda, "ZVONO3_OFF") == 0)      iskljuciZvono(3);
    else if (strcmp(komanda, "ZVONO4_ON") == 0)       ukljuciZvono(4);
    else if (strcmp(komanda, "ZVONO4_OFF") == 0)      iskljuciZvono(4);
    else if (strcmp(komanda, "OTKUCAVANJE_OFF") == 0) postaviBlokaduOtkucavanja(true);
    else if (strcmp(komanda, "OTKUCAVANJE_ON") == 0)  postaviBlokaduOtkucavanja(false);
    else if (strcmp(komanda, "SLAVLJENJE_ON") == 0)   zapocniSlavljenje();
    else if (strcmp(komanda, "SLAVLJENJE_OFF") == 0)  zaustaviSlavljenje();
    else if (strcmp(komanda, "MRTVACKO_ON") == 0)     zapocniMrtvacko();
    else if (strcmp(komanda, "MRTVACKO_OFF") == 0)    zaustaviMrtvacko();
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
