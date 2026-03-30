#pragma once
#include <RTClib.h>

void inicijalizirajDCF();
void osvjeziDCFSinkronizaciju();
void pokreniRucniDCFPrijem();
bool jeDCFSinkronizacijaUTijeku();
bool jeDCFImpulsAktivan();
uint32_t dohvatiDcfVizualniBrojac();
