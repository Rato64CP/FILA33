// otkucavanje.h – Upravljanje mehaničkim otkucavanjem sata
#pragma once

// Inicijalizacija PIN-ova za čekiće
void inicijalizirajOtkucavanje();

// Upravljanje otkucavanjem u loop-u
void upravljajOtkucavanjem();

// Otkucaj broj za puni sat (Bell 1)
void otkucajSate(int broj);

// Otkucaj jedan udarac za pola sata (Bell 2)
void otkucajPolasata();

// Blokira ili dozvoljava otkucavanje
void postaviBlokaduOtkucavanja(bool blokiraj);