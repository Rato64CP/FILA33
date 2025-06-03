#pragma once
#include <RTClib.h>

void inicijalizirajRTC();       // pokretanje RTC-a i provjera gubitka napajanja
bool isDST(int dan, int mjesec, int danUTjednu); // provjera DST razdoblja
void syncNTP();                 // sinkronizacija RTC-a preko NTP-a (ESP modul)
void syncDCF();                 // sinkronizacija RTC-a prema DCF77 signalu
