// postavke_migracije.h - Migracije starijih EEPROM zapisa toranjskog sata
#pragma once

#include <stdint.h>

#include "eeprom_konstante.h"

// Pokusava ucitati i migrirati stariji zapis postavki (v8-v10) u aktualni
// spremnik toranjskog sata. Vraca izvornu verziju ako je migracija uspjela,
// ili 0 ako nije pronadjen valjan stariji zapis.
uint8_t pokusajMigriratiLegacySpremnikPostavki(
    EepromLayout::PostavkeSpremnik& cilj,
    const EepromLayout::PostavkeSpremnik& zadane);
