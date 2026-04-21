# Arduino Mega pinout toranjskog sata

Ovaj dokument je citljiv pregled svih aktivnih pinova i konekcija za `Arduino Mega 2560` u sustavu toranjskog sata. Glavni izvor istine i dalje ostaje [podesavanja_piny.h](C:/Users/Rato/Documents/GitHub/FILA33/main/podesavanja_piny.h), a ova datoteka sluzi kao pomoc pri spajanju, servisiranju i provjeri instalacije.

## Releji kazaljki

| Funkcija | Pin | Napomena |
|---|---:|---|
| Relej parne kazaljke | `22` | Prva faza impulsa za kazaljke |
| Relej neparne kazaljke | `23` | Druga faza impulsa za kazaljke |

## Releji okretne ploce

| Funkcija | Pin | Napomena |
|---|---:|---|
| Relej parne ploce | `24` | Prva faza pomaka okretne ploce |
| Relej neparne ploce | `25` | Druga faza pomaka okretne ploce |

## Zvona i cekici

| Funkcija | Pin | Napomena |
|---|---:|---|
| Zvono 1 | `26` | Relej zvona 1 |
| Zvono 2 | `27` | Relej zvona 2 |
| Cekic 1 - muski | `28` | Satno otkucavanje i posebni nacini |
| Cekic 2 - zenski | `29` | Polusatno otkucavanje i posebni nacini |

## Ulazi okretne ploce

| Funkcija | Pin | Napomena |
|---|---:|---|
| Cavao 1 | `30` | Ulaz ploce |
| Cavao 2 | `31` | Ulaz ploce |
| Cavao 3 | `32` | Ulaz ploce |
| Cavao 4 | `33` | Ulaz ploce |
| Cavao 5 | `34` | Ulaz ploce |

## Sinkronizacija vremena

| Funkcija | Pin | Napomena |
|---|---:|---|
| DCF signal | `35` | `LOW = impuls` |
| DCF aktivacija prijemnika | `A9` | Opcionalni izlaz za `P1` DCF prijemnika |
| RTC SQW 1 Hz | `2` | `DS3231 SQW` takt za precizno okidanje |

## I2C sabirnica

| Funkcija | Pin | Napomena |
|---|---:|---|
| SDA | `20` | `DS3231 RTC` i vanjski EEPROM |
| SCL | `21` | `DS3231 RTC` i vanjski EEPROM |

## Matricna tipkovnica 4x5

### Retci

| Funkcija | Pin | Napomena |
|---|---:|---|
| Row 0 | `3` | Vod 0 matrice |
| Row 1 | `12` | Vod 1 matrice |
| Row 2 | `5` | Vod 2 matrice |
| Row 3 | `16` | Vod 3 matrice |

### Stupci

| Funkcija | Pin | Napomena |
|---|---:|---|
| Col 0 | `7` | Vod 4 matrice |
| Col 1 | `8` | Vod 5 matrice |
| Col 2 | `9` | Vod 6 matrice |
| Col 3 | `10` | Vod 7 matrice |
| Col 4 | `11` | Vod 8 matrice |

## Posebne tipke i prekidaci

| Funkcija | Pin | Napomena |
|---|---:|---|
| Kip-prekidac slavljenja | `43` | `LOW = slavljenje ukljuceno` |
| Tipka mrtvackog | `42` | Pritisak radi `toggle` |
| Kip-prekidac tihog rezima | `41` | `LOW = tihi rezim ON` |
| Rucna sklopka zvona 1 | `44` | `LOW = ON` |
| Rucna sklopka zvona 2 | `45` | `LOW = ON` |

## Signalne lampice

| Funkcija | Pin | Napomena |
|---|---:|---|
| Lampica Zvono 1 | `36` | `HIGH = upaljeno` |
| Lampica Zvono 2 | `37` | `HIGH = upaljeno` |
| Lampica Slavljenje | `38` | `HIGH = upaljeno` |
| Lampica Mrtvacko | `39` | `HIGH = upaljeno` |
| Lampica Tihi rezim | `46` | `HIGH = upaljeno` |

## Thumbwheel za mrtvacko zvono

### Desetice

| BCD bit | Pin | Napomena |
|---|---:|---|
| `1` | `A1` | Desetice bit 0 |
| `2` | `A2` | Desetice bit 1 |
| `4` | `A3` | Desetice bit 2 |
| `8` | `A4` | Desetice bit 3 |

### Jedinice

| BCD bit | Pin | Napomena |
|---|---:|---|
| `1` | `A5` | Jedinice bit 0 |
| `2` | `A6` | Jedinice bit 1 |
| `4` | `A7` | Jedinice bit 2 |
| `8` | `A8` | Jedinice bit 3 |

Napomena:
- thumbwheel treba zatvarati prema `GND`
- firmware koristi `INPUT_PULLUP`
- svaka znamenka je `BCD 1-2-4-8`

## Serijska komunikacija

| Port | Pinovi | Uloga |
|---|---|---|
| `Serial` | USB | PC log i dijagnostika (`115200`) |
| `Serial1` | `RX1=19`, `TX1=18` | Priprema za vanjski `Raspberry Pi` most (`9600`) |
| `Serial3` | `RX3=15`, `TX3=14` | Ugradeni `ESP8266` (`9600`) |

Aktualna postavka firmwarea:
- `ESP_SERIJSKI_PORT = Serial3`

## Kratki sazetak po rasponima pinova

- `2` -> `RTC SQW`
- `3`, `5`, `7-12`, `16` -> matrica tipki
- `14-15` -> `Serial3` prema `ESP8266`
- `18-19` -> `Serial1` za buduci `Raspberry Pi`
- `20-21` -> `I2C`
- `22-29` -> releji kazaljki, ploce, zvona i cekica
- `30-35` -> ulazi ploce i `DCF`
- `36-39`, `46` -> signalne lampice
- `41-45` -> tipke i sklopke
- `A1-A9` -> thumbwheel i `DCF` aktivacija

## Napomena za razvoj

Ako se raspored pinova ikad mijenja, prvo treba uskladiti [podesavanja_piny.h](C:/Users/Rato/Documents/GitHub/FILA33/main/podesavanja_piny.h), a tek zatim ovu dokumentaciju.
