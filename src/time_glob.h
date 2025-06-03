#pragma once

#include <RTClib.h>
#include <Arduino.h>

void inicijalizirajSat();
DateTime dohvatiTrenutnoVrijeme();
void postaviVrijemeIzNTP(const DateTime& dt);
void postaviVrijemeIzDCF(const DateTime& dt);
void postaviVrijemeRucno(const DateTime& dt);
void azurirajOznakuDana();
String dohvatiIzvorVremena();
char dohvatiOznakuDana();

