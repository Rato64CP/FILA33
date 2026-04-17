# 🕰️ Automatika Toranjskog Sata

Firmware i upravljacka logika za toranjski sat temeljena na podjeli poslova izmedu `Arduino Mega 2560` i vanjskog mreznog sloja (`ESP8266`, `ESP32` ili `Raspberry Pi` most).

## ✨ Sto sustav radi

- vodi vrijeme preko `DS3231 RTC`, `NTP` i `DCF77`
- upravlja kazaljkama sata uz korekciju i sinkronizaciju
- upravlja okretnom plocom kroz parne i neparne faze
- vodi zvona, cekice, slavljenje i mrtvacko
- cuva postavke i kriticno stanje u `24C32 EEPROM-u`
- vraca sustav u valjano stanje nakon watchdog ili power-loss reseta

## 🧭 Arhitektura

- `Arduino Mega 2560` upravlja kazaljkama, okretnom plocom, zvonima, cekicima, lokalnim postavkama i recovery logikom
- vanjski mrezni sloj (`ESP8266`, `ESP32` ili `Raspberry Pi`) sluzi za `WiFi`, `NTP`, `Home Assistant` integraciju i servisni API
- `Mega 2560` je jedino mjesto istine za stanje toranjskog sata
- osnovni rad sata mora ostati moguc i bez mreze

## 🔐 Pravila Mega <-> ESP

- Mega inicira kriticne operacije vezane uz rad toranjskog sata
- ESP ne donosi odluke o mehanici kazaljki, ploce, zvona ni cekica
- `NTP` sinkronizacija ide samo kad `Mega` procijeni da je trenutak siguran
- kvar ili restart vanjskog mreznog sloja ne smije zaustaviti osnovni rad sata

## 🔌 Serijska komunikacija

- Mega trenutno koristi `Serial3` za ugradeni `ESP8266`, a `Serial1` je pripremljen za buduci `Raspberry Pi` most
- aktivne naredbe su `WIFI:`, `WIFIEN:`, `WIFISTATUS?`, `NTPCFG:`, `NTPREQ:SYNC`, `NTP:`, `CMD:` i `STATUS?`
- `Mega 2560` sama bira siguran trenutak za `NTPREQ:SYNC`, tek kad su kazaljke i okretna ploca mirne
- `ESP` vise ne salje `NTP:` automatski po spajanju ili satno, nego odgovara samo na zahtjev Mege
- stare `WEBCFG?` i `WEBCFGSET:` poruke ostavljene su samo radi kompatibilnog odbijanja i vracaju `ERR:WEBCFGDISABLED`

## 🧩 Struktura Projekta

- `main/` - glavni firmware toranjskog sata za `Arduino Mega 2560`
- `esp_firmware/` - pomocni firmware za `ESP8266` i `ESP32`
- `pi_bridge/` - Python serijski most za `Raspberry Pi` i `Home Assistant`
- `main/main.ino` - inicijalizacija i glavna petlja
- `main/time_glob.*` - RTC, NTP, DCF i prioriteti izvora vremena
- `main/esp_serial.*` - serijska komunikacija s ESP modulom
- `main/kazaljke_sata.*` - logika kazaljki i korekcije
- `main/okretna_ploca.*` - upravljanje polozajem i fazama ploce
- `main/zvonjenje.*` - upravljanje zvonima
- `main/otkucavanje.*` - cekici, satno i polusatno otkucavanje
- `main/menu_system.*`, `main/tipke.*`, `main/lcd_display.*` - lokalni LCD izbornik i unos
- `main/postavke.*` - citanje, validacija i spremanje postavki
- `main/unified_motion_state.*` - zajednicko stanje kazaljki i ploce
- `main/power_recovery.*` i `main/watchdog.*` - oporavak i pouzdanost rada 24/7
- `main/wear_leveling.*` i `main/i2c_eeprom.*` - trajna pohrana u `24C32`

## 📶 Setup WiFi

- `ESP8266` moze pokrenuti privremenu setup mrezu `FILA33_setup`
- lozinka setup mreze je `toranj33`
- setup AP se aktivira dugim pritiskom tipke na `GPIO14 / D5` prema `GND`
- status LED koristi `GPIO12 / D6`
- setup stranica je dostupna na `http://192.168.4.1/` i `http://192.168.4.1/setup`
- nakon spremanja nove mreze `ESP8266` prosljeduje WiFi podatke i Megi kako bi cijeli toranjski sat ostao uskladen

## 💾 EEPROM I Recovery

- `24C32 EEPROM` cuva postavke i kriticno radno stanje
- `wear_leveling` smanjuje trosenje EEPROM-a kroz kruzno spremanje
- `unified_motion_state.*` i `power_recovery.*` vracaju kazaljke i plocu u dosljedno stanje nakon restarta
- kod izmjena koje diraju EEPROM raspored ili recovery logiku obavezno provjeri:
- `main/eeprom_konstante.h`
- `main/unified_motion_state.*`
- `main/power_recovery.*`

## ⚠️ Ponasanje Kod Gresaka

- gubitak `WiFi` veze: toranjski sat nastavlja rad preko `RTC`
- kvar `ESP8266`: nema utjecaja na osnovni rad kazaljki, ploce, zvona i cekica
- reset `Mega 2560`: recovery iz spremljenog stanja
- nestanak napajanja: nastavak iz zadnjeg valjanog stanja

## 🔧 Hardver

- `Arduino Mega 2560`
- `ESP8266`
- `DS3231 RTC`
- `24C32 EEPROM`
- `LCD 16x2` preko `I2C`
- `DCF77` prijemnik
- relejni izlazi za kazaljke, plocu, zvona i cekice
- 4x5 matricna tipkovnica za lokalni izbornik, servisne funkcije i brojcani unos `HH:MM`

## 📚 Dodatni README

- [README za Mega firmware](/C:/Users/Rato/Documents/GitHub/FILA33/main/README.md)
- [README za ESP firmware](/C:/Users/Rato/Documents/GitHub/FILA33/esp_firmware/README.md)
- [README za Raspberry Pi most](/C:/Users/Rato/Documents/GitHub/FILA33/pi_bridge/README.md)

## 🛠️ Napomene Za Razvoj

- glavna petlja mora ostati neblokirajuca
- `Mega 2560` mora ostati autoritet za stanje toranjskog sata
- kvar ili restart `ESP8266` ne smije utjecati na osnovni rad sata
- promjene koje diraju kazaljke, plocu, zvona, sinkronizaciju vremena ili recovery treba provjeriti u odnosu na postojece module u `main/`
