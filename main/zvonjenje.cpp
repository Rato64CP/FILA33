#include <Arduino.h>
#include <avr/pgmspace.h>
#include <RTClib.h>
#include "zvonjenje.h"
#include "slavljenje_mrtvacko.h"
#include "podesavanja_piny.h"
#include "time_glob.h"
#include "lcd_display.h"
#include "postavke.h"
#include "pc_serial.h"
#include "debouncing.h"

// ==================== CONSTANTS ====================

// Trajanje inercije: 90 sekundi nakon rada zvona.
const unsigned long TRAJANJE_INERCIJE_MS = 90000UL;
static const uint8_t BROJ_ZVONA_MAX = 2;
static const uint8_t BROJ_RUCNIH_SKLOPKI_ZVONA = 2;

static const uint8_t PINOVI_ZVONA[BROJ_ZVONA_MAX] = {
  PIN_ZVONO_1,
  PIN_ZVONO_2
};

static const uint8_t PINOVI_LAMPICA_ZVONA[BROJ_ZVONA_MAX] = {
  PIN_LAMPICA_ZVONO_1,
  PIN_LAMPICA_ZVONO_2
};

static const uint8_t PINOVI_RUCNIH_SKLOPKI[BROJ_RUCNIH_SKLOPKI_ZVONA] = {
  PIN_BELL1_SWITCH,
  PIN_BELL2_SWITCH
};

// ==================== STATE TRACKING ====================

static struct {
  bool aktivan[BROJ_ZVONA_MAX];
  unsigned long start_ms[BROJ_ZVONA_MAX];
  unsigned long duration_ms[BROJ_ZVONA_MAX];
} zvona = {};

// Inercija nakon aktivacije zvona.
static struct {
  bool inercija_aktivna;
  unsigned long vrijeme_pocetka;
  unsigned long trajanje_ms;
} inercija = {false, 0, TRAJANJE_INERCIJE_MS};

// Rucno upravljanje fizickim sklopkama ima prioritet nad automatikom.
static struct {
  bool override_aktivan[BROJ_RUCNIH_SKLOPKI_ZVONA];
} manualnoUpravljanje = {{false, false}};

static bool dozvoliPaljenjeZvonaIzRucneSklopke = false;
static bool globalnaBlokadaZvona = false;

// ==================== RELAY CONTROL ====================

static bool jeValjanIndeksZvona(int indeks) {
  return indeks >= 0 && indeks < BROJ_ZVONA_MAX;
}

static bool jeZvonoOmogucenoPoPostavkama(int zvono) {
  return zvono >= 1 && zvono <= dohvatiBrojZvona();
}

static void prekiniPosebneNacineZbogZvona(int indeks) {
  bool prekinutoSlavljenje = false;
  bool prekinutoMrtvacko = false;

  if (jeSlavljenjeUTijeku()) {
    zaustaviSlavljenje();
    prekinutoSlavljenje = true;
  }

  if (jeMrtvackoUTijeku()) {
    zaustaviMrtvacko();
    prekinutoMrtvacko = true;
  }

  if (!prekinutoSlavljenje && !prekinutoMrtvacko) {
    return;
  }

  char log[88];
  snprintf_P(log, sizeof(log), PSTR("Zvono%d: ima prioritet i prekida %s"),
             indeks + 1,
             (prekinutoSlavljenje && prekinutoMrtvacko) ? "slavljenje i mrtvacko"
             : (prekinutoSlavljenje ? "slavljenje" : "mrtvacko"));
  posaljiPCLog(log);
}

static void aktivirajBell_Relej(int indeks) {
  if (!jeValjanIndeksZvona(indeks)) {
    return;
  }

  // Zvona imaju prioritet nad posebnim nacinima cekica toranjskog sata.
  prekiniPosebneNacineZbogZvona(indeks);
  digitalWrite(PINOVI_ZVONA[indeks], HIGH);
  digitalWrite(PINOVI_LAMPICA_ZVONA[indeks], HIGH);
  inercija.inercija_aktivna = true;
  inercija.vrijeme_pocetka = millis();

  char log[56];
  snprintf_P(log, sizeof(log), PSTR("Zvono%d: aktivirana, inercija (90s) poceta"), indeks + 1);
  posaljiPCLog(log);
  signalizirajZvono_Ringing(indeks + 1);
}

static void deaktivirajBell_Relej(int indeks) {
  if (!jeValjanIndeksZvona(indeks)) {
    return;
  }

  digitalWrite(PINOVI_ZVONA[indeks], LOW);
  digitalWrite(PINOVI_LAMPICA_ZVONA[indeks], LOW);
  char log[32];
  snprintf_P(log, sizeof(log), PSTR("Zvono%d: deaktivirana"), indeks + 1);
  posaljiPCLog(log);
}

static void ukljuciZvonoIzRucneSklopke(int zvono) {
  dozvoliPaljenjeZvonaIzRucneSklopke = true;
  ukljuciZvono(zvono);
  dozvoliPaljenjeZvonaIzRucneSklopke = false;
}

// ==================== PUBLIC API ====================

void ukljuciZvono(int zvono) {
  const int indeks = zvono - 1;
  if (!jeValjanIndeksZvona(indeks) || !jeZvonoOmogucenoPoPostavkama(zvono)) {
    return;
  }

  if (globalnaBlokadaZvona) {
    return;
  }

  if (!jeVrijemePotvrdjenoZaAutomatiku() && !dozvoliPaljenjeZvonaIzRucneSklopke) {
    posaljiPCLog(F("Zvona: automatsko ili daljinsko paljenje blokirano dok vrijeme nije potvrdeno"));
    return;
  }

  if (!zvona.aktivan[indeks]) {
    aktivirajBell_Relej(indeks);
    zvona.aktivan[indeks] = true;
    // Rucno i daljinsko paljenje preko weba/API-ja traje dok ne stigne
    // eksplicitno gasenje. Samo putanja `aktivirajZvonjenjeNaTrajanje()`
    // postavlja vremenski ogranicen rad.
    zvona.start_ms[indeks] = millis();
    zvona.duration_ms[indeks] = 0;
  }
}

void iskljuciZvono(int zvono) {
  const int indeks = zvono - 1;
  if (!jeValjanIndeksZvona(indeks)) {
    return;
  }

  if (zvona.aktivan[indeks]) {
    deaktivirajBell_Relej(indeks);
    zvona.aktivan[indeks] = false;
    zvona.start_ms[indeks] = 0;
    zvona.duration_ms[indeks] = 0;
    inercija.inercija_aktivna = true;
    inercija.vrijeme_pocetka = millis();
  }
}

bool jeLiInerciaAktivna() {
  if (!inercija.inercija_aktivna) {
    return false;
  }

  unsigned long sadaMs = millis();
  unsigned long proteklo = sadaMs - inercija.vrijeme_pocetka;

  if (proteklo >= inercija.trajanje_ms) {
    inercija.inercija_aktivna = false;
    posaljiPCLog(F("Inercija: zavrsena nakon 90s"));
    return false;
  }

  return true;
}

bool jeZvonoUTijeku() {
  for (uint8_t i = 0; i < BROJ_ZVONA_MAX; i++) {
    if (zvona.aktivan[i]) {
      return true;
    }
  }
  return false;
}

bool jeZvonoAktivno(int zvono) {
  const int indeks = zvono - 1;
  if (!jeValjanIndeksZvona(indeks)) {
    return false;
  }
  return zvona.aktivan[indeks];
}

void aktivirajZvonjenje(int zvono) {
  ukljuciZvono(zvono);
}

void aktivirajZvonjenjeNaTrajanje(int zvono, unsigned long trajanjeMs) {
  const unsigned long sadaMs = millis();
  aktivirajZvonjenje(zvono);
  const int indeks = zvono - 1;
  if (jeValjanIndeksZvona(indeks)) {
    zvona.start_ms[indeks] = sadaMs;
    zvona.duration_ms[indeks] = trajanjeMs;
  }
}

void deaktivirajZvonjenje(int zvono) {
  iskljuciZvono(zvono);
}

void postaviGlobalnuBlokaduZvona(bool blokiraj) {
  if (globalnaBlokadaZvona == blokiraj) {
    return;
  }

  globalnaBlokadaZvona = blokiraj;

  if (globalnaBlokadaZvona) {
    for (uint8_t i = 0; i < BROJ_ZVONA_MAX; ++i) {
      if (!zvona.aktivan[i]) {
        continue;
      }

      deaktivirajBell_Relej(i);
      zvona.aktivan[i] = false;
      zvona.start_ms[i] = 0UL;
      zvona.duration_ms[i] = 0UL;
    }
    posaljiPCLog(F("Globalna blokada zvona: UKLJUCENA"));
  } else {
    posaljiPCLog(F("Globalna blokada zvona: ISKLJUCENA"));
  }
}

// ==================== INITIALIZATION ====================

void inicijalizirajZvona() {
  for (uint8_t i = 0; i < BROJ_ZVONA_MAX; i++) {
    pinMode(PINOVI_ZVONA[i], OUTPUT);
    digitalWrite(PINOVI_ZVONA[i], LOW);
    pinMode(PINOVI_LAMPICA_ZVONA[i], OUTPUT);
    digitalWrite(PINOVI_LAMPICA_ZVONA[i], LOW);
    zvona.aktivan[i] = false;
    zvona.start_ms[i] = 0;
    zvona.duration_ms[i] = 0;
  }

  pinMode(PIN_ULAZA_PLOCE_1, INPUT_PULLUP);
  pinMode(PIN_ULAZA_PLOCE_2, INPUT_PULLUP);
  pinMode(PIN_ULAZA_PLOCE_3, INPUT_PULLUP);
  pinMode(PIN_ULAZA_PLOCE_4, INPUT_PULLUP);
  pinMode(PIN_ULAZA_PLOCE_5, INPUT_PULLUP);

  for (uint8_t i = 0; i < BROJ_RUCNIH_SKLOPKI_ZVONA; i++) {
    pinMode(PINOVI_RUCNIH_SKLOPKI[i], INPUT_PULLUP);
    manualnoUpravljanje.override_aktivan[i] = false;
  }

  inercija.inercija_aktivna = false;
  posaljiPCLog(F("Zvona: inicijalizirana za 2 zvona toranjskog sata"));
}

// ==================== MAIN LOOP MANAGEMENT ====================

void upravljajZvonom() {
  unsigned long sadaMs = millis();
  SwitchState novoStanje = SWITCH_RELEASED;

  for (uint8_t i = 0; i < BROJ_RUCNIH_SKLOPKI_ZVONA; i++) {
    if (obradiDebouncedInput(PINOVI_RUCNIH_SKLOPKI[i], 30, &novoStanje)) {
      char log[80];
      snprintf_P(log, sizeof(log), PSTR("Rucno ZVONO%d %s"), i + 1,
                 (novoStanje == SWITCH_PRESSED) ? "ON" : "OFF");

      if (novoStanje == SWITCH_PRESSED) {
        if (jeZvonoOmogucenoPoPostavkama(i + 1)) {
          manualnoUpravljanje.override_aktivan[i] = true;
          if (!globalnaBlokadaZvona) {
            ukljuciZvonoIzRucneSklopke(i + 1);
          } else {
            snprintf_P(log, sizeof(log), PSTR("Rucno ZVONO%d ON (blokirano globalnom tisinom)"), i + 1);
          }
        } else {
          manualnoUpravljanje.override_aktivan[i] = false;
          snprintf_P(log, sizeof(log), PSTR("Rucno ZVONO%d ON (preskoceno)"), i + 1);
        }
      } else {
        manualnoUpravljanje.override_aktivan[i] = false;
        iskljuciZvono(i + 1);
      }
      posaljiPCLog(log);
    }
  }

  if (globalnaBlokadaZvona) {
    for (uint8_t i = 0; i < BROJ_ZVONA_MAX; ++i) {
      if (zvona.aktivan[i]) {
        deaktivirajBell_Relej(i);
        zvona.aktivan[i] = false;
        zvona.start_ms[i] = 0UL;
        zvona.duration_ms[i] = 0UL;
      }
    }

    jeLiInerciaAktivna();
    return;
  }

  // Ako je rucni override aktivan, ima prioritet nad web/API automatikom.
  for (uint8_t i = 0; i < BROJ_RUCNIH_SKLOPKI_ZVONA; i++) {
    if (manualnoUpravljanje.override_aktivan[i] && !zvona.aktivan[i]) {
      ukljuciZvonoIzRucneSklopke(i + 1);
    }
  }

  jeLiInerciaAktivna();

  for (uint8_t i = 0; i < BROJ_ZVONA_MAX; i++) {
    const bool rucniOverride =
        (i < BROJ_RUCNIH_SKLOPKI_ZVONA) ? manualnoUpravljanje.override_aktivan[i] : false;
    if (zvona.aktivan[i] && !rucniOverride && zvona.duration_ms[i] > 0) {
      const unsigned long proteklo = sadaMs - zvona.start_ms[i];
      if (proteklo >= zvona.duration_ms[i]) {
        iskljuciZvono(i + 1);
        char log[40];
        snprintf_P(log, sizeof(log), PSTR("Zvono%d: trajanje isteklo"), i + 1);
        posaljiPCLog(log);
      }
    }
  }
}
