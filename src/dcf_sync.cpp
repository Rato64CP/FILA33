// dcf_sync.cpp
#include <Arduino.h>
#include <DCF77.h>
#include "dcf_sync.h"
#include "podesavanja_piny.h"
#include "time_glob.h"
#include "vrijeme_izvor.h"

static const unsigned long DCF_INTERVAL_PROVJERE_MS = 60000; // cekanje izmedu pokusaja
static const unsigned long DCF_VRIJEME_STABILIZACIJE_MS = 60000; // vrijeme stabilizacije signala

static DCF77 dcfPrijemnik(PIN_DCF_SIGNAL, digitalPinToInterrupt(PIN_DCF_SIGNAL));
static bool dcfPokrenut = false;
static unsigned long zadnjaProvjeraMillis = 0;
static unsigned long vrijemePocetka = 0;
static DateTime zadnjeDCF = DateTime((uint32_t)0);

static bool dcfStabiliziran() {
  return millis() - vrijemePocetka > DCF_VRIJEME_STABILIZACIJE_MS;
}

void inicijalizirajDCF() {
#ifdef PIN_DCF_NAPAJANJE
  pinMode(PIN_DCF_NAPAJANJE, OUTPUT);
  digitalWrite(PIN_DCF_NAPAJANJE, HIGH);
#endif
  pinMode(PIN_DCF_SIGNAL, INPUT);
  dcfPrijemnik.Start();
  dcfPokrenut = true;
  vrijemePocetka = millis();
  zadnjaProvjeraMillis = 0;
}

bool jeDCFSpreman() {
  return dcfPokrenut && dcfStabiliziran();
}

DateTime dohvatiPosljednjeDCFVrijeme() {
  return zadnjeDCF;
}

void osvjeziDCFSinkronizaciju() {
  if (!dcfPokrenut) return;

  unsigned long sada = millis();
  if (zadnjaProvjeraMillis != 0 && (sada - zadnjaProvjeraMillis) < DCF_INTERVAL_PROVJERE_MS) {
    return;
  }
  zadnjaProvjeraMillis = sada;

  if (!dcfStabiliziran()) {
    return;
  }

  time_t primljeno = dcfPrijemnik.getTime();
  if (primljeno == 0) {
    return;
  }

  zadnjeDCF = DateTime((uint32_t)primljeno);

  if (dohvatiIzvorVremena() != "NTP" || jeSinkronizacijaZastarjela()) {
    azurirajVrijemeIzDCF(zadnjeDCF);
  }
}
