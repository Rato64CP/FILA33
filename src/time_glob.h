#pragma once
#include <RTClib.h>

extern RTC_DS3231 rtc;
extern String izvorVremena;

void inicijalizirajSat();
DateTime dohvatiTrenutnoVrijeme();
void postaviVrijemeIzNTP(const DateTime& dt);
void azurirajVrijemeIzNTP(const DateTime& dt);
void postaviVrijemeIzDCF(const DateTime& dt);
void postaviVrijemeRucno(const DateTime& dt);
void azurirajOznakuDana();
String dohvatiIzvorVremena();
char dohvatiOznakuDana();
