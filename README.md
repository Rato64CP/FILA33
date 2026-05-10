# 🕰️ ZVONKO v. 1.0

`ZVONKO v. 1.0` je firmware i upravljacka logika za toranjski sat temeljena na podjeli poslova izmedu `Arduino Mega 2560` i ESP mreznog sloja.

## ✨ Sto sustav radi

- vodi vrijeme preko `DS3231 RTC` i kontroliranog `NTP` zahtjeva
- upravlja kazaljkama sata uz korekciju i sinkronizaciju
- upravlja okretnom plocom kroz dvofazne korake i citanje cavala
- vodi zvona, cekice, slavljenje i mrtvacko
- podrzava odvojenu inerciju `INR1` i `INR2` za dva razlicita zvona
- podrzava opciju `K:0/1` za rad s kocnicom zvona ili bez nje
- uvodi termalnu zastitu slavljenja nakon `3 minute` rada kroz pauzu `3 s` svakih `30 s`
- podrzava blagdansko slavljenje i posebni raspored mrtvackog za Svi sveti / Dusni dan
- cuva postavke i kriticno stanje u `24C32 EEPROM-u`
- vraca sustav u valjano stanje nakon watchdog ili power-loss reseta
- zakljucava mehaniku u `safe mode` ako se dogodi previse watchdog resetova u kratkom vremenu
- prati zdravlje `RTC` i `EEPROM` podsustava i prelazi u ograniceni rad kad kvar postane ponovljiv
- pamti latched kvar `EEPROM-a` do rucne potvrde operatera
- podrzava jedinstveni tihi rezim za uskrsnu tisinu, rucni kip-prekidac i webski virtualni toggle preko `ESP` dashboarda
- podrzava `UPS mod` koji bez mreze ostavlja logiku toranjskog sata zivom, ali blokira izlaze prema kazaljkama, zvonima i cekicima

## 🧭 Arhitektura

- `Arduino Mega 2560` upravlja kazaljkama, okretnom plocom, zvonima, cekicima, lokalnim postavkama i recovery logikom
- ESP mrezni sloj sluzi za `WiFi`, `NTP`, bezicni servisni API, `OTA` nadogradnju i servisni dashboard
- `Mega 2560` je jedino mjesto istine za stanje toranjskog sata
- osnovni rad sata mora ostati moguc i bez mreze

## 🔐 Pravila Mega <-> ESP

- Mega inicira kriticne operacije vezane uz rad toranjskog sata
- ESP ne donosi odluke o mehanici kazaljki, ploce, zvona ni cekica
- `NTP` sinkronizacija ide samo kad `Mega` procijeni da je trenutak siguran
- kvar ili restart vanjskog mreznog sloja ne smije zaustaviti osnovni rad sata

## 🔄 Serijska komunikacija

- Mega koristi `Serial3` za ugradeni `ESP8266` kao jedini aktivni mrezni most
- aktivne naredbe su `WIFI:`, `WIFIEN:`, `WIFISTATUS?`, `NTPCFG:`, `NTPREQ:SYNC`, `NTP:`, `CMD:` i `STATUS?`
- `Mega 2560` sama bira siguran trenutak za `NTPREQ:SYNC`, tek kad su kazaljke i okretna ploca mirne
- `ESP` vise ne salje `NTP:` automatski po spajanju ili satno, nego odgovara samo na zahtjev Mege
- prvi `NTP` nakon restarta ili `WiFi` reconnecta `ESP` potvrduje drugim uzorkom prije prve sinkronizacije toranjskog sata
- stare `WEBCFG?` i `WEBCFGSET:` poruke ostavljene su samo radi kompatibilnog odbijanja i vracaju `ERR:WEBCFGDISABLED`

## 🧩 Struktura Projekta

- `main/` - glavni firmware toranjskog sata za `Arduino Mega 2560`
- `esp_firmware/` - pomocni firmware za `ESP8266` i `ESP32`
- `main/main.ino` - inicijalizacija i glavna petlja
- `main/time_glob.*` - RTC, NTP, DST i prioriteti izvora vremena
- `main/esp_serial.*` - serijska komunikacija s ESP modulom
- `main/kazaljke_sata.*` - logika kazaljki i korekcije
- `main/okretna_ploca.*` - upravljanje polozajem, fazama i cavlima
- `main/zvonjenje.*` - upravljanje zvonima i inercijom
- `main/otkucavanje.*` - cekici, satno i polusatno otkucavanje
- `main/slavljenje_mrtvacko.*` - posebni nacini rada cekica i thumbwheel timer mrtvackog
- `main/prekidac_tisine.*` - jedinstveni tihi rezim i lampica tihog moda
- `main/ups_nadzor.*` - nadzor mreznog napona i `UPS mod`
- `main/menu_system.*`, `main/tipke.*`, `main/lcd_display.*` - lokalni LCD izbornik i unos
- `main/postavke.*` - citanje, validacija i spremanje postavki
- `main/unified_motion_state.*` - zajednicko stanje kazaljki i ploce
- `main/power_recovery.*` i `main/watchdog.*` - oporavak i pouzdanost rada 24/7
- `main/wear_leveling.*` i `main/i2c_eeprom.*` - trajna pohrana u `24C32`

## 📶 Setup WiFi

- `ESP8266` moze pokrenuti privremenu setup mrezu `ZVONKO_setup`
- lozinka setup mreze je `zvonko10`
- setup AP se aktivira dugim pritiskom tipke na `GPIO14 / D5` prema `GND`
- setup AP se moze aktivirati i dugim istovremenim pritiskom `lijevo + desno` na tipkovnici, ali samo s glavnog prikaza sata
- status LED koristi `GPIO12 / D6`
- setup stranica je dostupna na `http://192.168.4.1/` i `http://192.168.4.1/setup`
- nakon spremanja nove mreze `ESP8266` prosljeduje WiFi podatke i Megi kako bi cijeli toranjski sat ostao uskladen
- servisni dashboard na `ESP` sada koristi glavne tipke `MUSKO`, `ZENSKO`, `SLAVI`, `BRECA`, sunceve tipke `JUTRO`, `PODNE`, `VECER` i crveni toggle `TIHI MOD`

## 💾 EEPROM I Recovery

- `24C32 EEPROM` cuva postavke, `UnifiedMotionState`, DST status i kriticni backup
- `UnifiedMotionState` koristi `24` rotirajuca slota za kazaljke i okretnu plocu
- svaki `UnifiedMotionState` slot ima checksum i nevaljan ili polovicno upisan zapis se preskace
- zapis zadnje sinkronizacije vremena sada ima vlastiti checksum uz kompatibilno citanje starog formata
- `power_recovery.*` vraca kazaljke i plocu u dosljedno stanje nakon restarta
- watchdog resetovi se prate kroz perzistentni brojac i nakon vise uzastopnih watchdog resetova aktivira se `safe mode`
- `safe mode` blokira kazaljke, plocu, zvona i cekice dok operater ne drzi `ENT / SELECT` `5 s`
- zdravlje `EEPROM-a` se provjerava i pri bootu i periodicki svakih `6 sati`
- kvar `EEPROM-a` ostaje latched u memoriji do rucne potvrde operatera
- kad je `EEPROM` u degradiranom nacinu rada, periodicni backup i pomocni zapisi poput DST i zadnje sinkronizacije se pauziraju
- `I2C` sabirnica koristi zajednicki `Wire` timeout i reset sabirnice za `LCD`, `DS3231`, `24C32` i servisno skeniranje
- `EEPROM/I2C` retry i polling petlje osvjezavaju watchdog kad je aktivan kako pomocni zapisi ne bi nepotrebno gurali toranjski sat prema WDT resetu
- stari `offset` ploce i MQTT tragovi vise nisu dio aktivnog EEPROM modela
- kod izmjena koje diraju EEPROM raspored ili recovery logiku obavezno provjeri:
- `main/eeprom_konstante.h`
- `main/unified_motion_state.*`
- `main/power_recovery.*`

## 🔕 Tihi Rezim I BAT

- jedinstveni tihi rezim moze se aktivirati uskrsnom tisinom, rucnim kip-prekidacem ili webskim virtualnim toggleom na `ESP` dashboardu
- lampica tihog moda svijetli samo kad je stvarno aktivan konacni tihi rezim
- tihi rezim blokira zvona, cekice, slavljenje i mrtvacko, ali ne zaustavlja kazaljke ni okretnu plocu
- `UPS mod` pali lampicu tihog moda i na LCD-u prikazuje `NEMA STRUJE!` dok mehanika toranjskog sata radi samo s pomocnog napajanja
- BAT / tihi sati iz postavki blokiraju samo otkucavanje
- sunceva automatika i cavli ploce rade i tijekom BAT raspona
- jutarnje suncevo zvono moze ranije otvoriti otkucavanje prije kraja BAT raspona
- blagdansko slavljenje ceka stvarni zavrsetak zvona, otkucavanja i inercije prije pokretanja

## ⚠️ Ponašanje Kod Gresaka

- gubitak `WiFi` veze: toranjski sat nastavlja rad preko `RTC`
- kvar `ESP8266`: nema utjecaja na osnovni rad kazaljki, ploce, zvona i cekica
- reset `Mega 2560`: recovery iz spremljenog stanja
- nestanak napajanja: nastavak iz zadnjeg valjanog stanja
- gubitak RTC SQW impulsa: kazaljke i ploca imaju `millis()` fallback za sigurno gasenje aktivne faze
- ponovljeni watchdog resetovi bez power-loss oznake: aktivira se `SUSTAV ZAKLJUCAN / PREVISE RESETA`
- ponovljena nevaljana RTC ocitanja: aktivira se `RTC OGRANICEN RAD / CEKAM OPORAVAK` i automatika vremena se privremeno blokira
- kvar `EEPROM-a`: aktivira se latched fault i periodicni EEPROM zapisi i health-checkovi se zaustavljaju do potvrde
- lampica zvona tijekom inercije treperi kako bi operater znao da cekice jos ne treba dirati
- lampica slavljenja treperi dok traje termalna pauza, iako slavljenje ostaje aktivno
- ako je `UPS mod` aktivan i nestane mreze, glavni LCD umjesto datuma prikazuje `NEMA STRUJE!`

## 🔧 Hardver

- `Arduino Mega 2560`
- `ESP8266`
- `DS3231 RTC`
- `24C32 EEPROM`
- `LCD 16x2` preko `I2C`
- dva trofazna elektromotora `Koncar 0.55 kW / 380 V`, po jedan za svako zvono toranjskog sata
- na straznjoj osovini svakog zvonarskog motora mikroprekidaci za okretanje faza i prijelaz rada zvona
- dva elektromagnetska bata / cekica `310 VDC`, po jedan za svako zvono, s impulsom oko `0,01 s`
- pogonski motor toranjskog sata za kazaljke s mehanizmom zupcanika koji radi na `PARNI/NEPARNI` impuls u trajanju oko `6 s`
- elektroormar s kontaktorima za okretanje faza zvona, kontaktorima za batove, osiguracima i ostalom zastitnom opremom
- thumbwheel `00-99` za trajanje mrtvackog zvona
- kip-prekidac tihog moda i lampica tihog moda
- LED lampice za `ZVONO 1`, `ZVONO 2`, `SLAVLJENJE` i `MRTVACKO`
- relejni izlazi za kazaljke, plocu, zvona i cekice
- 6 direktnih tipki za lokalni izbornik i servisne funkcije (`GORE`, `DOLJE`, `LIJEVO`, `DESNO`, `DA`, `NE`)
- lokalni servisni sloj koristi `ENT / SELECT` i za otkljucavanje `safe mode-a` te potvrdu latched kvarova
- lokalni meni `Sustav` sada uredjuje `UPS mod`, `K:0/1`, `INR1` i `INR2`

## 📚 Dodatni README

- [README za Mega firmware](main/README.md)
- [README za ESP firmware](esp_firmware/README.md)
- [Popis ESP web API ruta toranjskog sata](docs/esp_web_api_toranjskog_sata.md)
- [Tehnicka dokumentacija firmware sustava](docs/tehnicka_dokumentacija_firmware_sustava.md)

## 🛠️ Napomene Za Razvoj

- glavna petlja mora ostati neblokirajuca
- `Mega 2560` mora ostati autoritet za stanje toranjskog sata
- kvar ili restart `ESP8266` ne smije utjecati na osnovni rad sata
- `I2C` pristup za `LCD`, `RTC` i `24C32` treba ostati na zajednickoj pripremi sabirnice s timeoutom
- promjene koje diraju kazaljke, plocu, zvona, sinkronizaciju vremena ili recovery treba provjeriti u odnosu na postojece module u `main/`
