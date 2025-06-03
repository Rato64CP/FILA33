#pragma once
#include <RTClib.h>

bool isDST(int dan, int mjesec, int danUTjednu); // provjera DST razdoblja
void syncNTP();                 // sinkronizacija RTC-a preko NTP-a (ESP modul)
void syncDCF();                 // sinkronizacija RTC-a prema DCF77 signalu
extern String izvorVremena;     // "NTP", "DCF", "RU" ili "RTC" – zadnji izvor
