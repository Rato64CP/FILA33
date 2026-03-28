#pragma once

#include <RTClib.h>

void inicijalizirajRTC();

DateTime dohvatiTrenutnoVrijeme();
uint32_t dohvatiRtcSekundniBrojac();

void azurirajVrijemeIzNTP(const DateTime& ntpVrijeme);
void azurirajVrijemeIzDCF(const DateTime& dcfVrijeme);
void azurirajVrijemeRucno(const DateTime& rucnoVrijeme);

String dohvatiIzvorVremena();
const char* dohvatiOznakuIzvoraVremena();
char dohvatiOznakuDana();
void azurirajOznakuDana();

bool jeRTCPouzdan();
bool jeRtcSqwAktivan();
bool fallbackImaPouzdanuReferencu();

DateTime getZadnjeSinkroniziranoVrijeme();
void oznaciPovratakNaRTC();
bool jeSinkronizacijaZastarjela();
