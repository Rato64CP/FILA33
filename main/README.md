# 🔧 ZVONKO v. 1.0 - Mega firmware

Ova podmapa sadrzi glavni firmware projekta `ZVONKO v. 1.0` za `Arduino Mega 2560`. Mega je glavni kontroler toranjskog sata i jedino mjesto istine za mehaniku, postavke i recovery.

## ✨ Odgovornosti Mege

- upravljanje kazaljkama sata
- upravljanje okretnom plocom
- upravljanje zvonima i cekicima
- blagdansko slavljenje i posebni raspored mrtvackog za Svi sveti / Dusni dan
- lokalne postavke preko LCD izbornika i tipki
- pohrana postavki i stanja u `24C32 EEPROM`
- recovery nakon watchdog i power-loss reseta
- obrada RTC i NTP izvora vremena
- jedinstveni tihi rezim, BAT logika i lokalni overridei

## 🧭 Podjela poslova Mega / ESP

- `Mega 2560` vodi sve radne odluke toranjskog sata
- `ESP8266` ili `ESP32` je samo pomocni mrezni sloj
- vanjski mrezni sloj donosi WiFi, NTP, setup WiFi i bezicni servisni API
- postavke rada sata vise se ne uredjuju preko ESP weba
- stare `WEBCFG` poruke ostale su samo kao kompatibilno odbijanje u `main/esp_serial.cpp`

## 🧩 Najvazniji moduli

- `main.ino` - inicijalizacija i glavna petlja
- `time_glob.*` - upravljanje izvorima vremena, DST-om i sinkronizacijom
- `esp_serial.*` - UART protokol prema vanjskom mreznom mostu
- `kazaljke_sata.*` - kretanje i sinkronizacija kazaljki
- `okretna_ploca.*` - polozaj, koraci, faze i cavli ploce
- `zvonjenje.*` - zvona i pripadna stanja
- `otkucavanje.*` - satno i polusatno otkucavanje
- `slavljenje_mrtvacko.*` - slavljenje, mrtvacko i thumbwheel timer
- `prekidac_tisine.*` - jedinstveni tihi rezim i lampica
- `menu_system.*`, `lcd_display.*`, `tipke.*` - lokalni korisnicki sloj
- `postavke.*` - trajne postavke toranjskog sata
- `unified_motion_state.*` - zajednicko stanje gibanja
- `power_recovery.*` i `watchdog.*` - pouzdanost rada 24/7
- `wear_leveling.*` i `i2c_eeprom.*` - trajna pohrana i raspodjela zapisa

## ⏱️ Izvori vremena

- `DS3231 RTC` je glavni izvor za offline rad
- `NTP` dolazi preko ESP modula, ali trenutak sinkronizacije bira `Mega 2560`
- automatski prijelaz CET/CEST ostaje pod kontrolom firmwarea toranjskog sata
- `Mega` trazi `NTP` samo u sigurnom prozoru, kad kazaljke i okretna ploca nisu usred koraka

## 🔄 Serijska komunikacija s ESP-om

- Mega trenutno koristi `Serial3` za ugradeni `ESP8266` na Mega+WiFi R3 plocici
- `Serial1` nije dio aktivnog mreznog puta; komunikacija prema ESP-u ostaje na `Serial3`
- aktivni tokovi su `WIFI:`, `WIFIEN:`, `WIFISTATUS?`, `NTPCFG:`, `NTPREQ:SYNC`, `NTP:`, `CMD:` i `STATUS?`
- `NTPREQ:SYNC` sluzi za kontrolirani zahtjev prema ESP-u kad je mehanika toranjskog sata mirna
- vanjski mrezni most vise ne salje `NTP:` po vlastitom rasporedu, nego odgovara na zahtjev Mege
- `WEBCFG?` i `WEBCFGSET:` vise ne nose konfiguraciju sata i vracaju `ERR:WEBCFGDISABLED`

## 💾 EEPROM i recovery

- `24C32 EEPROM` cuva postavke i kriticno radno stanje
- `UnifiedMotionState` koristi `24` rotirajuca slota za kazaljke i okretnu plocu
- `power_recovery.*` vraca kazaljke i plocu u dosljedno stanje nakon restarta
- `offset` ploce i MQTT tragovi vise nisu dio aktivnog modela
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
- thumbwheel `00-99` za trajanje mrtvackog zvona
- kip-prekidac tihog moda i lampica tihog moda
- LED lampice za `ZVONO 1`, `ZVONO 2`, `SLAVLJENJE` i `MRTVACKO`
- 4x5 matricna tipkovnica, servisni ulazi i brojcani unos `HH:MM`

## ✅ Smjernice za razvoj

- glavna petlja mora ostati neblokirajuca
- Mega mora ostati sigurna i bez ovisnosti o stalnoj mrezi
- kvar ili restart ESP-a ne smije ugroziti osnovni rad sata
- svaka promjena koja dira kazaljke, plocu, zvona ili recovery treba se provjeriti u odnosu na postojece module u `main/`
