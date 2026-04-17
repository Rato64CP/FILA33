# 📡 ESP firmware za toranjski sat

Ova podmapa sadrzi firmware za `ESP8266` i `ESP32` koji radi kao vanjski mrezni modul toranjskog sata i serijski komunicira s `Arduino Megom 2560` kroz `main/esp_serial.cpp`.

## ✨ Uloga ESP modula

- spaja toranjski sat na lokalnu WiFi mrezu
- dohvaca NTP vrijeme i drzi ga spremnim za `Arduino Megu 2560`
- salje NTP vrijeme Megi tek kad `Mega` posalje `NTPREQ:SYNC`
- pruza kratki servisni web sloj
- prihvaca setup WiFi kroz privremeni AP
- prenosi jednostavne API naredbe prema Megi

## 🌐 Aktivne web rute

- `/` - kratka servisna pocetna stranica
- `/mise` - postavke rada misa na `ESP-u` za radni dan, nedjelju i blagdane
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
- `NTPREQ:SYNC` trazi trenutno NTP vrijeme s ESP-a u trenutku koji odabere Mega
- `NTP:YYYY-MM-DDTHH:MM:SS` salje lokalno vrijeme Megi
- `STATUS?` trazi kratki runtime status od Mege
- `TIME?` trazi samo trenutno lokalno vrijeme toranjskog sata s Mege
- `TIME:YYYY-MM-DDTHH:MM:SS` vraca lagani fallback za misni raspored na `ESP-u` kad NTP jos nije spreman
- `CMD:<naredba>` prenosi servisne naredbe za zvona i modove rada
- `CMD:MISA_RADNA`, `CMD:MISA_NEDJELJA` i `CMD:MISA_BLAGDAN` koristi `ESP` kad njegov raspored misa okine najavu

## ✨ Mise na ESP-u

- `ESP` moze cuvati raspored misa u vlastitom EEPROM-u
- ako `NTP` jos nije spreman, `ESP` za misni raspored trazi samo `TIME?` od `Mege` umjesto parsiranja opceg `STATUS:` toka
- web stranica `/mise` ima:
- glavni prekidac da li je `ESP` raspored misa aktivan
- vrijeme i dane za radnu misu
- vrijeme za nedjeljnu misu
- vrijeme i `DA/NE` za svaki blagdan
- `ESP` racuna pomicne blagdane iz tekuce godine i salje samo tri jednostavne naredbe prema `Megi`
- `Mega` i dalje ostaje zastitni sloj toranjskog sata:
- ceka da zavrsi otkucavanje
- postuje zauzeta zvona i inerciju
- odlucuje koje zvono stvarno pali i koliko dugo

## 🚫 Sto vise nije aktivno

- nema ruta `/detalji`, `/clock-config`, `/hand-service`, `/plate-service` ni `/password`
- nema web uredjivanja postavki toranjskog sata
- nema aktivnog `WEBCFG` toka prema Megi
- ako netko ipak posalje `WEBCFG?` ili `WEBCFGSET:...`, Mega vraca `ERR:WEBCFGDISABLED`

## 📶 Setup WiFi

- setup AP ima SSID `FILA33_setup`
- lozinka setup AP-a je `toranj33`
- na `ESP8266` AP se pali dugim pritiskom tipke na `GPIO14 / D5`
- na `ESP8266` status LED koristi `GPIO12 / D6`
- na `ESP32` zadano se koristi tipka na `GPIO27` i status LED na `GPIO26`
- setup stranica radi na `http://192.168.4.1/` i `http://192.168.4.1/setup`
- nakon spremanja mreze ESP prosljeduje novu konfiguraciju Megi preko `SETUPWIFI:`

## 🛠️ Upload i provjera

1. Otvori `esp_firmware.ino` u Arduino IDE-u ili PlatformIO okruzenju.
2. Odaberi odgovarajucu plocicu:
- `NodeMCU 1.0 (ESP-12E Module)` ili slicno za `ESP8266`
- `ESP32 Dev Module` ili odgovarajuci `ESP32D` profil za `ESP32`
3. Za `ESP32` zadano spoji `Mega TX1 (pin 18)` na `ESP RX GPIO16` preko djelitelja napona te `ESP TX GPIO17` na `Mega RX1 (pin 19)`.
4. Prenesi firmware i provjeri da su `GND` vodovi zajednicki.

## ✅ Sto provjeriti nakon boota

- serijski monitor treba pokazati `CFGREQ`, `WIFI:CONNECTED` i `WIFI:LOCAL_IP:...` kada je mreza dostupna
- `ESP` vise ne treba sam slati `NTP:` po spajanju; NTP prema Megi ide tek nakon `NTPREQ:SYNC`
- `http://<ip-esp>/status` treba vratiti JSON s IP adresom i stanjem veze
- pocetna stranica treba prikazati da `ESP` vodi WiFi, NTP, setup i misne postavke
- `http://<ip-esp>/mise` treba otvoriti jednostavan ekran za radni dan, nedjelju i blagdane
- API pozivi poput `http://<ip-esp>/api/bell1/on` trebaju poslati odgovarajuci `CMD:` prema Megi

## 📄 Datoteke

- `esp_firmware.ino` - glavni firmware za WiFi, setup AP, NTP i servisni web/API sloj toranjskog sata
