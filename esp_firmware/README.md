# ESP8266 firmware za toranjski sat

Ova podmapa sadrzi firmware za ESP8266 modul koji se serijski povezuje s Arduino Megom 2560 i modulom `main/esp_serial.cpp`.

## Uloge modula

- NTP klijent koristi `NTPClient` u UTC modu, a prije slanja prema Megi pretvara vrijeme u lokalni CET/CEST format.
- WiFi STA nacin spaja komunikacijski modul toranjskog sata na lokalnu mrezu radi sinkronizacije vremena i udaljenog nadzora.
- Web posluzitelj pruza `/status` i `/cmd?value=<NAREDBA>` za osnovni servisni pristup zvonima, cekicima i sinkronizaciji.
- MQTT transport prima naredbe iz `main/mqtt_handler.cpp`, spaja se na broker, objavljuje stanja toranjskog sata i vraca pretplacene poruke prema Megi.

## Prilagodba prije uploada

- Provjeri pocetne WiFi vrijednosti u `esp_firmware.ino` ako Mega jos nije poslala postavke za toranjski sat.
- Provjeri pocetne MQTT fallback vrijednosti u `esp_firmware.ino`, ali racunaj da ih Mega pri radu toranjskog sata moze prepisati spremljenim postavkama.
- Po potrebi promijeni `NTP_POSLUZITELJ` ako lokalna mreza tornjskog sata koristi vlastiti NTP izvor.
- U Arduino IDE-u ili PlatformIO okruzenju instaliraj biblioteku `PubSubClient`, jer je potrebna za MQTT dio.
- Ako se ESP iz toranjskog ormara iznosi na siru mrezu, zastiti HTTP rute dodatnom autentikacijom ili ih ograniciti na internu mrezu.

## Serijski protokol prema Megi

- `WIFI:<ssid>|<lozinka>|<dhcp>|<ip>|<maska>|<gateway>` prima mrezne postavke i pokrece novo WiFi spajanje.
- `NTP:YYYY-MM-DDTHH:MM:SS` salje lokalno vrijeme prema `main/esp_serial.cpp`, gdje se poziva `azurirajVrijemeIzNTP()`.
- `CMD:<naredba>` prenosi osnovne naredbe za zvona i modove rada toranjskog sata.
- `MQTT:CONNECT|<broker>|<port>|<korisnik>|<lozinka>`, `MQTT:DISCONNECT`, `MQTT:STATUS`, `MQTT:SUB|<tema>` i `MQTT:PUB|<tema>|<poruka>` cine MQTT transport izmedu ESP-a i Mege.
- `MQTT:MSG|<tema>|<poruka>`, `MQTT:CONNECTED` i `MQTT:DISCONNECTED` vracaju stanje MQTT veze prema `main/mqtt_handler.cpp`.

## Upload na ESP8266

1. Otvori `esp_firmware.ino` u Arduino IDE-u s instaliranim ESP8266 paketom i bibliotekom `PubSubClient`.
2. Odaberi odgovarajucu plocicu, npr. `NodeMCU 1.0 (ESP-12E Module)`, te ispravan serijski port.
3. Prenesi skicu i spoji UART prema opisu iz glavnog README-a, uz ispravno 3.3 V prilagodavanje.

## Provjera komunikacije

- Serijski monitor ESP-a treba pokazati `WIFI:CONNECTED`, `MQTT:CONNECTED` i `NTP:` logove kada je toranjski sat online.
- Otvaranjem `http://<ip-esp>/status` dobiva se JSON sa SSID-om, IP adresom, MQTT stanjem i zadnjom linijom prema Megi.
- Slanjem `http://<ip-esp>/cmd?value=ZVONO1_ON` toranjski sat treba proslijediti `CMD:ZVONO1_ON` prema `main/esp_serial.cpp`.
- Ako Mega ukljuci MQTT u izborniku postavki, `main/mqtt_handler.cpp` treba nakon spajanja dobiti povratne linije `MQTT:CONNECTED` i `MQTT:MSG|...`.

## Datoteke

- `esp_firmware.ino` - glavna skica za WiFi, NTP, MQTT i web pristup komunikacijskom modulu toranjskog sata.
