// kazaljke_sata.h
#pragma once

#include <RTClib.h>

void inicijalizirajKazaljke();
void upravljajKazaljkama();
void postaviTrenutniPolozajKazaljki(int trenutnaMinuta);
// Ako je pametanMod ukljucen, funkcije ce izbjeci nepotrebne pomake
// kada je razlika mala i umjesto toga sacekati sljedeci impuls.
void pomakniKazaljkeNaMinutu(int ciljMinuta, bool pametanMod);
void kompenzirajKazaljke(bool pametanMod);
void pomakniKazaljkeZa(int brojMinuta);
bool suKazaljkeUSinkronu();
int dohvatiMemoriraneKazaljkeMinuta();
void oznaciKazaljkeKaoSinkronizirane();

