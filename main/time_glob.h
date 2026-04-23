#pragma once

#include <RTClib.h>

enum RezultatProvjereSinkronizacije {
  SINKRONIZACIJA_ODBIJENA = 0,
  SINKRONIZACIJA_CEKA_DODATNU_POTVRDU = 1,
  SINKRONIZACIJA_PRIHVACENA = 2
};

void inicijalizirajRTC();

DateTime dohvatiTrenutnoVrijeme();
uint32_t dohvatiRtcSekundniBrojac();

void azurirajVrijemeIzNTP(const DateTime& ntpVrijeme,
                          bool imaEksplicitanDST = false,
                          bool dstAktivanIzvori = false);
void azurirajVrijemeRucno(const DateTime& rucnoVrijeme);

const char* dohvatiOznakuIzvoraVremena();
bool jeZadnjaSvjezaSinkronizacijaIzNTP();
bool jeRtcSqwAktivan();
bool jeVrijemePotvrdjenoZaAutomatiku();
DateTime dohvatiDatumUskrsaZaGodinu(int godina);
bool jeUskrsnaTisinaAktivna(const DateTime& vrijeme);
int dohvatiUTCOffsetMinuteZaLokalnoVrijeme(const DateTime& vrijeme);

void oznaciPovratakNaRTC();
void resetirajIzvorSinkronizacijeNaRTC();
bool jeSinkronizacijaZastarjela();
