#pragma once
#include <RTClib.h>

void inicijalizirajRTC();
DateTime dohvatiTrenutnoVrijeme();
void postaviVrijemeIzNTP(const DateTime& dt);
void azurirajVrijemeIzNTP(const DateTime& dt);
void postaviVrijemeIzDCF(const DateTime& dt);
void azurirajVrijemeIzDCF(const DateTime& dt);
void postaviVrijemeRucno(const DateTime& dt);
void azurirajOznakuDana();
String dohvatiIzvorVremena();
char dohvatiOznakuDana();
