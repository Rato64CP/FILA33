// kazaljke_sata.h
#pragma once

#include <RTClib.h>

void inicijalizirajKazaljke();
void upravljajKazaljkama();
void upravljajKorekcijomKazaljki();
void postaviTrenutniPolozajKazaljki(int trenutnaMinuta);
// Ako je pametanMod ukljucen, funkcije ce izbjeci nepotrebne pomake
// kada je razlika mala i umjesto toga sacekati sljedeci impuls.
void pomakniKazaljkeNaMinutu(int ciljMinuta, bool pametanMod);
void pomakniKazaljkeZa(int brojMinuta);
bool suKazaljkeUSinkronu();
int dohvatiMemoriraneKazaljkeMinuta();
void obavijestiKazaljkeDSTPromjena(int pomakMinuta);

// Ručna pozicija i brza korekcija kazaljki
void postaviRucnuPozicijuKazaljki(int satKazaljke, int minutaKazaljke);
void pokreniBudnoKorekciju();
void zatraziPoravnanjeTaktaKazaljki();
void postaviRucnuBlokaduKazaljki(bool blokirano);
bool jeRucnaBlokadaKazaljkiAktivna();
bool mozeSeRucnoNamjestatiKazaljke();
