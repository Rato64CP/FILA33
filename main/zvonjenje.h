// zvonjenje.h - upravljanje zvonima toranjskog sata
#pragma once

// Inicijalizacija sustava zvona
void inicijalizirajZvona();

// Obrada zvona u glavnoj petlji
void upravljajZvonom();

// Rucno ili automatsko ukljucivanje pojedinog zvona 1-2
void ukljuciZvono(int zvono);

// Iskljucivanje pojedinog zvona 1-2
void iskljuciZvono(int zvono);

// Upiti o stanju zvona
bool jeZvonoUTijeku();
bool jeZvonoAktivno(int zvono);

// Aktivacija/deaktivacija s trajanjem
void aktivirajZvonjenje(int zvono);
void aktivirajZvonjenjeNaTrajanje(int zvono, unsigned long trajanjeMs);
void deaktivirajZvonjenje(int zvono);

// Provjera inercijske blokade
bool jeLiInerciaAktivna();

// Slavljenje (implementacija u otkucavanje.cpp)
void zapocniSlavljenje();
void zaustaviSlavljenje();
bool jeSlavljenjeUTijeku();

// Mrtvacko zvono (implementacija u otkucavanje.cpp)
void zapocniMrtvacko();
void zaustaviMrtvacko();
bool jeMrtvackoUTijeku();
