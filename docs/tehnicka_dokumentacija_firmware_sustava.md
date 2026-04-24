# 📘 ZVONKO v. 1.0 - Tehnicka dokumentacija firmware sustava

Ovaj dokument opisuje kako sustav `ZVONKO v. 1.0` danas stvarno radi u pogonu: mehanicko kretanje kazaljki, okretne ploce, zvona i cekica, sinkronizaciju vremena te sigurnosne mehanizme. Fokus je na ponasanju i razlozima dizajna, ne na prepisivanju implementacije.

---

## 1. System Overview

### Sto sustav kontrolira
Firmware upravlja cetirima glavnim podsustavima toranjskog sata:
- **Kazaljke sata**: minutni koraci kroz releje `PARNI` i `NEPARNI`.
- **Okretna ploca**: diskretne pozicije koje predstavljaju raspored mehanickih dogadaja.
- **Zvona**: dulji rad releja zvona, ukljucujuci rucne sklopke i automatske ulaze s ploce.
- **Cekice / otkucavanje**: kratki impulsi za puni sat, pola sata, slavljenje i mrtvacko.

### Koncept glavne runtime petlje
Glavna `loop()` petlja je organizirana kao kooperativni scheduler bez blokiranja:
1. osvjezi watchdog
2. obradi komunikacije i UI (`ESP`, meni, tipke)
3. obradi mehaniku (zvona, otkucavanje, kazaljke, ploca)
4. obradi dodatnu sinkronizaciju (`NTP`)
5. periodicki spremi kriticno stanje
6. ponovno osvjezi watchdog

Time se osigurava da nijedan podsustav ne gladuje, a svi rade ciklicki u malim koracima.

### Arhitektura na visokoj razini
- **Vrijeme i sinkronizacija**: [main/time_glob.cpp](C:/Users/Rato/Documents/GitHub/FILA33/main/time_glob.cpp), [main/esp_serial.cpp](C:/Users/Rato/Documents/GitHub/FILA33/main/esp_serial.cpp)
- **Kretanje mehanike**: [main/kazaljke_sata.cpp](C:/Users/Rato/Documents/GitHub/FILA33/main/kazaljke_sata.cpp), [main/okretna_ploca.cpp](C:/Users/Rato/Documents/GitHub/FILA33/main/okretna_ploca.cpp), [main/unified_motion_state.cpp](C:/Users/Rato/Documents/GitHub/FILA33/main/unified_motion_state.cpp)
- **Udari i zvona**: [main/otkucavanje.cpp](C:/Users/Rato/Documents/GitHub/FILA33/main/otkucavanje.cpp), [main/zvonjenje.cpp](C:/Users/Rato/Documents/GitHub/FILA33/main/zvonjenje.cpp), [main/slavljenje_mrtvacko.cpp](C:/Users/Rato/Documents/GitHub/FILA33/main/slavljenje_mrtvacko.cpp)
- **UI i postavke**: [main/tipke.cpp](C:/Users/Rato/Documents/GitHub/FILA33/main/tipke.cpp), [main/menu_system.cpp](C:/Users/Rato/Documents/GitHub/FILA33/main/menu_system.cpp), [main/postavke.cpp](C:/Users/Rato/Documents/GitHub/FILA33/main/postavke.cpp)
- **Otpornost i oporavak**: [main/watchdog.cpp](C:/Users/Rato/Documents/GitHub/FILA33/main/watchdog.cpp), [main/power_recovery.cpp](C:/Users/Rato/Documents/GitHub/FILA33/main/power_recovery.cpp), [main/wear_leveling.cpp](C:/Users/Rato/Documents/GitHub/FILA33/main/wear_leveling.cpp), [main/i2c_eeprom.cpp](C:/Users/Rato/Documents/GitHub/FILA33/main/i2c_eeprom.cpp)

---

## 2. Unified State Model

### Uloga `UnifiedMotionState`
`UnifiedMotionState` je jedinstveni zapis stanja za kazaljke i okretnu plocu. Ideja je da oba mehanizma dijele isti model i isto spremanje, pa se nakon restarta tocno zna:
- gdje je sustav stao
- je li impuls bio aktivan
- koja je faza koraka bila u tijeku

### Znacenje polja
- `hand_position`: logicka pozicija kazaljki u rasponu `0-719`
- `hand_active`: je li trenutno aktivan impuls kazaljki
- `hand_relay`: koji relej vodi impuls kazaljki (`nijedan`, `PARNI`, `NEPARNI`)
- `hand_start_ms`: lokalni `millis()` trenutak starta aktivne faze
- `plate_position`: trenutna pozicija okretne ploce `0-63`
- `plate_phase`: faza ploce (`stabilno`, `prvi relej`, `drugi relej`)

### Cache vs EEPROM
Sloj `UnifiedMotionStateStore` koristi dvije razine:
- RAM cache za brzo citanje bez nepotrebnog `I2C` prometa
- EEPROM za trajnost kroz nestanak napajanja

Za testnu reviziju toranjskog sata `UnifiedMotionState` koristi **24 rotirajuca slota** u vlastitom EEPROM bloku. Pri citanju se skeniraju svi konfigurirani slotovi, a najnoviji zapis bira se po internoj sekvenci bez zajednickog meta-zapisa.

Tok rada:
1. kod citanja prvo pokusava cache
2. ako cache nije inicijaliziran, cita iz EEPROM-a
3. ako EEPROM nije valjan, radi migraciju ili rekonstrukciju inicijalnog stanja i odmah ga zapisuje

### Kada se stanje sprema
Stanje se sprema samo kad postoji promjena:
- pri startu i stopu impulsa kazaljki
- pri promjeni faze ploce
- pri dovrsetku koraka i promjeni pozicije
- pri rucnom postavljanju pozicija

To smanjuje trosenje EEPROM-a i zadrzava konzistentnost mehanike.

---

## 3. Clock Hands

### Minutni korak
Kazaljke ne skacu na cilj odmah. Svaki fizicki korak radi ovako:
- aktivira se odgovarajuci relej
- drzi se aktivnim oko `6 s`
- zatim se korak zakljucuje i `hand_position` se poveca za `1`

### Parni i neparni relejni model
Relej se bira prema paritetu trenutne logicke pozicije:
- parna pozicija -> `PARNI` relej
- neparna pozicija -> `NEPARNI` relej

### Zavrsavanje aktivne faze
Primarni autoritet za ritam i dalje je `RTC SQW`, ali završetak aktivne faze ima i `millis()` fallback. Time relej ne moze ostati trajno ukljucen ako `RTC SQW` privremeno nestane.

### Kako se ispravlja mismatch prema RTC-u
Cilj je uvijek `RTC vrijeme -> (sat % 12) * 60 + minuta`.
Ako `hand_position != cilj`:
1. pokrene se jedan korak
2. priceka zavrsetak koraka
3. ponovno izracuna cilj
4. po potrebi ponovi

Ako su kazaljke malo naprijed, sustav ne forsira puni krug nego pusta da ih stvarno vrijeme sustigne.

---

## 4. Rotating Plate

### Logika koraka svakih 15 minuta
Ciljna pozicija ploce racuna se iz vremena u `15`-minutnim blokovima. Aktivni dnevni prozor je po postavkama konfigurabilan, a zadani prozor je:
- od `04:59` do `20:44` kao logicki raspon pozicija
- citanje cavala ide minutu kasnije, na `HH:MM:30 + 1 min`

### Dvofazni model
Jedan korak ploce nije trenutan, nego ide kroz dvije faze:
1. faza 1: prvi relej aktivan `6 s`
2. faza 2: drugi relej aktivan `6 s`
3. zavrsetak: `plate_position = (plate_position + 1) % 64`, faza vracena na stabilno

### Mapiranje pozicija `0-63`
- `0` odgovara pocetku dnevnog prozora
- svaka iduca pozicija predstavlja `+15 min`
- `63` je zadnja ili nocna referenca

### Cavli i raspored zvona
Aktualni firmware podrzava:
- `5` mjesta za cavle
- `2` zvona
- poseban cavao za `SLAVLJENJE`
- nema vise zasebnog aktivnog modela za `MRTVACKO` cavao u postavkama

Za radne dane i nedjelju posebno se sprema:
- raspored cavala za `ZVONO 1`
- raspored cavala za `ZVONO 2`
- cavao za `SLAVLJENJE`

Vrijednost `0` znaci da određeno zvono ili slavljenje nema dodijeljen cavao.

### Kada se cavli citaju
Cavli se ne citaju tijekom pomaka ploce. Citanje je dozvoljeno samo kad:
- vrijeme je potvrdeno
- ploca je konfigurirana
- ploca je u `FAZA_STABILNO`
- ploca je na ciljnoj poziciji za aktualni termin

Referentni trenutak citanja je pomaknut na **minutu nakon logickog termina**, u sekundi `:30`.

Primjer sa zadanim pocetkom:
- slot `04:59` cita se u `05:00:30`
- slot `05:14` cita se u `05:15:30`
- slot `05:29` cita se u `05:30:30`
- slot `05:44` cita se u `05:45:30`

### Sinkronizacija ploce nakon nestanka napajanja
Kod boota sustav ucitava zadnje poznato stanje iz EEPROM-a. Nakon toga u runtime-u:
- ako je ploca vec na cilju, nema pokreta
- ako nije, korigira se korak-po-korak dok ne dode do cilja

Time se izbjegava agresivno premotavanje ploce.

---

## 5. Hammer Striking

### Puni sat i pola sata
- puni sat (`minute == 00`): broj udaraca `1-12`, muski cekic
- pola sata (`minute == 30`): jedan udarac, zenski cekic

### Tajming
Za redovno otkucavanje sekvenca koristi:
- impuls cekica iz postavki, ogranicen sigurnosnim limitom
- definirane pauze izmedu udaraca

### Posebni nacini rada cekica
- `slavljenje 1`, `mrtvacko 1` i redovno otkucavanje i dalje koriste zajednicki impuls iz postavki
- `slavljenje 2` koristi fiksni slijed: `C1 110 ms -> pauza 90 ms -> C2 110 ms -> pauza 190 ms`
- `mrtvacko 2` koristi fiksni slijed: `C1 300 ms -> pauza 700 ms -> C2 300 ms -> pauza 3700 ms`

### Lokalni ulazi za slavljenje i mrtvacko
- `slavljenje` je spojeno na fizicki kip-prekidac
- stanje `LOW` na ulazu znaci da slavljenje treba biti ukljuceno
- povratak prekidaca u `HIGH` gasi slavljenje
- `mrtvacko` ostaje zasebno tipkalo i radi kao `toggle` pri pritisku

### Thumbwheel za mrtvacko
Mrtvacko koristi dvoznamenkasti `BCD` thumbwheel `00-99`:
- `00` znaci radi stalno do rucnog gasenja
- `01-99` znaci auto-stop nakon toliko minuta
- vrijednost se stabilizira u pozadini
- ako se vrijednost promijeni tijekom aktivnog mrtvackog, nova vrijednost odmah postaje autoritet i restartira lokalno odbrojavanje

### BAT i tihi sati
`BAT / tihi sati` iz postavki blokiraju samo redovno otkucavanje. Ne blokiraju:
- suncevu automatiku
- cavao-zvonjenja s ploce

Ako je jutarnje suncevo zvono odradeno, ono moze otvoriti otkucavanje i prije regularnog kraja `BAT` raspona.

### Tihi rezim
Jedinstveni tihi rezim blokira:
- zvona
- cekice
- slavljenje
- mrtvacko

Kazaljke i okretna ploca ostaju aktivne.

---

## 6. Bells

### Razlika izmedu zvona i cekica
- zvona: dulja aktivacija releja, vezana uz ulaze ploce, rucne sklopke i automatsko trajanje
- cekici: kratki impulsni udari za otkucavanje i posebne nacine

### Rucno upravljanje preko sklopki
Postoje fizicke sklopke za `ZVONO 1` i `ZVONO 2`. Kada je rucni override aktivan, on ima prioritet nad automatikom.

### Inercija
Nakon ukljucivanja ili iskljucivanja zvona aktivira se inercija. Vrijednost se zasebno postavlja za `Zvono 1` i `Zvono 2` kroz meni `Sustav`, a u tom periodu se blokiraju udari cekica kako se ne bi preklapala mehanicka gibanja.

### Tihi rezim i zvona
Kad je aktivan tihi rezim:
- automatska zvonjenja ne rade
- rucne sklopke ne mogu ukljuciti zvona
- cavli se mogu ocitati, ali ne mogu pokrenuti zvonjenje

---

## 7. NTP / RTC Sinkronizacija

### RTC kao primarni izvor
Sustav kontinuirano cita `DS3231` i to je lokalni autoritet vremena tijekom normalnog rada.

### NTP kao kontrolirana korekcija
`ESP` vise ne gura `NTP` po svom rasporedu. Mega sama trazi `NTP` samo u sigurnom prozoru kad:
- mehanika miruje
- nije aktivan osjetljiv trenutak otkucavanja ili korekcije
- mreza je spremna

Prihvacena `NTP` sinkronizacija se, kad god je moguce, poravnava na sljedeci `RTC SQW` tik sekunde.

### Zastita od sumnjivog skoka vremena
Ako novo vrijeme napravi prevelik skok, sustav ga ne prihvaca odmah nego trazi dodatnu potvrdu. Time se izbjegava upis krivog vremena u `RTC`.

### DST
Firmware sam vodi `CET/CEST` status, sprema ga u EEPROM i automatski primjenjuje prijelaz.

---

## 8. Menu I Sustav Postavki

### Jasna podjela odgovornosti
- [main/tipke.cpp](C:/Users/Rato/Documents/GitHub/FILA33/main/tipke.cpp): fizicko skeniranje tipki i pretvorba u `KeyEvent`
- [main/menu_system.cpp](C:/Users/Rato/Documents/GitHub/FILA33/main/menu_system.cpp): stanje UI-a, ekrani, navigacija i poziv poslovnih funkcija
- [main/postavke.cpp](C:/Users/Rato/Documents/GitHub/FILA33/main/postavke.cpp): trajna pohrana, validacija, fallback na default i zapis u EEPROM

### Kako se postavke mijenjaju i spremaju
1. korisnik promijeni vrijednost kroz meni
2. `menu_system` pozove API iz `postavke`
3. `postavke` validira, pripremi integritet i zapis
4. promjena se sprema u EEPROM

Pravilo upravljanja tipkama je ujednaceno:
- strelice sluze za kretanje po stavkama i promjenu vrijednosti
- `Ent` sprema promjene za aktivnu granu menija
- `Esc` izlazi bez spremanja i vraca se jednu granu natrag

Meni `Sunce` trenutno uredjuje:
- jutarnji dogadaj
- podnevni dogadaj
- vecernji dogadaj
- nocnu rasvjetu kao cetvrtu stranicu nakon `Jutro`, `Podne` i `Vecer`

Nocna rasvjeta je odvojena od zvona, ali koristi isti izracun izlaska i zalaska sunca:
- ukljucuje relej `PIN_RELEJ_NOCNE_RASVJETE` pri vecernjem dogadaju
- gasi ga pri jutarnjem dogadaju
- po danu je `OFF`, po noci je `ON`
- u meniju `Sunce` `Gore/Dolje` na stranici nocne rasvjete mijenja `AUTO`, a `Lijevo/Desno` prelazi na susjednu stranicu

Meni `Blagdani` uredjuje automatsko slavljenje nakon suncevih dogadaja:
- `SLAVI J0 P0 V0` odredjuje smije li se slaviti nakon jutarnje Zdravomarije, podnevnog zvona i vecernje Zdravomarije
- `A:0 P:0 VG:0` ukljucuje razdoblja za sv. Antu, sv. Petra i Veliku Gospu
- sv. Ante vrijedi od `6.6.` do ukljucivo `13.6.`
- sv. Petar vrijedi od `22.6.` do ukljucivo `28.6.`
- Velika Gospa vrijedi od `8.8.` do ukljucivo `15.8.`
- slavljenje se zakazuje tek nakon stvarno pokrenutog suncevog zvona
- prije starta slavljenja ceka se kraj zvona, otkucavanja i inercije
- trajanje i odgoda slavljenja koriste postojece postavke iz `Stapici`

Druga stranica menija `Blagdani` uredjuje Svi sveti i Dusni dan:
- `SVI SVETI 0/1` ukljucuje ili iskljucuje posebni mrtvacki raspored
- `P:15` oznacava pocetni sat mrtvackog na dan `1.11.`
- `Z:8` oznacava zavrsni sat mrtvackog na dan `2.11.`
- ako je ukljuceno, mrtvacko radi `1.11.` od `P:00` do `21:00`
- nakon toga je tisina do `2.11.` u `06:00`
- mrtvacko ponovno radi `2.11.` od `06:00` do `Z:00`
- `1.11.` se preskace vecernja Zdravomarija
- `2.11.` se preskace jutarnja Zdravomarija
- mrtvacko za Svi sveti namjerno ignorira thumbwheel auto-stop, jer trajanje odreduje kalendarski raspored

Meni `Sustav` trenutno uredjuje:
- `LCD svjetlo`
- `Logiranje`
- `Impuls cekica`
- `Inercija Z1`
- `Inercija Z2`

### EEPROM verzioniranje i validacija
Postavke imaju trostruku zastitu:
- potpis
- verziju layouta
- checksum cijele strukture

Ako validacija ne prode, sustav se vraca na zadane vrijednosti i ponovno ih snima.

---

## 9. Boot Sequence

### Redoslijed inicijalizacije
`setup()` redom inicijalizira:
1. LCD i PC serial
2. vanjski EEPROM
3. RTC i ucitavanje postavki
4. tipke, ESP i meni
5. zvona, otkucavanje, kazaljke i plocu
6. watchdog
7. power-recovery oznake i boot recovery

### Watchdog inicijalizacija
Watchdog se podize na `8 s` timeout i odmah biljezi razlog prethodnog reseta (`WDT`, `BOR`, `POR`, `EXTRF`).

### Oporavak nakon nestanka napajanja
Power recovery cita kruzni skup backup slotova i trazi zadnji valjani zapis. Ako ga nade, vraca:
- poziciju kazaljki
- poziciju ploce
- vraca eventualni prekinuti aktivni korak u neaktivno stanje iz iste pozicije, tako da se pri ponovnom radu korak odradi ponovno fizicki

`offset` ploce vise nije dio recovery modela.

### Obnova stanja iz EEPROM-a
Kriticno stanje se periodicki sprema svakih `60 s` u rotirajuce slotove. To ogranicava gubitak stanja na najvise posljednju minutu prije ispada.

### Inicijalno sinkronizacijsko ponasanje
Nakon boota sustav ne radi hard jump mehanike. Umjesto toga, mehanizmi ulaze u redovni korak-po-korak rezim i sami dolaze do ciljnog vremena i pozicije.

---

## 10. Error Handling I Sigurnost

### Watchdog
Dvaput u glavnoj petlji radi se `wdt_reset()`. Ako petlja zapne, `MCU` se resetira i razlog reseta ostaje dostupan za recovery dijagnostiku.

### EEPROM validacija
- postavke: potpis + verzija + checksum
- backup kriticnog stanja: checksum
- unified stanje: rasponi polja + verzija + sekvenca

### Rukovanje ostecenim postavkama
Ako su podaci nevaljani:
- vracaju se sigurni defaulti
- string polja se sanitiziraju i `null`-terminiraju
- ispravljena struktura se ponovno zapisuje

### Sprjecavanje neispravnih stanja
- vrijednosti se ogranicavaju na validne raspone
- medusobno iskljucivi nacini (`slavljenje` vs `mrtvacko`)
- zabrana paralelnih sekvenci otkucavanja
- sigurno gasenje releja kod prekida i pri inicijalizaciji

---

## 11. Poznate Dizajnerske Odluke

### Zasto korekcija ide korak-po-korak
Mehanika toranjskog sata ima masu i inerciju. Postupna korekcija smanjuje udarna opterecenja, pregrijavanje releja i rizik od mehanickog preskoka.

### Zasto se koristi unified state
Jedan izvor istine za kazaljke i plocu smanjuje race-condition scenarije i pojednostavljuje recovery jer se oba pogona oporavljaju iz istog koncepta stanja.

### Zasto je dopusten blokirajuci boot recovery
Kratko deterministicko kasnjenje pri bootu je prihvatljivo jer je vaznije vratiti mehaniku u konzistentno stanje prije normalnog ciklickog rada.

### Zasto Mega bira trenutak za NTP
Time se izbjegava nepotrebna mikrokorekcija usred aktivnog mehanickog ciklusa i cuva se ritam toranjskog sata.

---

## 🛠️ Developer Notes

- [main/wear_leveling.cpp](C:/Users/Rato/Documents/GitHub/FILA33/main/wear_leveling.cpp) i dalje postoji za sporije EEPROM segmente, ali glavno stanje kazaljki i ploce vodi [main/unified_motion_state.cpp](C:/Users/Rato/Documents/GitHub/FILA33/main/unified_motion_state.cpp)
- [main/okretna_ploca.cpp](C:/Users/Rato/Documents/GitHub/FILA33/main/okretna_ploca.cpp) ocekuje da su pocetak i kraj prozora rada poravnani na `15`-minutne blokove
- kod promjena layouta obavezno zajedno provjeriti [main/eeprom_konstante.h](C:/Users/Rato/Documents/GitHub/FILA33/main/eeprom_konstante.h), [main/unified_motion_state.cpp](C:/Users/Rato/Documents/GitHub/FILA33/main/unified_motion_state.cpp) i [main/power_recovery.cpp](C:/Users/Rato/Documents/GitHub/FILA33/main/power_recovery.cpp)
- rucni override zvona ima prioritet nad automatikom; pri dijagnostici zasto zvono ne staje prvo provjeriti stanje fizickih sklopki i tihi rezim
