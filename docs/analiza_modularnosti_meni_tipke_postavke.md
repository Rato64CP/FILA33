# 🧭 Analiza modularnosti: `postavke.cpp`, `menu_system.cpp`, `tipke.cpp`

Ovaj dokument je zadrzan kao razvojna analiza granica odgovornosti izmedu modula. Dio tada navedenih problema je u meduvremenu vec ociscen, pa ga treba citati kao arhivsku analizu, a ne kao potpun opis danasnjeg stanja.

## 🎯 Sažetak preporuke

- datoteke zadrzati odvojene, bez spajanja u jednu ili dvije datoteke
- cistiti granice odgovornosti ciljano, a ne agresivnim merge refaktorom
- `tipke.cpp` treba ostati hardware-input sloj
- `menu_system.cpp` treba ostati UI/state sloj
- `postavke.cpp` treba ostati settings/persistence sloj

## 1. Sto je i dalje dobra arhitektonska podjela

### `tipke.cpp`
- fizicko skeniranje tipki
- debounce
- pretvorba u `KeyEvent`

### `menu_system.cpp`
- stanje menija
- navigacija
- ekrani
- unos i potvrda korisnickih promjena

### `postavke.cpp`
- default vrijednosti
- validacija
- checksum, potpis i verzija
- ucitavanje i spremanje u EEPROM

## 2. Sto je od starih nalaza vec zastarjelo

- dio nekad sumnjivih wrapper funkcija i mrtvih API-ja je u meduvremenu uklonjen
- `offset` ploce vise nije aktivni dio modela postavki ni recovery logike
- dio stare kompatibilne EEPROM ostavstine je ociscen iz aktivnog modela

## 3. Sto i dalje ostaje razumna preporuka

- ne seliti persistence logiku iz `postavke.cpp` u `menu_system.cpp`
- ne seliti hardware skeniranje tipki iz `tipke.cpp` u `menu_system.cpp`
- sve slozenije validacije i dalje drzati u `postavke.cpp`

## 4. Napomena za toranjski sat

Predlozena podjela modula cuva postojece ponasanje toranjskog sata i smanjuje rizik da tipke, navigacija i spremanje postavki medusobno uvjetuju neocekivano ponasanje pri otkucavanju, tihom rezimu i recoveryju.
