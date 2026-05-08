# 🔧 ZVONKO v. 1.0 - ESP firmware

Ova podmapa sadrzi firmware projekta `ZVONKO v. 1.0` za `ESP8266` i `ESP32`. `ESP` radi kao vanjski mrezni modul toranjskog sata i serijski komunicira s `Arduino Megom 2560` kroz [main/esp_serial.cpp](/C:/Users/Rato/Documents/GitHub/FILA33/main/esp_serial.cpp).

## ✨ Uloga ESP modula

- spaja toranjski sat na lokalnu WiFi mrezu
- odrzava pomocni UDP `NTP` sloj za sinkronizaciju vremena
- salje `NTP` vrijeme Megi tek kad `Mega` posalje `NTPREQ:SYNC`
- interno prati `UTC` u milisekundama od zadnje uspjesne sinkronizacije
- koristi `NTP` sekunde, `fraction` dio odgovora i `RTT/2` korekciju za precizniji mrezni timestamp
- pruza servisni web dashboard i API gumbe prema Megi
- prihvaca setup WiFi kroz privremeni AP
- ostaje pomocni mrezni sloj i ne zaobilazi `safe mode`, `RTC` degraded ni `EEPROM` degraded odluke koje donose [main/power_recovery.cpp](/C:/Users/Rato/Documents/GitHub/FILA33/main/power_recovery.cpp) i [main/time_glob.cpp](/C:/Users/Rato/Documents/GitHub/FILA33/main/time_glob.cpp)
- ima interni WiFi watchdog: ako je `WiFi` prijavljen kao spojen, a `NTP` ne uspije osvjeziti vrijeme `2 sata`, `ESP` radi `WiFi.disconnect()` i pokrece novo spajanje

## 🌐 Aktivne web rute

- `/` - jedina glavna web stranica, svedeni dashboard za `MUSKO`, `ZENSKO`, `SLAVI`, `BRECA` i suncevu automatiku
- `/setup` - setup stranica za unos nove WiFi mreze dok je aktivan privremeni AP
- `/api/status` - JSON status `ESP` veze i glavnih stanja koja dashboard boja prikazuje
- `/api/...` - rucne servisne naredbe prema Megi

## 🔐 Autentikacija

- dashboard `/` i `API` koriste `Basic Auth`
- lozinka se ucitava iz EEPROM-a ili pada na zadanu vrijednost iz firmwarea
- `/setup` ne trazi `Basic Auth` dok je aktivan setup AP
- na `ESP` strani vise ne postoji web ekran za promjenu lozinke

## 🧵 Serijski protokol prema Megi

### `Mega -> ESP`

- `WIFI:<ssid>|<lozinka>|<dhcp>|<ip>|<maska>|<gateway>` salje mrezne postavke toranjskog sata
- `WIFIEN:0` i `WIFIEN:1` gase ili pale WiFi radio
- `WIFISTATUS?` trazi trenutno WiFi stanje mreznog mosta
- `NTPCFG:<server>` postavlja `NTP` server
- `NTPREQ:SYNC` trazi trenutno `NTP` vrijeme u trenutku koji odabere `Mega`

### `ESP -> Mega`

- `CFGREQ` trazi pocetnu konfiguraciju nakon boota
- `WIFI:CONNECTED`, `WIFI:DISCONNECTED`, `WIFI:LOCAL_IP:...`, `WIFI:RSSI:...`, `WIFI:MAC:...` prijavljuju stanje veze
- `NTP:YYYY-MM-DDTHH:MM:SS;DST=0/1` salje lokalno vrijeme toranjskog sata
- `SETUPWIFI:<ssid>|<lozinka>` prosljeduje novu mrezu koju je korisnik upisao kroz setup AP
- `CMD:<naredba>` prenosi servisne naredbe prema [main/esp_serial.cpp](/C:/Users/Rato/Documents/GitHub/FILA33/main/esp_serial.cpp)
- `ACK:*`, `ERR:*` i `NTPLOG:*` linije sluze za potvrde i dijagnostiku mreznog mosta

## ⏱️ UDP NTP tok

- `ESP` vise ne koristi `NTPClient`, nego vlastiti UDP `NTP` dohvat u [esp_firmware.ino](/C:/Users/Rato/Documents/GitHub/FILA33/esp_firmware/esp_firmware.ino)
- prije novog upita odbacuju se zaostali UDP paketi kako stari odgovor ne bi pokvario novo `RTT` mjerenje
- prihvacaju se samo valjani `NTP` odgovori, uz osnovnu provjeru moda, `stratum` vrijednosti i vremena
- `ESP` iz odgovora racuna precizniji `UTC ms`, ali prema Megi i dalje salje isti tekstualni `NTP:` format radi kompatibilnosti s [main/time_glob.cpp](/C:/Users/Rato/Documents/GitHub/FILA33/main/time_glob.cpp)
- `Mega` i dalje ostaje jedini vlasnik `RTC` upisa i poravnanja na `RTC SQW` granicu sekunde

## 🛡️ Rad uz sigurnosne blokade Mege

- `ESP` i dalje moze odrzavati WiFi i `NTP` dok je `Mega` u ogranicenom radu
- `ESP` ne otkljucava `safe mode` i ne potvrduje latched faultove; to ostaje lokalna servisna funkcija na tipkama i LCD-u toranjskog sata
- kad `Mega` blokira automatiku zbog `RTC` ili `EEPROM` problema, `ESP` ostaje samo pomocni izvor mreze i vremena bez ovlasti nad mehanikom toranjskog sata
- nakon WiFi watchdog reseta `Mega` dobiva `NTP:` tek kad `ESP` ponovno potvrdi svjeze vrijeme, a ne iz starog klijentskog cachea

## 🚫 Sto vise nije aktivno

- nema ruta `/mise` ni `/mise/blagdani`
- nema `ESP` rasporeda za mise, blagdane ni posebnu zvonjavu
- nema `TIME?` fallbacka prema Megi
- nema ruta `/detalji`, `/clock-config`, `/hand-service`, `/plate-service` ni `/password`
- nema web uredjivanja postavki toranjskog sata
- nema aktivnog `WEBCFG` toka prema Megi
- ako netko ipak posalje `WEBCFG?` ili `WEBCFGSET:...`, `Mega` vraca `ERR:WEBCFGDISABLED`

## 📶 Setup WiFi

- setup AP ima SSID `ZVONKO_setup`
- lozinka setup AP-a je `zvonko10`
- na `ESP8266` AP se pali dugim pritiskom tipke na `GPIO14 / D5`
- setup AP se moze pokrenuti i dugim istovremenim pritiskom `lijevo + desno` na Mega tipkovnici, ali samo s glavnog prikaza sata
- na `ESP8266` status LED koristi `GPIO12 / D6`
- na `ESP32` zadano se koristi tipka na `GPIO27` i status LED na `GPIO26`
- dok je setup AP aktivan, i root ruta `http://192.168.4.1/` i `http://192.168.4.1/setup` otvaraju setup stranicu
- nakon spremanja mreze `ESP` prosljeduje novu konfiguraciju Megi preko `SETUPWIFI:`

## 🛠️ Upload i provjera

1. Otvori `esp_firmware.ino` u Arduino IDE-u ili PlatformIO okruzenju.
2. Odaberi odgovarajucu plocicu.
- `NodeMCU 1.0 (ESP-12E Module)` ili slicno za `ESP8266`
- `ESP32 Dev Module` ili odgovarajuci `ESP32D` profil za `ESP32`
3. Za `ESP32` spoji `Mega TX1 (pin 18)` na `ESP RX GPIO16` preko djelitelja napona te `ESP TX GPIO17` na `Mega RX1 (pin 19)`.
4. Prenesi firmware i provjeri da su `GND` vodovi zajednicki.

## ✅ Sto provjeriti nakon boota

- serijski monitor treba pokazati `CFGREQ`, `WIFI:CONNECTED` i `WIFI:LOCAL_IP:...` kada je mreza dostupna
- `ESP` vise ne treba sam slati `NTP:` po spajanju; `NTP` prema Megi ide tek nakon `NTPREQ:SYNC`
- u `NTPLOG:` linijama treba se vidjeti uspjesno UDP osvjezavanje vremena mreznog mosta toranjskog sata
- `http://<ip-esp>/api/status` treba vratiti JSON sa stanjem `WiFi` veze i glavnih komandi dashboarda
- pocetna stranica treba prikazati samo glavne tipke i donje kartice za suncevu automatiku
- API pozivi poput `http://<ip-esp>/api/bell1/on` trebaju poslati odgovarajuci `CMD:` prema Megi

## 📄 Datoteke

- [esp_firmware.ino](/C:/Users/Rato/Documents/GitHub/FILA33/esp_firmware/esp_firmware.ino) - glavni firmware za WiFi, setup AP, UDP `NTP` i servisni web/API sloj toranjskog sata
- [Popis ESP web API ruta toranjskog sata](/C:/Users/Rato/Documents/GitHub/FILA33/docs/esp_web_api_toranjskog_sata.md)
