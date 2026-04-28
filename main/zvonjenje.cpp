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

static const uint8_t BROJ_ZVONA_MAX = 2;
static const uint8_t BROJ_RUCNIH_SKLOPKI_ZVONA = 2;
static const unsigned long INTERVAL_TREPTANJA_LAMPICE_INERCIJE_MS = 500UL;

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
  bool aktivna[BROJ_ZVONA_MAX];
  unsigned long vrijeme_pocetka[BROJ_ZVONA_MAX];
  unsigned long trajanje_ms[BROJ_ZVONA_MAX];
} inercija = {};

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

static unsigned long dohvatiTrajanjeInercijeZvonaMs(int indeks) {
  if (indeks == 0) {
    return static_cast<unsigned long>(dohvatiInercijuZvona1Sekunde()) * 1000UL;
  }
  if (indeks == 1) {
    return static_cast<unsigned long>(dohvatiInercijuZvona2Sekunde()) * 1000UL;
  }
  return 0UL;
}

static void osvjeziLampicuZvona(int indeks, unsigned long sadaMs) {
  if (!jeValjanIndeksZvona(indeks)) {
    return;
  }

  bool lampicaUkljucena = false;
  if (zvona.aktivan[indeks]) {
    lampicaUkljucena = true;
  } else if (inercija.aktivna[indeks]) {
    lampicaUkljucena =
        ((sadaMs / INTERVAL_TREPTANJA_LAMPICE_INERCIJE_MS) % 2UL) == 0UL;
  }

  digitalWrite(PINOVI_LAMPICA_ZVONA[indeks], lampicaUkljucena ? HIGH : LOW);
}

static void osvjeziLampiceZvona(unsigned long sadaMs) {
  for (uint8_t i = 0; i < BROJ_ZVONA_MAX; ++i) {
    osvjeziLampicuZvona(i, sadaMs);
  }
}

static void pokreniInercijuZvona(int indeks) {
  if (!jeValjanIndeksZvona(indeks)) {
    return;
  }

  inercija.aktivna[indeks] = true;
  inercija.vrijeme_pocetka[indeks] = millis();
  inercija.trajanje_ms[indeks] = dohvatiTrajanjeInercijeZvonaMs(indeks);
}

static void zaustaviInercijuZvona(int indeks) {
  if (!jeValjanIndeksZvona(indeks)) {
    return;
  }

  inercija.aktivna[indeks] = false;
  inercija.vrijeme_pocetka[indeks] = 0UL;
  inercija.trajanje_ms[indeks] = 0UL;
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
  zaustaviInercijuZvona(indeks);
  digitalWrite(PINOVI_ZVONA[indeks], HIGH);
  osvjeziLampicuZvona(indeks, millis());

  char log[40];
  snprintf_P(log,
             sizeof(log),
             PSTR("Zvono%d: aktivirana"),
             indeks + 1);
  posaljiPCLog(log);
  signalizirajZvono_Ringing(indeks + 1);
}

static void deaktivirajBell_Relej(int indeks) {
  if (!jeValjanIndeksZvona(indeks)) {
    return;
  }

  digitalWrite(PINOVI_ZVONA[indeks], LOW);
  osvjeziLampicuZvona(indeks, millis());
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
    osvjeziLampicuZvona(indeks, zvona.start_ms[indeks]);
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
    pokreniInercijuZvona(indeks);
    osvjeziLampicuZvona(indeks, millis());

    char log[56];
    snprintf_P(log,
               sizeof(log),
               PSTR("Zvono%d: inercija (%us) poceta"),
               indeks + 1,
               static_cast<unsigned>((inercija.trajanje_ms[indeks] + 500UL) / 1000UL));
    posaljiPCLog(log);
  }
}

bool jeLiInerciaAktivna() {
  bool baremJednaAktivna = false;
  const unsigned long sadaMs = millis();

  for (uint8_t i = 0; i < BROJ_ZVONA_MAX; ++i) {
    if (!inercija.aktivna[i]) {
      continue;
    }

    const unsigned long proteklo = sadaMs - inercija.vrijeme_pocetka[i];
    if (proteklo >= inercija.trajanje_ms[i]) {
      inercija.aktivna[i] = false;
      osvjeziLampicuZvona(i, sadaMs);
      char log[48];
      snprintf_P(log,
                 sizeof(log),
                 PSTR("Inercija Z%d: zavrsena nakon %us"),
                 i + 1,
                 static_cast<unsigned>((inercija.trajanje_ms[i] + 500UL) / 1000UL));
      posaljiPCLog(log);
      continue;
    }

    baremJednaAktivna = true;
  }

  osvjeziLampiceZvona(sadaMs);
  return baremJednaAktivna;
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
    osvjeziLampiceZvona(millis());
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

  for (uint8_t i = 0; i < BROJ_ZVONA_MAX; ++i) {
    inercija.aktivna[i] = false;
    inercija.vrijeme_pocetka[i] = 0UL;
    inercija.trajanje_ms[i] = 0UL;
  }
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
  osvjeziLampiceZvona(sadaMs);

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
