// okretna_ploca.h
#pragma once

void inicijalizirajPlocu();
void upravljajPločom();
void postaviTrenutniPolozajPloce(int pozicija);
void postaviOffsetMinuta(int offset);
int dohvatiPozicijuPloce();
int dohvatiOffsetMinuta();
void pomakniPlocuZa(int brojKvadranata);
// PametniMod izbjegava pomicanje ako je pozicija vec gotovo ispravna
void kompenzirajPlocu(bool pametniMod);
