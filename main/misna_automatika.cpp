#include "misna_automatika.h"

#include <Arduino.h>
#include <RTClib.h>
#include <string.h>
#include "otkucavanje.h"
#include "postavke.h"
#include "time_glob.h"
#include "zvonjenje.h"
#include "pc_serial.h"

namespace {

constexpr unsigned long ODGODA_MISNE_PROVJERE_MS = 1000UL;

enum TipPlanaMise {
  PLAN_MISE_NISTA = 0,
  PLAN_MISE_RADNI_DAN = 1,
  PLAN_MISE_NEDJELJA = 2,
  PLAN_MISE_BLAGDAN = 3
};

struct ZakazanaMisnaNajava {
  bool aktivna;
  uint8_t tip;
  unsigned long startMs;
  unsigned long trajanjeMs;
};

static ZakazanaMisnaNajava zakazanaNajava = {false, PLAN_MISE_NISTA, 0, 0};
static uint32_t zadnjiObradeniKljucSekunde = 0xFFFFFFFFUL;

static bool vrijemeProslo(unsigned long ciljMs) {
  return static_cast<long>(millis() - ciljMs) >= 0;
}

static uint32_t napraviKljucSekunde(const DateTime& vrijeme) {
  const uint32_t datumKljuc = static_cast<uint32_t>((vrijeme.year() - 2000) * 512L +
                                                    vrijeme.month() * 32L +
                                                    vrijeme.day());
  return (datumKljuc * 86400UL) +
         static_cast<uint32_t>(vrijeme.hour() * 3600UL +
                               vrijeme.minute() * 60UL +
                               vrijeme.second());
}

static void opisiTipMise(uint8_t tip, char* odrediste, size_t velicina) {
  if (odrediste == nullptr || velicina == 0) {
    return;
  }

  if (tip == PLAN_MISE_RADNI_DAN) {
    strncpy(odrediste, "RADNI", velicina - 1);
  } else if (tip == PLAN_MISE_NEDJELJA) {
    strncpy(odrediste, "NEDJELJA", velicina - 1);
  } else if (tip == PLAN_MISE_BLAGDAN) {
    strncpy(odrediste, "BLAGDAN", velicina - 1);
  } else {
    strncpy(odrediste, "NISTA", velicina - 1);
  }
  odrediste[velicina - 1] = '\0';
}

static unsigned long dohvatiTrajanjeMisnogZvonjenjaMs(uint8_t tip) {
  return (tip == PLAN_MISE_RADNI_DAN)
      ? dohvatiTrajanjeZvonjenjaRadniMs()
      : dohvatiTrajanjeZvonjenjaNedjeljaMs();
}

static void aktivirajMisnoZvonjenje(uint8_t tip, unsigned long trajanjeMs) {
  aktivirajZvonjenjeNaTrajanje(1, trajanjeMs);
  if (tip != PLAN_MISE_RADNI_DAN) {
    aktivirajZvonjenjeNaTrajanje(2, trajanjeMs);
  }
}

static void zakaziESPMisnuNajavu(uint8_t tip, unsigned long trajanjeMs) {
  zakazanaNajava.aktivna = true;
  zakazanaNajava.tip = tip;
  zakazanaNajava.startMs = millis() + ODGODA_MISNE_PROVJERE_MS;
  zakazanaNajava.trajanjeMs = trajanjeMs;

  char izvor[12];
  opisiTipMise(tip, izvor, sizeof(izvor));
  char log[96];
  snprintf(log,
           sizeof(log),
           "ESP misa: %s odgodena zbog zauzetih zvona/cekica",
           izvor);
  posaljiPCLog(log);
}

static void obradiZakazanuNajavu() {
  if (!zakazanaNajava.aktivna) {
    return;
  }

  if (!vrijemeProslo(zakazanaNajava.startMs)) {
    return;
  }

  if (jeOtkucavanjeUTijeku() || jeZvonoUTijeku() || jeLiInerciaAktivna()) {
    zakazanaNajava.startMs = millis() + ODGODA_MISNE_PROVJERE_MS;
    return;
  }

  aktivirajMisnoZvonjenje(zakazanaNajava.tip, zakazanaNajava.trajanjeMs);

  char izvor[12];
  opisiTipMise(zakazanaNajava.tip, izvor, sizeof(izvor));
  char log[96];
  snprintf(log,
           sizeof(log),
           "ESP misa: %s pokrenuta nakon odgode",
           izvor);
  posaljiPCLog(log);

  zakazanaNajava.aktivna = false;
}

static bool pokreniESPMisnuNajavu(uint8_t tip) {
  if (zakazanaNajava.aktivna) {
    char izvor[12];
    opisiTipMise(tip, izvor, sizeof(izvor));
    char log[96];
    snprintf(log,
             sizeof(log),
             "ESP misa: %s ignorirana jer je druga misna najava vec na cekanju",
             izvor);
    posaljiPCLog(log);
    return false;
  }

  const unsigned long trajanjeMs = dohvatiTrajanjeMisnogZvonjenjaMs(tip);
  if (jeOtkucavanjeUTijeku() || jeZvonoUTijeku() || jeLiInerciaAktivna()) {
    zakaziESPMisnuNajavu(tip, trajanjeMs);
    return true;
  }

  aktivirajMisnoZvonjenje(tip, trajanjeMs);

  char izvor[12];
  opisiTipMise(tip, izvor, sizeof(izvor));
  char log[96];
  snprintf(log,
           sizeof(log),
           "ESP misa: %s pokrenuta odmah",
           izvor);
  posaljiPCLog(log);
  return true;
}

}  // namespace

void inicijalizirajMisnuAutomatiku() {
  zakazanaNajava = {false, PLAN_MISE_NISTA, 0, 0};
  zadnjiObradeniKljucSekunde = 0xFFFFFFFFUL;
}

void upravljajMisnomAutomatikom() {
  const DateTime sada = dohvatiTrenutnoVrijeme();
  if (sada.unixtime() == 0) {
    return;
  }

  const uint32_t kljucSekunde = napraviKljucSekunde(sada);
  if (kljucSekunde == zadnjiObradeniKljucSekunde) {
    return;
  }
  zadnjiObradeniKljucSekunde = kljucSekunde;

  obradiZakazanuNajavu();
}

bool pokreniESPMisnuNajavuRadniDan() {
  return pokreniESPMisnuNajavu(PLAN_MISE_RADNI_DAN);
}

bool pokreniESPMisnuNajavuNedjelja() {
  return pokreniESPMisnuNajavu(PLAN_MISE_NEDJELJA);
}

bool pokreniESPMisnuNajavuBlagdan() {
  return pokreniESPMisnuNajavu(PLAN_MISE_BLAGDAN);
}
