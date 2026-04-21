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
void postaviGlobalnuBlokaduZvona(bool blokiraj);

// Provjera inercijske blokade
bool jeLiInerciaAktivna();
