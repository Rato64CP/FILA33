// kazaljke_sata.h
#pragma once

#include <RTClib.h>

void inicijalizirajKazaljke();
void upravljajKazaljkama();
void postaviTrenutniPolozajKazaljki(int trenutnaMinuta);
void pomakniKazaljkeNaMinutu(int ciljMinuta, bool pametanMod);
void kompenzirajKazaljke(bool pametanMod);
void pomakniKazaljkeZa(int brojMinuta);

