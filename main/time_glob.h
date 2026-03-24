// time_glob.h – Globalno rukovanje vremenom (RTC + NTP fallback)
#pragma once

#include <RTClib.h>

// Inicijalizacija RTC modula (DS3231)
void inicijalizirajRTC();

// Dohvati trenutno vrijeme iz aktivnog izvora (RTC, NTP ili DCF)
DateTime dohvatiTrenutnoVrijeme();

// Ažuriranje vremena iz NTP izvora
void azurirajVrijemeIzNTP(const DateTime& ntpVrijeme);

// Ažuriranje vremena iz DCF77 izvora
void azurirajVrijemeIzDCF(const DateTime& dcfVrijeme);

// Oznaka izvora vremena: "RTC", "NTP", "DCF" ili fallback
String dohvatiIzvorVremena();

// Oznaka dana tjedna za LCD prikaz
char dohvatiOznakuDana();

// Ažuriranje oznake dana
void azurirajOznakuDana();

// Provjera validnosti RTC baterije
bool jeRTCPouzdan();

// Fallback reference (koristi se ako nema NTP/DCF)
bool fallbackImaPouzdanuReferencu();

// Dohvati vrijeme zadnje sinkronizacije
DateTime getZadnjeSinkroniziranoVrijeme();

// Oznaka: ponovi na RTC ako je NTP/DCF nesigurna
void oznaciPovratakNaRTC();

// Provjera je li sinkronizacija zastarjela
bool jeSinkronizacijaZastarjela();