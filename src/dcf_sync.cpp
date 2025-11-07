// dcf_sync.cpp
#include <Arduino.h>
#include <DCF77.h>
#include "dcf_sync.h"
#include "podesavanja_piny.h"
#include "time_glob.h"
#include "vrijeme_izvor.h"

static const unsigned long DCF_INTERVAL_PROVJERE_MS = 60000; // cekanje izmedu pokusaja
static const unsigned long DCF_VRIJEME_STABILIZACIJE_MS = 60000; // vrijeme stabilizacije signala
static const uint8_t DCF_SAT_NOC_OD = 22; // nocni prozor pocinje u 22 h
static const uint8_t DCF_SAT_NOC_DO = 6;  // nocni prozor traje do 6 h

// DCF77 knjižnica koristi prekid nad jednim digitalnim pinom (otvoreni kolektor antene)
static DCF77 dcfPrijemnik(PIN_DCF_SIGNAL, digitalPinToInterrupt(PIN_DCF_SIGNAL));
static bool dcfPokrenut = false;
static unsigned long zadnjaProvjeraMillis = 0;
static unsigned long vrijemePocetka = 0;
static DateTime zadnjeDCF = DateTime((uint32_t)0);

static bool jeNocniDCFInterval() {
  DateTime sada = dohvatiTrenutnoVrijeme(); // koristimo RTC iz time_glob modula
  uint8_t sat = sada.hour();
  if (DCF_SAT_NOC_OD < DCF_SAT_NOC_DO) {
    return sat >= DCF_SAT_NOC_OD && sat < DCF_SAT_NOC_DO;
  }
  return sat >= DCF_SAT_NOC_OD || sat < DCF_SAT_NOC_DO;
}

static bool dcfStabiliziran() {
  return millis() - vrijemePocetka > DCF_VRIJEME_STABILIZACIJE_MS;
}

void inicijalizirajDCF() {
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

  if (!jeNocniDCFInterval()) {
    zadnjaProvjeraMillis = 0; // resetiraj provjeru kako bi noc krenula bez dodatnog cekanja
    return;
  }

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
