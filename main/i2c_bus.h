#pragma once

// Zajednicka priprema I2C sabirnice toranjskog sata.
// Koristi se prije rada LCD-a, DS3231 RTC-a, vanjskog FRAM/EEPROM spremnika i servisnog skeniranja
// kako bi `Wire` uvijek imao isti timeout i reset sabirnice kod blokade.
void pripremiI2CSabirnicuSigurno();
