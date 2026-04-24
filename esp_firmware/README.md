# 🔧 ZVONKO v. 1.0 - ESP firmware

Ova podmapa sadrži firmware projekta `ZVONKO v. 1.0` za `ESP8266` i `ESP32` koji radi kao vanjski mrežni modul toranjskog sata i serijski komunicira s `Arduino Megom 2560` kroz `main/esp_serial.cpp`.

## ✨ Uloga ESP modula

- spaja toranjski sat na lokalnu WiFi mrežu
- dohvaća NTP vrijeme i drži ga spremnim za `Arduino Megu 2560`
- šalje NTP vrijeme Megi tek kad `Mega` pošalje `NTPREQ:SYNC`
- pruža kratki servisni web sloj
- prihvaća setup WiFi kroz privremeni AP
- prenosi jednostavne API naredbe prema Megi

## 🌐 Aktivne web rute

- `/` - kratka servisna početna stranica
- `/setup` - unos nove WiFi mreže
- `/status` - JSON pregled WiFi stanja
- `/api/...` - ručne servisne naredbe prema Megi

## 🔐 Autentikacija

- web i API koriste `Basic Auth`
- lozinka se učitava iz EEPROM-a ili pada na zadanu vrijednost iz firmwarea
- na ESP-u više ne postoji web ekran za promjenu lozinke

## 🧵 Serijski protokol prema Megi

- `WIFI:<ssid>|<lozinka>|<dhcp>|<ip>|<maska>|<gateway>` prima mrežne postavke od Mege
- `WIFIEN:0` i `WIFIEN:1` gase ili pale WiFi radio
- `WIFISTATUS?` traži trenutno WiFi stanje
- `NTPCFG:<server>` postavlja NTP server
- `NTPREQ:SYNC` traži trenutno NTP vrijeme s ESP-a u trenutku koji odabere Mega
- `NTP:YYYY-MM-DDTHH:MM:SS;DST=0/1` šalje lokalno vrijeme Megi
- `STATUS?` traži kratki runtime status od Mege
- `CMD:<naredba>` prenosi servisne naredbe za zvona i modove rada

## 🚫 Što više nije aktivno

- nema ruta `/mise` ni `/mise/blagdani`
- nema ESP rasporeda za mise, blagdane ni bilo kakvu posebnu zvonjavu
- nema `TIME?` fallbacka prema Megi
- nema ruta `/detalji`, `/clock-config`, `/hand-service`, `/plate-service` ni `/password`
- nema web uređivanja postavki toranjskog sata
- nema aktivnog `WEBCFG` toka prema Megi
- ako netko ipak pošalje `WEBCFG?` ili `WEBCFGSET:...`, Mega vraća `ERR:WEBCFGDISABLED`

## 📶 Setup WiFi

- setup AP ima SSID `ZVONKO_setup`
- lozinka setup AP-a je `toranj33`
- na `ESP8266` AP se pali dugim pritiskom tipke na `GPIO14 / D5`
- na `ESP8266` status LED koristi `GPIO12 / D6`
- na `ESP32` zadano se koristi tipka na `GPIO27` i status LED na `GPIO26`
- setup stranica radi na `http://192.168.4.1/` i `http://192.168.4.1/setup`
- nakon spremanja mreže ESP prosljeđuje novu konfiguraciju Megi preko `SETUPWIFI:`

## 🛠️ Upload i provjera

1. Otvori `esp_firmware.ino` u Arduino IDE-u ili PlatformIO okruženju.
2. Odaberi odgovarajuću pločicu:
- `NodeMCU 1.0 (ESP-12E Module)` ili slično za `ESP8266`
- `ESP32 Dev Module` ili odgovarajući `ESP32D` profil za `ESP32`
3. Za `ESP32` zadano spoji `Mega TX1 (pin 18)` na `ESP RX GPIO16` preko djelitelja napona te `ESP TX GPIO17` na `Mega RX1 (pin 19)`.
4. Prenesi firmware i provjeri da su `GND` vodovi zajednički.

## ✅ Što provjeriti nakon boota

- serijski monitor treba pokazati `CFGREQ`, `WIFI:CONNECTED` i `WIFI:LOCAL_IP:...` kada je mreža dostupna
- `ESP` više ne treba sam slati `NTP:` po spajanju; NTP prema Megi ide tek nakon `NTPREQ:SYNC`
- `http://<ip-esp>/status` treba vratiti JSON s IP adresom i stanjem veze
- početna stranica treba prikazati da `ESP` vodi WiFi, NTP, setup i servisni API
- API pozivi poput `http://<ip-esp>/api/bell1/on` trebaju poslati odgovarajući `CMD:` prema Megi

## 📄 Datoteke

- `esp_firmware.ino` - glavni firmware za WiFi, setup AP, NTP i servisni web/API sloj toranjskog sata
