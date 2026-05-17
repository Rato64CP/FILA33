// postavke_kalendar.h - Liturgijski kalendar i mise toranjskog sata
#pragma once

#include "eeprom_konstante.h"
#include "postavke.h"

EepromLayout::BlagdaniSpremnik napraviZadaneBlagdane();
EepromLayout::MiseSpremnik napraviZadaneMise();
uint8_t ograniceniSviSvetiPocetakSat(uint8_t sat);
uint8_t ograniceniSviSvetiZavrsetakSat(uint8_t sat);
void sanitizirajRedovituMisuPostavku(bool& omogucena, uint8_t& satMise, uint8_t& minutaMise);
void sanitizirajNepomicniBlagdan(uint8_t indeks, NepomicniBlagdanPostavka& blagdan);
void sanitizirajPomicniBlagdan(uint8_t indeks, PomicniBlagdanPostavka& blagdan);
