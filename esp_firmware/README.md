# 📡 ESP8266 firmware za toranjski sat

Ova podmapa sadrzi firmware za `ESP8266` koji radi kao vanjski mrezni modul toranjskog sata i serijski komunicira s `Arduino Megom 2560` kroz `main/esp_serial.cpp`.

## ✨ Uloga ESP modula

- spaja toranjski sat na lokalnu WiFi mrezu
- dohvaca NTP vrijeme i salje ga Megi u lokalnom CET/CEST obliku
- pruza kratki servisni web sloj
- prihvaca setup WiFi kroz privremeni AP
- prenosi jednostavne API naredbe prema Megi

## 🌐 Aktivne web rute

- `/` - kratka servisna pocetna stranica
- `/setup` - unos nove WiFi mreze
- `/status` - JSON pregled WiFi stanja
- `/api/...` - rucne servisne naredbe prema Megi

## 🔐 Autentikacija

- Web i API koriste `Basic Auth`.
- Lozinka se ucitava iz EEPROM-a ili pada na zadanu vrijednost iz firmwarea.
- Na ESP-u vise ne postoji web ekran za promjenu lozinke.

## 🧵 Serijski protokol prema Megi

- `WIFI:<ssid>|<lozinka>|<dhcp>|<ip>|<maska>|<gateway>` prima mrezne postavke od Mege
- `WIFIEN:0` i `WIFIEN:1` gase ili pale WiFi radio
- `WIFISTATUS?` trazi trenutno WiFi stanje
- `NTPCFG:<server>` postavlja NTP server
- `NTPREQ:SYNC` trazi trenutno NTP vrijeme s ESP-a
- `NTP:YYYY-MM-DDTHH:MM:SS` salje lokalno vrijeme Megi
- `STATUS?` trazi kratki runtime status od Mege
- `CMD:<naredba>` prenosi servisne naredbe za zvona i modove rada

## 🚫 Sto vise nije aktivno

- nema ruta `/detalji`, `/clock-config`, `/hand-service`, `/plate-service` ni `/password`
- nema web uredjivanja postavki toranjskog sata
- nema aktivnog `WEBCFG` toka prema Megi
- ako netko ipak posalje `WEBCFG?` ili `WEBCFGSET:...`, Mega vraca `ERR:WEBCFGDISABLED`

## 📶 Setup WiFi

- setup AP ima SSID `FILA33_setup`
- lozinka setup AP-a je `toranj33`
- AP se pali dugim pritiskom tipke na `GPIO14 / D5`
- status LED koristi `GPIO12 / D6`
- setup stranica radi na `http://192.168.4.1/` i `http://192.168.4.1/setup`
- nakon spremanja mreze ESP prosljeduje novu konfiguraciju Megi preko `SETUPWIFI:`

## 🛠️ Upload i provjera

1. Otvori `esp_firmware.ino` u Arduino IDE-u ili PlatformIO okruzenju s instaliranim `ESP8266` paketom.
2. Odaberi odgovarajucu plocicu, primjerice `NodeMCU 1.0 (ESP-12E Module)`.
3. Prenesi firmware i spoji UART prema Megi uz ispravno 3.3 V napajanje i logicke razine.

## ✅ Sto provjeriti nakon boota

- serijski monitor treba pokazati `CFGREQ`, `WIFI:CONNECTED` i `WIFI:LOCAL_IP:...` kada je mreza dostupna
- `http://<ip-esp>/status` treba vratiti JSON s IP adresom i stanjem veze
- pocetna stranica treba prikazati da je ESP ogranicen na WiFi, NTP, setup i API
- API pozivi poput `http://<ip-esp>/api/bell1/on` trebaju poslati odgovarajuci `CMD:` prema Megi

## 📄 Datoteke

- `esp_firmware.ino` - glavni firmware za WiFi, setup AP, NTP i servisni web/API sloj toranjskog sata
