# Automatika sata – Toranjski sustav

Ovaj projekt zamjenjuje postojeći sustav upravljanja toranjskim satom korištenjem Arduino Mega 2560, RTC (DS3231), LCD 2x16, ESP-01 (NTP), DCF77 antenom i ostalim upravljačkim komponentama.

---

## 🛠️ Glavne funkcionalnosti

- Prikaz točnog vremena i datuma na LCD-u
- Upravljanje kazaljkama toranjskog sata
- Upravljanje zvonima preko interneta
- Detekcija izvora vremena: RTC / NTP / DCF / ručno
- Otkucavanje punih i pola sati pomoću čekića
- Zvonjenje, slavljenje i mrtvačko zvono
- Upravljanje okretnom pločom sa štapićima
- Upravljačka tipkovnica (6 tipki: GORE, DOLJE, LIJEVO, DESNO, DA, NE)
- Postavke se spremaju u EEPROM

---

## 📦 Komponente

- Arduino Mega 2560
- RTC DS3231 s baterijom
- LCD 2x16 s I2C adapterom
- Tipke: 6x (digitalni ulazi s pull-up)
- Relejna pločica (5V)
- ULN2803 + optokapleri (npr. TLP504)
- DCF77 antena (aktivna, DCF+ i DCF-)
- ESP-01 za NTP sinkronizaciju (preko UART)
- Napajanje: SMPS 5V 10A + LM2596 za 3.3V

---

## 📁 Struktura projekta (src/)

```
src/
├── main.ino               # Glavni program
├── lcd_display.h/.cpp     # Prikaz sata i menija na LCD-u
├── rtc_vrijeme.h/.cpp     # DST logika, sinkronizacija
├── otkucavanje.h/.cpp     # Upravljanje čekićima (batovi)
├── zvonjenje.h/.cpp       # Slavljenje, brecanje, zvona
├── tipke.h/.cpp           # Tipkovnica i meniji
├── postavke.h/.cpp        # EEPROM postavke
├── dcf_decoder.h/.cpp     # DCF dekoder (uskoro)
├── okretna_ploca.h/.cpp   # Upravljanje mehanizmom ploče
```

---

## 🔄 Buduće nadogradnje

- Dekodiranje DCF signala
- Automatsko prepoznavanje izvora vremena
- Test mod za LED-ice umjesto releja
- Web konfiguracija preko ESP-01

---

## 📝 Licenca

Projekt je slobodan za edukaciju i osobnu upotrebu.

---

Za sve komentare, prijedloge ili izmjene slobodno otvori issue na GitHubu ✍️
