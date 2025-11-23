// zvonjenje.h
#pragma once

void inicijalizirajZvona();
void upravljajZvonom();
void zapocniSlavljenje();
void zaustaviSlavljenje();
void zapocniMrtvacko();
void zaustaviZvonjenje();

void aktivirajZvonjenje(int koje);
void deaktivirajZvonjenje(int koje);
bool jeZvonoUTijeku();
bool jeSlavljenjeUTijeku();
bool jeMrtvackoUTijeku();
