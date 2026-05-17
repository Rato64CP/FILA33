// pogrebne_skripte.cpp - Sekvence POKOJNIK / POKOJNICA za toranjski sat

#include <Arduino.h>
#include <avr/pgmspace.h>

#include "pogrebne_skripte.h"

#include "pc_serial.h"
#include "slavljenje_mrtvacko.h"
#include "time_glob.h"
#include "ups_nadzor.h"
#include "zvonjenje.h"
#include "otkucavanje.h"
#include "prekidac_tisine.h"

namespace {

constexpr unsigned long TRAJANJE_POCETNOG_ZVONA_MS = 2UL * 60UL * 1000UL;
constexpr uint8_t TRAJANJE_MRTVACKOG_MIN = 10U;
constexpr uint8_t MUSKO_ZVONO = 1;
constexpr uint8_t ZENSKO_ZVONO = 2;

enum FazaPogrebneSkripte : uint8_t {
  POGREBNA_SKRIPTA_NEAKTIVNA = 0,
  POGREBNA_SKRIPTA_ZVONO = 1,
  POGREBNA_SKRIPTA_CEKA_ZAVRSETAK = 2,
  POGREBNA_SKRIPTA_MRTVACKO = 3
};

struct StanjePogrebneSkripte {
  bool aktivna;
  uint8_t zvono;
  FazaPogrebneSkripte faza;
};

static StanjePogrebneSkripte stanje = {
    false, MUSKO_ZVONO, POGREBNA_SKRIPTA_NEAKTIVNA};

static const __FlashStringHelper* nazivSkripte(uint8_t zvono) {
  return (zvono == MUSKO_ZVONO) ? F("POKOJNIK") : F("POKOJNICA");
}

static bool jeSekvencaBlokirana() {
  return jeRtcIzlazniFailSafeAktivan() ||
         !jeVrijemePotvrdjenoZaAutomatiku() ||
         jePrekidacTisineAktivan() ||
         jeUPSModAktivan();
}

static bool jeToranjZauzetZaPogrebnuSekvencu() {
  return jeZvonoUTijeku() ||
         jeLiInerciaAktivna() ||
         jeOtkucavanjeUTijeku() ||
         jeSlavljenjeUTijeku() ||
         jeMrtvackoUTijeku();
}

static void logirajStanjeSkripte(const __FlashStringHelper* poruka) {
  char log[128];
  char naziv[16];
  char tekstPoruke[72];
  strncpy_P(naziv, reinterpret_cast<PGM_P>(nazivSkripte(stanje.zvono)), sizeof(naziv) - 1);
  naziv[sizeof(naziv) - 1] = '\0';
  strncpy_P(tekstPoruke, reinterpret_cast<PGM_P>(poruka), sizeof(tekstPoruke) - 1);
  tekstPoruke[sizeof(tekstPoruke) - 1] = '\0';
  snprintf_P(log,
             sizeof(log),
             PSTR("%s: %s"),
             naziv,
             tekstPoruke);
  posaljiPCLog(log);
}

static void zaustaviAktivnuSekvencuBezLoga() {
  if (jeZvonoAktivno(stanje.zvono)) {
    iskljuciZvono(stanje.zvono);
  }
  if (jeMrtvackoUTijeku()) {
    zaustaviMrtvacko();
  }
  stanje.aktivna = false;
  stanje.faza = POGREBNA_SKRIPTA_NEAKTIVNA;
}

static bool pokreniPogrebnuSekvencu(uint8_t zvono) {
  if (stanje.aktivna || (zvono != MUSKO_ZVONO && zvono != ZENSKO_ZVONO)) {
    return false;
  }

  if (jeSekvencaBlokirana() || jeToranjZauzetZaPogrebnuSekvencu()) {
    return false;
  }

  aktivirajZvonjenjeNaTrajanje(zvono, TRAJANJE_POCETNOG_ZVONA_MS);
  if (!jeZvonoAktivno(zvono)) {
    return false;
  }

  stanje.aktivna = true;
  stanje.zvono = zvono;
  stanje.faza = POGREBNA_SKRIPTA_ZVONO;
  logirajStanjeSkripte(F("pokrecem zvono 2 minute, zatim cekam inerciju i mrtvacko 10 minuta"));
  return true;
}

static bool prebaciPogrebnuSekvencu(uint8_t zvono) {
  if (zvono != MUSKO_ZVONO && zvono != ZENSKO_ZVONO) {
    return false;
  }

  if (!stanje.aktivna) {
    return pokreniPogrebnuSekvencu(zvono);
  }

  if (stanje.zvono != zvono) {
    logirajStanjeSkripte(F("odbijeno jer je druga pogrebna sekvenca vec aktivna"));
    return false;
  }

  logirajStanjeSkripte(F("rucno zaustavljeno ponovnim pritiskom tipke"));
  zaustaviAktivnuSekvencuBezLoga();
  return true;
}

}  // namespace

void inicijalizirajPogrebneSkripte() {
  stanje.aktivna = false;
  stanje.zvono = MUSKO_ZVONO;
  stanje.faza = POGREBNA_SKRIPTA_NEAKTIVNA;
}

void upravljajPogrebnimSkriptama(unsigned long sadaMs) {
  (void)sadaMs;

  if (!stanje.aktivna) {
    return;
  }

  if (jeSekvencaBlokirana()) {
    if (jeZvonoAktivno(stanje.zvono)) {
      iskljuciZvono(stanje.zvono);
    }
    if (jeMrtvackoUTijeku()) {
      zaustaviMrtvacko();
    }
    logirajStanjeSkripte(F("prekinuto zbog tihog rezima, gubitka vremena, RTC fail-safea ili UPS moda"));
    stanje.aktivna = false;
    stanje.faza = POGREBNA_SKRIPTA_NEAKTIVNA;
    return;
  }

  switch (stanje.faza) {
    case POGREBNA_SKRIPTA_ZVONO:
      if (!jeZvonoAktivno(stanje.zvono)) {
        stanje.faza = POGREBNA_SKRIPTA_CEKA_ZAVRSETAK;
        logirajStanjeSkripte(F("zvono je zavrsilo, cekam da se smiri inercija"));
      }
      break;

    case POGREBNA_SKRIPTA_CEKA_ZAVRSETAK:
      if (jeZvonoUTijeku() || jeLiInerciaAktivna()) {
        return;
      }

      if (jeOtkucavanjeUTijeku() || jeSlavljenjeUTijeku() || jeMrtvackoUTijeku()) {
        return;
      }

      if (pokusajZapocetiMrtvackoSaFiksnimTrajanjemBezCekanja(TRAJANJE_MRTVACKOG_MIN)) {
        stanje.faza = POGREBNA_SKRIPTA_MRTVACKO;
        logirajStanjeSkripte(F("pokrecem mrtvacko 10 minuta"));
      }
      break;

    case POGREBNA_SKRIPTA_MRTVACKO:
      if (!jeMrtvackoUTijeku()) {
        logirajStanjeSkripte(F("sekvenca zavrsena"));
        stanje.aktivna = false;
        stanje.faza = POGREBNA_SKRIPTA_NEAKTIVNA;
      }
      break;

    default:
      stanje.aktivna = false;
      stanje.faza = POGREBNA_SKRIPTA_NEAKTIVNA;
      break;
  }
}

bool pokusajPokrenutiPokojnika() {
  return pokreniPogrebnuSekvencu(MUSKO_ZVONO);
}

bool pokusajPokrenutiPokojnicu() {
  return pokreniPogrebnuSekvencu(ZENSKO_ZVONO);
}

void zaustaviPogrebneSkripte() {
  zaustaviAktivnuSekvencuBezLoga();
}

bool jePogrebnaSkriptaAktivna() {
  return stanje.aktivna;
}

uint8_t dohvatiTipAktivnePogrebneSkripte() {
  if (!stanje.aktivna) {
    return 0;
  }
  return stanje.zvono;
}

bool prebaciPokojnika() {
  return prebaciPogrebnuSekvencu(MUSKO_ZVONO);
}

bool prebaciPokojnicu() {
  return prebaciPogrebnuSekvencu(ZENSKO_ZVONO);
}
