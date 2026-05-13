# 🌐 ESP Web API Toranjskog Sata

Ovaj dokument popisuje web rute i servisne API pozive koje `ESP` firmware koristi za mrežni sloj toranjskog sata. `ESP` ostaje pomoćni modul: web zahtjev prima `ESP`, a stvarnu odluku i pogon nad `zvonima`, `čekićima`, `slavljenjem`, `mrtvačkim`, `sunčevom automatikom`, `tihim režimom`, `sinkronizacijom vremena` i sigurnosnim blokadama i dalje donosi `Mega` kroz module u `main/`.

Povezani moduli toranjskog sata:
- `main/esp_serial.*` za serijski most između `ESP-a` i `Mege`
- `main/zvonjenje.*` i `main/otkucavanje.*` za ručne i automatske radnje nad `zvonima` i `čekićima`
- `main/slavljenje_mrtvacko.*` za ručne servisne modove `slavljenja` i `mrtvačkog`
- `main/sunceva_automatika.*` za uključivanje i isključivanje sunčevih događaja
- `main/prekidac_tisine.*` za jedinstveni tihi režim toranjskog sata
- `main/time_glob.*` za sinkronizaciju vremena toranjskog sata
- `main/power_recovery.*` za `safe mode`, recovery i latched fault ograničenja

## 🔐 Autentikacija

- `Basic Auth` koristi korisničko ime `admin`
- lozinku `ESP` učitava iz vlastitog `EEPROM`-a ili pada na zadanu firmware vrijednost
- dashboard `/` i rute `/api/...` traže autentikaciju
- ruta `/update` koristi isti `Basic Auth`
- `/setup` ne traži `Basic Auth` dok je aktivna privremena setup mreža toranjskog sata

## 🧭 Glavne web rute

| Ruta | Metoda | Auth | Svrha |
|---|---|---|---|
| `/` | `GET` | da, osim kad je aktivan setup `AP` | Svedeni dashboard za `MUŠKO`, `ŽENSKO`, `SLAVI`, `BRECA`, `JUTRO`, `PODNE`, `VEČER` i `TIHI MOD` |
| `/settings` | `GET` | da, osim kad je aktivan setup `AP` | Zasebna stranica za sigurne web postavke `Sustav`, `Stapici`, `BAT` i `Sunce` |
| `/setup` | `GET` | ne | Prikazuje setup stranicu za novu `WiFi` mrežu toranjskog sata dok je aktivan setup `AP` |
| `/setup` | `POST` | ne | Sprema novi `SSID` i lozinku te ih šalje Megi radi sinkronizacije mrežnih postavki |
| `/update` | `GET` | da | Prikazuje skrivenu `OTA` stranicu za upload novog `ESP` firmwarea |
| `/update` | `POST` | da | Prima `OTA` upload i nakon uspjeha zakazuje restart `ESP` modula |
| `/api/status` | `GET` | da | Vraća `JSON` status `ESP` veze i stvarnog stanja koje dashboard koristi za boju tipki |
| `/api/settings/system` | `GET` | da | Vraća `JSON` sigurne `Sustav` postavke koje `Mega` drži u [main/postavke.*](../main/postavke.h) |
| `/api/settings/system` | `POST` | da | Sprema sigurne `Sustav` postavke slanjem punog paketa prema `Megi` |

## 📡 JSON status ruta

### `GET /api/status`

Vraća `JSON` tijelo oblika:

```json
{
  "wifi_ip": "192.168.1.50",
  "wifi_connected": true,
  "mega_status_known": true,
  "bell1_active": false,
  "bell2_active": true,
  "slavljenje_active": false,
  "mrtvacko_active": false,
  "solar_morning_active": true,
  "solar_noon_active": false,
  "solar_evening_active": true,
  "silent_mode_active": false
}
```

Polja:
- `wifi_ip`: lokalna IP adresa `ESP` modula
- `wifi_connected`: je li `ESP` trenutno spojen na mrežu i spreman za `NTP` tok toranjskog sata
- `mega_status_known`: je li `ESP` uspio dohvatiti svježi `STATUS:` odgovor s `Mege`
- `bell1_active`: stanje tipke `MUŠKO`
- `bell2_active`: stanje tipke `ŽENSKO`
- `slavljenje_active`: stanje tipke `SLAVI`
- `mrtvacko_active`: stanje tipke `BRECA`
- `solar_morning_active`: stanje tipke `JUTRO`
- `solar_noon_active`: stanje tipke `PODNE`
- `solar_evening_active`: stanje tipke `VEČER`
- `silent_mode_active`: stanje tipke `TIHI MOD`

Dodatno:
- `GET /api/status?force=1` prisiljava `ESP` da odmah pošalje `STATUS?` prema `Megi`
- dashboard koristi `force=1` nakon klika kako bi korisnik odmah dobio stvarnu povratnu informaciju
- dashboard koristi i jedan početni prisilni dohvat nakon otvaranja stranice kako bi se tipke obojile prema stvarnom stanju toranjskog sata

## ⚙️ API sigurnih `Sustav` postavki

### `GET /api/settings/system`

Vraća `JSON` tijelo oblika:

```json
{
  "known": true,
  "lcd_backlight": true,
  "pc_logging": false,
  "rs485_enabled": true,
  "ups_mode": false,
  "bell_brake": true,
  "inertia1_seconds": 90,
  "inertia2_seconds": 90,
  "hammer_pulse_ms": 150
}
```

Polja:
- `known`: je li `ESP` uspio dohvatiti stvarni paket `SET:SUSTAV|...` s `Mege`
- `lcd_backlight`: stanje `LCD` pozadinskog osvjetljenja
- `pc_logging`: stanje servisnog logiranja
- `rs485_enabled`: stanje `RS485` komunikacije
- `ups_mode`: stanje `UPS` moda
- `bell_brake`: stanje rada s `K:0/1`
- `inertia1_seconds`: `INR1` za prvo zvono
- `inertia2_seconds`: `INR2` za drugo zvono
- `hammer_pulse_ms`: trajanje impulsa elektromagnetskih batova

Dodatno:
- `GET /api/settings/system?force=1` prisiljava `ESP` da odmah pošalje `SETREQ:SUSTAV` prema `Megi`
- stranica `/settings` taj prisilni dohvat koristi pri prvom otvaranju i nakon spremanja

### `POST /api/settings/system`

Očekuje `application/x-www-form-urlencoded` polja:
- `lcd`
- `log`
- `rs`
- `ups`
- `koc`
- `inr1`
- `inr2`
- `imp`

Pravila:
- toggle polja `lcd`, `log`, `rs`, `ups`, `koc` moraju biti `0` ili `1`
- `inr1` i `inr2` moraju biti cijeli brojevi `10-180`
- `imp` mora biti cijeli broj `10-300` u koraku `10`
- `ESP` šalje cijeli paket prema `Megi` kroz `SETCFG:SUSTAV|...`
- `Mega` i dalje ostaje jedini autoritet za validaciju i spremanje preko [main/postavke.cpp](../main/postavke.cpp)

Tipični statusi odgovora:
- `200` `Mega` je spremila sustavske postavke
- `400` nedostaje jedno ili više obaveznih polja
- `422` neispravan unos ili je `Mega` odbila paket
- `504` `Mega` nije odgovorila na zahtjev za spremanje

## 📶 Setup WiFi API

### `GET /setup`

- radi samo dok je aktivan setup `AP` `ZVONKO_setup`
- ako setup `AP` nije aktivan, vraća `404`
- dok je setup `AP` aktivan, i root ruta `/` prikazuje istu setup stranicu

### `POST /setup`

Očekuje `form` parametre:
- `ssid`
- `lozinka`

Ponašanje:
- validira da su `SSID` i lozinka jednolinijski i bez znaka `|`
- šalje novu mrežnu konfiguraciju Megi preko serijskog toka `SETUPWIFI:...`
- `Mega` zatim sprema i koristi istu mrežnu konfiguraciju toranjskog sata

Tipični statusi odgovora:
- `200` uspješno prihvaćena nova mreža
- `400` nedostaje `ssid` ili `lozinka`
- `409` setup `AP` nije aktivan
- `422` neispravan unos ili `Mega` nije prihvatila postavke

## 🔧 Servisne API rute prema Megi

Sve rute ispod:
- koriste `GET`
- traže `Basic Auth`
- `ESP` ih prevodi u `CMD:<naredba>` prema `Megi`
- stvarni učinak ovisi o stanju modula u `main/`, pa `safe mode`, `RTC` degraded ili `EEPROM` degraded i dalje mogu blokirati mehaniku toranjskog sata

| Ruta | Mega naredba | Komponenta toranjskog sata | Opis |
|---|---|---|---|
| `/api/bell1/on` | `CMD:ZVONO1_ON` | `zvono 1`, `čekići` | Ručno uključi prvo zvono ako `Mega` dopusti naredbu |
| `/api/bell1/off` | `CMD:ZVONO1_OFF` | `zvono 1`, `čekići` | Ručno isključi prvo zvono |
| `/api/bell2/on` | `CMD:ZVONO2_ON` | `zvono 2`, `čekići` | Ručno uključi drugo zvono |
| `/api/bell2/off` | `CMD:ZVONO2_OFF` | `zvono 2`, `čekići` | Ručno isključi drugo zvono |
| `/api/slavljenje/on` | `CMD:SLAVLJENJE_ON` | `slavljenje`, `čekići` | Ručno pokrene `slavljenje` |
| `/api/slavljenje/off` | `CMD:SLAVLJENJE_OFF` | `slavljenje`, `čekići` | Ručno zaustavi `slavljenje` |
| `/api/mrtvacko/on` | `CMD:MRTVACKO_ON` | `mrtvačko`, `čekići` | Ručno pokrene `mrtvačko` |
| `/api/mrtvacko/off` | `CMD:MRTVACKO_OFF` | `mrtvačko`, `čekići` | Ručno zaustavi `mrtvačko` |
| `/api/solar/morning/on` | `CMD:SUNCE_JUTRO_ON` | `sunčeva automatika` | Uključi jutarnji sunčev događaj |
| `/api/solar/morning/off` | `CMD:SUNCE_JUTRO_OFF` | `sunčeva automatika` | Isključi jutarnji sunčev događaj |
| `/api/solar/noon/on` | `CMD:SUNCE_PODNE_ON` | `sunčeva automatika` | Uključi podnevni sunčev događaj |
| `/api/solar/noon/off` | `CMD:SUNCE_PODNE_OFF` | `sunčeva automatika` | Isključi podnevni sunčev događaj |
| `/api/solar/evening/on` | `CMD:SUNCE_VECER_ON` | `sunčeva automatika` | Uključi večernji sunčev događaj |
| `/api/solar/evening/off` | `CMD:SUNCE_VECER_OFF` | `sunčeva automatika` | Isključi večernji sunčev događaj |
| `/api/quiet/on` | `CMD:TIHI_ON` | `tihi režim`, `zvona`, `čekići` | Uključi virtualni tihi režim preko `ESP` dashboarda |
| `/api/quiet/off` | `CMD:TIHI_OFF` | `tihi režim`, `zvona`, `čekići` | Isključi virtualni tihi režim preko `ESP` dashboarda |

## 🧾 Odgovori servisnog API-ja

Za `/api/...` rute `ESP` koristi ove `HTTP` statuse:

- `200` `Mega` je prihvatila naredbu i `ESP` vraća kratku tekstualnu potvrdu
- `409` `Mega` je zauzeta i nije prihvatila servisnu naredbu
- `502` `Mega` je eksplicitno odbila servisnu naredbu
- `504` `Mega` nije odgovorila unutar timeouta
- `500` lokalna konfiguracija rute na `ESP-u` nije valjana

Napomena:
- za `slavljenje` i `mrtvačko` `409` tipično znači da zvona ili inercija još traju i da korisnik treba pokušati ponovno

## ⏱️ NTP i vrijeme

- `NTP` sinkronizacija vremena toranjskog sata ne ide kroz `/api/...` rute nego kroz serijski protokol `NTPCFG:` i `NTPREQ:SYNC`
- `Mega` bira siguran trenutak za `NTPREQ:SYNC`
- `ESP` koristi UDP `NTP` s `fraction` dijelom i `RTT/2` korekcijom
- `ESP` prema `Megi` sada salje `NTP:YYYY-MM-DDTHH:MM:SS.mmm;DST=0/1`, pa toranjski sat uz cijelu sekundu dobiva i milisekundni dio uzorka
- prvi `NTP` uzorak nakon restarta ili `WiFi` reconnecta ne šalje se odmah `Megi`
- prvi uzorak se pamti, a `ESP` odmah traži drugi radi stabilizacije
- tek potvrđen drugi uzorak postaje autoritet za prvu sinkronizaciju toranjskog sata
- `Mega` i dalje poravnava primjenu na `RTC SQW` granicu sekunde, ali sada pri tom izboru koristi i milisekundni dio uzorka
- nakon ručnog unosa vremena na `Megi` `ESP` ne šalje `NTP` odmah, nego tek u prvom sljedećem sigurnom prozoru koji `Mega` odabere

## 🧠 Napomene za rad toranjskog sata

- API ne zaobilazi `safe mode` ni recovery odluke iz `main/power_recovery.*`
- API ne potvrđuje latched fault niti `RTC` upozorenja; to ostaje lokalna funkcija tipki i `LCD`-a toranjskog sata
- `ESP` ne upravlja izravno relejima `kazaljki`, `okretne ploče`, `zvona` ili `čekića`, nego samo šalje servisni zahtjev `Megi`
- `TIHI MOD` preko weba ulazi u isti jedinstveni tihi režim kao fizički kip-prekidač u [main/prekidac_tisine.cpp](../main/prekidac_tisine.cpp)
- ako se fizički kip-prekidač tihog moda promijeni, on postaje autoritet i gasi prethodno webom zadanu virtualnu blokadu
