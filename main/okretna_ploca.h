// okretna_ploca.h
#pragma once

void inicijalizirajPlocu();
void upravljajPlocom();
void postaviTrenutniPolozajPloce(int pozicija);
int dohvatiPozicijuPloce();
bool jePlocaUSinkronu();
void zatraziPoravnanjeTaktaPloce();
void postaviRucnuBlokaduPloce(bool blokirano);
bool jeRucnaBlokadaPloceAktivna();
bool mozeSeRucnoNamjestatiPloca();
