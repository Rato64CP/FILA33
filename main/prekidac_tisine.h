// prekidac_tisine.h - Jedinstveni tihi rezim zvona i cekica toranjskog sata
#pragma once

// Inicijalizira ulaz kip prekidaca i pocetno stanje tihog rezima.
void inicijalizirajPrekidacTisine();

// Periodicki uskladuje tihi rezim iz kip prekidaca i uskrsne tisine.
void osvjeziPrekidacTisine();

// Vraca true kad je aktivan jedinstveni tihi rezim (zvona i cekici blokirani).
bool jePrekidacTisineAktivan();

// Ukljucuje ili iskljucuje webski virtualni prekidac tihog rezima.
// Fizicki kip prekidac i uskrsna tisina i dalje imaju prednost u jedinstvenom stanju.
void postaviWebTihiRezim(bool aktivan);

// Osvjezava lampicu tihog rezima i za vanjske blokade poput UPS moda.
void osvjeziSignalizacijuTihogRezima();
