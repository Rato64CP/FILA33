// mrtvacko_thumbwheel.h - Ocitavanje BCD thumbwheel timera mrtvackog zvona
#pragma once

#include <stdint.h>

// Inicijalizira 8 ulaza za dvije BCD znamenke timera mrtvackog zvona.
void inicijalizirajMrtvackoThumbwheel();

// Periodicki osvjezava ulaz thumbwheel timera bez blokiranja ostatka toranjskog sata.
void osvjeziMrtvackoThumbwheel();

// Vraca true ako postoji stabilno i valjano BCD ocitanje obje znamenke.
bool jeMrtvackoThumbwheelValjan();

// Vraca ukupnu vrijednost 0-99 iz stabilnog ocitanja; u suprotnom vraca 0.
uint8_t dohvatiMrtvackoThumbwheelVrijednost();
