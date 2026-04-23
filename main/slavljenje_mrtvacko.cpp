// slavljenje_mrtvacko.cpp - Slavljenje i mrtvacko kao posebni nacini rada cekica

#include <Arduino.h>
#include <avr/pgmspace.h>

#include "slavljenje_mrtvacko.h"

#include "otkucavanje_interno.h"
#include "podesavanja_piny.h"
#include "postavke.h"
#include "lcd_display.h"
#include "pc_serial.h"
#include "debouncing.h"
#include "mrtvacko_thumbwheel.h"

namespace {

// Slavljenje mod 1: tocan uzorak 1-2-2 s definiranim pauzama.
const unsigned long SLAVLJENJE_PAUZA_NAKON_CEKIC1_MS = 300UL;
const unsigned long SLAVLJENJE_PAUZA_NAKON_CEKIC2_MS = 150UL;

// Slavljenje mod 2: C1 110 ms -> 90 ms pauza -> C2 110 ms -> 190 ms pauza.
const unsigned long SLAVLJENJE_MOD2_TRAJANJE_IMPULSA_MS = 110UL;
const unsigned long SLAVLJENJE_MOD2_PAUZA_NAKON_CEKIC1_MS = 90UL;
const unsigned long SLAVLJENJE_MOD2_PAUZA_NAKON_CEKIC2_MS = 190UL;

// Mrtvacko: oba cekica s konfiguriranim impulsom, zatim 10 s pauza.
const unsigned long MRTVACKO_PAUZA_MS = 10000UL;
const unsigned long MRTVACKO_MOD2_TRAJANJE_IMPULSA_MS = 300UL;
const unsigned long MRTVACKO_MOD2_PAUZA_NAKON_CEKIC1_MS = 700UL;
const unsigned long MRTVACKO_MOD2_PAUZA_NAKON_CEKIC2_MS = 3700UL;
const unsigned long TREPTANJE_LAMPICE_CEKANJA_MS = 500UL;

enum ModMrtvackog {
  MOD_MRTVACKO_KLASICNO = 1,
  MOD_MRTVACKO_SEKVENCIJALNO = 2
};

enum FazaMrtvackog {
  MRTVACKO_FAZA_OBA_CEKICA = 0,
  MRTVACKO_FAZA_CEKIC1 = 1,
  MRTVACKO_FAZA_PAUZA_NAKON_CEKIC1 = 2,
  MRTVACKO_FAZA_CEKIC2 = 3,
  MRTVACKO_FAZA_PAUZA = 4
};

struct SlavljenjeStanje {
  bool aktivno;
  unsigned long vrijemePocetkaMs;
  int trenutniKorak;
  uint8_t aktivniMod;
  unsigned long vrijemeKorakaMs;
  bool cekicAktivan;
  int aktivniPin;
};

struct MrtvackoStanje {
  bool aktivno;
  unsigned long vrijemePocetkaMs;
  bool cekiciAktivni;
  unsigned long vrijemeFazeMs;
  uint8_t aktivniMod;
  uint8_t faza;
  bool autoStopUkljucen;
  unsigned long autoStopNakonMs;
  uint8_t zadanoTrajanjeMin;
  bool ignorirajThumbwheel;
};

SlavljenjeStanje slavljenje = {false, 0UL, 0, 1, 0UL, false, -1};
MrtvackoStanje mrtvacko = {
    false, 0UL, false, 0UL, MOD_MRTVACKO_KLASICNO, MRTVACKO_FAZA_OBA_CEKICA, false, 0UL, 0, false};
bool slavljenjeNaCekanju = false;
bool mrtvackoNaCekanju = false;

void osvjeziLampicePosebnihNacina(unsigned long sadaMs) {
  const bool treptajUkljucen = ((sadaMs / TREPTANJE_LAMPICE_CEKANJA_MS) % 2UL) == 0UL;

  digitalWrite(PIN_LAMPICA_SLAVLJENJE,
               slavljenje.aktivno ? HIGH : (slavljenjeNaCekanju && treptajUkljucen ? HIGH : LOW));
  digitalWrite(PIN_LAMPICA_MRTVACKO,
               mrtvacko.aktivno ? HIGH : (mrtvackoNaCekanju && treptajUkljucen ? HIGH : LOW));
}

void posaljiLogPromjeneMrtvackogTimer(const MrtvackoStanje& stanje) {
  char log[96];
  snprintf_P(log, sizeof(log),
             PSTR("Mrtvacko: promjena thumbwheel timera na %u min, %s"),
             stanje.zadanoTrajanjeMin,
             stanje.autoStopUkljucen ? "brojim od nove vrijednosti" : "auto-stop iskljucen");
  posaljiPCLog(log);
}

void posaljiLogStartaSlavljenja(uint8_t modSlavljenja) {
  char log[96];
  if (modSlavljenja == 2) {
    snprintf_P(log, sizeof(log),
               PSTR("Slavljenje: pokrenuto (mod %u, C1 110ms -> 90ms -> C2 110ms -> 190ms)"),
               modSlavljenja);
  } else {
    snprintf_P(log, sizeof(log),
               PSTR("Slavljenje: pokrenuto (mod %u, uzorak C1-C2-C2)"),
               modSlavljenja);
  }
  posaljiPCLog(log);
}

void posaljiLogStartaMrtvackog(uint8_t modMrtvackog,
                               unsigned long trajanjeImpulsaMs,
                               bool autoStopUkljucen,
                               uint8_t zadanoTrajanjeMin) {
  char log[160];
  if (modMrtvackog == MOD_MRTVACKO_SEKVENCIJALNO) {
    snprintf_P(log, sizeof(log),
               PSTR("Mrtvacko: pokrenuto (mod %u, C1 300ms -> 700ms -> C2 300ms -> 3700ms, %s)"),
               modMrtvackog,
               autoStopUkljucen ? "auto-stop aktivan" : "radi stalno do rucnog gasenja");
  } else if (autoStopUkljucen) {
    snprintf_P(log, sizeof(log),
               PSTR("Mrtvacko: pokrenuto (mod %u, oba cekica %lums / pauza 10s, auto-stop nakon %u min)"),
               modMrtvackog,
               trajanjeImpulsaMs,
               zadanoTrajanjeMin);
  } else {
    snprintf_P(log, sizeof(log),
               PSTR("Mrtvacko: pokrenuto (mod %u, oba cekica %lums / pauza 10s, radi stalno do rucnog gasenja)"),
               modMrtvackog,
               trajanjeImpulsaMs);
  }
  posaljiPCLog(log);
}

void primijeniTrajanjeMrtvackogIzThumbwheela(unsigned long sadaMs, bool priPokretanju) {
  const uint8_t novoTrajanjeMin = dohvatiMrtvackoThumbwheelVrijednost();
  if (!priPokretanju && novoTrajanjeMin == mrtvacko.zadanoTrajanjeMin) {
    return;
  }

  mrtvacko.zadanoTrajanjeMin = novoTrajanjeMin;
  mrtvacko.autoStopUkljucen = (novoTrajanjeMin > 0);
  mrtvacko.autoStopNakonMs =
      mrtvacko.autoStopUkljucen ? static_cast<unsigned long>(novoTrajanjeMin) * 60000UL : 0UL;
  mrtvacko.vrijemePocetkaMs = sadaMs;

  if (priPokretanju) {
    return;
  }

  posaljiLogPromjeneMrtvackogTimer(mrtvacko);
}

int dohvatiPinSlavljenjaZaKorak(uint8_t mod, int korak) {
  if (mod == 2) {
    return (korak == 0) ? PIN_CEKIC_MUSKI : PIN_CEKIC_ZENSKI;
  }

  return (korak == 0) ? PIN_CEKIC_MUSKI : PIN_CEKIC_ZENSKI;
}

int dohvatiBrojKorakaSlavljenja(uint8_t mod) {
  return (mod == 2) ? 2 : 3;
}

unsigned long dohvatiPauzuSlavljenjaNakonKoraka(uint8_t mod, int korak) {
  if (mod == 2) {
    return (korak == 0) ? SLAVLJENJE_MOD2_PAUZA_NAKON_CEKIC1_MS
                        : SLAVLJENJE_MOD2_PAUZA_NAKON_CEKIC2_MS;
  }

  return (korak == 0) ? SLAVLJENJE_PAUZA_NAKON_CEKIC1_MS : SLAVLJENJE_PAUZA_NAKON_CEKIC2_MS;
}

unsigned long dohvatiTrajanjeImpulsaSlavljenjaZaMod(uint8_t mod) {
  return (mod == 2) ? SLAVLJENJE_MOD2_TRAJANJE_IMPULSA_MS
                    : dohvatiTrajanjeImpulsaCekicaZaPosebneNacineMs();
}

unsigned long dohvatiTrajanjeImpulsaMrtvackogZaFazu(uint8_t mod, uint8_t faza) {
  if (mod == MOD_MRTVACKO_SEKVENCIJALNO &&
      (faza == MRTVACKO_FAZA_CEKIC1 || faza == MRTVACKO_FAZA_CEKIC2)) {
    return MRTVACKO_MOD2_TRAJANJE_IMPULSA_MS;
  }

  return dohvatiTrajanjeImpulsaCekicaZaPosebneNacineMs();
}

unsigned long dohvatiPauzuMrtvackogZaFazu(uint8_t mod, uint8_t faza) {
  if (mod == MOD_MRTVACKO_SEKVENCIJALNO) {
    return (faza == MRTVACKO_FAZA_PAUZA_NAKON_CEKIC1)
               ? MRTVACKO_MOD2_PAUZA_NAKON_CEKIC1_MS
               : MRTVACKO_MOD2_PAUZA_NAKON_CEKIC2_MS;
  }

  (void)faza;
  return MRTVACKO_PAUZA_MS;
}

void uskladiStanjeNakonSigurnosnogLimita(unsigned long sadaMs) {
  if (slavljenje.cekicAktivan &&
      !jeCekicSigurnosnoAktivanZaPosebniNacin(slavljenje.aktivniPin)) {
    slavljenje.cekicAktivan = false;
    slavljenje.aktivniPin = -1;
    slavljenje.vrijemeKorakaMs = sadaMs;
  }

  if (mrtvacko.cekiciAktivni &&
      !jeCekicSigurnosnoAktivanZaPosebniNacin(PIN_CEKIC_MUSKI) &&
      !jeCekicSigurnosnoAktivanZaPosebniNacin(PIN_CEKIC_ZENSKI)) {
    mrtvacko.cekiciAktivni = false;
    mrtvacko.vrijemeFazeMs = sadaMs;
    if (mrtvacko.aktivniMod == MOD_MRTVACKO_SEKVENCIJALNO &&
        mrtvacko.faza == MRTVACKO_FAZA_CEKIC1) {
      mrtvacko.faza = MRTVACKO_FAZA_PAUZA_NAKON_CEKIC1;
    } else {
      mrtvacko.faza = MRTVACKO_FAZA_PAUZA;
    }
  }
}

void azurirajSlavljenje(unsigned long sadaMs) {
  if (!slavljenje.aktivno) {
    return;
  }

  const unsigned long proteklo = sadaMs - slavljenje.vrijemeKorakaMs;
  const unsigned long trazenaPauza =
      dohvatiPauzuSlavljenjaNakonKoraka(slavljenje.aktivniMod, slavljenje.trenutniKorak);
  const unsigned long trajanjeImpulsa =
      dohvatiTrajanjeImpulsaSlavljenjaZaMod(slavljenje.aktivniMod);

  if (slavljenje.cekicAktivan) {
    if (proteklo >= trajanjeImpulsa) {
      deaktivirajObaCekicaZaPosebniNacin();
      slavljenje.cekicAktivan = false;
      slavljenje.aktivniPin = -1;
      slavljenje.vrijemeKorakaMs = sadaMs;
    }
    return;
  }

  if (proteklo >= trazenaPauza) {
    const int brojKoraka = dohvatiBrojKorakaSlavljenja(slavljenje.aktivniMod);
    slavljenje.trenutniKorak = (slavljenje.trenutniKorak + 1) % brojKoraka;
    const int sljedeciPin =
        dohvatiPinSlavljenjaZaKorak(slavljenje.aktivniMod, slavljenje.trenutniKorak);
    aktivirajJedanCekicZaPosebniNacin(sljedeciPin, trajanjeImpulsa);
    slavljenje.aktivniPin = sljedeciPin;
    slavljenje.cekicAktivan = true;
    slavljenje.vrijemeKorakaMs = sadaMs;
  }
}

void azurirajMrtvacko(unsigned long sadaMs) {
  if (!mrtvacko.aktivno) {
    return;
  }

  // Ako se timer na thumbwheelu promijeni tijekom rucnog rada, nova vrijednost
  // odmah postaje autoritet. Automatsko mrtvacko za Svi sveti ga namjerno ignorira.
  if (!mrtvacko.ignorirajThumbwheel) {
    primijeniTrajanjeMrtvackogIzThumbwheela(sadaMs, false);
  }

  if (mrtvacko.autoStopUkljucen) {
    const unsigned long protekloOdPocetka = sadaMs - mrtvacko.vrijemePocetkaMs;
    if (protekloOdPocetka >= mrtvacko.autoStopNakonMs) {
      char log[48];
      snprintf_P(log, sizeof(log), PSTR("Mrtvacko: auto-stop nakon %u min"), mrtvacko.zadanoTrajanjeMin);
      posaljiPCLog(log);
      zaustaviMrtvacko();
      return;
    }
  }

  const unsigned long proteklo = sadaMs - mrtvacko.vrijemeFazeMs;
  const unsigned long trajanjeImpulsa =
      dohvatiTrajanjeImpulsaMrtvackogZaFazu(mrtvacko.aktivniMod, mrtvacko.faza);

  if (mrtvacko.cekiciAktivni) {
    if (proteklo >= trajanjeImpulsa) {
      deaktivirajObaCekicaZaPosebniNacin();
      mrtvacko.cekiciAktivni = false;
      mrtvacko.vrijemeFazeMs = sadaMs;
      if (mrtvacko.aktivniMod == MOD_MRTVACKO_SEKVENCIJALNO &&
          mrtvacko.faza == MRTVACKO_FAZA_CEKIC1) {
        mrtvacko.faza = MRTVACKO_FAZA_PAUZA_NAKON_CEKIC1;
      } else {
        mrtvacko.faza = MRTVACKO_FAZA_PAUZA;
      }
    }
    return;
  }

  if (mrtvacko.aktivniMod == MOD_MRTVACKO_SEKVENCIJALNO &&
      mrtvacko.faza == MRTVACKO_FAZA_PAUZA_NAKON_CEKIC1) {
    if (proteklo >= dohvatiPauzuMrtvackogZaFazu(mrtvacko.aktivniMod, mrtvacko.faza)) {
      aktivirajJedanCekicZaPosebniNacin(PIN_CEKIC_ZENSKI, MRTVACKO_MOD2_TRAJANJE_IMPULSA_MS);
      mrtvacko.cekiciAktivni = true;
      mrtvacko.faza = MRTVACKO_FAZA_CEKIC2;
      mrtvacko.vrijemeFazeMs = sadaMs;
    }
    return;
  }

  if (proteklo >= dohvatiPauzuMrtvackogZaFazu(mrtvacko.aktivniMod, mrtvacko.faza)) {
    if (mrtvacko.aktivniMod == MOD_MRTVACKO_SEKVENCIJALNO) {
      aktivirajJedanCekicZaPosebniNacin(PIN_CEKIC_MUSKI, MRTVACKO_MOD2_TRAJANJE_IMPULSA_MS);
      mrtvacko.faza = MRTVACKO_FAZA_CEKIC1;
    } else {
      aktivirajObaCekicaZaPosebniNacin(dohvatiTrajanjeImpulsaCekicaZaPosebneNacineMs());
      mrtvacko.faza = MRTVACKO_FAZA_OBA_CEKICA;
    }
    mrtvacko.cekiciAktivni = true;
    mrtvacko.vrijemeFazeMs = sadaMs;
  }
}

void provjeriPrekidacSlavljenja() {
  SwitchState novoStanje = SWITCH_RELEASED;
  if (!obradiDebouncedInput(PIN_KEY_CELEBRATION, 30, &novoStanje)) {
    return;
  }

  if (novoStanje == SWITCH_PRESSED) {
    if (!slavljenje.aktivno) {
      if (jeOperacijaCekicaDozvoljena() && !mrtvacko.aktivno) {
        slavljenjeNaCekanju = false;
        zapocniSlavljenje();
        posaljiPCLog(F("Prekidac slavljenja: ukljuceno"));
      } else {
        slavljenjeNaCekanju = true;
        posaljiPCLog(F("Prekidac slavljenja: zahtjev na cekanju do kraja inercije/blokade"));
      }
    }
  } else {
    slavljenjeNaCekanju = false;
    if (slavljenje.aktivno) {
      zaustaviSlavljenje();
      posaljiPCLog(F("Prekidac slavljenja: iskljuceno"));
    }
  }
}

void provjeriDugmeMrtvackog() {
  SwitchState novoStanje = SWITCH_RELEASED;
  if (!obradiDebouncedInput(PIN_KEY_FUNERAL, 30, &novoStanje)) {
    return;
  }

  if (novoStanje == SWITCH_PRESSED) {
    if (mrtvacko.aktivno) {
      mrtvackoNaCekanju = false;
      zaustaviMrtvacko();
      posaljiPCLog(F("Dugme: mrtvacko zaustavljeno"));
    } else if (mrtvackoNaCekanju) {
      mrtvackoNaCekanju = false;
      posaljiPCLog(F("Dugme: mrtvacko skinuto s cekanja"));
    } else {
      if (jeOperacijaCekicaDozvoljena() && !slavljenje.aktivno) {
        mrtvackoNaCekanju = false;
        zapocniMrtvacko();
        posaljiPCLog(F("Dugme: mrtvacko pokrenuto"));
      } else {
        mrtvackoNaCekanju = true;
        posaljiPCLog(F("Dugme: mrtvacko na cekanju do kraja inercije/blokade"));
      }
    }
  }
}

void obradiCekanjePosebnihNacina() {
  if (slavljenjeNaCekanju &&
      !slavljenje.aktivno &&
      !mrtvacko.aktivno &&
      digitalRead(PIN_KEY_CELEBRATION) == LOW &&
      jeOperacijaCekicaDozvoljena()) {
    slavljenjeNaCekanju = false;
    zapocniSlavljenje();
    posaljiPCLog(F("Slavljenje: automatski pokrecem nakon zavrsetka inercije/blokade"));
  }

  if (mrtvackoNaCekanju &&
      !mrtvacko.aktivno &&
      !slavljenje.aktivno &&
      jeOperacijaCekicaDozvoljena()) {
    mrtvackoNaCekanju = false;
    zapocniMrtvacko();
    posaljiPCLog(F("Mrtvacko: automatski pokrecem nakon zavrsetka inercije/blokade"));
  }
}

bool pokreniMrtvackoInterno(bool koristiThumbwheel, bool postaviCekanjeAkoBlokirano) {
  const unsigned long sadaMs = millis();
  const uint8_t modMrtvackog = dohvatiModMrtvackog();

  if (!jeOperacijaCekicaDozvoljena()) {
    if (postaviCekanjeAkoBlokirano) {
      mrtvackoNaCekanju = true;
      posaljiPCLog(F("Mrtvacko: ne moze se odmah pokrenuti, ostaje na cekanju"));
    }
    return false;
  }

  if (slavljenje.aktivno) {
    if (postaviCekanjeAkoBlokirano) {
      mrtvackoNaCekanju = true;
      posaljiPCLog(F("Mrtvacko: slavljenje je aktivno, zahtjev ostaje na cekanju"));
    }
    return false;
  }

  mrtvackoNaCekanju = false;
  prekiniAktivnoOtkucavanjeZbogPosebnogNacina(F("mrtvacko preuzima cekice toranjskog sata"));

  mrtvacko.aktivno = true;
  mrtvacko.vrijemePocetkaMs = sadaMs;
  mrtvacko.cekiciAktivni = true;
  mrtvacko.vrijemeFazeMs = sadaMs;
  mrtvacko.aktivniMod = modMrtvackog;
  mrtvacko.ignorirajThumbwheel = !koristiThumbwheel;
  digitalWrite(PIN_LAMPICA_MRTVACKO, HIGH);
  if (koristiThumbwheel) {
    primijeniTrajanjeMrtvackogIzThumbwheela(sadaMs, true);
  } else {
    mrtvacko.zadanoTrajanjeMin = 0;
    mrtvacko.autoStopUkljucen = false;
    mrtvacko.autoStopNakonMs = 0UL;
  }

  if (modMrtvackog == MOD_MRTVACKO_SEKVENCIJALNO) {
    mrtvacko.faza = MRTVACKO_FAZA_CEKIC1;
    aktivirajJedanCekicZaPosebniNacin(PIN_CEKIC_MUSKI, MRTVACKO_MOD2_TRAJANJE_IMPULSA_MS);
  } else {
    mrtvacko.faza = MRTVACKO_FAZA_OBA_CEKICA;
    aktivirajObaCekicaZaPosebniNacin(dohvatiTrajanjeImpulsaCekicaZaPosebneNacineMs());
  }

  posaljiLogStartaMrtvackog(modMrtvackog,
                            dohvatiTrajanjeImpulsaCekicaZaPosebneNacineMs(),
                            mrtvacko.autoStopUkljucen,
                            mrtvacko.zadanoTrajanjeMin);
  signalizirajFuneral_Mode();
  return true;
}

}  // namespace

void inicijalizirajSlavljenjeIMrtvacko() {
  pinMode(PIN_KEY_CELEBRATION, INPUT_PULLUP);
  pinMode(PIN_KEY_FUNERAL, INPUT_PULLUP);
  pinMode(PIN_LAMPICA_SLAVLJENJE, OUTPUT);
  pinMode(PIN_LAMPICA_MRTVACKO, OUTPUT);
  digitalWrite(PIN_LAMPICA_SLAVLJENJE, LOW);
  digitalWrite(PIN_LAMPICA_MRTVACKO, LOW);

  slavljenje.aktivno = false;
  slavljenje.vrijemePocetkaMs = 0UL;
  slavljenje.trenutniKorak = 0;
  slavljenje.aktivniMod = 1;
  slavljenje.vrijemeKorakaMs = 0UL;
  slavljenje.cekicAktivan = false;
  slavljenje.aktivniPin = -1;

  mrtvacko.aktivno = false;
  mrtvacko.vrijemePocetkaMs = 0UL;
  mrtvacko.cekiciAktivni = false;
  mrtvacko.vrijemeFazeMs = 0UL;
  mrtvacko.aktivniMod = MOD_MRTVACKO_KLASICNO;
  mrtvacko.faza = MRTVACKO_FAZA_OBA_CEKICA;
  mrtvacko.autoStopUkljucen = false;
  mrtvacko.autoStopNakonMs = 0UL;
  mrtvacko.zadanoTrajanjeMin = 0;
  mrtvacko.ignorirajThumbwheel = false;
  slavljenjeNaCekanju = false;
  mrtvackoNaCekanju = false;
}

void upravljajSlavljenjemIMrtvackim(unsigned long sadaMs) {
  uskladiStanjeNakonSigurnosnogLimita(sadaMs);
  provjeriPrekidacSlavljenja();
  provjeriDugmeMrtvackog();
  obradiCekanjePosebnihNacina();
  azurirajSlavljenje(sadaMs);
  azurirajMrtvacko(sadaMs);
  osvjeziLampicePosebnihNacina(sadaMs);
}

void zapocniSlavljenje() {
  const unsigned long sadaMs = millis();
  const uint8_t modSlavljenja = dohvatiModSlavljenja();

  if (!jeOperacijaCekicaDozvoljena()) {
    slavljenjeNaCekanju = true;
    posaljiPCLog(F("Slavljenje: ne moze se odmah pokrenuti, ostaje na cekanju"));
    return;
  }

  if (mrtvacko.aktivno) {
    slavljenjeNaCekanju = true;
    posaljiPCLog(F("Slavljenje: mrtvacko je aktivno, zahtjev ostaje na cekanju"));
    return;
  }

  slavljenjeNaCekanju = false;
  prekiniAktivnoOtkucavanjeZbogPosebnogNacina(F("slavljenje preuzima cekice toranjskog sata"));

  slavljenje.aktivno = true;
  slavljenje.vrijemePocetkaMs = sadaMs;
  slavljenje.trenutniKorak = 0;
  slavljenje.aktivniMod = modSlavljenja;
  slavljenje.vrijemeKorakaMs = sadaMs;
  slavljenje.cekicAktivan = true;
  slavljenje.aktivniPin = dohvatiPinSlavljenjaZaKorak(modSlavljenja, 0);
  aktivirajJedanCekicZaPosebniNacin(
      slavljenje.aktivniPin, dohvatiTrajanjeImpulsaSlavljenjaZaMod(modSlavljenja));
  digitalWrite(PIN_LAMPICA_SLAVLJENJE, HIGH);

  posaljiLogStartaSlavljenja(modSlavljenja);
  signalizirajCelebration_Mode();
}

void zaustaviSlavljenje() {
  if (!slavljenje.aktivno) {
    return;
  }

  slavljenjeNaCekanju = false;
  slavljenje.aktivno = false;
  deaktivirajObaCekicaZaPosebniNacin();
  slavljenje.cekicAktivan = false;
  slavljenje.aktivniPin = -1;
  digitalWrite(PIN_LAMPICA_SLAVLJENJE, LOW);
  posaljiPCLog(F("Slavljenje: zaustavljeno"));
}

bool jeSlavljenjeUTijeku() {
  return slavljenje.aktivno;
}

void zapocniMrtvacko() {
  (void)pokreniMrtvackoInterno(true, true);
}

bool pokusajZapocetiMrtvackoBezAutoStopa() {
  return pokreniMrtvackoInterno(false, false);
}

void zaustaviMrtvacko() {
  if (!mrtvacko.aktivno) {
    return;
  }

  mrtvackoNaCekanju = false;
  mrtvacko.aktivno = false;
  deaktivirajObaCekicaZaPosebniNacin();
  mrtvacko.cekiciAktivni = false;
  mrtvacko.aktivniMod = MOD_MRTVACKO_KLASICNO;
  mrtvacko.faza = MRTVACKO_FAZA_OBA_CEKICA;
  mrtvacko.autoStopUkljucen = false;
  mrtvacko.autoStopNakonMs = 0UL;
  mrtvacko.zadanoTrajanjeMin = 0;
  mrtvacko.ignorirajThumbwheel = false;
  digitalWrite(PIN_LAMPICA_MRTVACKO, LOW);
  posaljiPCLog(F("Mrtvacko: zaustavljeno"));
}

bool jeMrtvackoUTijeku() {
  return mrtvacko.aktivno;
}
