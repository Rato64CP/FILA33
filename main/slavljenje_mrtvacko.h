// slavljenje_mrtvacko.h - Posebni nacini rada cekica toranjskog sata
#pragma once

// Inicijalizira lokalne tipke i pocetno stanje slavljenja i mrtvackog.
void inicijalizirajSlavljenjeIMrtvacko();

// Obraduje tipke i automat stanja posebnih nacina rada cekica.
void upravljajSlavljenjemIMrtvackim(unsigned long sadaMs);

// Slavljenje koristi cekice izvan redovnog satnog otkucavanja.
void zapocniSlavljenje();
void zaustaviSlavljenje();
bool jeSlavljenjeUTijeku();

// Mrtvacko koristi cekice izvan redovnog satnog otkucavanja.
void zapocniMrtvacko();
void zaustaviMrtvacko();
bool jeMrtvackoUTijeku();
