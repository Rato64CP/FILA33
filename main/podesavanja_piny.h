// podesavanja_piny.h - objedinjene definicije pinova toranjskog sata
// Jedino mjesto istine za raspored svih hardverskih pinova.
// Pinout je prilagoden za Arduino Mega 2560.

#ifndef PODESAVANJA_PINY_H
#define PODESAVANJA_PINY_H

// ==================== RELAY CONTROL PINS ====================
// Impulse relay pins for hand position control (K-minuta tracking)
// 6-second pulses via ULN2803 optocoupler drivers to 5V relays

#define PIN_RELEJ_PARNE_KAZALJKE      22  // Even relay (first phase)
#define PIN_RELEJ_NEPARNE_KAZALJKE    23  // Odd relay (second phase)

// Impulse relay pins for rotating plate control (position tracking)
#define PIN_RELEJ_PARNE_PLOCE         24  // Even relay for plate (first phase)
#define PIN_RELEJ_NEPARNE_PLOCE       25  // Odd relay for plate (second phase)

// ==================== PINOVI ZVONA I CEKICA ====================
// Toranjski sat koristi samo dva releja zvona.

#define PIN_ZVONO_1                   26  // Zvono 1
#define PIN_ZVONO_2                   27  // Zvono 2

#define PIN_CEKIC_MUSKI               28  // Cekic 1 - muski
#define PIN_CEKIC_ZENSKI              29  // Cekic 2 - zenski

// ==================== ULAZI OKRETNE PLOCE ====================
// Mehanicki ulazi ploce koriste 5 cavala.

#define PIN_ULAZA_PLOCE_1             30  // Cavao 1
#define PIN_ULAZA_PLOCE_2             31  // Cavao 2
#define PIN_ULAZA_PLOCE_3             32  // Cavao 3
#define PIN_ULAZA_PLOCE_4             33  // Cavao 4
#define PIN_ULAZA_PLOCE_5             34  // Cavao 5

// ==================== TIME SYNCHRONIZATION INPUTS ====================
// RTC SQW ostaje jedini lokalni takt sinkronizacije vremena toranjskog sata.

#define PIN_RTC_SQW                   2   // DS3231 SQW 1 Hz takt za precizno okidanje

// ==================== I2C BUS ====================
// I2C communication for RTC (DS3231) and external EEPROM (24C32)
// Arduino Mega I2C: SDA=20, SCL=21 (fixed pins)

#define PIN_SDA                       20  // I2C SDA - serial data
#define PIN_SCL                       21  // I2C SCL - serial clock

// ==================== MATRICNA TIPKOVNICA 4x5 ====================
// Lokalni LCD izbornik toranjskog sata koristi matricu od 20 tipki s 9 vodova.
// Preporuceno mapiranje logickih naredbi definira main/tipke.cpp:
// strelice = navigacija, Ent = SELECT, ESC = BACK.

#define PIN_KEYPAD_ROW_0              3   // Vod 0 matrice
#define PIN_KEYPAD_ROW_1              12  // Vod 1 matrice - testni premjestaj
#define PIN_KEYPAD_ROW_2              5   // Vod 2 matrice
#define PIN_KEYPAD_ROW_3              16  // Vod 3 matrice - testni premjestaj
#define PIN_KEYPAD_COL_0              7   // Vod 4 matrice
#define PIN_KEYPAD_COL_1              8   // Vod 5 matrice
#define PIN_KEYPAD_COL_2              9   // Vod 6 matrice
#define PIN_KEYPAD_COL_3              10  // Vod 7 matrice
#define PIN_KEYPAD_COL_4              11  // Vod 8 matrice

// ==================== SUNCEVA AUTOMATIKA - LOKALNE TIPKE I LAMPICE ====================
// Dvije fizicke tipke ukljucuju/iskljucuju jutarnju i vecernju automatiku,
// a dvije LED lampice prikazuju stanje tih suncevih dogadaja.

#define PIN_TIPKA_SUNCE_VECER         A9   // Tipka vecernje automatike (LOW=toggle)
#define PIN_LAMPICA_SUNCE_VECER       A10  // LED za vecernju automatiku (HIGH=upali)
#define PIN_TIPKA_SUNCE_JUTRO         A11  // Tipka jutarnje automatike (LOW=toggle)
#define PIN_LAMPICA_SUNCE_JUTRO       A12  // LED za jutarnju automatiku (HIGH=upali)

// ==================== SLAVLJENJE I MRTVACKO ULAZI ====================
// Slavljenje koristi kip-prekidac, a mrtvacko zasebno tipkalo.

#define PIN_KEY_CELEBRATION           43  // Kip-prekidac slavljenja (LOW=ukljuceno)
#define PIN_KEY_FUNERAL               42  // Tipkalo mrtvackog (pritisak=toggle)

// ==================== GLOBALNI PREKIDAC TISINE ====================
// Kip prekidac za tihi rad: LOW=aktivna tisina, blokira zvona i cekice,
// a ostavlja kazaljke toranjskog sata aktivnima.

#define PIN_PREKIDAC_TISINE           41  // Globalni tihi rad (LOW=ON)
#define PIN_LAMPICA_TIHI_REZIM        46  // Lampica tihog rezima (HIGH=upali)

// ==================== SIGNALNE LAMPICE STANJA ====================
// Pojedinacne LED lampice za lokalnu signalizaciju rada zvona i posebnih nacina.

#define PIN_LAMPICA_ZVONO_1           36  // LED za ZVONO 1 (HIGH=upali)
#define PIN_LAMPICA_ZVONO_2           37  // LED za ZVONO 2 (HIGH=upali)
#define PIN_LAMPICA_SLAVLJENJE        38  // LED za slavljenje (HIGH=upali)
#define PIN_LAMPICA_MRTVACKO          39  // LED za mrtvacko (HIGH=upali)

// ==================== NOCNA RASVJETA ====================
// Relej nocne rasvjete toranjskog sata, upravljan prema jutarnjem i vecernjem
// suncevom dogadaju. Danju je OFF, nocu je ON.

#define PIN_RELEJ_NOCNE_RASVJETE      47  // Relej nocne rasvjete (HIGH=ukljuci)

// ==================== MANUAL BELL TOGGLE INPUTS ====================
// Fizicke sklopke na GND za rucno upravljanje zvonima toranjskog sata

#define PIN_BELL1_SWITCH              44  // Rucna sklopka za BELL 1 (LOW=ON)
#define PIN_BELL2_SWITCH              45  // Rucna sklopka za BELL 2 (LOW=ON)

// ==================== MRTVACKO ZVONO - THUMBWHEEL TIMER ====================
// Dvije BCD znamenke za trajanje mrtvackog zvona.
// Pretpostavka: thumbwheel zatvara prema GND pa koristimo INPUT_PULLUP.

#define PIN_MRTVACKO_TIMER_DESETICE_BIT0  A1  // BCD 1
#define PIN_MRTVACKO_TIMER_DESETICE_BIT1  A2  // BCD 2
#define PIN_MRTVACKO_TIMER_DESETICE_BIT2  A3  // BCD 4
#define PIN_MRTVACKO_TIMER_DESETICE_BIT3  A4  // BCD 8
#define PIN_MRTVACKO_TIMER_JEDINICE_BIT0  A5  // BCD 1
#define PIN_MRTVACKO_TIMER_JEDINICE_BIT1  A6  // BCD 2
#define PIN_MRTVACKO_TIMER_JEDINICE_BIT2  A7  // BCD 4
#define PIN_MRTVACKO_TIMER_JEDINICE_BIT3  A8  // BCD 8

// ==================== SERIAL COMMUNICATION ====================
// Arduino Mega provides 4 hardware serial ports (Serial, Serial1-3)

// Serial0 (USB):  115200 baud - PC debugging/logging
// Serial1:        rezerviran za buduci RS485 sloj (Rx1=pin19, Tx1=pin18)
// Serial3:        9600 baud   - ugradeni ESP8266 na Mega+WiFi R3 plocici (Rx3=pin15, Tx3=pin14)

// RS485 ostaje dodatni transportni sloj za toranjski sat. Postojeci relejni
// izlazi ostaju aktivni, a RS485 se uvodi nerazorno za kasnije slanje naredbi.
#define PIN_RS485_SMJER              35  // DE/RE upravljanje transceiverom (HIGH=TX, LOW=RX)
#define RS485_SERIJSKI_PORT          Serial1

// Toranjski sat koristi ESP kao jedini aktivni mrezni most i bezicni API sloj.
#define ESP_SERIJSKI_PORT            Serial3

// All PIN assignments consolidated in this single header file
// No duplicate definitions allowed in other source files

#endif  // PODESAVANJA_PINY_H
