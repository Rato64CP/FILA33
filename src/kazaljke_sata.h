// kazaljke_sata.h
#pragma once

// Inicijalizacija pinova za releje kazaljki
void inicijalizirajKazaljke();

// Poziv iz loop() svakih 1000ms za pomicanje kazaljki
void upravljajKazaljkama();

// Ručno postavljanje kazaljki - korisnik unosi koliko je trenutno pokazano
void postaviTrenutniPolozajKazaljki(int trenutnaMinuta);

// Ručno pomicanje kazaljki da bi se dosegla prava minuta
void pomakniKazaljkeNaMinutu(int ciljMinuta);

// Kompenzacija nakon nestanka struje - vraćanje u sinkronizaciju
void kompenzirajKazaljke();
