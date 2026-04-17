# 🔧 Mega firmware toranjskog sata

Ova podmapa sadrzi glavni firmware za `Arduino Mega 2560`. Mega je glavni kontroler toranjskog sata i jedino mjesto istine za mehaniku, postavke i recovery.

## ✨ Odgovornosti Mege

- upravljanje kazaljkama sata
- upravljanje okretnom plocom
- upravljanje zvonima i cekicima
- lokalne postavke preko LCD izbornika i tipki
- pohrana postavki i stanja u `24C32 EEPROM`
- recovery nakon watchdog i power-loss reseta
- obrada DCF, RTC i NTP izvora vremena

## 🧭 Podjela poslova Mega / ESP

- `Mega 2560` vodi sve radne odluke toranjskog sata.
- `ESP8266`, `ESP32` ili `Raspberry Pi` je samo pomocni mrezni sloj.
- Vanjski mrezni sloj donosi WiFi, NTP, setup WiFi, Home Assistant integraciju i servisni API.
- Postavke rada sata vise se ne uredjuju preko ESP weba.
- Stare `WEBCFG` poruke ostale su samo kao kompatibilno odbijanje u `main/esp_serial.cpp`.

## 🧩 Najvazniji moduli

- `main.ino` - inicijalizacija i glavna petlja
- `time_glob.*` - upravljanje izvorima vremena i sinkronizacijom
- `esp_serial.*` - UART protokol prema vanjskom mreznom mostu
- `kazaljke_sata.*` - kretanje i sinkronizacija kazaljki
- `okretna_ploca.*` - polozaj, koraci i faze ploce
- `zvonjenje.*` - zvona i pripadna stanja
- `otkucavanje.*` - satno i polusatno otkucavanje, slavljenje i mrtvacko
- `menu_system.*`, `lcd_display.*`, `tipke.*` - lokalni korisnicki sloj
- `postavke.*` - trajne postavke toranjskog sata
- `unified_motion_state.*` - zajednicko stanje gibanja
- `power_recovery.*` i `watchdog.*` - pouzdanost rada 24/7
- `wear_leveling.*` i `i2c_eeprom.*` - trajna pohrana i raspodjela zapisa

## ⏱️ Izvori vremena

- `DS3231 RTC` je glavni izvor za offline rad
- `NTP` dolazi preko ESP modula, ali trenutak sinkronizacije bira `Mega 2560`
- `DCF77` sluzi kao dodatna sinkronizacija i fallback
- automatski prijelaz CET/CEST ostaje pod kontrolom firmwarea toranjskog sata
- `Mega` trazi `NTP` samo u sigurnom prozoru, kad kazaljke i okretna ploca nisu usred koraka

## 🔌 Serijska komunikacija s ESP-om

- Mega trenutno koristi `Serial3` za ugradeni `ESP8266` na Mega+WiFi R3 plocici
- `Serial1` ostaje pripremljen za buduci vanjski `Raspberry Pi` most
- aktivni tokovi su `WIFI:`, `WIFIEN:`, `WIFISTATUS?`, `NTPCFG:`, `NTPREQ:SYNC`, `NTP:`, `CMD:` i `STATUS?`
- `NTPREQ:SYNC` sluzi za kontrolirani zahtjev prema ESP-u kad je mehanika toranjskog sata mirna
- vanjski mrezni most vise ne salje `NTP:` po vlastitom rasporedu, nego odgovara na zahtjev Mege
- `WEBCFG?` i `WEBCFGSET:` vise ne nose konfiguraciju sata i vracaju `ERR:WEBCFGDISABLED`

## 💾 EEPROM i recovery

- `24C32 EEPROM` cuva postavke i kriticno radno stanje
- `wear_leveling` smanjuje trosenje kroz kruzno spremanje
- `unified_motion_state.*` i `power_recovery.*` vracaju kazaljke i plocu u dosljedno stanje nakon restarta
- pri svakoj izmjeni EEPROM rasporeda ili recovery logike provjeri:
- `eeprom_konstante.h`
- `unified_motion_state.*`
- `power_recovery.*`

## 🔩 Hardver koji Mega vodi

- releji za parne i neparne faze kazaljki
- releji za okretnu plocu
- izlazi za zvona i cekice
- DS3231 RTC i 24C32 EEPROM preko I2C
- LCD 16x2 preko I2C
- DCF77 prijemnik
- 4x5 matricna tipkovnica, servisni ulazi i brojcani unos `HH:MM`

## ✅ Smjernice za razvoj

- glavna petlja mora ostati neblokirajuca
- Mega mora ostati sigurna i bez ovisnosti o stalnoj mrezi
- kvar ili restart ESP-a ne smije ugroziti osnovni rad sata
- svaka promjena koja dira kazaljke, plocu, zvona ili recovery treba se provjeriti u odnosu na postojece module u `main/`
