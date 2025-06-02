// kazaljke_sata.h
#pragma once

#include <RTClib.h>

void inicijalizirajKazaljke();
void upravljajKazaljkama();
void postaviTrenutniPolozajKazaljki(int trenutnaMinuta);
void pomakniKazaljkeNaMinutu(int ciljMinuta);
void kompenzirajKazaljke();
void pomakniKazaljkeZa(int brojMinuta);

