#pragma once
#include <RTClib.h>

// Global RTC instance used by all modules
extern RTC_DS3231 rtc;

void inicijalizirajSat();
DateTime dohvatiTrenutnoVrijeme();
void postaviVrijemeIzNTP(const DateTime& dt);
void azurirajVrijemeIzNTP(const DateTime& dt);
void postaviVrijemeIzDCF(const DateTime& dt);
void postaviVrijemeRucno(const DateTime& dt);
void azurirajOznakuDana();
String dohvatiIzvorVremena();
char dohvatiOznakuDana();
