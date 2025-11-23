// esp_serial.cpp
#include <Arduino.h>
#include <RTClib.h>
#include "esp_serial.h"
#include "time_glob.h"
#include "zvonjenje.h"
#include "podesavanja_piny.h"
#include "otkucavanje.h"

#if defined(ARDUINO_AVR_MEGA2560) || defined(ARDUINO_AVR_MEGA)
static HardwareSerial &espSerijskiPort = Serial3;  // Ugrađeni WiFi modul na Mega 2560 WiFi
#else
#include <SoftwareSerial.h>
static SoftwareSerial espSerijskiPort(PIN_ESP_RX, PIN_ESP_TX);  // Modul za toranjski sat na softverskom UART-u
#endif

static const unsigned long ESP_BRZINA = 9600;

String ulazniBuffer = "";

void inicijalizirajESP() {
  espSerijskiPort.begin(ESP_BRZINA);
  ulazniBuffer.reserve(128);
}

void obradiESPSerijskuKomunikaciju() {
  while (espSerijskiPort.available()) {
    char znak = espSerijskiPort.read();
    if (znak == '\n') {
      ulazniBuffer.trim();
      if (ulazniBuffer.startsWith("NTP:")) {
        String iso = ulazniBuffer.substring(4);
        DateTime ntpVrijeme = DateTime(iso.c_str());
        azurirajVrijemeIzNTP(ntpVrijeme);
        espSerijskiPort.println("ACK:NTP");
      }
      else if (ulazniBuffer.startsWith("CMD:")) {
        String komanda = ulazniBuffer.substring(4);
        bool uspjeh = true;
        if (komanda == "ZVONO1_ON") aktivirajZvonjenje(1);
        else if (komanda == "ZVONO1_OFF") deaktivirajZvonjenje(1);
        else if (komanda == "ZVONO2_ON") aktivirajZvonjenje(2);
        else if (komanda == "ZVONO2_OFF") deaktivirajZvonjenje(2);
        else if (komanda == "OTKUCAVANJE_OFF") postaviBlokaduOtkucavanja(true);
        else if (komanda == "OTKUCAVANJE_ON") postaviBlokaduOtkucavanja(false);
        else if (komanda == "SLAVLJENJE_ON") zapocniSlavljenje();
        else if (komanda == "SLAVLJENJE_OFF") zaustaviSlavljenje();
        else if (komanda == "MRTVACKO_ON") zapocniMrtvacko();
        else if (komanda == "MRTVACKO_OFF") zaustaviZvonjenje();
        else uspjeh = false;

        if (uspjeh) espSerijskiPort.println("ACK:CMD_OK");
        else espSerijskiPort.println("ERR:CMD");
      }
      else {
        espSerijskiPort.println("ERR:FORMAT");
      }
      ulazniBuffer = "";
    } else {
      ulazniBuffer += znak;
    }
  }
}
