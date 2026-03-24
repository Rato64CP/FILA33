# 🌐 ESP8266 firmware za toranjski sat

Ova podmapa sadrži firmware za ESP8266 modul koji se serijski povezuje s Arduino Megom 2560 i modulom `src/esp_serial.cpp`.

## 🔄 Uloge modula

- ⏱️ **NTP klijent** koristi `NTPClient` (UTC) i prije slanja radi pretvorbu u lokalno CET/CEST vrijeme, pa periodički šalje poruke `NTP:YYYY-MM-DDTHH:MM:SS` prema Megi (bez `Z` sufiksa), čime se sinkronizira vrijeme u `obradiESPSerijskuKomunikaciju()`.
- 🌐 **WiFi STA način** spaja modul na kućnu mrežu kako bi mogao dohvatiti NTP i prihvatiti udaljene zahtjeve.
- 🛰️ **Web poslužitelj** pruža rutu `/cmd?value=<NAREDBA>` koja formira `CMD:` poruke kompatibilne s funkcijama zvona i otkucaja (`aktivirajZvonjenje()`, `postaviBlokaduOtkucavanja()`, `zapocniSlavljenje()` i dr.).

## 🛠️ Prilagodba prije uploada

- U `esp_firmware.ino` postavi `WIFI_SSID` i `WIFI_LOZINKA` na podatke lokalne mreže toranjskog ormara.
- Po potrebi promijeni `NTP_POSLUZITELJ` ili `NTP_OFFSET_SEKUNDI` ako toranjski sat mora raditi u drugoj vremenskoj zoni.
- Poželjno je dodati zaštitu (npr. API ključ) na HTTP rute prije spajanja na internet.

## 🚀 Upload na ESP8266

1. Otvori `esp_firmware.ino` u Arduino IDE-u s instaliranim ESP8266 paketom.
2. Odaberi pločicu (npr. *NodeMCU 1.0 (ESP-12E Module)*) i postavi serijski port.
3. Prenesi skicu i spoji UART (TX/RX) na Serial1 Arduina Mega 2560 uz prevod na 3.3 V kako je opisano u glavnom README-u.

## ✅ Provjera komunikacije

- Serijski monitor ESP-a trebao bi prikazati odgovore `ACK:NTP` ili `ACK:CMD_OK` koje prima iz `src/esp_serial.cpp`.
- Otvorom `http://<IP-ESP>/status` dobiva se JSON s SSID-om, IP adresom i zadnjim odgovorom Arduina.
- Slanjem `http://<IP-ESP>/cmd?value=ZVONO1_ON` aktivira se muško zvono putem `aktivirajZvonjenje(1)` u kodu toranjskog sata.

## 📂 Datoteke

- `esp_firmware.ino` – glavna skica s WiFi povezivanjem, NTP sinkronizacijom i web poslužiteljem.

Svi komentari i dokumentacija ostaju na hrvatskom jeziku u skladu s pravilima repozitorija.
