// okretna_ploca.h
#pragma once

void inicijalizirajPlocu();
void upravljajPlocom();
void postaviTrenutniPolozajPloce(int pozicija);
void postaviOffsetMinuta(int offset);
int dohvatiPozicijuPloce();
int dohvatiOffsetMinuta();
bool pretvoriPozicijuPloceUVrijeme(int pozicija, int& sat24, int& minuta);
bool pretvoriVrijemeUPozicijuPloce(int sat24, int minuta, int& pozicija);
bool jePlocaUSinkronu();
void oznaciPlocuKaoSinkroniziranu();
void zatraziPoravnanjeTaktaPloce();
void postaviRucnuBlokaduPloce(bool blokirano);
bool jeRucnaBlokadaPloceAktivna();
bool mozeSeRucnoNamjestatiPloca();
