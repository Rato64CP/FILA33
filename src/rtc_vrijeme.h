#pragma once
#include <RTClib.h>

void inicijalizirajRTC();                      // pokretanje RTC-a i provjera gubitka napajanja
bool isDST(int dan, int mjesec, int danUTjednu); // određuje je li trenutno razdoblje DST-a
void syncNTP();                                // sinkronizacija RTC-a preko NTP-a (ESP modul)
void syncDCF();                                // sinkronizacija RTC-a prema DCF77 signalu
extern String izvorVremena;                    // “NTP”, “DCF”, “RU” ili “RTC” – zadnji izvor
