// okretna_ploca.h
#pragma once

void inicijalizirajPlocu();
void upravljajPlocom();
void postaviTrenutniPolozajPloce(int pozicija);
void postaviOffsetMinuta(int offset);
int dohvatiPozicijuPloce();
int dohvatiOffsetMinuta();
bool jePlocaUSinkronu();
void oznaciPlocuKaoSinkroniziranu();
void zatraziPoravnanjeTaktaPloce();
