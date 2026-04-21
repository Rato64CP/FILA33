// prekidac_tisine.h - Jedinstveni tihi rezim zvona i cekica toranjskog sata
#pragma once

// Inicijalizira ulaz kip prekidaca i pocetno stanje tihog rezima.
void inicijalizirajPrekidacTisine();

// Periodicki uskladuje tihi rezim iz kip prekidaca i uskrsne tisine.
void osvjeziPrekidacTisine();

// Vraca true kad je aktivan jedinstveni tihi rezim (zvona i cekici blokirani).
bool jePrekidacTisineAktivan();
