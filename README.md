# Automatika sata – Toranjski sustav

Ovaj projekt modernizira pogon toranjskog sata korištenjem Arduino Mega 2560, RTC DS3231, LCD 2x16 i ESP-01/ESP-12 za mrežnu sinkronizaciju, uz ULN2803 i relejne izlaze za čekiće, zvona i kazaljke.

---

## 🛠️ Glavne funkcionalnosti

- Prikaz točnog vremena i datuma na LCD-u toranjskog ormara
- Upravljanje kazaljkama toranjskog sata dvostrukim impulsima
- Upravljanje zvonima (muško, žensko, slavljenje, mrtvačko) i čekićima
- Automatsko zakazivanje zvona i slavljenja prema ulazima okretne ploče
- Praćenje izvora vremena (RTC, NTP, ručno) i spremanje u EEPROM
- Tipkovnica s 6 tipki za lokalne postavke i servisne komande

---

## 🧩 Moduli i ključne funkcije

- `kazaljke_sata` inicijalizira relejne izlaze, vodi dnevnik položaja u EEPROM-u i kompenzira kazaljke na zadano vrijeme (`inicijalizirajKazaljke()`, `upravljajKazaljkama()`, `kompenzirajKazaljke(bool)`), čime toranjski sat ostaje sinkroniziran s RTC-om.【F:src/kazaljke_sata.cpp†L46-L147】
- `okretna_ploca` čita pet ulaza ploče, pokreće releje za smjer rotacije te automatizira zvona i slavljenje u koordinaciji s toranjskim rasporedom (`inicijalizirajPlocu()`, `kompenzirajPlocu(bool)`, `obradiUlazePloce(...)`).【F:src/okretna_ploca.cpp†L92-L219】【F:src/okretna_ploca.cpp†L283-L307】
- `zvonjenje` definira sekvence čekića, upravlja trajanjima i sigurnosnim odgodama te sinkronizira slavljenje i mrtvačko zvono (`inicijalizirajZvona()`, `upravljajZvonom()`, `zapocniSlavljenje()`).【F:src/zvonjenje.cpp†L61-L153】
- `esp_serial` otvara UART1 prema ESP-01/ESP-12 te obrađuje NTP i naredbe zvona (`inicijalizirajESP()`, `obradiESPSerijskuKomunikaciju()`).【F:src/esp_serial.cpp†L8-L45】
- `time_glob` i `vrijeme_izvor` spremaju izvor vremena, ručna i NTP ažuriranja te nadziru starost sinkronizacije, što je ključno za toranjski raspored zvona.【F:src/time_glob.cpp†L12-L44】【F:src/vrijeme_izvor.cpp†L7-L34】

---

## 📦 Komponente

- Arduino Mega 2560 (glavna kontrolna ploča)
- RTC DS3231 s baterijom (rezervni izvor vremena)
- LCD 2x16 s I2C adapterom (vizualne informacije u ormaru)
- ULN2803 i optokapleri (izolacija i pogon toranjskih releja)
- Relejna pločica 5 V (kazaljke, okretna ploča, zvona)
- Tipkovnica: 6 tipki (GORE, DOLJE, LIJEVO, DESNO, DA, NE)
- ESP-01 / ESP-12 (NTP i udaljene naredbe)
- Napajanje: 5 V / 10 A SMPS + spuštanje na 3.3 V za ESP

---

## 🖥️ Prikaz na LCD-u

- 📟 **Gornji red** prikazuje sat toranjskog ormara u formatu `HH:MM:SS`, pri čemu se sekunde izmjenjuju svake pola sekunde s razmakom kako bi tehničar odmah vidio da sustav osvježava prikaz (`prikaziSekunde`). Desno od vremena stoji oznaka izvora (`RTC`, `NTP`, `RUC`) iz modula `vrijeme_izvor` te slovna oznaka aktualnog dana u tjednu (`dohvatiOznakuDana()`), što olakšava provjeru sinkronizacije toranjskog sata.【F:src/lcd_display.cpp†L49-L74】
- 📅 **Donji red** prikazuje kratice dana (`Ned`, `Pon`, ...) i datum u obliku `DD.MM.YYYY`, koristeći podatke iz RTC-a (`DateTime now = dohvatiTrenutnoVrijeme()`), čime servisno osoblje odmah vidi kalendarske informacije toranjskog ormara.【F:src/lcd_display.cpp†L49-L74】
- 🔁 **Poruke i blinkanje** privremeno brišu standardni prikaz: kada `prikaziPoruku()` stigne iz drugih modula, oba reda se pune prilagođenim tekstom, a funkcija `postaviLCDBlinkanje()` uključuje ili isključuje pulsiranje pozadinskog osvjetljenja svakih 500 ms kako bi upozorenja za toranjski sat bila uočljiva.【F:src/lcd_display.cpp†L24-L47】【F:src/lcd_display.cpp†L76-L118】

---

## 🔗 Povezivanje i preporučeni pinovi

- **Napajanje i zaštita**
  - 5 V rail napaja Arduino, ULN2803 i releje; zvona i motori ploče ostaju na zasebnim napajanjima uz optičku izolaciju.
  - Obavezno uzemljenje zajedničke mase između logike i napajanja toranjskog ormara.
- **I2C sabirnica**
  - DS3231 i LCD dijele SDA (D20) i SCL (D21) linije Mega kontrolera, s kratkim vodičima radi otpornika pull-up.
- **Releji kazaljki**
  - PIN_RELEJ_PARNE_KAZALJKE (D10) i PIN_RELEJ_NEPARNE_KAZALJKE (D11) vode dvije faze impulsa kazaljki preko ULN2803 u relejne zavojnice.【F:src/podesavanja_piny.h†L7-L10】
- **Releji okretne ploče**
  - PIN_RELEJ_PARNE_PLOCE (D8) pokreće naprijed, a PIN_RELEJ_NEPARNE_PLOCE (D9) natrag; oba izlaza uvode se preko optokaplera radi zaštite mehanizma toranjske ploče.【F:src/podesavanja_piny.h†L11-L13】
- **Ulazi okretne ploče**
  - PIN_PLOCA_ULAZ_1–5 (D30–D34) koriste interno povlačenje i čitaju reed sklopke / čavle koji najavljuju raspored zvona i slavljenja.【F:src/podesavanja_piny.h†L15-L20】
- **Tipkovnica**
  - PIN_TIPKA_GORE–PIN_TIPKA_NE (D40–D45) se povezuju na tipke prema masi; aktiviraj `INPUT_PULLUP` u `tipke` modulu kako bi toranjski tehničar mogao upravljati postavkama bez vanjskih otpornika.【F:src/podesavanja_piny.h†L22-L28】
- **Zvonjenja i čekići**
  - PIN_ZVONO_MUSKO (D4) i PIN_ZVONO_ZENSKO (D5) vode zavojnice zvona, dok PIN_CEKIC_MUSKI (D12) i PIN_CEKIC_ZENSKI (D3) upravljaju čekićima preko releja ili SSR-a.【F:src/podesavanja_piny.h†L30-L39】
- **Slavljenje i eksterni signali**
  - PIN_SLAVLJENJE_SIGNAL (D2) prati ulaz s procesne logike (aktivno LOW) za ručno pokretanje slavljenja.【F:src/podesavanja_piny.h†L34-L35】
- **ESP komunikacija**
  - ESP-01/ESP-12 se spaja na hardware UART1 (RX1=D19, TX1=D18) uz level shifting na 3.3 V; `Serial1` se inicijalizira na 9600 bps u `esp_serial` modulu.【F:src/esp_serial.cpp†L8-L26】

---

## 🔌 ESP serijska komunikacija

- Serial1 (9600 bps) prima `NTP:` vremenske oznake i `CMD:` naredbe za zvona, svaka završena novim redom.
- Nakon uspješne obrade Arduino vraća `ACK:NTP` ili `ACK:CMD_OK`, dok pogreške daju `ERR:CMD` ili `ERR:FORMAT`, čime toranjski sustav olakšava integraciju s Home Assistantom ili vlastitim nadzornim serverom.【F:src/esp_serial.cpp†L17-L38】
- Dostupne `CMD:` naredbe omogućuju udaljeni nadzor toranjskog sata preko Home Assistanta i MQTT-a:
  - `ZVONO1_ON` / `ZVONO1_OFF` – aktivacija i deaktivacija muškog zvona.
  - `ZVONO2_ON` / `ZVONO2_OFF` – aktivacija i deaktivacija ženskog zvona.
  - `OTKUCAVANJE_ON` / `OTKUCAVANJE_OFF` – uključenje ili privremena blokada automatskih otkucaja čekića iz modula `otkucavanje`.
  - `SLAVLJENJE_ON` / `SLAVLJENJE_OFF` – ručno pokretanje ili gašenje slavljenja.
  - `MRTVACKO_ON` / `MRTVACKO_OFF` – pokretanje ili zaustavljanje mrtvačkog brecanja preko modula `zvonjenje`.

---

## 📁 Struktura projekta (src/)

```
src/
├── main.ino               # Glavna petlja i inicijalizacija toranjskog sustava
├── esp_serial.*           # NTP sinkronizacija i udaljene naredbe
├── kazaljke_sata.*        # Upravljanje kazaljkama
├── lcd_display.*          # Prikaz poruka i menija
├── okretna_ploca.*        # Rotacija ploče i vanjski ulazi
├── otkucavanje.*          # Čekići i otkucaji
├── postavke.*             # EEPROM postavke
├── tipke.*                # Tipkovnica i izbornici
├── time_glob.*            # Globalno vrijeme i izvori
├── zvonjenje.*            # Zvona, slavljenje i mrtvačko
└── vrijeme_izvor.*        # Evidencija zadnje sinkronizacije
```

---

## 🔄 Buduće nadogradnje

- Automatsko prepoznavanje najstabilnijeg izvora vremena (RTC, NTP, ručno)
- Test mod s LED indikacijom umjesto releja za brzu provjeru u radionici
- Web konfigurator preko ESP-01 za udaljeni raspored zvona toranjskog sata
- Hardverska sinkronizacija sekundi putem SQW izlaza DS3231 i prekidnog ulaza kontrolera, uz prilagodbu ISR logike otkucavanja toranjskog sata

---

## 📝 Licenca

Projekt je slobodan za edukaciju i osobnu upotrebu toranjskih sustava.

---

Za sve komentare, prijedloge ili izmjene slobodno otvori issue na GitHubu ✍️
