// esp_serial.cpp
#include <Arduino.h>
#include <RTClib.h>
#include "esp_serial.h"
#include "time_glob.h"
#include "zvonjenje.h"
#include "podesavanja_piny.h"
#include "otkucavanje.h"
#include "pc_serial.h"

// Uvijek koristi Serial3 (za Mega 2560 toranjski sat)
static HardwareSerial &espSerijskiPort = Serial3;

static const unsigned long ESP_BRZINA = 9600;

String ulazniBuffer = "";

void inicijalizirajESP() {
  espSerijskiPort.begin(ESP_BRZINA);
  ulazniBuffer.reserve(128);
  posaljiPCLog(F("ESP serijska veza inicijalizirana"));
}

static bool parsirajISOVrijeme(const String& iso, DateTime& dt) {
  const int osnovnaDuljina = 19;
  bool imaZuluneSufiks = iso.length() == 20 && iso.charAt(19) == 'Z';

  if (!(iso.length() == osnovnaDuljina || imaZuluneSufiks)) return false;
  if (iso.charAt(4) != '-' || iso.charAt(7) != '-' || iso.charAt(10) != 'T' || iso.charAt(13) != ':' || iso.charAt(16) != ':') {
    return false;
  }

  for (int i = 0; i < osnovnaDuljina; ++i) {
    if (i == 4 || i == 7 || i == 10 || i == 13 || i == 16) continue;
    if (!isDigit(iso.charAt(i))) return false;
  }

  int godina = iso.substring(0, 4).toInt();
  int mjesec = iso.substring(5, 7).toInt();
  int dan = iso.substring(8, 10).toInt();
  int sat = iso.substring(11, 13).toInt();
  int minuta = iso.substring(14, 16).toInt();
  int sekunda = iso.substring(17, 19).toInt();

  bool poljaIspravna = godina >= 2024 && mjesec >= 1 && mjesec <= 12 && dan >= 1 && dan <= 31 &&
                       sat >= 0 && sat <= 23 && minuta >= 0 && minuta <= 59 && sekunda >= 0 && sekunda <= 59;
  if (!poljaIspravna) return false;

  dt = DateTime(godina, mjesec, dan, sat, minuta, sekunda);
  return true;
}

void obradiESPSerijskuKomunikaciju() {
  while (espSerijskiPort.available()) {
    char znak = espSerijskiPort.read();

    // RAW debug: svaki primljeni znak pošalji na PC
    String s = F("ESP RX RAW: '");
    if (znak == '\n')      s += "\\n";
    else if (znak == '\r') s += "\\r";
    else                   s += znak;
    s += "' (";
    s += (int)znak;
    s += ")";
    posaljiPCLog(s);

    if (znak == '\n') {
      ulazniBuffer.trim();

      if (ulazniBuffer.length() > 0) {
        posaljiPCLog(String(F("ESP linija: ")) + ulazniBuffer);
      }

      if (ulazniBuffer.startsWith("NTP:")) {
        String iso = ulazniBuffer.substring(4);
        DateTime ntpVrijeme;
        if (parsirajISOVrijeme(iso, ntpVrijeme)) {
          azurirajVrijemeIzNTP(ntpVrijeme);
          espSerijskiPort.println("ACK:NTP");
          posaljiPCLog(String(F("Primljen NTP iz ESP-a: ")) + iso);
        } else {
          espSerijskiPort.println("ERR:NTP");
          posaljiPCLog(String(F("Neispravan NTP format: ")) + iso);
        }
      }
      else if (ulazniBuffer.startsWith("CMD:")) {
        String komanda = ulazniBuffer.substring(4);
        bool uspjeh = true;

        if      (komanda == "ZVONO1_ON")       aktivirajZvonjenje(1);
        else if (komanda == "ZVONO1_OFF")      deaktivirajZvonjenje(1);
        else if (komanda == "ZVONO2_ON")       aktivirajZvonjenje(2);
        else if (komanda == "ZVONO2_OFF")      deaktivirajZvonjenje(2);
        else if (komanda == "OTKUCAVANJE_OFF") postaviBlokaduOtkucavanja(true);
        else if (komanda == "OTKUCAVANJE_ON")  postaviBlokaduOtkucavanja(false);
        else if (komanda == "SLAVLJENJE_ON")   zapocniSlavljenje();
        else if (komanda == "SLAVLJENJE_OFF")  zaustaviSlavljenje();
        else if (komanda == "MRTVACKO_ON")     zapocniMrtvacko();
        else if (komanda == "MRTVACKO_OFF")    zaustaviZvonjenje();
        else uspjeh = false;

        if (uspjeh) {
          espSerijskiPort.println("ACK:CMD_OK");
          posaljiPCLog(String(F("Izvrsena CMD naredba: ")) + komanda);
        } else {
          espSerijskiPort.println("ERR:CMD");
          posaljiPCLog(String(F("Nepoznata CMD naredba: ")) + komanda);
        }
      }
      else {
        // sve ostalo je samo log s ESP-a
        posaljiPCLog(String(F("ESP LOG: ")) + ulazniBuffer);
      }

      ulazniBuffer = "";
    } else {
      ulazniBuffer += znak;
    }
  }
}