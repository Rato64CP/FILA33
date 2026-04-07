# 🕰️ Automatika toranjskog sata

Firmware i prateca logika za toranjski sat temeljen na podjeli poslova:
- `Arduino Mega 2560` upravlja kazaljkama, okretnom plocom, zvonima, cekicima, lokalnim postavkama i recovery logikom.
- `ESP8266` sluzi kao vanjski mrezni modul za WiFi, NTP, setup WiFi i servisni API.

## ✨ Sto sustav radi

- vodi vrijeme preko `DS3231 RTC`, `NTP` i `DCF77`
- upravlja kazaljkama sata uz korekciju i sinkronizaciju
- upravlja okretnom plocom kroz parne i neparne faze
- vodi zvona, cekice, slavljenje i mrtvacko
- cuva kriticno stanje i postavke u `24C32 EEPROM-u`
- vraca sustav u valjano stanje nakon watchdog ili power-loss reseta

## 🧭 Trenutna arhitektura

- `main/` je glavni firmware toranjskog sata za `Mega 2560`.
- `esp_firmware/` je pomocni firmware za `ESP8266`.
- Mega je jedino mjesto istine za radne postavke sata.
- ESP vise ne uredjuje postavke sata preko weba.
- ESP web sloj ostaje ogranicen na `/`, `/setup`, `/status` i `/api/...`.

## 🔌 Serijska veza Mega <-> ESP

- Mega koristi `Serial3` za komunikaciju s ESP modulom.
- Aktivne naredbe obuhvacaju `WIFI:`, `WIFIEN:`, `WIFISTATUS?`, `NTPCFG:`, `NTPREQ:SYNC`, `NTP:`, `CMD:` i `STATUS?`.
- Stare `WEBCFG?` i `WEBCFGSET:` poruke ostavljene su samo radi kompatibilnog odbijanja i vracaju `ERR:WEBCFGDISABLED`.

## 🧩 Glavni moduli u `main/`

- `main.ino` - inicijalizacija i glavna petlja toranjskog sata
- `time_glob.*` - RTC, NTP, DCF i prioritet izvora vremena
- `esp_serial.*` - serijska veza s ESP modulom
- `kazaljke_sata.*` - logika kazaljki i korekcije
- `okretna_ploca.*` - upravljanje polozajem i fazama ploce
- `zvonjenje.*` - upravljanje zvonima
- `otkucavanje.*` - cekici, satno i polusatno otkucavanje
- `menu_system.*` i `tipke.*` - lokalni LCD izbornik i unos
- `postavke.*` - citanje, validacija i spremanje postavki
- `unified_motion_state.*` - zajednicko stanje kazaljki i ploce
- `power_recovery.*` i `watchdog.*` - 24/7 pouzdanost i oporavak
- `wear_leveling.*` i `i2c_eeprom.*` - trajna pohrana u 24C32

## 📶 Setup WiFi za toranjski sat

- ESP moze pokrenuti privremenu setup mrezu `FILA33_setup`.
- Lozinka setup mreze je `toranj33`.
- Setup AP se aktivira dugim pritiskom tipke na `GPIO14 / D5` prema `GND`.
- Status LED na `GPIO12 / D6` signalizira stanje WiFi veze i setup AP moda.
- Setup stranica je dostupna na `http://192.168.4.1/` i `http://192.168.4.1/setup`.
- Nakon spremanja nove mreze ESP prosljeduje WiFi podatke i Megi kako bi cijeli toranjski sat ostao uskladen.

## 💾 EEPROM i recovery

- `24C32 EEPROM` cuva postavke i kriticno radno stanje.
- `wear_leveling` smanjuje trosenje EEPROM-a kroz kruzno spremanje.
- `unified_motion_state.*` i `power_recovery.*` vracaju kazaljke i plocu u dosljedno stanje nakon restarta.
- Kod izmjena koje diraju EEPROM raspored ili recovery obavezno provjeri:
- `main/eeprom_konstante.h`
- `main/unified_motion_state.*`
- `main/power_recovery.*`

## 🔧 Hardver

- Arduino Mega 2560
- ESP8266 modul
- DS3231 RTC
- 24C32 EEPROM
- LCD 16x2 preko I2C
- DCF77 prijemnik
- relejni izlazi za kazaljke, plocu, zvona i cekice
- tipke za lokalni izbornik i servisne funkcije

## 📚 README vodi

- [README za Mega firmware](/C:/Users/Rato/Documents/GitHub/FILA33/main/README.md)
- [README za ESP firmware](/C:/Users/Rato/Documents/GitHub/FILA33/esp_firmware/README.md)

## 🛡️ Napomene za razvoj

- glavna petlja mora ostati neblokirajuca
- Mega mora ostati autoritet za postavke toranjskog sata
- kvar ESP-a ne smije zaustaviti osnovni rad kazaljki, ploce, zvona i cekica
- pri izmjenama recovery putanja provjeri uskladenost modula koji diraju EEPROM i stanje gibanja
