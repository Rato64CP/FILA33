// zvonjenje.h – Bell control (hourly and half-hourly strikes)
#pragma once

// Initialize bell system
void inicijalizirajZvona();

// Manage bells in loop
void upravljajZvonom();

// Enable Bell 1 or 2
void uključiZvono(int zvono);
void ukljuciZvono(int zvono);  // ASCII variant

// Disable Bell 1 or 2
void isključiZvono(int zvono);
void iskljuciZvono(int zvono);  // ASCII variant

// Check if any bell is ringing
bool jeZvonoUTijeku();

// Process mechanical plate inputs
void obradiCavleNaPloči();

// Activate bells if conditions met
void aktivirajZvonaAkoTrebaju();

// Activate/deactivate bell with duration
void aktivirajZvonjenje(int zvono);
void deaktivirajZvonjenje(int zvono);

// Check if inertia blocking is active
bool jeLiInerciaAktivna();

// Celebration mode (implemented in otkucavanje.cpp)
void zapocniSlavljenje();
void zaustaviSlavljenje();
bool jeSlavljenjeUTijeku();

// Funeral mode (implemented in otkucavanje.cpp)
void zapocniMrtvacko();
void zaustaviMrtvacko();
bool jeMrtvackoUTijeku();