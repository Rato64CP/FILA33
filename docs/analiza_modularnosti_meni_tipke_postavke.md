# 🧭 Analiza modularnosti: `postavke.cpp`, `menu_system.cpp`, `tipke.cpp`

## 🎯 Sažetak preporuke
- Datoteke **zadržati odvojene** (3 modula), bez spajanja u jednu ili dvije datoteke.
- Napraviti **ciljano čišćenje granica odgovornosti** i izvući nekoliko zajedničkih helpera.
- Najveći problem je **sprega između `tipke.cpp` i `menu_system.cpp`** oko timeouta i "aktivnosti menija".

## 1) 🔁 Preklapanja i duplicirana logika

### `tipke.cpp` ↔ `menu_system.cpp`
- Dvostruki timeout menija:
  - `tipke.cpp`: `TIMEOUT_MENIJA = 30000`, `meniJeAktivan`, `zadnjaDjetelnost`.
  - `menu_system.cpp`: `TIMEOUT_MENIJA_MS = 30000`, `zadnjaAktivnost` i auto-return u `upravljajMenuSistemom()`.
- Posljedica: ista poslovna odluka (kada meni ističe) postoji na dva mjesta.

### `menu_system.cpp` ↔ `postavke.cpp`
- `menu_system.cpp` čita/piše postavke kroz API (`dohvatiTihiPeriod...`, `postaviTihiPeriod...`, `jeMQTTOmogucen`, `postaviMQTTOmogucen`) što je ispravno; nema direktnog EEPROM koda.
- Nema pune duplikacije persistence logike, ali postoji funkcionalno preklapanje u smislu da UI dio (`menu_system.cpp`) djelomično upravlja validacijskim tokom kroz faze unosa (vrijeme/tihi sati), dok stvarna validacija ostaje u `postavke.cpp`.

## 2) 🧩 Miješanje odgovornosti

### `tipke.cpp`
- Trebao bi biti samo fizičke tipke + debounce + event emitiranje.
- Trenutno dodatno drži:
  - stanje "meni aktivan",
  - timeout politiku,
  - funkciju `uPostavkama()` koja ovisi o `menu_system` stanju.
- To su odgovornosti UI/state sloja, ne driverskog sloja tipki.

### `menu_system.cpp`
- Core meni state machine je legitimno ovdje.
- Međutim, uključuje **hardversko skeniranje I2C sabirnice** (`otkrijI2CAdrese()`), što je dijagnostika/hardware concern, ne čisti menu flow.
- Također sadrži neiskorištene dijelove API-ja za konfirmaciju/lozinku koji djeluju nedovršeno.

### `postavke.cpp`
- Uglavnom dobro fokusiran: default vrijednosti, validacija, checksum/potpis/verzija, load/save EEPROM, getter/setter.
- Dodatni LCD-redak helperi (`dohvatiPostavkeRedak1/2`) su prezentacijski detalj i rubno izlaze iz domene "data/persistence".

## 3) 📦 Što iz `menu_system.cpp` pripada u `postavke.cpp`?
- **Ništa od postojećeg EEPROM/persist dijela nije u `menu_system.cpp`**, što je dobro.
- Ono što bi se moglo dodatno pooštriti:
  - eventualna složenija validacija raspona za "tihi sati" ostati/pojačati u `postavke.cpp` setteru (već djelomično postoji).
- Zaključak: ne prebacivati menu state kod u `postavke.cpp`; granica je većinom dobra.

## 4) ⌨️ Što iz `tipke.cpp` pripada u `menu_system.cpp`?
- Prebaciti iz `tipke.cpp` u `menu_system.cpp` (ili ukloniti):
  - `meniJeAktivan`, `zadnjaDjetelnost`, `TIMEOUT_MENIJA`,
  - timeout granu u `provjeriTipke()`,
  - `uPostavkama()` kao wrapper koji kombinira "tipke + meni stanje".
- `tipke.cpp` treba ostati: mapiranje pin→`KeyEvent`, debounce i `obradiKluc(event)` poziv.

## 5) 🔗 Procjena sprege (tipke, navigacija, persistence)
- **Tipke i navigacija su previše spojeni** zbog dvostrukog upravljanja timeoutom.
- **Navigacija i persistence su umjereno spojeni** kroz API pozive, što je prihvatljivo.
- Najveći rizik regresije je timeout ponašanje jer ga trenutno vode dva modula.

## 6) 🧹 Zastarjeli API / mrtvi kod / suvišni wrapperi
- Izgleda neiskorišteno u ostatku projekta:
  - `postavke.cpp`: `dohvatiPostavkeRedak1()`, `dohvatiPostavkeRedak2()`.
  - `menu_system.cpp/h`: `dohvatiOdabraniIndex()`, `potvrdiAkciju(bool)`, `ulaziUManjuLozinkom()`.
  - `tipke.cpp/h`: `uPostavkama()`.
- Sumnjivi indikatori nedovršenosti:
  - `cekamo_da_ne` varijabla je deklarirana, ali praktički ne sudjeluje u toku.
  - `MENU_STATE_CONFIRMATION` infrastruktura postoji, ali lifecycle izgleda parcijalan.

## 7) 🏗️ Preporučena konačna arhitektura
- Opcija: **zadržati 3 datoteke odvojene + izdvojiti zajedničke helper funkcije**.
- Ne preporučuje se merge datoteka:
  - `tipke.cpp` je prirodni hardware-input modul,
  - `menu_system.cpp` je prirodni UI/state modul,
  - `postavke.cpp` je prirodni settings/persistence modul.

## 🛡️ Najsigurniji refaktor (minimalan rizik)
1. **Faza 1 (bez promjene ponašanja):**
   - Ukloniti timeout logiku iz `tipke.cpp`; timeout ostaviti samo u `menu_system.cpp`.
   - Ostaviti skeniranje tipki + debounce nepromijenjeno.
2. **Faza 2:**
   - Ukloniti neiskorištene API-je (`uPostavkama`, `dohvatiPostavkeRedak1/2` ako nema stvarnih poziva, te neiskorištene menu wrappere).
3. **Faza 3:**
   - Premjestiti `otkrijI2CAdrese()` iz `menu_system.cpp` u zaseban dijagnostički/hardware modul.
4. **Faza 4 (opcijski):**
   - Izdvojiti pomoćne funkcije za crtanje LCD redaka u mali UI helper modul radi manjeg `menu_system.cpp`.

## ⛪ Napomena za toranjski sat
- Predložene promjene čuvaju postojeću funkcionalnost toranjskog sata i smanjuju rizik da tipke, navigacija i spremanje postavki međusobno uvjetuju neočekivano ponašanje pri otkucavanju i tihim satima.
