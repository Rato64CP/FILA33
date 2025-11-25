// podesavanja_piny.h
#pragma once

#ifndef PODESAVANJA_PINY_H
#define PODESAVANJA_PINY_H

// Kazaljke sata
#define PIN_RELEJ_PARNE_KAZALJKE   10
#define PIN_RELEJ_NEPARNE_KAZALJKE 11

// Okretna ploča
#define PIN_RELEJ_PARNE_PLOCE      8
#define PIN_RELEJ_NEPARNE_PLOCE    9

// Ulazi okretne ploče (čavli) – aktivni na LOW uz interno povlačenje prema Vcc
#define PIN_PLOCA_ULAZ_1           30
#define PIN_PLOCA_ULAZ_2           31
#define PIN_PLOCA_ULAZ_3           32
#define PIN_PLOCA_ULAZ_4           33
#define PIN_PLOCA_ULAZ_5           34

// Tipkovnica (4x4 matrica, redovi i stupci)
#define PIN_TIPKOVNICA_RED1         40
#define PIN_TIPKOVNICA_RED2         41
#define PIN_TIPKOVNICA_RED3         42
#define PIN_TIPKOVNICA_RED4         43
#define PIN_TIPKOVNICA_STUPAC1      44
#define PIN_TIPKOVNICA_STUPAC2      45
#define PIN_TIPKOVNICA_STUPAC3      46
#define PIN_TIPKOVNICA_STUPAC4      47

// Čekići
#define PIN_CEKIC_MUSKI             12
#define PIN_CEKIC_ZENSKI            3

// Ulaz za slavljenje (aktivno na LOW uz interno pull-up otpornik)
#define PIN_SLAVLJENJE_SIGNAL       2

// Zvona
#define PIN_ZVONO_1                 4
#define PIN_ZVONO_2                 5

// DCF77 antena (koristi digitalni pin s hardverskim prekidom)
#define PIN_DCF_SIGNAL              18

#endif // PODESAVANJA_PINY_H
