// slavljenje_mrtvacko.cpp - Slavljenje i mrtvacko kao posebni nacini rada cekica

#include <Arduino.h>

#include "slavljenje_mrtvacko.h"

#include "otkucavanje_interno.h"
#include "podesavanja_piny.h"
#include "time_glob.h"
#include "postavke.h"
#include "lcd_display.h"
#include "pc_serial.h"
#include "debouncing.h"
#include "mrtvacko_thumbwheel.h"

namespace {

// Slavljenje mod 1: tocan uzorak 1-2-2 s definiranim pauzama.
const unsigned long SLAVLJENJE_PAUZA_NAKON_CEKIC1_MS = 300UL;
const unsigned long SLAVLJENJE_PAUZA_NAKON_CEKIC2_MS = 150UL;

// Slavljenje mod 2: C1 -> 100 ms -> C2 -> 300 ms -> ponavljanje.
const unsigned long SLAVLJENJE_MOD2_PAUZA_NAKON_CEKIC1_MS = 100UL;
const unsigned long SLAVLJENJE_MOD2_PAUZA_NAKON_CEKIC2_MS = 300UL;

// Mrtvacko: oba cekica s konfiguriranim impulsom, zatim 10 s pauza.
const unsigned long MRTVACKO_PAUZA_MS = 10000UL;
const unsigned long MRTVACKO_MOD2_PAUZA_MS = 5000UL;

enum ModMrtvackog {
  MOD_MRTVACKO_KLASICNO = 1,
  MOD_MRTVACKO_SEKVENCIJALNO = 2
};

enum FazaMrtvackog {
  MRTVACKO_FAZA_OBA_CEKICA = 0,
  MRTVACKO_FAZA_CEKIC1 = 1,
  MRTVACKO_FAZA_CEKANJE_IDUCE_SEKUNDE = 2,
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
  uint32_t rtcTickFaze;
  bool autoStopUkljucen;
  unsigned long autoStopNakonMs;
  uint8_t zadanoTrajanjeMin;
};

SlavljenjeStanje slavljenje = {false, 0UL, 0, 1, 0UL, false, -1};
MrtvackoStanje mrtvacko = {
    false, 0UL, false, 0UL, MOD_MRTVACKO_KLASICNO, MRTVACKO_FAZA_OBA_CEKICA, 0UL, false, 0UL, 0};

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

unsigned long dohvatiPauzuMrtvackogZaMod(uint8_t mod) {
  return (mod == MOD_MRTVACKO_SEKVENCIJALNO) ? MRTVACKO_MOD2_PAUZA_MS : MRTVACKO_PAUZA_MS;
}

bool jeDosegnutaIducaSekundaMrtvackog(uint32_t rtcTickFaze,
                                      unsigned long vrijemeFazeMs,
                                      unsigned long sadaMs) {
  return dohvatiRtcSekundniBrojac() != rtcTickFaze || (sadaMs - vrijemeFazeMs) >= 1000UL;
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
    mrtvacko.rtcTickFaze = dohvatiRtcSekundniBrojac();
    if (mrtvacko.aktivniMod == MOD_MRTVACKO_SEKVENCIJALNO &&
        mrtvacko.faza == MRTVACKO_FAZA_CEKIC1) {
      mrtvacko.faza = MRTVACKO_FAZA_CEKANJE_IDUCE_SEKUNDE;
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

  if (slavljenje.cekicAktivan) {
    if (proteklo >= dohvatiTrajanjeImpulsaCekicaZaPosebneNacineMs()) {
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
    aktivirajJedanCekicZaPosebniNacin(sljedeciPin, dohvatiTrajanjeImpulsaCekicaZaPosebneNacineMs());
    slavljenje.aktivniPin = sljedeciPin;
    slavljenje.cekicAktivan = true;
    slavljenje.vrijemeKorakaMs = sadaMs;
  }
}

void azurirajMrtvacko(unsigned long sadaMs) {
  if (!mrtvacko.aktivno) {
    return;
  }

  if (mrtvacko.autoStopUkljucen) {
    const unsigned long protekloOdPocetka = sadaMs - mrtvacko.vrijemePocetkaMs;
    if (protekloOdPocetka >= mrtvacko.autoStopNakonMs) {
      String log = F("Mrtvacko: auto-stop nakon ");
      log += String(mrtvacko.zadanoTrajanjeMin);
      log += F(" min");
      posaljiPCLog(log);
      zaustaviMrtvacko();
      return;
    }
  }

  const unsigned long proteklo = sadaMs - mrtvacko.vrijemeFazeMs;

  if (mrtvacko.cekiciAktivni) {
    if (proteklo >= dohvatiTrajanjeImpulsaCekicaZaPosebneNacineMs()) {
      deaktivirajObaCekicaZaPosebniNacin();
      mrtvacko.cekiciAktivni = false;
      mrtvacko.vrijemeFazeMs = sadaMs;
      mrtvacko.rtcTickFaze = dohvatiRtcSekundniBrojac();
      if (mrtvacko.aktivniMod == MOD_MRTVACKO_SEKVENCIJALNO &&
          mrtvacko.faza == MRTVACKO_FAZA_CEKIC1) {
        mrtvacko.faza = MRTVACKO_FAZA_CEKANJE_IDUCE_SEKUNDE;
      } else {
        mrtvacko.faza = MRTVACKO_FAZA_PAUZA;
      }
    }
    return;
  }

  if (mrtvacko.aktivniMod == MOD_MRTVACKO_SEKVENCIJALNO &&
      mrtvacko.faza == MRTVACKO_FAZA_CEKANJE_IDUCE_SEKUNDE) {
    if (jeDosegnutaIducaSekundaMrtvackog(mrtvacko.rtcTickFaze, mrtvacko.vrijemeFazeMs, sadaMs)) {
      aktivirajJedanCekicZaPosebniNacin(PIN_CEKIC_ZENSKI,
                                        dohvatiTrajanjeImpulsaCekicaZaPosebneNacineMs());
      mrtvacko.cekiciAktivni = true;
      mrtvacko.faza = MRTVACKO_FAZA_CEKIC2;
      mrtvacko.vrijemeFazeMs = sadaMs;
      mrtvacko.rtcTickFaze = dohvatiRtcSekundniBrojac();
    }
    return;
  }

  if (proteklo >= dohvatiPauzuMrtvackogZaMod(mrtvacko.aktivniMod)) {
    if (mrtvacko.aktivniMod == MOD_MRTVACKO_SEKVENCIJALNO) {
      aktivirajJedanCekicZaPosebniNacin(PIN_CEKIC_MUSKI,
                                        dohvatiTrajanjeImpulsaCekicaZaPosebneNacineMs());
      mrtvacko.faza = MRTVACKO_FAZA_CEKIC1;
    } else {
      aktivirajObaCekicaZaPosebniNacin(dohvatiTrajanjeImpulsaCekicaZaPosebneNacineMs());
      mrtvacko.faza = MRTVACKO_FAZA_OBA_CEKICA;
    }
    mrtvacko.cekiciAktivni = true;
    mrtvacko.vrijemeFazeMs = sadaMs;
    mrtvacko.rtcTickFaze = dohvatiRtcSekundniBrojac();
  }
}

void provjeriDugmeSlavljenja() {
  SwitchState novoStanje = SWITCH_RELEASED;
  if (!obradiDebouncedInput(PIN_KEY_CELEBRATION, 30, &novoStanje)) {
    return;
  }

  if (novoStanje == SWITCH_PRESSED) {
    if (!slavljenje.aktivno) {
      zapocniSlavljenje();
      posaljiPCLog(F("Dugme: slavljenje pokrenuto"));
    }
  } else if (slavljenje.aktivno) {
    zaustaviSlavljenje();
    posaljiPCLog(F("Dugme: slavljenje zaustavljeno"));
  }
}

void provjeriDugmeMrtvackog() {
  SwitchState novoStanje = SWITCH_RELEASED;
  if (!obradiDebouncedInput(PIN_KEY_FUNERAL, 30, &novoStanje)) {
    return;
  }

  if (novoStanje == SWITCH_PRESSED) {
    if (mrtvacko.aktivno) {
      zaustaviMrtvacko();
      posaljiPCLog(F("Dugme: mrtvacko zaustavljeno"));
    } else {
      zapocniMrtvacko();
      posaljiPCLog(F("Dugme: mrtvacko pokrenuto"));
    }
  }
}

}  // namespace

void inicijalizirajSlavljenjeIMrtvacko() {
  pinMode(PIN_KEY_CELEBRATION, INPUT_PULLUP);
  pinMode(PIN_KEY_FUNERAL, INPUT_PULLUP);

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
  mrtvacko.rtcTickFaze = 0UL;
  mrtvacko.autoStopUkljucen = false;
  mrtvacko.autoStopNakonMs = 0UL;
  mrtvacko.zadanoTrajanjeMin = 0;
}

void upravljajSlavljenjemIMrtvackim(unsigned long sadaMs) {
  uskladiStanjeNakonSigurnosnogLimita(sadaMs);
  provjeriDugmeSlavljenja();
  provjeriDugmeMrtvackog();
  azurirajSlavljenje(sadaMs);
  azurirajMrtvacko(sadaMs);
}

void zapocniSlavljenje() {
  const unsigned long sadaMs = millis();
  const uint8_t modSlavljenja = dohvatiModSlavljenja();

  if (!jeOperacijaCekicaDozvoljena()) {
    posaljiPCLog(F("Slavljenje: ne moze se pokrenuti (inercija ili blok)"));
    return;
  }

  if (mrtvacko.aktivno) {
    posaljiPCLog(F("Slavljenje: odbijeno - mrtvacko je aktivno (mutual exclusion)"));
    return;
  }

  prekiniAktivnoOtkucavanjeZbogPosebnogNacina(F("slavljenje preuzima cekice toranjskog sata"));

  slavljenje.aktivno = true;
  slavljenje.vrijemePocetkaMs = sadaMs;
  slavljenje.trenutniKorak = 0;
  slavljenje.aktivniMod = modSlavljenja;
  slavljenje.vrijemeKorakaMs = sadaMs;
  slavljenje.cekicAktivan = true;
  slavljenje.aktivniPin = dohvatiPinSlavljenjaZaKorak(modSlavljenja, 0);
  aktivirajJedanCekicZaPosebniNacin(slavljenje.aktivniPin,
                                    dohvatiTrajanjeImpulsaCekicaZaPosebneNacineMs());

  String log = F("Slavljenje: pokrenuto (mod ");
  log += modSlavljenja;
  log += (modSlavljenja == 2) ? F(", uzorak C1-100ms-C2-300ms)") : F(", uzorak C1-C2-C2)");
  posaljiPCLog(log);
  signalizirajCelebration_Mode();
}

void zaustaviSlavljenje() {
  if (!slavljenje.aktivno) {
    return;
  }

  slavljenje.aktivno = false;
  deaktivirajObaCekicaZaPosebniNacin();
  slavljenje.cekicAktivan = false;
  slavljenje.aktivniPin = -1;
  posaljiPCLog(F("Slavljenje: zaustavljeno"));
}

bool jeSlavljenjeUTijeku() {
  return slavljenje.aktivno;
}

void zapocniMrtvacko() {
  const unsigned long sadaMs = millis();
  const uint8_t trajanjeMin = dohvatiMrtvackoThumbwheelVrijednost();
  const uint8_t modMrtvackog = dohvatiModMrtvackog();

  if (!jeOperacijaCekicaDozvoljena()) {
    posaljiPCLog(F("Mrtvacko: ne moze se pokrenuti (inercija ili blok)"));
    return;
  }

  if (slavljenje.aktivno) {
    posaljiPCLog(F("Mrtvacko: odbijeno - slavljenje je aktivno (mutual exclusion)"));
    return;
  }

  prekiniAktivnoOtkucavanjeZbogPosebnogNacina(F("mrtvacko preuzima cekice toranjskog sata"));

  mrtvacko.aktivno = true;
  mrtvacko.vrijemePocetkaMs = sadaMs;
  mrtvacko.cekiciAktivni = true;
  mrtvacko.vrijemeFazeMs = sadaMs;
  mrtvacko.aktivniMod = modMrtvackog;
  mrtvacko.rtcTickFaze = dohvatiRtcSekundniBrojac();
  mrtvacko.zadanoTrajanjeMin = trajanjeMin;
  mrtvacko.autoStopUkljucen = (trajanjeMin > 0);
  mrtvacko.autoStopNakonMs =
      mrtvacko.autoStopUkljucen ? static_cast<unsigned long>(trajanjeMin) * 60000UL : 0UL;
  if (modMrtvackog == MOD_MRTVACKO_SEKVENCIJALNO) {
    mrtvacko.faza = MRTVACKO_FAZA_CEKIC1;
    aktivirajJedanCekicZaPosebniNacin(PIN_CEKIC_MUSKI,
                                      dohvatiTrajanjeImpulsaCekicaZaPosebneNacineMs());
  } else {
    mrtvacko.faza = MRTVACKO_FAZA_OBA_CEKICA;
    aktivirajObaCekicaZaPosebniNacin(dohvatiTrajanjeImpulsaCekicaZaPosebneNacineMs());
  }

  String log = F("Mrtvacko: pokrenuto (mod ");
  log += String(modMrtvackog);
  if (modMrtvackog == MOD_MRTVACKO_SEKVENCIJALNO) {
    log += F(", C1 impuls -> iduca sekunda -> C2 impuls -> pauza 5s");
  } else {
    log += F(", oba cekica ");
    log += String(dohvatiTrajanjeImpulsaCekicaZaPosebneNacineMs());
    log += F("ms / pauza 10s");
  }
  if (mrtvacko.autoStopUkljucen) {
    log += F(", auto-stop nakon ");
    log += String(trajanjeMin);
    log += F(" min)");
  } else {
    log += F(", radi stalno do rucnog gasenja)");
  }
  posaljiPCLog(log);
  signalizirajFuneral_Mode();
}

void zaustaviMrtvacko() {
  if (!mrtvacko.aktivno) {
    return;
  }

  mrtvacko.aktivno = false;
  deaktivirajObaCekicaZaPosebniNacin();
  mrtvacko.cekiciAktivni = false;
  mrtvacko.aktivniMod = MOD_MRTVACKO_KLASICNO;
  mrtvacko.faza = MRTVACKO_FAZA_OBA_CEKICA;
  mrtvacko.rtcTickFaze = 0UL;
  mrtvacko.autoStopUkljucen = false;
  mrtvacko.autoStopNakonMs = 0UL;
  mrtvacko.zadanoTrajanjeMin = 0;
  posaljiPCLog(F("Mrtvacko: zaustavljeno"));
}

bool jeMrtvackoUTijeku() {
  return mrtvacko.aktivno;
}
