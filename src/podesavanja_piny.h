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

// Tipkovnica (6 tipki s internim pull-upom)
#define PIN_TIPKA_GORE              40
#define PIN_TIPKA_DOLJE             41
#define PIN_TIPKA_LIJEVO            42
#define PIN_TIPKA_DESNO             43
#define PIN_TIPKA_DA                44
#define PIN_TIPKA_NE                45

// Čekići
#define PIN_CEKIC_MUSKI             12
#define PIN_CEKIC_ZENSKI            3

// Ulaz za slavljenje (aktivno na LOW uz interno pull-up otpornik)
#define PIN_SLAVLJENJE_SIGNAL       2

// Zvona
#define PIN_ZVONO_MUSKO             4
#define PIN_ZVONO_ZENSKO            5

// ESP komunikacija (softverski serijski port primjerice)
#define PIN_ESP_RX                  6
#define PIN_ESP_TX                  7

#endif // PODESAVANJA_PINY_H
