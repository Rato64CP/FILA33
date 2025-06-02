// esp_serial.cpp
#include <Arduino.h>
#include <RTClib.h>
#include "esp_serial.h"
#include "time_glob.h"
#include "zvonjenje.h"

#define ESP_SERIAL Serial1  // RX1=19, TX1=18

String ulazniBuffer = "";

void inicijalizirajESP() {
  ESP_SERIAL.begin(9600);
  ulazniBuffer.reserve(128);
}

void obradiESPSerijskuKomunikaciju() {
  while (ESP_SERIAL.available()) {
    char znak = ESP_SERIAL.read();
    if (znak == '\n') {
      ulazniBuffer.trim();
      if (ulazniBuffer.startsWith("NTP:")) {
        String iso = ulazniBuffer.substring(4);
        DateTime ntpVrijeme = DateTime(iso.c_str());
        azurirajVrijemeIzNTP(ntpVrijeme);
        ESP_SERIAL.println("ACK:NTP");
      }
      else if (ulazniBuffer.startsWith("CMD:")) {
        String komanda = ulazniBuffer.substring(4);
        if (komanda == "ZVONO1_ON") aktivirajZvonjenje(1);
        else if (komanda == "ZVONO1_OFF") deaktivirajZvonjenje(1);
        else if (komanda == "ZVONO2_ON") aktivirajZvonjenje(2);
        else if (komanda == "ZVONO2_OFF") deaktivirajZvonjenje(2);
        else ESP_SERIAL.println("ERR:CMD");
        ESP_SERIAL.println("ACK:CMD_OK");
      }
      else {
        ESP_SERIAL.println("ERR:FORMAT");
      }
      ulazniBuffer = "";
    } else {
      ulazniBuffer += znak;
    }
  }
}
