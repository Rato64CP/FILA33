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
bool jeVrijemeSvjezeZaRtcTick(uint32_t rtcTick);

void azurirajVrijemeIzNTP(const DateTime& ntpVrijeme,
                          uint16_t ntpMilisekunde = 0,
                          bool imaEksplicitanDST = false,
                          bool dstAktivanIzvori = false);
void azurirajVrijemeRucno(const DateTime& rucnoVrijeme);

const char* dohvatiOznakuIzvoraVremena();
bool jeRtcSqwAktivan();
bool jeRtcSqwGreskaAktivna();
bool jeRtcSqwPrvaPolovicaSekunde();
bool jeRtcDegradiraniNacinAktivan();
bool jeRtcIzlazniFailSafeAktivan();
bool dohvatiRtcTemperaturu(float& temperaturaC);
bool jeVrijemePotvrdjenoZaAutomatiku();
bool jeUskrsnaTisinaAktivna(const DateTime& vrijeme);
DateTime dohvatiDatumUskrsaZaGodinu(int godina);
int dohvatiUTCOffsetMinuteZaLokalnoVrijeme(const DateTime& vrijeme);

void resetirajIzvorSinkronizacijeNaRTC();
bool jeSinkronizacijaZastarjela();
