// esp_serial.cpp
#include <Arduino.h>
#include <RTClib.h>
#include "esp_serial.h"
#include "time_glob.h"
#include "zvonjenje.h"
#include "podesavanja_piny.h"
#include "otkucavanje.h"
#include "pc_serial.h"
#include "kazaljke_sata.h"
#include "postavke.h"
#include "mqtt_handler.h"

// Always use Serial3 (for Mega 2560 tower clock)
static HardwareSerial &espSerijskiPort = Serial3;

static const unsigned long ESP_BRZINA = 9600;
static const size_t ESP_ULAZNI_BUFFER_MAX = 256;

String ulazniBuffer = "";
int zadnjiPrihvaceniNtpSatniKljuc = -1;

void inicijalizirajESP() {
  espSerijskiPort.begin(ESP_BRZINA);
  ulazniBuffer.reserve(128);
  posaljiPCLog(F("ESP serijska veza inicijalizirana"));
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

void posaljiESPKomandu(const String& komanda) {
  espSerijskiPort.println(komanda);
}

static bool parsirajISOVrijeme(const String& iso, DateTime& dt) {
  const int osnovnaDuljina = 19;
  bool imaZuluneSufiks = iso.length() == 20 && iso.charAt(19) == 'Z';

  if (!(iso.length() == osnovnaDuljina || imaZuluneSufiks)) return false;
  if (iso.charAt(4) != '-' || iso.charAt(7) != '-' || iso.charAt(10) != 'T' ||
      iso.charAt(13) != ':' || iso.charAt(16) != ':') {
    return false;
  }

  for (int i = 0; i < osnovnaDuljina; ++i) {
    if (i == 4 || i == 7 || i == 10 || i == 13 || i == 16) continue;
    if (!isDigit(iso.charAt(i))) return false;
  }

  int godina  = iso.substring(0, 4).toInt();
  int mjesec  = iso.substring(5, 7).toInt();
  int dan     = iso.substring(8, 10).toInt();
  int sat     = iso.substring(11, 13).toInt();
  int minuta  = iso.substring(14, 16).toInt();
  int sekunda = iso.substring(17, 19).toInt();

  if (godina < 2024 || mjesec < 1 || mjesec > 12) return false;

  static const uint8_t daniUMjesecu[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  bool prijestupnaGodina = ((godina % 4 == 0) && (godina % 100 != 0)) || (godina % 400 == 0);
  int maxDana = daniUMjesecu[mjesec - 1];
  if (mjesec == 2 && prijestupnaGodina) {
    maxDana = 29;
  }

  bool poljaIspravna =
      dan >= 1 && dan <= maxDana &&
      sat >= 0 && sat <= 23 && minuta >= 0 && minuta <= 59 && sekunda >= 0 && sekunda <= 59;
  if (!poljaIspravna) return false;

  dt = DateTime(godina, mjesec, dan, sat, minuta, sekunda);
  return true;
}

static bool jeStrogiNTPPayload(const String& payload) {
  DateTime ignorirano;
  return parsirajISOVrijeme(payload, ignorirano);
}

static int izracunajSatniKljuc(const DateTime& vrijeme) {
  return vrijeme.year() * 1000000L +
         vrijeme.month() * 10000L +
         vrijeme.day() * 100L +
         vrijeme.hour();
}

void obradiESPSerijskuKomunikaciju() {
  while (espSerijskiPort.available()) {
    char znak = espSerijskiPort.read();

    // Skip \r, lines end with \n

    if (znak == '\r') {
      continue;
    }

    if (znak == '\n') {
      ulazniBuffer.trim();

      if (ulazniBuffer.length() > 0) {
        // Log entire line
        posaljiPCLog(String(F("ESP linija: ")) + ulazniBuffer);
      } else {
        ulazniBuffer = "";
        continue;
      }

      // NTP message
      if (ulazniBuffer.startsWith("NTP:")) {
        String iso = ulazniBuffer.substring(4);
        if (!jeStrogiNTPPayload(iso)) {
          espSerijskiPort.println(F("ERR:NTP"));
          posaljiPCLog(String(F("Neispravan NTP format: ")) + iso);
          ulazniBuffer = "";
          continue;
        }

        DateTime ntpVrijeme;
        if (parsirajISOVrijeme(iso, ntpVrijeme)) {
          const int satniKljuc = izracunajSatniKljuc(ntpVrijeme);
          if (ntpVrijeme.minute() != 0) {
            espSerijskiPort.println(F("ACK:NTP"));
            String log = F("NTP preskocen: minuta nije 00 (");
            log += iso;
            log += ')';
            posaljiPCLog(log);
          } else if (satniKljuc == zadnjiPrihvaceniNtpSatniKljuc) {
            espSerijskiPort.println(F("ACK:NTP"));
            String log = F("NTP preskocen: vec prihvacen za isti sat (");
            log += iso;
            log += ')';
            posaljiPCLog(log);
          } else {
            azurirajVrijemeIzNTP(ntpVrijeme);
            zadnjiPrihvaceniNtpSatniKljuc = satniKljuc;
            espSerijskiPort.println(F("ACK:NTP"));
            posaljiPCLog(String(F("Primljen NTP iz ESP-a (satna sinkronizacija): ")) + iso);

            // Check and start dynamic hand correction after new NTP sync
            if (!suKazaljkeUSinkronu()) {
              posaljiPCLog(F("Kazaljke nisu u sinkronu nakon NTP"));
              pokreniBudnoKorekciju();
              posaljiPCLog(F("Pokrenuta dinamička korekcija kazaljki nakon NTP sinkronizacije"));
            } else {
              posaljiPCLog(F("Kazaljke su vec u sinkronu s vremenom nakon NTP"));
            }
          }
        }
      }
      // CMD message
      else if (ulazniBuffer.startsWith("CMD:")) {
        String komanda = ulazniBuffer.substring(4);
        bool uspjeh = true;

        // Fixed: Removed corrupted UTF-8 characters, using ASCII names
        if      (komanda == "ZVONO1_ON")       ukljuciZvono(1);
        else if (komanda == "ZVONO1_OFF")      iskljuciZvono(1);
        else if (komanda == "ZVONO2_ON")       ukljuciZvono(2);
        else if (komanda == "ZVONO2_OFF")      iskljuciZvono(2);
        else if (komanda == "OTKUCAVANJE_OFF") postaviBlokaduOtkucavanja(true);
        else if (komanda == "OTKUCAVANJE_ON")  postaviBlokaduOtkucavanja(false);
        else if (komanda == "SLAVLJENJE_ON")   zapocniSlavljenje();
        else if (komanda == "SLAVLJENJE_OFF")  zaustaviSlavljenje();
        else if (komanda == "MRTVACKO_ON")     zapocniMrtvacko();
        else if (komanda == "MRTVACKO_OFF")    zaustaviMrtvacko();
        else uspjeh = false;

        if (uspjeh) {
          espSerijskiPort.println(F("ACK:CMD_OK"));
          posaljiPCLog(String(F("Izvrsena CMD naredba: ")) + komanda);
        } else {
          espSerijskiPort.println(F("ERR:CMD"));
          posaljiPCLog(String(F("Nepoznata CMD naredba: ")) + komanda);
        }
      }
      // MQTT transport line - route to MQTT handler
      else if (ulazniBuffer.startsWith("MQTT:")) {
        obradiMQTTLinijuIzESPa(ulazniBuffer);
      }
      // Other lines - just log
      else {
        posaljiPCLog(String(F("ESP LOG: ")) + ulazniBuffer);
      }

      ulazniBuffer = "";
    } else {
      if (ulazniBuffer.length() < ESP_ULAZNI_BUFFER_MAX) {
        ulazniBuffer += znak;
      } else {
        posaljiPCLog(F("ESP RX: preduga linija, odbacujem buffer"));
        ulazniBuffer = "";
      }
    }
  }
}
