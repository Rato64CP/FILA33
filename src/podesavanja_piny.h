// podesavanja_piny.h
#pragma once

// DCF signalni ulaz
#ifndef PODESAVANJA_PINY_H
#define PODESAVANJA_PINY_H
#define DCF_PIN 2 

// Kazaljke sata
#define PIN_RELEJ_PARNE_KAZALJKE   10
#define PIN_RELEJ_NEPARNE_KAZALJKE 11

// Okretna ploča
#define PIN_RELEJ_PARNE_PLOCE      8
#define PIN_RELEJ_NEPARNE_PLOCE    9

// Čekići
#define PIN_CEKIC_MUSKI             2
#define PIN_CEKIC_ZENSKI            3

// Zvona
#define PIN_ZVONO_MUSKO             4
#define PIN_ZVONO_ZENSKO            5

// LCD I2C — koristi se I2C adresiranje, nema potrebu za pinovima ovdje

// RTC I2C — koristi se I2C adresiranje, nema potrebu za pinovima ovdje

// ESP komunikacija (softverski serijski port primjerice)
#define PIN_ESP_RX                  6
#define PIN_ESP_TX                  7
