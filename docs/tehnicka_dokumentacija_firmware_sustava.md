# 📘 Tehnička dokumentacija firmware sustava toranjskog sata

Ovaj dokument opisuje **kako sustav radi u pogonu**: mehaničko kretanje kazaljki, okretne ploče, zvona i čekića, sinkronizaciju vremena te sigurnosne mehanizme. Fokus je na ponašanju i razlozima dizajna, ne na prepisivanju implementacije.

---

## 1. SYSTEM OVERVIEW

### Što sustav kontrolira
Firmware upravlja četirima glavnim podsustavima toranjskog sata:
- **Kazaljke sata** (minutni koraci kroz releje PARNI/NEPARNI).
- **Okretna ploča** (diskretne pozicije koje predstavljaju raspored mehaničkih događaja).
- **Zvona** (dulji rad releja zvona, uključujući ručni override i automatske ulaze s ploče).
- **Čekiće / otkucavanje** (kratki impulsi za puni sat, pola sata, slavljenje i mrtvačko).

### Koncept glavne runtime petlje
Glavna `loop()` petlja je organizirana kao **kooperativni scheduler** bez blokiranja:
1. osvježi watchdog,
2. obradi komunikacije i UI (ESP, meni, tipke),
3. obradi mehaniku (zvona, otkucavanje, kazaljke, ploča),
4. obradi dodatnu sinkronizaciju (DCF),
5. periodički spremi kritično stanje,
6. ponovno osvježi watchdog.

Time se osigurava da nijedan podsustav ne „gladuje“, a svi rade ciklički u malim koracima.

### Arhitektura na visokoj razini
- **Vrijeme i sinkronizacija:** `time_glob.*`, `esp_serial.*`, `dcf_sync.*`.
- **Kretanje mehanike:** `kazaljke_sata.*`, `okretna_ploca.*`, `unified_motion_state.*`.
- **Udari i zvona:** `otkucavanje.*`, `zvonjenje.*`.
- **UI i postavke:** `tipke.*`, `menu_system.*`, `postavke.*`.
- **Otpornost i oporavak:** `watchdog.*`, `power_recovery.*`, `wear_leveling.*`, `i2c_eeprom.*`.

---

## 2. UNIFIED STATE MODEL

### Uloga `UnifiedMotionState`
`UnifiedMotionState` je jedinstveni zapis stanja za **kazaljke + okretnu ploču**. Ideja je da oba mehanizma dijele isti model i isto spremanje, pa se nakon restarta točno zna:
- gdje je sustav stao,
- je li impuls bio aktivan,
- koja je faza koraka bila u tijeku.

### Značenje polja
- **`hand_position`**: logička pozicija kazaljki u rasponu 0–719 (12 h × 60 min).
- **`hand_active`**: `0/1` zastavica je li trenutno aktivan impuls kazaljki.
- **`hand_relay`**: koji relej vodi impuls kazaljki (`nijedan`, `PARNI`, `NEPARNI`).
- **`plate_position`**: trenutna pozicija okretne ploče (0–63).
- **`plate_phase`**: faza ploče (`stabilno`, `prvi relej`, `drugi relej`).

### Cache vs EEPROM
Sloj `UnifiedMotionStateStore` koristi dvije razine:
- **RAM cache (`cacheStanje`)** za brzo čitanje bez nepotrebnog I2C prometa.
- **EEPROM (wear-leveling)** za trajnost kroz nestanak napajanja.

Tok rada:
1. kod čitanja prvo pokušava cache,
2. ako cache nije inicijaliziran, čita iz EEPROM-a,
3. ako EEPROM nije valjan, radi migraciju/rekonstrukciju inicijalnog stanja i odmah ga zapisuje.

### Kada se stanje sprema
Stanje se sprema **samo kad postoji promjena** (memcmp provjera):
- pri startu/stopu impulsa kazaljki,
- pri promjeni faze ploče,
- pri dovršetku koraka i promjeni pozicije,
- pri ručnom postavljanju pozicija.

To smanjuje trošenje EEPROM-a i zadržava konzistentnost mehanike.

---

## 3. CLOCK HANDS (KAZALJKE)

### Minutni korak: jedan impuls = jedna minuta
Kazaljke ne „skaču“ na cilj odmah. Svaki fizički korak radi ovako:
- aktivira se odgovarajući relej,
- drži se aktivnim **6 sekundi**,
- zatim se korak zaključuje i `hand_position` se poveća za 1.

### Parni/neparni relejni model
Relej se bira prema paritetu trenutne logičke pozicije:
- parna pozicija → PARNI relej,
- neparna pozicija → NEPARNI relej.

Tako je svaki idući minutni korak električki konzistentan s mehanikom pogona.

### 6-sekundni impulsni model
Sustav periodički provjerava treba li novi korak tek nakon isteka faznog vremena. U praksi to znači:
- dok je impuls aktivan, ne pokreće se novi,
- tek nakon 6 s impuls se gasi i pozicija napreduje.

### Kako se ispravlja mismatch prema RTC-u (korak-po-korak)
Cilj je uvijek `RTC vrijeme -> (sat % 12)*60 + minuta`.
Ako `hand_position != cilj`:
1. pokrene se jedan korak,
2. pričeka završetak koraka,
3. ponovno izračuna cilj,
4. po potrebi ponovi.

**Primjer:**
- RTC kaže 10:15 (cilj 615),
- kazaljke su na 10:12 (612),
- sustav odradi 3 uzastopna koraka (svaki 6 s),
- nakon trećeg koraka stanje je sinkronizirano.

Ovo je namjerno „meka“ korekcija radi zaštite mehanike.

---

## 4. ROTATING PLATE (OKRETNA PLOČA)

### Logika koraka svakih 15 minuta
Ciljna pozicija ploče se računa iz vremena u 15-minutnim blokovima. Aktivni dnevni prozor je:
- **od 04:59 do 20:44**,
- izvan tog prozora cilj je noćna pozicija 63.

### Dvofazni model (PARNI + NEPARNI)
Jedan korak ploče nije trenutan, nego ide kroz dvije faze:
1. faza 1: prvi relej aktivan 6 s,
2. faza 2: drugi relej aktivan 6 s,
3. završetak: `plate_position = (plate_position + 1) % 64`, faza vraćena na stabilno.

### Mapiranje pozicija 0–63
- `0` odgovara početku dnevnog prozora,
- svaka iduća pozicija predstavlja +15 min,
- `63` je zadnja/noćna referenca.

### Sinkronizacija ploče nakon nestanka napajanja
Kod boota sustav učitava zadnje poznato stanje iz EEPROM-a (pozicija + faza). Nakon toga u runtime-u:
- ako je ploča već na cilju, nema pokreta,
- ako nije, korigira se korak-po-korak dok ne dođe do cilja.

Time se izbjegava agresivno „premotavanje“ ploče.

---

## 5. HAMMER STRIKING (OTKUCAVANJE)

### Puni sat vs pola sata
- **Puni sat (`minute == 00`)**: broj udaraca 1–12 (12-satni format), muški čekić.
- **Pola sata (`minute == 30`)**: jedan udarac, ženski čekić.

### Tajming
Za satno otkucavanje sekvenca je fiksirana:
- **150 ms impuls** čekića,
- **2 s pauza** između udaraca.

Za ostale načine mogu se koristiti postavke, ali puni sat ostaje fiksan radi predvidljivog mehaničkog ritma.

### Quiet hours logika
Prije automatskog otkucavanja provjeravaju se:
- dopušteni satni raspon (`satOd`–`satDo`),
- tihi period (`tihiSatiOd`–`tihiSatiDo`, uključujući raspon preko ponoći).

Ako je aktivan tihi period, otkucavanje se preskače i samo se logira događaj.

### Interakcija s inercijom i blokadom
Ako su zvona aktivna ili je svježe završilo zvonjenje, aktivna je inercijska blokada i čekići se ne pokreću. Također postoji korisnička globalna blokada otkucavanja. Ako blokada nastupi usred sekvence, sekvenca se sigurno prekida.

---

## 6. BELLS (ZVONA)

### Razlika između zvona i čekića
- **Zvona**: dulja aktivacija releja (sekunde/minute), vezana uz ulaze ploče, ručne sklopke i automatsko trajanje.
- **Čekići**: kratki impulsni udari (stotine ms) za otkucavanje i posebne načine.

### Ručno upravljanje preko sklopki
Postoje fizičke sklopke za BELL1 i BELL2 (debounce ~30 ms). Kada je ručni override aktivan, on ima prioritet nad automatikom.

### Inercija
Nakon uključivanja/isključivanja zvona aktivira se inercija od **90 s**. U tom periodu se blokiraju udari čekića da se ne preklapa mehaničko gibanje.

### Zašto se zvona više ne koriste za satno otkucavanje
Satno otkucavanje je sada prebačeno na čekiće jer traži precizan impulsni ritam (150 ms + pauza). Zvona ostaju za duže liturgijske/rasporedne događaje i ručne intervencije.

---

## 7. NTP / RTC SINKRONIZACIJA

### RTC (DS3231) kao primarni izvor
Sustav kontinuirano čita DS3231 i to je lokalni autoritet vremena tijekom normalnog rada.

### ESP NTP kao periodička korekcija
NTP dolazi serijski od ESP-a u strogom ISO formatu (`NTP:YYYY-MM-DDTHH:MM:SS[Z]`). Kad je prihvaćen, vrijeme se upisuje i u RTC.

### Pravilo „samo jednom po satu“ (minuta 00)
NTP se prihvaća samo ako:
- je minuta `00`,
- taj sat još nije prihvaćen (satni ključ godina+mjesec+dan+sat).

### `ACK:NTP` vs `SKIP:NTP`
- **`ACK:NTP`**: NTP je valjan i prihvaćen.
- **`SKIP:NTP`**: poruka valjana, ali odbijena pravilom (minuta nije 00 ili je taj sat već obrađen).

Nakon `ACK:NTP`, sustav pokreće „budnu“ korekciju kazaljki ako nisu sinkronizirane.

---

## 8. MENU I SUSTAV POSTAVKI

### Jasna podjela odgovornosti
- **`tipke.cpp`**: fizičko skeniranje tipki + debounce + pretvorba u `KeyEvent`.
- **`menu_system.cpp`**: stanje UI-a, ekrani, navigacija, logika potvrde i poziv poslovnih funkcija.
- **`postavke.cpp`**: trajna pohrana, validacija, fallback na default i zapis u EEPROM.

### Kako se postavke mijenjaju i spremaju
1. korisnik promijeni vrijednost kroz meni,
2. `menu_system` pozove API iz `postavke` (npr. tihi sati, MQTT),
3. `postavke` validira, pripremi integritet (potpis/verzija/checksum),
4. zapis ide kroz wear-leveling slotove u EEPROM.

### EEPROM verzioniranje i validacija
Postavke imaju trostruku zaštitu:
- **potpis** (`POSTAVKE_POTPIS`),
- **verziju layouta** (`POSTAVKE_VERZIJA`),
- **checksum** cijele strukture.

Ako bilo što ne prođe validaciju, sustav se vraća na zadane vrijednosti i ponovno ih snima.

---

## 9. BOOT SEQUENCE

### Redoslijed inicijalizacije
`setup()` redom inicijalizira:
1. LCD + PC serial,
2. vanjski EEPROM,
3. RTC + učitavanje postavki,
4. tipke, ESP, opcionalno MQTT, meni,
5. zvona, otkucavanje, kazaljke, ploču, DCF,
6. watchdog,
7. power-recovery oznake i boot recovery.

### Watchdog inicijalizacija
Watchdog se podiže na 8 s timeout i odmah bilježi razlog prethodnog reseta (WDT/BOR/POR/EXTRF).

### Oporavak nakon nestanka napajanja
Power recovery čita kružni skup backup slotova i traži zadnji valjani zapis (checksum). Ako ga nađe, vraća:
- poziciju kazaljki,
- poziciju ploče,
- offset ploče.

### Obnova stanja iz EEPROM-a
Kritično stanje se periodički sprema svakih 60 s u rotirajuće slotove. To ograničava gubitak stanja na najviše posljednju minutu prije ispada.

### Inicijalno sinkronizacijsko ponašanje
Nakon boota sustav ne radi „hard jump“ mehanike. Umjesto toga, mehanizmi uđu u redovni korak-po-korak režim i sami dođu do ciljnog vremena/pozicije.

---

## 10. ERROR HANDLING I SIGURNOST

### Watchdog
Dvaput u glavnoj petlji se radi `wdt_reset()`. Ako petlja zapne, MCU se resetira i reset-uzrok ostaje logiran za recovery dijagnostiku.

### EEPROM validacija (potpis/verzija/checksum)
- Postavke: potpis + verzija + checksum.
- Backup kritičnog stanja: checksum.
- Unified stanje: rasponi polja + verzija.

### Rukovanje oštećenim postavkama
Ako su podaci nevaljani:
- vraća se sigurni default,
- string polja se sanitiziraju i null-terminiraju,
- ispravljena struktura se ponovo zapisuje.

### Sprječavanje neispravnih stanja
- vrijednosti se `constrain`-aju (npr. 0–719, 0–63, 0–14),
- međusobno isključivi načini (slavljenje vs mrtvačko),
- zabrana paralelnih sekvenci otkucavanja,
- sigurno gašenje releja kod prekida i pri inicijalizaciji.

---

## 11. POZNATE DIZAJNERSKE ODLUKE

### Zašto korekcija ide korak-po-korak, a ne agresivni skok
Mehanika toranjskog sata ima masu i inertnost. Postupna korekcija smanjuje udarna opterećenja, pregrijavanje releja i rizik od mehaničkog preskoka.

### Zašto se koristi unified state
Jedan izvor istine za kazaljke i ploču smanjuje race-condition scenarije i pojednostavljuje recovery jer se oba pogona oporavljaju iz istog koncepta stanja.

### Zašto je dopušten „blokirajući“ boot recovery
Kratko determinističko kašnjenje pri bootu je prihvatljivo jer je važnije vratiti mehaniku u konzistentno stanje prije normalnog cikličkog rada. Time se izbjegava start u polu-nepoznatom stanju releja.

### Zašto je NTP ograničen na satne točke
Satna granularnost (minuta 00) smanjuje jitter i nepotrebne mikrokorekcije kroz dan, a i dalje redovito „zaključava“ vrijeme prema mrežnom izvoru.

---

## 🛠️ Developer notes (ključne zamke)

- `WearLeveling::spremi` koristi statički brojač po tipu/predlošku; pri većim refaktorima paziti na neočekivane obrasce raspodjele zapisa.
- U `okretna_ploca.cpp` ciljna pozicija je trenutno vezana uz fiksne konstante 04:59–20:44; ako se želi potpuno konfigurabilan prozor, treba uskladiti izračun cilja s postavkama.
- `power_recovery.cpp` ima vlastitu lokalnu strukturu backupa; kod promjena layouta obavezno uskladiti i `eeprom_konstante.h` i recovery logiku.
- Ručni override zvona ima prioritet nad automatikom; pri dijagnostici „zašto zvono ne staje“ prvo provjeriti stanje fizičkih sklopki.

