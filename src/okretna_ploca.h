// okretna_ploca.h
#pragma once

void inicijalizirajPlocu();
void upravljajPločom();

// Ručno postavljanje trenutnog položaja ploče (0-95)
void postaviTrenutniPolozajPloce(int pozicija);

// Kompenzacija položaja ploče nakon nestanka struje
void kompenzirajPlocu();
