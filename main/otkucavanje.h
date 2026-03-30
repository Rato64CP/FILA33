// otkucavanje.h - Upravljanje mehanickim otkucavanjem sata
#pragma once

// Inicijalizacija PIN-ova za cekice
void inicijalizirajOtkucavanje();

// Upravljanje otkucavanjem u loop-u
void upravljajOtkucavanjem();

// Otkucaj broj za puni sat (Bell 1)
void otkucajSate(int broj);

// Otkucaj jedan udarac za pola sata (Bell 2)
void otkucajPolasata();

// Blokira ili dozvoljava otkucavanje
void postaviBlokaduOtkucavanja(bool blokiraj);

// Dinamicki status satnog/polusatnog otkucavanja za LCD i telemetriju.
bool jeOtkucavanjeUTijeku();
