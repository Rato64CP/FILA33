# 🔧 ZVONKO v. 1.0 - Mega firmware

Ova podmapa sadrzi glavni firmware projekta `ZVONKO v. 1.0` za `Arduino Mega 2560`. Mega je glavni kontroler toranjskog sata i jedino mjesto istine za mehaniku, postavke i recovery.

## ✨ Odgovornosti Mege

- upravljanje kazaljkama sata
- upravljanje okretnom plocom
- upravljanje zvonima i cekicima
- odvojena inercija `INR1` i `INR2` za dva zvona
- izbor rada s kocnicom zvona kroz `K:0/1`
- sigurnosno gasenje rucnih sklopki `ZVONO 1`, `ZVONO 2` i prekidaca `SLAVLJENJE` nakon `30 min` neprekidnog ukljucenja
- termalna zastita slavljenja nakon `3 minute` rada kroz pauzu `3 s` svakih `30 s`
- blagdansko slavljenje i posebni raspored mrtvackog za Svi sveti / Dusni dan
- lokalne postavke preko LCD izbornika i tipki
- pohrana postavki i stanja u vanjski `24C32 EEPROM` ili `FM24W256 FRAM`
- recovery nakon watchdog i power-loss reseta
- watchdog `safe mode` i servisno otkljucavanje nakon previse resetova
- runtime dijagnostika `EEPROM-a` i latched fault potvrda preko LCD-a i tipki
- obrada RTC i NTP izvora vremena
- degradirani nacin rada za `RTC` i `EEPROM` kad se kvar ponavlja
- jedinstveni tihi rezim, BAT logika, lokalni overridei i webski virtualni toggle tihog moda
- `UPS mod` s odvojenim ulazom za nadzor mreznog napona

## 🧭 Podjela poslova Mega / ESP

- `Mega 2560` vodi sve radne odluke toranjskog sata
- vanjski `ESP32` je samo pomocni mrezni sloj
- vanjski mrezni sloj donosi WiFi, NTP, setup WiFi i bezicni servisni API
- preko `ESP32` weba sada su dopustene sigurne skupine `Sustav`, `Stapici`, `BAT`, `Sunce` i `Blagdani`, dok `Mega` i dalje ostaje jedini autoritet za validaciju i spremanje
- zasebni modul `main/mise_automatika.*` sada vodi redovite dnevne i nedjeljne mise te posebne blagdanske mise, odvojeno od `main/sunceva_automatika.*`
- zasebni modul `main/pogrebne_skripte.*` sada vodi jednokratne sekvence `POKOJNIK` i `POKOJNICA` s `ESP32` dashboarda
- `POKOJNIK` pokrece `MUSKO` zvono `2 minute`, ceka zavrsetak inercije pa zatim pokrece `MRTVACKO` `10 minuta`
- `POKOJNICA` pokrece `ZENSKO` zvono `2 minute`, ceka zavrsetak inercije pa zatim pokrece `MRTVACKO` `10 minuta`
- `Blagdani` koriste unaprijed zadanu listu nepomicnih i pomicnih blagdana; web i serijski sloj uredjuju samo ukljucenje i vrijeme mise `HH:MM`, a toranjski sat zatim sam vodi nedjeljno zvonjenje oba zvona `2 h` i `1 h` prije mise, bez dodatnog `slavljenja`
- `Redovite mise` nose dnevno i nedjeljno vrijeme mise `HH:MM`; dnevna misa koristi samo `MUSKO` zvono `30 min` prije mise, a nedjeljna misa nedjeljno zvonjenje oba zvona `2 h` i `1 h` prije mise
- prazno vrijeme iz web sloja znaci da su odgovarajuca dnevna misa, nedjeljna misa ili blagdan iskljuceni, bez obzira na stanje kvacice
- sva misna zvonjenja iz [main/mise_automatika.cpp](mise_automatika.cpp) startaju u `25.` sekundi minute, sinkronizirano s citanjem cavala iz [main/okretna_ploca.cpp](okretna_ploca.cpp)
- `BAT od/do` iz weba i lokalnog menija tumace se kao raspon u kojem je redovno otkucavanje dopusteno; izvan njega `Mega` blokira samo otkucavanje

## 🧩 Najvazniji moduli

- `main.ino` - inicijalizacija i glavna petlja
- `time_glob.*` - upravljanje izvorima vremena, DST-om i sinkronizacijom
- `esp_serial.*` - javna jezgra UART protokola prema vanjskom mreznom mostu
- `esp_serial_internal.h` - interni dogovor izmedu serijskih podmodula
- `esp_serial_status.cpp` - `STATUS` snapshot i push prema `ESP32` dashboardu
- `esp_serial_ntp.cpp` - `WiFi`, `NTP` i vremenska koordinacija prema mreznom mostu
- `esp_serial_postavke.cpp` - `SETREQ/SETCFG` razmjena skupina postavki
- `esp_serial_cmd.cpp` - `CMD:` komande za zvona, slavljenje, tihi mod i pogrebne skripte
- `esp_serial_parser.cpp` - parser dolaznih redaka s `ESP32` mosta
- `kazaljke_sata.*` - kretanje i sinkronizacija kazaljki
- `okretna_ploca.*` - polozaj, koraci, faze i cavli ploce
- `mise_automatika.*` - redovite dnevne/nedjeljne mise i blagdanske mise
- `pogrebne_skripte.*` - jednokratne sekvence `POKOJNIK` i `POKOJNICA`
- `zvonjenje.*` - zvona i pripadna stanja
- `otkucavanje.*` - satno i polusatno otkucavanje
- `slavljenje_mrtvacko.*` - slavljenje, mrtvacko i thumbwheel timer
- `prekidac_tisine.*` - jedinstveni tihi rezim i lampica
- `ups_nadzor.*` - nadzor mreznog napona i blokada mehanike dok toranjski sat radi samo s UPS-a
- `menu_system.*`, `lcd_display.*`, `tipke.*` - lokalni korisnicki sloj
- `postavke.*` - glavna jezgra trajnih postavki toranjskog sata
- `postavke_skladistenje.*` - checksum, citanje/pisanje spremnika i EEPROM helperi
- `postavke_mreza.*` - WiFi, IP i NTP tekstualna validacija za `ESP32` most
- `postavke_kalendar.*` - liturgijski kalendar, blagdani i satnice misa
- `unified_motion_state.*` - zajednicko stanje gibanja
- `power_recovery.*` i `watchdog.*` - pouzdanost rada 24/7
- `wear_leveling.*` i `i2c_eeprom.*` - trajna pohrana i raspodjela zapisa

## ⏱️ Izvori vremena

- `DS3231 RTC` je glavni izvor za offline rad
- `NTP` dolazi preko ESP modula, ali trenutak sinkronizacije bira `Mega 2560`
- automatski prijelaz CET/CEST ostaje pod kontrolom firmwarea toranjskog sata
- `Mega` trazi `NTP` samo u sigurnom prozoru, kad kazaljke i okretna ploca nisu usred koraka
- nakon restarta se starost zadnje sinkronizacije rekonstruira iz RTC vremena kako novi boot ne bi lazno izgledao svjez `24 sata`
- nakon vise uzastopnih nevaljanih RTC ocitanja aktivira se `RTC OGRANICEN RAD` i automatika se drzi na sigurnoj strani dok se RTC ne oporavi
- gubitak `RTC/I2C` veze sada pali strogi izlazni fail-safe u [main/time_glob.cpp](time_glob.cpp), pa zvona, cekici, kazaljke i okretna ploca ostaju blokirani dok se `DS3231` ponovno ne oporavi

## 🔄 Serijska komunikacija s ESP-om

- Mega trenutno koristi `Serial3` za vanjski `ESP32` mrezni most
- `Serial1` je aktivni `RS485` transportni sloj, dok komunikacija prema `ESP-u` ostaje na `Serial3`
- aktivni tokovi su `WIFI:`, `WIFIEN:`, `WIFISTATUS?`, `NTPCFG:`, `NTPREQ:SYNC`, `NTP:`, `CMD:`, `STATUS?`, `SETREQ:*` i `SETCFG:*` za skupine `SUSTAV`, `STAPICI`, `BAT`, `SUNCE`, `MISE`, `BLAGDANI_NEP` i `BLAGDANI_POM`
- `NTPREQ:SYNC` sluzi za kontrolirani zahtjev prema ESP-u kad je mehanika toranjskog sata mirna
- vanjski mrezni most vise ne salje `NTP:` po vlastitom rasporedu, nego odgovara na zahtjev Mege
- prvi `NTP` nakon restarta ili `WiFi` reconnecta `ESP` potvrduje drugim uzorkom prije nego sto ga `Mega` prihvati za toranjski sat
- prihvaceni `NTP` zapis i start redovnog otkucavanja poravnavaju se na `RTC SQW` granicu sekunde kad je dostupna
- `SETREQ:SUSTAV`, `SETREQ:STAPICI`, `SETREQ:BAT`, `SETREQ:SUNCE`, `SETREQ:MISE`, `SETREQ:BLAGDANI_NEP` i `SETREQ:BLAGDANI_POM` traze trenutno stanje pojedine skupine iz `main/postavke.*`
- `SETCFG:SUSTAV|...`, `SETCFG:STAPICI|...`, `SETCFG:BAT|...`, `SETCFG:SUNCE|...`, `SETCFG:MISE|...`, `SETCFG:BLAGDANI_NEP|...` i `SETCFG:BLAGDANI_POM|...` salju puni paket pojedine skupine, a `Mega` ga validira i sprema
- `SET:MISE|rd=...|nd=...` vraca dnevnu i nedjeljnu misu, `SET:BLAGDANI_NEP|...` vraca nepomicne blagdane, a `SET:BLAGDANI_POM|...` vraca pomicne blagdane

## 💾 EEPROM i recovery

- vanjska `24C32 EEPROM` ili `FM24W256 FRAM` memorija cuva postavke i kriticno radno stanje
- iako je fizicki kapacitet veci, toranjski sat zadrzava postojeci kompatibilni raspored unutar prvih `4096 B`
- `UnifiedMotionState` koristi `24` rotirajuca slota za kazaljke i okretnu plocu
- svaki zapis `UnifiedMotionState` nosi checksum kako bi se preskocio korumpirani slot nakon prekida napajanja usred upisa
- zapis zadnje sinkronizacije vremena ima vlastiti checksum i kompatibilan legacy fallback
- `power_recovery.*` vraca kazaljke i plocu u dosljedno stanje nakon restarta
- watchdog resetovi se pamte u zasebnom EEPROM bloku i tek ponovljeni watchdog resetovi bez power-loss oznake vode u `safe mode`
- `safe mode` blokira mehaniku i prikazuje `SUSTAV ZAKLJUCAN / PREVISE RESETA` dok operater ne drzi `ENT / SELECT` `5 s`
- `power_recovery.*` radi periodicki `EEPROM` health-check svakih `6 sati`
- latched fault `EEPROM-a` ostaje spremljen do rucne potvrde operatera i pali degradirani nacin rada za `EEPROM`
- u degradiranom `EEPROM` nacinu rada pauziraju se periodicni backup i pomocni zapisi iz `time_glob.*`
- `LCD`, `RTC`, vanjska `EEPROM/FRAM` memorija i servisni `I2C` scan koriste zajednicku pripremu `Wire` sabirnice s timeoutom
- `EEPROM/I2C` retry i polling petlje osvjezavaju watchdog kad je aktivan
- pri svakoj izmjeni EEPROM rasporeda ili recovery logike provjeri:
- `eeprom_konstante.h`
- `unified_motion_state.*`
- `power_recovery.*`

## 🔩 Hardver koji Mega vodi

- dva trofazna elektromotora `Koncar 0.55 kW / 380 V`, po jedan za `Zvono 1` i `Zvono 2`
- mikroprekidaci na straznjoj osovini svakog zvonarskog motora za okretanje faza i sigurnu izmjenu rada zvona
- dva elektromagnetska bata / cekica `310 VDC`, po jedan po zvonu, s impulsom oko `0,01 s`
- pogonski motor kazaljki s mehanizmom zupcanika koji radi na `PARNI/NEPARNI` impuls oko `6 s`, sto odgovara logici u `main/kazaljke_sata.*`
- elektroormar s kontaktorima za okretanje faza zvona, kontaktorima za batove, osiguracima i ostalom razvodnom i zastitnom opremom
- releji za parne i neparne faze kazaljki
- releji za okretnu plocu
- izlazi za zvona i cekice
- DS3231 RTC i vanjska `24C32 EEPROM` ili `FM24W256 FRAM` memorija preko `I2C`
- LCD 16x2 preko I2C
- thumbwheel `00-99` za trajanje mrtvackog zvona
- kip-prekidac tihog moda i lampica tihog moda
- ulaz za nadzor mreznog napona radi `UPS moda`
- LED lampice za `ZVONO 1`, `ZVONO 2`, `SLAVLJENJE` i `MRTVACKO`
- 6 direktnih tipki lokalnog izbornika (`GORE`, `DOLJE`, `LIJEVO`, `DESNO`, `DA`, `NE`)
- uredivanje polozaja okretne ploce u izborniku sada ide samo po valjanim koracima od `15 min`
- glavni LCD u `UPS modu` prikazuje `NEMA STRUJE!`, a ponedjeljak skracuje u `PON.` radi urednog prikaza datuma
- prvi red LCD-a koristi polja `11-13` za `NTP`, `MAN`, `ERR` ili `---`, dok polja `15-16` prikazuju temperaturu `DS3231` modula
- zvjezdica aktivnosti `*`, oznaka `R/N` i `W` za `WiFi` vise se ne prikazuju na glavnom retku
- dvotocke trepere u ritmu `1/2 SQW` samo dok [main/zvonjenje.cpp](zvonjenje.cpp), [main/otkucavanje.cpp](otkucavanje.cpp), [main/kazaljke_sata.cpp](kazaljke_sata.cpp) ili [main/okretna_ploca.cpp](okretna_ploca.cpp) trenutno rade, ili kad `WiFi` nije spojen

## ✅ Smjernice za razvoj

- glavna petlja mora ostati neblokirajuca
- Mega mora ostati sigurna i bez ovisnosti o stalnoj mrezi
- kvar ili restart ESP-a ne smije ugroziti osnovni rad sata
- `I2C` pristup za `LCD`, `RTC` i vanjsku `EEPROM/FRAM` memoriju treba ostati na zajednickoj pripremi sabirnice s timeoutom
- svaka promjena koja dira kazaljke, plocu, zvona ili recovery treba se provjeriti u odnosu na postojece module u `main/`
- lokalni SRAM tuning zadnje revizije prebacio je vecinu fiksnih log stringova u flash i smanjio velike serijske buffere u `esp_serial.*`
