// pogrebne_skripte.h - Sekvence POKOJNIK / POKOJNICA za toranjski sat
#pragma once

#include <stdint.h>

// Priprema internu automatiku za pogrebne sekvence s web dashboarda.
void inicijalizirajPogrebneSkripte();

// Periodicki vodi sekvencu:
// zvono 2 minute -> cekanje inercije -> mrtvacko 10 minuta.
void upravljajPogrebnimSkriptama(unsigned long sadaMs);

// Pokrece sekvencu `POKOJNIK` s muskim zvonom.
bool pokusajPokrenutiPokojnika();

// Pokrece sekvencu `POKOJNICA` sa zenskim zvonom.
bool pokusajPokrenutiPokojnicu();

// Ako je `POKOJNIK` vec aktivan, isti zahtjev ga zaustavlja.
// Ako je aktivna druga sekvenca, zahtjev se odbija.
bool prebaciPokojnika();

// Ako je `POKOJNICA` vec aktivna, isti zahtjev je zaustavlja.
// Ako je aktivna druga sekvenca, zahtjev se odbija.
bool prebaciPokojnicu();

// Sigurno prekida aktivnu ili zakazanu pogrebnu sekvencu.
void zaustaviPogrebneSkripte();

// Dijagnosticki upit za trenutno stanje sekvence.
bool jePogrebnaSkriptaAktivna();

// Vraca 0 kad sekvenca nije aktivna, 1 za `POKOJNIK`, 2 za `POKOJNICA`.
uint8_t dohvatiTipAktivnePogrebneSkripte();
