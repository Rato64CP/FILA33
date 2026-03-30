#include <Arduino.h>
#include <RTClib.h>
#include "okretna_ploca.h"
#include "podesavanja_piny.h"
#include "time_glob.h"
#include "zvonjenje.h"
#include "postavke.h"
#include "eeprom_konstante.h"
#include "wear_leveling.h"
#include "pc_serial.h"
#include "unified_motion_state.h"

namespace {
constexpr unsigned long TRAJANJE_FAZE_MS = 6000UL;
constexpr int BROJ_POZICIJA = 64;
constexpr int VRIJEME_POCETKA_OPERACIJE_ZADANO = 299;
constexpr int VRIJEME_KRAJA_OPERACIJE_ZADANO = 1244;
constexpr int POZICIJA_NOCI = 63;
constexpr int MINUTNI_BLOK = 15;

constexpr uint8_t FAZA_STABILNO = 0;
constexpr uint8_t FAZA_PRVI_RELEJ = 1;
constexpr uint8_t FAZA_DRUGI_RELEJ = 2;

unsigned long pocetakFazeMs = 0;
unsigned long zadnjaProvjeraMs = 0;
uint32_t zadnjiObradeniRtcTick = 0;
uint32_t pocetakFazeTick = 0;

int offsetMinuta = 14;
int zadnjiSlotUlaza = -1;

static const uint8_t BROJ_ULAZA_PLOCE = 5;
static const uint8_t BROJ_ZVONA_MAX = 2;
static const uint8_t PIN_ULAZA_PLOCE[BROJ_ULAZA_PLOCE] = {
  PIN_ULAZA_PLOCE_1,
  PIN_ULAZA_PLOCE_2,
  PIN_ULAZA_PLOCE_3,
  PIN_ULAZA_PLOCE_4,
  PIN_ULAZA_PLOCE_5
};

enum PosebniAutomatskiNacin {
  POSEBNI_NACIN_NONE = 0,
  POSEBNI_NACIN_SLAVLJENJE = 1,
  POSEBNI_NACIN_MRTVACKO = 2
};

bool autoZvonoAktivno[BROJ_ZVONA_MAX] = {false, false};
bool autoZvonoZakazano[BROJ_ZVONA_MAX] = {false, false};
unsigned long autoZvonoStart[BROJ_ZVONA_MAX] = {0, 0};
unsigned long autoZvonoKraj[BROJ_ZVONA_MAX] = {0, 0};
PosebniAutomatskiNacin autoPosebniZakazaniNacin = POSEBNI_NACIN_NONE;
unsigned long autoPosebniStart = 0;
unsigned long autoPosebniTrajanje = 0;
PosebniAutomatskiNacin autoPosebniAktivniNacin = POSEBNI_NACIN_NONE;
unsigned long autoPosebniKraj = 0;

bool jeBootRecoveryAktivan() {
  const uint8_t resetFlags = MCUSR;
  const bool watchdogReset = (resetFlags & (1 << WDRF)) != 0;
  const bool powerLossReset = (resetFlags & ((1 << BORF) | (1 << PORF))) != 0;
  const bool vanjskiReset = (resetFlags & (1 << EXTRF)) != 0;
  return watchdogReset || (powerLossReset && !vanjskiReset);
}

void resetirajAutomatskiPosebniNacin() {
  autoPosebniZakazaniNacin = POSEBNI_NACIN_NONE;
  autoPosebniStart = 0;
  autoPosebniTrajanje = 0;
  autoPosebniAktivniNacin = POSEBNI_NACIN_NONE;
  autoPosebniKraj = 0;
}

bool jePosebniNacinAktivan(PosebniAutomatskiNacin nacin) {
  if (nacin == POSEBNI_NACIN_SLAVLJENJE) {
    return jeSlavljenjeUTijeku();
  }
  if (nacin == POSEBNI_NACIN_MRTVACKO) {
    return jeMrtvackoUTijeku();
  }
  return false;
}

void pokreniPosebniNacin(PosebniAutomatskiNacin nacin) {
  if (nacin == POSEBNI_NACIN_SLAVLJENJE) {
    zapocniSlavljenje();
  } else if (nacin == POSEBNI_NACIN_MRTVACKO) {
    zapocniMrtvacko();
  }
}

void zaustaviPosebniNacin(PosebniAutomatskiNacin nacin) {
  if (nacin == POSEBNI_NACIN_SLAVLJENJE) {
    zaustaviSlavljenje();
  } else if (nacin == POSEBNI_NACIN_MRTVACKO) {
    zaustaviMrtvacko();
  }
}

int izracunajCiljnuPoziciju(const DateTime& now) {
  if (!jePlocaKonfigurirana()) {
    return POZICIJA_NOCI;
  }

  const int ukupnoMinuta = now.hour() * 60 + now.minute();
  const int diff = ukupnoMinuta - VRIJEME_POCETKA_OPERACIJE_ZADANO;
  if (diff < 0 || ukupnoMinuta > VRIJEME_KRAJA_OPERACIJE_ZADANO) {
    return POZICIJA_NOCI;
  }

  int pozicija = diff / MINUTNI_BLOK;
  if (pozicija < 0) pozicija = 0;
  if (pozicija > POZICIJA_NOCI) pozicija = POZICIJA_NOCI;
  return pozicija;
}

void aktivirajRelejePoFazi(const EepromLayout::UnifiedMotionState& stanje) {
  if (!jeVrijemePotvrdjenoZaAutomatiku()) {
    digitalWrite(PIN_RELEJ_PARNE_PLOCE, LOW);
    digitalWrite(PIN_RELEJ_NEPARNE_PLOCE, LOW);
    return;
  }

  if (stanje.plate_phase == FAZA_STABILNO) {
    digitalWrite(PIN_RELEJ_PARNE_PLOCE, LOW);
    digitalWrite(PIN_RELEJ_NEPARNE_PLOCE, LOW);
  } else if (stanje.plate_phase == FAZA_PRVI_RELEJ) {
    digitalWrite(PIN_RELEJ_PARNE_PLOCE, HIGH);
    digitalWrite(PIN_RELEJ_NEPARNE_PLOCE, LOW);
  } else {
    digitalWrite(PIN_RELEJ_PARNE_PLOCE, LOW);
    digitalWrite(PIN_RELEJ_NEPARNE_PLOCE, HIGH);
  }
}

void obradiKorak(EepromLayout::UnifiedMotionState& stanje, unsigned long sadaMs) {
  const uint32_t rtcTick = dohvatiRtcSekundniBrojac();
  if (stanje.plate_phase == FAZA_PRVI_RELEJ &&
      (rtcTick - pocetakFazeTick) >= (TRAJANJE_FAZE_MS / 1000UL)) {
    stanje.plate_phase = FAZA_DRUGI_RELEJ;
    pocetakFazeMs = sadaMs;
    pocetakFazeTick = rtcTick;
    aktivirajRelejePoFazi(stanje);
    UnifiedMotionStateStore::spremiAkoPromjena(stanje);
    posaljiPCLog(F("Ploca: faza 2"));
    UnifiedMotionStateStore::logirajStanje(stanje);
    return;
  }

  if (stanje.plate_phase == FAZA_DRUGI_RELEJ &&
      (rtcTick - pocetakFazeTick) >= (TRAJANJE_FAZE_MS / 1000UL)) {
    stanje.plate_position = static_cast<uint8_t>((stanje.plate_position + 1) % BROJ_POZICIJA);
    stanje.plate_phase = FAZA_STABILNO;
    aktivirajRelejePoFazi(stanje);
    UnifiedMotionStateStore::spremiAkoPromjena(stanje);
    posaljiPCLog(F("Ploca: korak dovrsen"));
    UnifiedMotionStateStore::logirajStanje(stanje);
  }
}

void pokreniKorakAkoTreba(EepromLayout::UnifiedMotionState& stanje, unsigned long sadaMs) {
  const uint32_t rtcTick = dohvatiRtcSekundniBrojac();
  if (stanje.plate_phase != FAZA_STABILNO || rtcTick == zadnjiObradeniRtcTick) {
    return;
  }
  zadnjiObradeniRtcTick = rtcTick;
  const DateTime rtcVrijeme = dohvatiTrenutnoVrijeme();
  if ((rtcVrijeme.second() % 6) != 0) {
    return;
  }
  zadnjaProvjeraMs = sadaMs;

  const int cilj = izracunajCiljnuPoziciju(rtcVrijeme);
  if (stanje.plate_position == cilj) {
    return;
  }

  stanje.plate_phase = FAZA_PRVI_RELEJ;
  pocetakFazeMs = sadaMs;
  pocetakFazeTick = rtcTick;
  aktivirajRelejePoFazi(stanje);
  UnifiedMotionStateStore::spremiAkoPromjena(stanje);
  posaljiPCLog(F("Ploca: start"));
  UnifiedMotionStateStore::logirajStanje(stanje);
}

bool vrijemeProslo(unsigned long sada, unsigned long cilj) {
  return static_cast<long>(sada - cilj) >= 0;
}

void azurirajAutomatskaZvonjenja(unsigned long sadaMs) {
  for (int i = 0; i < BROJ_ZVONA_MAX; ++i) {
    if (autoZvonoZakazano[i] && vrijemeProslo(sadaMs, autoZvonoStart[i])) {
      aktivirajZvonjenjeNaTrajanje(i + 1, autoZvonoKraj[i] - autoZvonoStart[i]);
      if (jeZvonoAktivno(i + 1)) {
        autoZvonoAktivno[i] = true;
        autoZvonoZakazano[i] = false;
      }
    }

    if (autoZvonoAktivno[i] && vrijemeProslo(sadaMs, autoZvonoKraj[i])) {
      deaktivirajZvonjenje(i + 1);
      autoZvonoAktivno[i] = false;
    }
  }

  if (autoPosebniAktivniNacin != POSEBNI_NACIN_NONE &&
      !jePosebniNacinAktivan(autoPosebniAktivniNacin)) {
    autoPosebniAktivniNacin = POSEBNI_NACIN_NONE;
  }

  if (autoPosebniZakazaniNacin != POSEBNI_NACIN_NONE &&
      vrijemeProslo(sadaMs, autoPosebniStart)) {
    pokreniPosebniNacin(autoPosebniZakazaniNacin);
    if (jePosebniNacinAktivan(autoPosebniZakazaniNacin)) {
      autoPosebniAktivniNacin = autoPosebniZakazaniNacin;
      autoPosebniZakazaniNacin = POSEBNI_NACIN_NONE;
      autoPosebniKraj = sadaMs + autoPosebniTrajanje;
    }
  }

  if (autoPosebniAktivniNacin != POSEBNI_NACIN_NONE &&
      vrijemeProslo(sadaMs, autoPosebniKraj)) {
    zaustaviPosebniNacin(autoPosebniAktivniNacin);
    if (!jePosebniNacinAktivan(autoPosebniAktivniNacin)) {
      autoPosebniAktivniNacin = POSEBNI_NACIN_NONE;
    } else {
      // Ako poseban nacin nije stao zbog neke blokade, pokusaj ponovno kasnije.
      autoPosebniKraj = sadaMs + 1000UL;
    }
  }
}

void zaustaviAutomatikuPloceZbogNepotvrdenogVremena() {
  for (int i = 0; i < BROJ_ZVONA_MAX; ++i) {
    if (autoZvonoAktivno[i]) {
      deaktivirajZvonjenje(i + 1);
    }
    autoZvonoAktivno[i] = false;
    autoZvonoZakazano[i] = false;
    autoZvonoStart[i] = 0;
    autoZvonoKraj[i] = 0;
  }

  if (autoPosebniAktivniNacin != POSEBNI_NACIN_NONE) {
    zaustaviPosebniNacin(autoPosebniAktivniNacin);
  }
  resetirajAutomatskiPosebniNacin();
}

void zakaziPosebniNacin(PosebniAutomatskiNacin nacin, unsigned long startMs, unsigned long trajanjeMs) {
  if (nacin == POSEBNI_NACIN_NONE) {
    return;
  }

  autoPosebniZakazaniNacin = nacin;
  autoPosebniStart = startMs;
  autoPosebniTrajanje = trajanjeMs;
}

unsigned long dohvatiKrajNajduljegAktivnogZvona(unsigned long pocetnaVrijednost) {
  unsigned long kraj = pocetnaVrijednost;
  for (int i = 0; i < BROJ_ZVONA_MAX; ++i) {
    if ((autoZvonoAktivno[i] || autoZvonoZakazano[i]) && autoZvonoKraj[i] > kraj) {
      kraj = autoZvonoKraj[i];
    }
  }
  return kraj;
}

uint8_t dohvatiOznakuCavlaIzPolja(bool ulazi[BROJ_ULAZA_PLOCE], uint8_t brojMjestaZaCavle, uint8_t cavao) {
  if (cavao < 1 || cavao > brojMjestaZaCavle) {
    return 0;
  }
  return ulazi[cavao - 1] ? cavao : 0;
}

bool jeKonfliktPosebnihNacina(bool slavljenjeAktivno, bool mrtvackoAktivno) {
  return slavljenjeAktivno && mrtvackoAktivno;
}

void pokreniAutomatskoZvonjenje(int index,
                                unsigned long startMs,
                                unsigned long trajanjeMs,
                                bool odgodiPocetak) {
  if (index < 0 || index >= BROJ_ZVONA_MAX) return;
  autoZvonoStart[index] = startMs;
  autoZvonoKraj[index] = startMs + trajanjeMs;

  if (odgodiPocetak) {
    autoZvonoZakazano[index] = true;
    autoZvonoAktivno[index] = false;
  } else {
    aktivirajZvonjenjeNaTrajanje(index + 1, trajanjeMs);
    if (jeZvonoAktivno(index + 1)) {
      autoZvonoAktivno[index] = true;
      autoZvonoZakazano[index] = false;
    } else {
      autoZvonoAktivno[index] = false;
      autoZvonoZakazano[index] = false;
      autoZvonoStart[index] = 0;
      autoZvonoKraj[index] = 0;
    }
  }
}

void obradiUlazePloce(const DateTime& now, unsigned long sadaMs) {
  bool ulazi[BROJ_ULAZA_PLOCE];
  for (uint8_t i = 0; i < BROJ_ULAZA_PLOCE; ++i) {
    ulazi[i] = (digitalRead(PIN_ULAZA_PLOCE[i]) == LOW);
  }

  const bool jeNedjelja = (now.dayOfTheWeek() == 0);
  const uint8_t brojMjestaZaCavle = dohvatiBrojMjestaZaCavle();
  const uint8_t brojZvona = dohvatiBrojZvona();
  const uint8_t cavaoSlavljenja = dohvatiCavaoSlavljenja();
  const uint8_t cavaoMrtvackog = dohvatiCavaoMrtvackog();

  String log = F("Cavli ploce:");
  for (uint8_t i = 0; i < brojMjestaZaCavle; ++i) {
    log += ' ';
    log += (i + 1);
    log += '=';
    log += ulazi[i] ? F("ON") : F("OFF");
  }
  log += jeNedjelja ? F(" | nedjelja") : F(" | pon-sub");
  posaljiPCLog(log);

  const unsigned long trajanjeZvona = jeNedjelja ? dohvatiTrajanjeZvonjenjaNedjeljaMs()
                                                 : dohvatiTrajanjeZvonjenjaRadniMs();
  const unsigned long trajanjeSlavljenja = dohvatiTrajanjeSlavljenjaMs();
  const bool slavljenjePrije = jeSlavljenjePrijeZvonjenja();
  const bool slavljenjeAktivno = dohvatiOznakuCavlaIzPolja(ulazi, brojMjestaZaCavle, cavaoSlavljenja) != 0;
  const bool mrtvackoAktivno = dohvatiOznakuCavlaIzPolja(ulazi, brojMjestaZaCavle, cavaoMrtvackog) != 0;

  if (jeKonfliktPosebnihNacina(slavljenjeAktivno, mrtvackoAktivno)) {
    posaljiPCLog(F("Cavli: istodobno aktivni slavljenje i mrtvacko - prednost ima slavljenje"));
  }

  bool imaZvono = false;
  for (uint8_t zvono = 1; zvono <= brojZvona && zvono <= BROJ_ZVONA_MAX; ++zvono) {
    const uint8_t cavao = jeNedjelja ? dohvatiCavaoNedjeljaZaZvono(zvono)
                                     : dohvatiCavaoRadniZaZvono(zvono);
    if (dohvatiOznakuCavlaIzPolja(ulazi, brojMjestaZaCavle, cavao) == 0) {
      continue;
    }

    pokreniAutomatskoZvonjenje(zvono - 1,
                               slavljenjeAktivno && slavljenjePrije ? (sadaMs + trajanjeSlavljenja) : sadaMs,
                               trajanjeZvona,
                               slavljenjeAktivno && slavljenjePrije);
    if (autoZvonoAktivno[zvono - 1] || autoZvonoZakazano[zvono - 1]) {
      imaZvono = true;
      String bellLog = F("Cavli: aktiviran BELL");
      bellLog += zvono;
      bellLog += F(" preko cavla ");
      bellLog += cavao;
      posaljiPCLog(bellLog);
    }
  }

  const PosebniAutomatskiNacin trazeniPosebniNacin = slavljenjeAktivno
      ? POSEBNI_NACIN_SLAVLJENJE
      : (mrtvackoAktivno ? POSEBNI_NACIN_MRTVACKO : POSEBNI_NACIN_NONE);

  if (trazeniPosebniNacin == POSEBNI_NACIN_NONE) {
    if (autoPosebniAktivniNacin == POSEBNI_NACIN_NONE) {
      resetirajAutomatskiPosebniNacin();
    }
    return;
  }

  const unsigned long trajanjePosebnog =
      (trazeniPosebniNacin == POSEBNI_NACIN_SLAVLJENJE) ? trajanjeSlavljenja : trajanjeZvona;

  if (!imaZvono || (trazeniPosebniNacin == POSEBNI_NACIN_SLAVLJENJE && slavljenjePrije)) {
    pokreniPosebniNacin(trazeniPosebniNacin);
    if (jePosebniNacinAktivan(trazeniPosebniNacin)) {
      autoPosebniAktivniNacin = trazeniPosebniNacin;
      autoPosebniKraj = sadaMs + trajanjePosebnog;
      autoPosebniZakazaniNacin = POSEBNI_NACIN_NONE;
    }

    if (trazeniPosebniNacin == POSEBNI_NACIN_SLAVLJENJE) {
      posaljiPCLog(slavljenjePrije
          ? F("Cavli: cavao slavljenja pokrece SLAVLJENJE PRIJE zvona")
          : F("Cavli: cavao slavljenja odmah pokrece SLAVLJENJE"));
    } else {
      posaljiPCLog(F("Cavli: cavao mrtvackog odmah pokrece MRTVACKO"));
    }
    return;
  }

  const unsigned long startPosebnog = dohvatiKrajNajduljegAktivnogZvona(sadaMs + trajanjeZvona);
  zakaziPosebniNacin(trazeniPosebniNacin, startPosebnog, trajanjePosebnog);
  if (trazeniPosebniNacin == POSEBNI_NACIN_SLAVLJENJE) {
    posaljiPCLog(F("Cavli: slavljenje zakazano nakon zavrsetka zvona"));
  } else {
    posaljiPCLog(F("Cavli: mrtvacko zakazano nakon zavrsetka zvona"));
  }
}

}  // namespace

void inicijalizirajPlocu() {
  pinMode(PIN_RELEJ_PARNE_PLOCE, OUTPUT);
  pinMode(PIN_RELEJ_NEPARNE_PLOCE, OUTPUT);
  digitalWrite(PIN_RELEJ_PARNE_PLOCE, LOW);
  digitalWrite(PIN_RELEJ_NEPARNE_PLOCE, LOW);

  for (uint8_t i = 0; i < BROJ_ULAZA_PLOCE; ++i) pinMode(PIN_ULAZA_PLOCE[i], INPUT_PULLUP);

  WearLeveling::ucitaj(EepromLayout::BAZA_OFFSET_MINUTA,
                       EepromLayout::SLOTOVI_OFFSET_MINUTA,
                       offsetMinuta);
  offsetMinuta = constrain(offsetMinuta, 0, 14);

  EepromLayout::UnifiedMotionState stanje = UnifiedMotionStateStore::dohvatiIliMigriraj();
  if (jeBootRecoveryAktivan()) {
    posaljiPCLog(F("Ploca: zadrzavam spremljeno stanje za boot recovery"));
  } else {
    posaljiPCLog(F("Ploca: zadrzavam spremljenu poziciju pri normalnom restartu"));
  }
  aktivirajRelejePoFazi(stanje);

  pocetakFazeMs = millis();
  zadnjaProvjeraMs = millis();
  zadnjiObradeniRtcTick = 0;
  pocetakFazeTick = 0;
  zadnjiSlotUlaza = -1;
  for (uint8_t i = 0; i < BROJ_ZVONA_MAX; ++i) {
    autoZvonoAktivno[i] = false;
    autoZvonoZakazano[i] = false;
    autoZvonoStart[i] = 0;
    autoZvonoKraj[i] = 0;
  }
  resetirajAutomatskiPosebniNacin();
  UnifiedMotionStateStore::logirajStanje(stanje);
  posaljiPCLog(F("Ploca: inicijalizirana kroz jedinstveni model stanja"));
}

void upravljajPlocom() {
  if (!jeVrijemePotvrdjenoZaAutomatiku()) {
    EepromLayout::UnifiedMotionState stanje = UnifiedMotionStateStore::dohvatiIliMigriraj();
    if (stanje.plate_phase != FAZA_STABILNO) {
      stanje.plate_phase = FAZA_STABILNO;
      UnifiedMotionStateStore::spremiAkoPromjena(stanje);
      posaljiPCLog(F("Ploca: automatika blokirana dok vrijeme nije potvrdeno"));
    }
    zaustaviAutomatikuPloceZbogNepotvrdenogVremena();
    zadnjiObradeniRtcTick = 0;
    pocetakFazeTick = 0;
    zadnjiSlotUlaza = -1;
    aktivirajRelejePoFazi(stanje);
    return;
  }

  const DateTime now = dohvatiTrenutnoVrijeme();
  const unsigned long sadaMs = millis();
  azurirajAutomatskaZvonjenja(sadaMs);

  if (jePlocaKonfigurirana() && now.minute() % 15 == 0 && now.second() >= 30) {
    int slot = now.hour() * 4 + (now.minute() / 15);
    if (slot != zadnjiSlotUlaza) {
      zadnjiSlotUlaza = slot;
      obradiUlazePloce(now, sadaMs);
    }
  }

  EepromLayout::UnifiedMotionState stanje = UnifiedMotionStateStore::dohvatiIliMigriraj();
  aktivirajRelejePoFazi(stanje);

  if (stanje.plate_phase != FAZA_STABILNO) {
    obradiKorak(stanje, sadaMs);
    return;
  }

  pokreniKorakAkoTreba(stanje, sadaMs);
}

void postaviTrenutniPolozajPloce(int pozicija) {
  EepromLayout::UnifiedMotionState stanje = UnifiedMotionStateStore::dohvatiIliMigriraj();
  stanje.plate_position = static_cast<uint8_t>(constrain(pozicija, 0, BROJ_POZICIJA - 1));
  stanje.plate_phase = FAZA_STABILNO;
  UnifiedMotionStateStore::spremiAkoPromjena(stanje);
}

void postaviOffsetMinuta(int offset) {
  offsetMinuta = constrain(offset, 0, 14);
  WearLeveling::spremi(EepromLayout::BAZA_OFFSET_MINUTA,
                       EepromLayout::SLOTOVI_OFFSET_MINUTA,
                       offsetMinuta);
}

int dohvatiPozicijuPloce() {
  return UnifiedMotionStateStore::dohvatiIliMigriraj().plate_position;
}

int dohvatiOffsetMinuta() {
  return offsetMinuta;
}

bool jePlocaUSinkronu() {
  if (!jeVrijemePotvrdjenoZaAutomatiku()) {
    return false;
  }

  EepromLayout::UnifiedMotionState stanje = UnifiedMotionStateStore::dohvatiIliMigriraj();
  return stanje.plate_phase == FAZA_STABILNO &&
         stanje.plate_position == izracunajCiljnuPoziciju(dohvatiTrenutnoVrijeme());
}

void oznaciPlocuKaoSinkroniziranu() {
  zadnjaProvjeraMs = 0;
  zadnjiObradeniRtcTick = 0;
}

void zatraziPoravnanjeTaktaPloce() {
  zadnjiObradeniRtcTick = 0;
}
