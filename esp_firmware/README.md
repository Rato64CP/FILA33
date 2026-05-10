# 🔧 ZVONKO v. 1.0 - ESP firmware

Ova podmapa sadrzi firmware za `ESP8266` i `ESP32` koji radi kao vanjski mrezni modul toranjskog sata. `ESP` serijski suraduje s `Arduino Megom 2560` kroz [main/esp_serial.cpp](../main/esp_serial.cpp), ali ne preuzima vlasnistvo nad `RTC`-om, zvonima, cekicima, kazaljkama ni okretnom plocom.

## ✨ Uloga ESP modula

- spaja toranjski sat na lokalnu `WiFi` mrezu
- odrzava vlastiti UDP `NTP` sloj
- salje `NTP` vrijeme Megi samo nakon `NTPREQ:SYNC`
- interno prati `UTC` u milisekundama od zadnje potvrdene sinkronizacije
- koristi `NTP` sekunde, `fraction` dio i `RTT/2` korekciju za precizniji mrezni timestamp
- potvrduje prvi `NTP` uzorak nakon restarta ili `WiFi` reconnecta drugim uzorkom prije prve sinkronizacije toranjskog sata
- pruza svedeni web dashboard i servisni API prema Megi
- prihvaca setup `WiFi` kroz privremeni `AP`
- ostaje pomocni mrezni sloj i ne zaobilazi odluke koje donose [main/time_glob.cpp](../main/time_glob.cpp), [main/prekidac_tisine.cpp](../main/prekidac_tisine.cpp) i [main/power_recovery.cpp](../main/power_recovery.cpp)

## 🌐 Aktivne web rute

- `/` - jedina glavna stranica dashboarda
- `/setup` - setup stranica za unos nove `WiFi` mreze dok je aktivan privremeni `AP`
- `/update` - skrivena `OTA` stranica za upload novog `ESP` firmwarea
- `/api/status` - `JSON` status `WiFi` veze i stvarnog stanja koje dashboard boja prikazuje
- `/api/...` - servisne naredbe prema Megi

## 🧭 Dashboard

- gornji 2x2 blok koristi tipke `MUSKO`, `ZENSKO`, `SLAVI`, `BRECA`
- gornje tipke su naglasene i tamnije plave kad su ukljucene
- donji blok koristi tipke `JUTRO`, `PODNE`, `VECER`
- ispod suncevih tipki postoji crveni toggle `TIHI MOD`
- `TIHI MOD` preko weba ulazi u isti jedinstveni tihi rezim kao [main/prekidac_tisine.cpp](../main/prekidac_tisine.cpp)
- ako korisnik promijeni stanje fizickim kip-prekidacem tihog moda, dashboard nakon sljedezeg `STATUS:` osvjezavanja prikazuje stvarno stanje iz Mege
- dashboard pri prvom otvaranju odmah radi jedan prisilni dohvat `STATUS?` kako bi se tipke obojile prema stvarnom stanju toranjskog sata

## 🔐 Autentikacija

- dashboard `/` i sve `/api/...` rute koriste `Basic Auth`
- ruta `/update` koristi isti `Basic Auth`
- lozinka se ucitava iz `EEPROM`-a ili pada na zadanu firmware vrijednost
- `/setup` ne trazi `Basic Auth` dok je aktivan setup `AP`

## 📡 OTA nadogradnja

- `OTA` je izveden kao skrivena web ruta `/update`
- koristi se upload kompajlirane `.bin` datoteke za `ESP8266` ili `ESP32`
- tijekom upisa firmwarea `ESP` privremeno zaustavlja redovni `NTP` i ostale web/serijske poslove koji nisu potrebni za upload
- nakon uspjesne nadogradnje `ESP` sam zakazuje kratki restart i vraca se u normalan rad
- dashboard ne prikazuje link prema `/update`; ruta se otvara rucnim upisom adrese u pregledniku

## 🧵 Serijski protokol prema Megi

### `Mega -> ESP`

- `WIFI:<ssid>|<lozinka>|<dhcp>|<ip>|<maska>|<gateway>` salje mrezne postavke toranjskog sata
- `WIFIEN:0` i `WIFIEN:1` gase ili pale `WiFi` radio
- `WIFISTATUS?` trazi trenutno `WiFi` stanje mreznog mosta
- `NTPCFG:<server>` postavlja `NTP` server
- `NTPREQ:SYNC` trazi trenutno `NTP` vrijeme u trenutku koji odabere `Mega`

### `ESP -> Mega`

- `CFGREQ` trazi pocetnu konfiguraciju nakon boota
- `WIFI:CONNECTED`, `WIFI:DISCONNECTED`, `WIFI:LOCAL_IP:...`, `WIFI:MAC:...` prijavljuju stanje veze
- `NTP:YYYY-MM-DDTHH:MM:SS;DST=0/1` salje lokalno vrijeme toranjskog sata
- `SETUPWIFI:<ssid>|<lozinka>` prosljeduje novu mrezu koju je korisnik upisao kroz setup `AP`
- `CMD:<naredba>` prenosi servisne naredbe prema [main/esp_serial.cpp](../main/esp_serial.cpp)
- `STATUS:` vraca objedinjeni status koji dashboard koristi za boju tipki
- `ACK:*`, `ERR:*` i `NTPLOG:*` linije sluze za potvrde i dijagnostiku mreznog mosta

## ⏱️ UDP NTP tok

- `ESP` vise ne koristi `NTPClient`, nego vlastiti UDP `NTP` dohvat u [esp_firmware.ino](esp_firmware.ino)
- prije novog upita odbacuju se zaostali UDP paketi kako kasni odgovor ne bi pokvario novo `RTT` mjerenje
- prihvacaju se samo valjani `NTP` odgovori, uz osnovnu provjeru `mode`, `stratum` vrijednosti i vremena
- prvi `NTP` uzorak nakon restarta ili `WiFi` reconnecta ne salje se odmah Megi
- prvi uzorak se pamti, a `ESP` odmah trazi drugi uzorak radi stabilizacije
- tek potvrden drugi uzorak postaje autoritet za prvu `NTP` sinkronizaciju toranjskog sata
- `ESP` iz odgovora racuna precizniji `UTC ms`, ali prema Megi i dalje salje isti tekstualni `NTP:` format radi kompatibilnosti s [main/time_glob.cpp](../main/time_glob.cpp)
- `Mega` i dalje ostaje jedini vlasnik `RTC` upisa i poravnanja na `RTC SQW` granicu sekunde

## 🛡️ Rad uz sigurnosne blokade Mege

- `ESP` i dalje moze odrzavati `WiFi` i `NTP` dok je `Mega` u ogranicenom radu
- `ESP` ne otkljucava `safe mode` i ne potvrduje latched faultove
- kad `Mega` blokira automatiku zbog `RTC` ili `EEPROM` problema, `ESP` ostaje samo pomocni izvor mreze i vremena bez ovlasti nad mehanikom toranjskog sata
- nakon `WiFi` watchdog reseta `Mega` dobiva `NTP:` tek kad `ESP` ponovno potvrdi svjeze vrijeme

## 🚫 Sto vise nije aktivno

- nema ruta `/mise` ni `/mise/blagdani`
- nema `ESP` rasporeda za mise, blagdane ni posebnu zvonjavu
- nema `TIME?` fallbacka prema Megi
- nema ruta `/detalji`, `/clock-config`, `/hand-service`, `/plate-service` ni `/password`
- nema web uredjivanja trajnih postavki toranjskog sata
- nema aktivnog `WEBCFG` toka prema Megi
- ako netko ipak posalje `WEBCFG?` ili `WEBCFGSET:...`, `Mega` vraca `ERR:WEBCFGDISABLED`

## 📶 Setup WiFi

- setup `AP` ima `SSID` `ZVONKO_setup`
- lozinka setup `AP`-a je `zvonko10`
- na `ESP8266` `AP` se pali dugim pritiskom tipke na `GPIO14 / D5`
- setup `AP` se moze pokrenuti i dugim istovremenim pritiskom `lijevo + desno` na Mega tipkovnici, ali samo s glavnog prikaza sata
- na `ESP8266` status `LED` koristi `GPIO12 / D6`
- na `ESP32` zadano se koristi tipka na `GPIO27` i status `LED` na `GPIO26`
- dok je setup `AP` aktivan, i root ruta `http://192.168.4.1/` i `http://192.168.4.1/setup` otvaraju setup stranicu
- nakon spremanja mreze `ESP` prosljeduje novu konfiguraciju Megi preko `SETUPWIFI:`

## 🛠️ Upload i provjera

1. Otvori `esp_firmware.ino` u `Arduino IDE`-u ili `PlatformIO` okruzenju.
2. Odaberi odgovarajucu plocicu.
- `NodeMCU 1.0 (ESP-12E Module)` ili slicno za `ESP8266`
- `ESP32 Dev Module` ili odgovarajuci `ESP32` profil za `ESP32`
3. Za `ESP32` spoji `Mega TX1 (pin 18)` na `ESP RX GPIO16` preko djelitelja napona te `ESP TX GPIO17` na `Mega RX1 (pin 19)`.
4. Prenesi firmware i provjeri da su `GND` vodovi zajednicki.

### OTA upload preko mreze

1. U istom razvojnom okruzenju kompajliraj firmware i pronadi izlaznu `.bin` datoteku.
2. Otvori `http://<ip-esp>/update`.
3. Prijavi se istim `Basic Auth` podacima kao za dashboard toranjskog sata.
4. Odaberi novu `.bin` datoteku i pricekaj potvrdu o uspjesnoj nadogradnji.
5. Pricekaj automatski restart `ESP` modula prije novog otvaranja dashboarda.

## ✅ Sto provjeriti nakon boota

- serijski monitor treba pokazati `CFGREQ`, `WIFI:CONNECTED` i `WIFI:LOCAL_IP:...` kada je mreza dostupna
- prvi `NTP` nakon restarta treba u `NTPLOG:` prikazati spremanje prvog uzorka i potvrdu drugim uzorkom
- `ESP` vise ne treba sam slati `NTP:` po spajanju; `NTP` prema Megi ide tek nakon `NTPREQ:SYNC`
- `http://<ip-esp>/api/status` treba vratiti `JSON` sa stanjem `WiFi` veze, glavnih tipki, suncevih tipki i `TIHOG MODA`
- pocetna stranica treba prikazati samo glavne tipke, sunceve tipke i crveni `TIHI MOD`
- API pozivi poput `http://<ip-esp>/api/bell1/on` i `http://<ip-esp>/api/quiet/on` trebaju poslati odgovarajuci `CMD:` prema Megi
- `http://<ip-esp>/update` treba otvoriti `OTA` upload stranicu i nakon uspjesnog slanja firmwarea izazvati restart `ESP` modula

## 📄 Datoteke

- [esp_firmware.ino](esp_firmware.ino) - glavni firmware za `WiFi`, setup `AP`, UDP `NTP`, dashboard i servisni web/API sloj toranjskog sata
- [Popis ESP web API ruta toranjskog sata](../docs/esp_web_api_toranjskog_sata.md)
