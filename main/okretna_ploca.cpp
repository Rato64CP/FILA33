#include <Arduino.h>
#include <RTClib.h>
#include <stdio.h>
#include "okretna_ploca.h"
#include "podesavanja_piny.h"
#include "time_glob.h"
#include "zvonjenje.h"
#include "slavljenje_mrtvacko.h"
#include "postavke.h"
#include "pc_serial.h"
#include "unified_motion_state.h"
#include "debouncing.h"
#include "prekidac_tisine.h"
#include "watchdog.h"

namespace {
constexpr int BROJ_POZICIJA = 64;
constexpr int POZICIJA_NOCI = 63;
constexpr int MINUTNI_BLOK = 15;
constexpr uint8_t DEBOUNCE_ULAZA_PLOCE_MS = 30;

constexpr uint8_t FAZA_STABILNO = 0;
constexpr uint8_t FAZA_PRVI_RELEJ = 1;
constexpr uint8_t FAZA_DRUGI_RELEJ = 2;

uint32_t pocetakFazeRtcTick = 0;
uint32_t zadnjiObradeniRtcTick = 0;

uint32_t zadnjiKljucCitanjaCavala = 0;
bool zadnjiKljucCitanjaCavalaValjan = false;
bool rucnaBlokadaPloce = false;

static const uint8_t BROJ_ULAZA_PLOCE = 5;
static const uint8_t BROJ_ZVONA_MAX = 2;
static const uint8_t PIN_ULAZA_PLOCE[BROJ_ULAZA_PLOCE] = {
  PIN_ULAZA_PLOCE_1,
  PIN_ULAZA_PLOCE_2,
  PIN_ULAZA_PLOCE_3,
  PIN_ULAZA_PLOCE_4,
  PIN_ULAZA_PLOCE_5
};
bool stabilnaStanjaUlazaPloce[BROJ_ULAZA_PLOCE] = {false, false, false, false, false};

enum PosebniAutomatskiNacin {
  POSEBNI_NACIN_NONE = 0,
  POSEBNI_NACIN_SLAVLJENJE = 1
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

void resetirajAutomatskiPosebniNacin() {
  autoPosebniZakazaniNacin = POSEBNI_NACIN_NONE;
  autoPosebniStart = 0;
  autoPosebniTrajanje = 0;
  autoPosebniAktivniNacin = POSEBNI_NACIN_NONE;
  autoPosebniKraj = 0;
}

void procitajStabilnaStanjaUlazaPloce(bool ulazi[BROJ_ULAZA_PLOCE]) {
  for (uint8_t i = 0; i < BROJ_ULAZA_PLOCE; ++i) {
    SwitchState stanje = stabilnaStanjaUlazaPloce[i] ? SWITCH_PRESSED : SWITCH_RELEASED;
    obradiDebouncedInput(PIN_ULAZA_PLOCE[i], DEBOUNCE_ULAZA_PLOCE_MS, &stanje);
    stabilnaStanjaUlazaPloce[i] = (stanje == SWITCH_PRESSED);
    ulazi[i] = stabilnaStanjaUlazaPloce[i];
  }
}

bool jePosebniNacinAktivan(PosebniAutomatskiNacin nacin) {
  return (nacin == POSEBNI_NACIN_SLAVLJENJE) && jeSlavljenjeUTijeku();
}

void pokreniPosebniNacin(PosebniAutomatskiNacin nacin) {
  if (nacin == POSEBNI_NACIN_SLAVLJENJE) {
    zapocniSlavljenje();
  }
}

void zaustaviPosebniNacin(PosebniAutomatskiNacin nacin) {
  if (nacin == POSEBNI_NACIN_SLAVLJENJE) {
    zaustaviSlavljenje();
  }
}

int izracunajCiljnuPoziciju(const DateTime& now) {
  if (!jePlocaKonfigurirana()) {
    return POZICIJA_NOCI;
  }

  const int ukupnoMinuta = now.hour() * 60 + now.minute();
  const int pocetakOperacije = dohvatiPocetakPloceMinute();
  const int krajOperacije = dohvatiKrajPloceMinute();
  const int diff = ukupnoMinuta - pocetakOperacije;
  if (diff < 0 || ukupnoMinuta > krajOperacije) {
    return POZICIJA_NOCI;
  }

  int pozicija = diff / MINUTNI_BLOK;
  if (pozicija < 0) pozicija = 0;
  if (pozicija > POZICIJA_NOCI) pozicija = POZICIJA_NOCI;
  return pozicija;
}

bool izracunajVrijemeZaPoziciju(int pozicija, int& sat24, int& minuta) {
  if (pozicija < 0 || pozicija >= BROJ_POZICIJA) {
    return false;
  }

  const int pocetakOperacije = dohvatiPocetakPloceMinute();
  const int ukupnoMinuta = pocetakOperacije + (pozicija * MINUTNI_BLOK);
  if (ukupnoMinuta < 0 || ukupnoMinuta > (23 * 60 + 59)) {
    return false;
  }

  sat24 = ukupnoMinuta / 60;
  minuta = ukupnoMinuta % 60;
  return true;
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

void obradiKorak(EepromLayout::UnifiedMotionState& stanje) {
  const uint32_t rtcTick = dohvatiRtcSekundniBrojac();
  if (stanje.plate_phase != FAZA_STABILNO && pocetakFazeRtcTick == 0) {
    pocetakFazeRtcTick = rtcTick;
  }

  const bool istekPoSqw =
      stanje.plate_phase != FAZA_STABILNO &&
      pocetakFazeRtcTick != 0 &&
      (rtcTick - pocetakFazeRtcTick) >= 6U;

  if (stanje.plate_phase == FAZA_PRVI_RELEJ && istekPoSqw) {
    stanje.plate_phase = FAZA_DRUGI_RELEJ;
    pocetakFazeRtcTick = rtcTick;
    aktivirajRelejePoFazi(stanje);
    UnifiedMotionStateStore::spremiAkoPromjena(stanje);
    posaljiPCLog(F("Ploca: faza 2 po RTC SQW taktu"));
    UnifiedMotionStateStore::logirajStanje(stanje);
    return;
  }

  if (stanje.plate_phase == FAZA_DRUGI_RELEJ && istekPoSqw) {
    stanje.plate_position = static_cast<uint8_t>((stanje.plate_position + 1) % BROJ_POZICIJA);
    stanje.plate_phase = FAZA_STABILNO;
    pocetakFazeRtcTick = 0;
    aktivirajRelejePoFazi(stanje);
    UnifiedMotionStateStore::spremiAkoPromjena(stanje);
    posaljiPCLog(F("Ploca: korak dovrsen po RTC SQW taktu"));
    UnifiedMotionStateStore::logirajStanje(stanje);
  }
}

void pokreniKorakAkoTreba(EepromLayout::UnifiedMotionState& stanje) {
  const uint32_t rtcTick = dohvatiRtcSekundniBrojac();
  if (stanje.plate_phase != FAZA_STABILNO) {
    return;
  }
  if (rtcTick == zadnjiObradeniRtcTick) {
    return;
  }
  zadnjiObradeniRtcTick = rtcTick;
  const DateTime rtcVrijeme = dohvatiTrenutnoVrijeme();
  if ((rtcVrijeme.second() % 6) != 0) {
    return;
  }

  const int cilj = izracunajCiljnuPoziciju(rtcVrijeme);
  if (stanje.plate_position == cilj) {
    return;
  }

  stanje.plate_phase = FAZA_PRVI_RELEJ;
  pocetakFazeRtcTick = rtcTick;
  aktivirajRelejePoFazi(stanje);
  UnifiedMotionStateStore::spremiAkoPromjena(stanje);
  posaljiPCLog(F("Ploca: start po RTC SQW taktu"));
  UnifiedMotionStateStore::logirajStanje(stanje);
}

bool vrijemeProslo(unsigned long sada, unsigned long cilj) {
  return static_cast<long>(sada - cilj) >= 0;
}

bool izracunajTerminCitanjaCavala(const EepromLayout::UnifiedMotionState& stanje,
                                  const DateTime& now,
                                  int& minutaTerminaUDanu,
                                  long& protekloOdTerminaMs) {
  int satTermina = 0;
  int minutaTermina = 0;
  if (!izracunajVrijemeZaPoziciju(stanje.plate_position, satTermina, minutaTermina)) {
    return false;
  }

  // Citanje cavala namjerno kasni jednu minutu u odnosu na nominalni termin
  // pozicije ploce, kako bi npr. slot 04:59 okidao u 05:00:30.
  minutaTerminaUDanu = (satTermina * 60 + minutaTermina + 1) % 1440;
  const long sadaMsUDanu =
      static_cast<long>((now.hour() * 3600L + now.minute() * 60L + now.second()) * 1000L);
  const long terminMsUDanu =
      static_cast<long>((minutaTerminaUDanu * 60L + 30L) * 1000L);
  protekloOdTerminaMs = sadaMsUDanu - terminMsUDanu;
  return true;
}

uint32_t izracunajKljucCitanjaCavala(const DateTime& now, int minutaTerminaUDanu) {
  const uint32_t godina = static_cast<uint32_t>((now.year() >= 2000) ? (now.year() - 2000) : 0);
  return (godina << 20) |
         (static_cast<uint32_t>(now.month()) << 16) |
         (static_cast<uint32_t>(now.day()) << 11) |
         static_cast<uint32_t>(minutaTerminaUDanu);
}

bool jePlocaSpremnaZaCitanjeCavala(const EepromLayout::UnifiedMotionState& stanje,
                                   const DateTime& now) {
  return stanje.plate_phase == FAZA_STABILNO &&
         stanje.plate_position == izracunajCiljnuPoziciju(now);
}

bool jeCavaoAktivan(const bool ulazi[BROJ_ULAZA_PLOCE],
                    uint8_t brojMjestaZaCavle,
                    uint8_t cavao) {
  if (cavao < 1 || cavao > brojMjestaZaCavle || cavao > BROJ_ULAZA_PLOCE) {
    return false;
  }
  return ulazi[cavao - 1];
}

void logirajStanjaCavala(const bool ulazi[BROJ_ULAZA_PLOCE], bool jeNedjelja, uint8_t brojMjestaZaCavle) {
  char log[96];
  int duljina = snprintf(log, sizeof(log), "Cavli ploce:");
  for (uint8_t i = 0; i < brojMjestaZaCavle && i < BROJ_ULAZA_PLOCE; ++i) {
    if (duljina < 0 || duljina >= static_cast<int>(sizeof(log))) {
      break;
    }
    duljina += snprintf(log + duljina,
                        sizeof(log) - static_cast<size_t>(duljina),
                        " %u=%s",
                        i + 1,
                        ulazi[i] ? "ON" : "OFF");
  }
  if (duljina >= 0 && duljina < static_cast<int>(sizeof(log))) {
    snprintf(log + duljina,
             sizeof(log) - static_cast<size_t>(duljina),
             "%s",
             jeNedjelja ? " | nedjelja" : " | pon-sub");
  }
  posaljiPCLog(log);
}

void otkaziZakazanoSlavljenjeZbogIzvadenogCavla() {
  if (autoPosebniZakazaniNacin == POSEBNI_NACIN_SLAVLJENJE) {
    resetirajAutomatskiPosebniNacin();
    posaljiPCLog(F("Cavli: 5. cavao izvaden, zakazano slavljenje otkazano"));
  }
}

void zaustaviAktivnoSlavljenjeZbogIzvadenogCavla() {
  // 5. cavao smije zaustaviti samo slavljenje koje je pokrenula automatika ploce.
  // Rucno ili daljinski pokrenuto slavljenje ne smije pasti odmah nakon starta.
  if (autoPosebniAktivniNacin != POSEBNI_NACIN_SLAVLJENJE) {
    return;
  }

  zaustaviPosebniNacin(POSEBNI_NACIN_SLAVLJENJE);
  if (!jeSlavljenjeUTijeku()) {
    autoPosebniAktivniNacin = POSEBNI_NACIN_NONE;
    autoPosebniKraj = 0;
    posaljiPCLog(F("Cavli: 5. cavao izvaden, slavljenje zaustavljeno"));
  }
}

void otkaziZakazanoZvonoZbogIzvadenogCavla(int indeks, uint8_t cavao) {
  if (indeks < 0 || indeks >= BROJ_ZVONA_MAX || !autoZvonoZakazano[indeks]) {
    return;
  }

  autoZvonoZakazano[indeks] = false;
  autoZvonoAktivno[indeks] = false;
  autoZvonoStart[indeks] = 0;
  autoZvonoKraj[indeks] = 0;

  char log[64];
  snprintf(log,
           sizeof(log),
           "Cavli: cavao %u izvaden, zakazano ZVONO%u otkazano",
           cavao,
           indeks + 1);
  posaljiPCLog(log);
}

void zaustaviAktivnoZvonoZbogIzvadenogCavla(int indeks, uint8_t cavao) {
  if (indeks < 0 || indeks >= BROJ_ZVONA_MAX || !autoZvonoAktivno[indeks]) {
    return;
  }

  deaktivirajZvonjenje(indeks + 1);
  autoZvonoAktivno[indeks] = false;
  autoZvonoZakazano[indeks] = false;
  autoZvonoStart[indeks] = 0;
  autoZvonoKraj[indeks] = 0;

  char log[64];
  snprintf(log,
           sizeof(log),
           "Cavli: cavao %u izvaden, ZVONO%u zaustavljeno",
           cavao,
           indeks + 1);
  posaljiPCLog(log);
}

void azurirajAutomatskaZvonjenja(unsigned long sadaMs,
                                 const bool ulazi[BROJ_ULAZA_PLOCE],
                                 bool jeNedjelja,
                                 uint8_t brojMjestaZaCavle,
                                 bool cavaoSlavljenjaAktivan) {
  for (int i = 0; i < BROJ_ZVONA_MAX; ++i) {
    const uint8_t cavao = jeNedjelja ? dohvatiCavaoNedjeljaZaZvono(i + 1)
                                     : dohvatiCavaoRadniZaZvono(i + 1);
    if (jeCavaoAktivan(ulazi, brojMjestaZaCavle, cavao)) {
      continue;
    }

    otkaziZakazanoZvonoZbogIzvadenogCavla(i, cavao);
    zaustaviAktivnoZvonoZbogIzvadenogCavla(i, cavao);
  }

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

  if (!cavaoSlavljenjaAktivan) {
    otkaziZakazanoSlavljenjeZbogIzvadenogCavla();
    zaustaviAktivnoSlavljenjeZbogIzvadenogCavla();
  }

  if (autoPosebniZakazaniNacin != POSEBNI_NACIN_NONE &&
      vrijemeProslo(sadaMs, autoPosebniStart) &&
      !jeZvonoUTijeku() &&
      !jeLiInerciaAktivna()) {
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

void zaustaviAutomatikuPloceZbogUskrsneTisine() {
  for (int i = 0; i < BROJ_ZVONA_MAX; ++i) {
    if (autoZvonoAktivno[i] || autoZvonoZakazano[i]) {
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

void obradiUlazePloce(const DateTime& now,
                      unsigned long sadaMs,
                      const bool ulazi[BROJ_ULAZA_PLOCE],
                      unsigned long protekloOdTerminaMs) {
  const bool jeNedjelja = (now.dayOfTheWeek() == 0);
  const uint8_t brojMjestaZaCavle = dohvatiBrojMjestaZaCavle();
  const uint8_t brojZvona = dohvatiBrojZvona();
  const uint8_t cavaoSlavljenja = dohvatiCavaoSlavljenja();
  logirajStanjaCavala(ulazi, jeNedjelja, brojMjestaZaCavle);

  const unsigned long trajanjeZvona = jeNedjelja ? dohvatiTrajanjeZvonjenjaNedjeljaMs()
                                                 : dohvatiTrajanjeZvonjenjaRadniMs();
  const unsigned long trajanjeSlavljenja = dohvatiTrajanjeSlavljenjaMs();
  const unsigned long odgodaSlavljenjaMs =
      static_cast<unsigned long>(dohvatiOdgoduSlavljenjaSekunde()) * 1000UL;
  const bool slavljenjeAktivno = jeCavaoAktivan(ulazi, brojMjestaZaCavle, cavaoSlavljenja);

  bool imaZvono = false;
  for (uint8_t zvono = 1; zvono <= brojZvona && zvono <= BROJ_ZVONA_MAX; ++zvono) {
    const uint8_t cavao = jeNedjelja ? dohvatiCavaoNedjeljaZaZvono(zvono)
                                     : dohvatiCavaoRadniZaZvono(zvono);
    if (!jeCavaoAktivan(ulazi, brojMjestaZaCavle, cavao)) {
      continue;
    }

    if (protekloOdTerminaMs >= trajanjeZvona) {
      continue;
    }

    const unsigned long preostaloTrajanjeZvona = trajanjeZvona - protekloOdTerminaMs;

    pokreniAutomatskoZvonjenje(zvono - 1,
                               sadaMs,
                               preostaloTrajanjeZvona,
                               false);
    if (autoZvonoAktivno[zvono - 1] || autoZvonoZakazano[zvono - 1]) {
      imaZvono = true;
      char bellLog[48];
      snprintf(bellLog, sizeof(bellLog), "Cavli: aktiviran ZVONO%u preko cavla %u", zvono, cavao);
      posaljiPCLog(bellLog);
    }
  }

  const PosebniAutomatskiNacin trazeniPosebniNacin =
      slavljenjeAktivno ? POSEBNI_NACIN_SLAVLJENJE : POSEBNI_NACIN_NONE;

  if (trazeniPosebniNacin == POSEBNI_NACIN_NONE) {
    return;
  }

  const unsigned long trajanjePosebnog =
      (trazeniPosebniNacin == POSEBNI_NACIN_SLAVLJENJE) ? trajanjeSlavljenja : trajanjeZvona;

  autoPosebniZakazaniNacin = trazeniPosebniNacin;
  autoPosebniStart = sadaMs + odgodaSlavljenjaMs;
  autoPosebniTrajanje = trajanjePosebnog;

  if (imaZvono || jeZvonoUTijeku() || jeLiInerciaAktivna() || odgodaSlavljenjaMs > 0) {
    posaljiPCLog(F("Cavli: 5. cavao ceka kraj zvonjenja, inercije i odgode prije slavljenja"));
  } else {
    posaljiPCLog(F("Cavli: 5. cavao spreman za neposredno slavljenje"));
  }
}

}  // namespace

void inicijalizirajPlocu() {
  pinMode(PIN_RELEJ_PARNE_PLOCE, OUTPUT);
  pinMode(PIN_RELEJ_NEPARNE_PLOCE, OUTPUT);
  digitalWrite(PIN_RELEJ_PARNE_PLOCE, LOW);
  digitalWrite(PIN_RELEJ_NEPARNE_PLOCE, LOW);

  for (uint8_t i = 0; i < BROJ_ULAZA_PLOCE; ++i) pinMode(PIN_ULAZA_PLOCE[i], INPUT_PULLUP);

  EepromLayout::UnifiedMotionState stanje = UnifiedMotionStateStore::dohvatiIliMigriraj();
  if (stanje.plate_phase != FAZA_STABILNO) {
    // Aktivna faza iz stare sesije ne smije se vratiti direktno na releje pri bootu,
    // nego je treba odraditi ponovno punim korakom na sljedecoj 6-sekundnoj granici.
    stanje.plate_phase = FAZA_STABILNO;
    UnifiedMotionStateStore::spremiAkoPromjena(stanje);
    posaljiPCLog(F("Ploca: pronaden aktivni korak iz stare sesije, vracam u mirno stanje"));
  }
  if (jeBootRecoveryResetDetektiran()) {
    posaljiPCLog(F("Ploca: zadrzavam spremljeno stanje za boot recovery"));
  } else {
    posaljiPCLog(F("Ploca: zadrzavam spremljenu poziciju pri normalnom restartu"));
  }
  aktivirajRelejePoFazi(stanje);

  pocetakFazeRtcTick = 0;
  zadnjiObradeniRtcTick = 0;
  zadnjiKljucCitanjaCavala = 0;
  zadnjiKljucCitanjaCavalaValjan = false;
  for (uint8_t i = 0; i < BROJ_ULAZA_PLOCE; ++i) {
    stabilnaStanjaUlazaPloce[i] = false;
  }
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
  static bool prethodniTihiRezimAktivan = false;

  if (!jeVrijemePotvrdjenoZaAutomatiku()) {
    EepromLayout::UnifiedMotionState stanje = UnifiedMotionStateStore::dohvatiIliMigriraj();
    if (stanje.plate_phase != FAZA_STABILNO) {
      stanje.plate_phase = FAZA_STABILNO;
      UnifiedMotionStateStore::spremiAkoPromjena(stanje);
      posaljiPCLog(F("Ploca: automatika blokirana dok vrijeme nije potvrdeno"));
    }
    zaustaviAutomatikuPloceZbogNepotvrdenogVremena();
    zadnjiObradeniRtcTick = 0;
    zadnjiKljucCitanjaCavalaValjan = false;
    aktivirajRelejePoFazi(stanje);
    return;
  }

  const DateTime now = dohvatiTrenutnoVrijeme();
  const bool tihiRezimAktivan = jePrekidacTisineAktivan();
  const bool jeNedjelja = (now.dayOfTheWeek() == 0);
  const uint8_t brojMjestaZaCavle = dohvatiBrojMjestaZaCavle();
  bool ulaziPloce[BROJ_ULAZA_PLOCE];
  procitajStabilnaStanjaUlazaPloce(ulaziPloce);
  const bool cavaoSlavljenjaAktivan =
      jeCavaoAktivan(ulaziPloce, brojMjestaZaCavle, dohvatiCavaoSlavljenja());

  if (tihiRezimAktivan) {
    if (!prethodniTihiRezimAktivan) {
      posaljiPCLog(F("Ploca: tihi rezim aktivan, cavao-zvonjenja i posebni nacini su blokirani"));
    }
    zaustaviAutomatikuPloceZbogUskrsneTisine();
  } else if (prethodniTihiRezimAktivan) {
    posaljiPCLog(F("Ploca: tihi rezim zavrsen, cavao-zvonjenja su ponovno dozvoljena"));
  }
  prethodniTihiRezimAktivan = tihiRezimAktivan;

  azurirajAutomatskaZvonjenja(
      millis(), ulaziPloce, jeNedjelja, brojMjestaZaCavle, cavaoSlavljenjaAktivan);

  EepromLayout::UnifiedMotionState stanje = UnifiedMotionStateStore::dohvatiIliMigriraj();

  if (!jeRtcSqwAktivan()) {
    if (stanje.plate_phase != FAZA_STABILNO) {
      stanje.plate_phase = FAZA_STABILNO;
      pocetakFazeRtcTick = 0;
      UnifiedMotionStateStore::spremiAkoPromjena(stanje);
      aktivirajRelejePoFazi(stanje);
      posaljiPCLog(F("Ploca: RTC SQW nije aktivan, gasim aktivnu fazu bez pomaka"));
    }
    return;
  }

  aktivirajRelejePoFazi(stanje);

  if (rucnaBlokadaPloce) {
    if (stanje.plate_phase != FAZA_STABILNO) {
      stanje.plate_phase = FAZA_STABILNO;
      pocetakFazeRtcTick = 0;
      UnifiedMotionStateStore::spremiAkoPromjena(stanje);
      aktivirajRelejePoFazi(stanje);
    }
    zadnjiObradeniRtcTick = 0;
    zadnjiKljucCitanjaCavalaValjan = false;
    return;
  }

  const unsigned long trajanjeProzoraCitanjaMs =
      jeNedjelja ? dohvatiTrajanjeZvonjenjaNedjeljaMs() : dohvatiTrajanjeZvonjenjaRadniMs();
  int minutaTerminaUDanu = 0;
  long protekloOdTerminaMs = 0;

  if (!tihiRezimAktivan &&
      jePlocaKonfigurirana() &&
      jePlocaSpremnaZaCitanjeCavala(stanje, now) &&
      izracunajTerminCitanjaCavala(stanje, now, minutaTerminaUDanu, protekloOdTerminaMs) &&
      protekloOdTerminaMs >= 0 &&
      static_cast<unsigned long>(protekloOdTerminaMs) < trajanjeProzoraCitanjaMs) {
    const uint32_t kljucCitanja = izracunajKljucCitanjaCavala(now, minutaTerminaUDanu);
    if (!zadnjiKljucCitanjaCavalaValjan || kljucCitanja != zadnjiKljucCitanjaCavala) {
      zadnjiKljucCitanjaCavala = kljucCitanja;
      zadnjiKljucCitanjaCavalaValjan = true;
      obradiUlazePloce(now, millis(), ulaziPloce, static_cast<unsigned long>(protekloOdTerminaMs));
    }
  }

  if (stanje.plate_phase != FAZA_STABILNO) {
    obradiKorak(stanje);
    return;
  }

  pokreniKorakAkoTreba(stanje);
}

void postaviTrenutniPolozajPloce(int pozicija) {
  EepromLayout::UnifiedMotionState stanje = UnifiedMotionStateStore::dohvatiIliMigriraj();
  stanje.plate_position = static_cast<uint8_t>(constrain(pozicija, 0, BROJ_POZICIJA - 1));
  stanje.plate_phase = FAZA_STABILNO;
  pocetakFazeRtcTick = 0;
  UnifiedMotionStateStore::spremiAkoPromjena(stanje);
}

int dohvatiPozicijuPloce() {
  return UnifiedMotionStateStore::dohvatiIliMigriraj().plate_position;
}

bool jePlocaUSinkronu() {
  if (!jeVrijemePotvrdjenoZaAutomatiku()) {
    return false;
  }

  EepromLayout::UnifiedMotionState stanje = UnifiedMotionStateStore::dohvatiIliMigriraj();
  return stanje.plate_phase == FAZA_STABILNO &&
         stanje.plate_position == izracunajCiljnuPoziciju(dohvatiTrenutnoVrijeme());
}

void zatraziPoravnanjeTaktaPloce() {
  zadnjiObradeniRtcTick = 0;
}

void postaviRucnuBlokaduPloce(bool blokirano) {
  if (rucnaBlokadaPloce == blokirano) {
    return;
  }

  rucnaBlokadaPloce = blokirano;
  pocetakFazeRtcTick = 0;
  zadnjiObradeniRtcTick = 0;
  zadnjiKljucCitanjaCavalaValjan = false;

  posaljiPCLog(blokirano ? F("Ploca: ukljucena rucna blokada za namjestanje")
                         : F("Ploca: iskljucena rucna blokada"));
}

bool jeRucnaBlokadaPloceAktivna() {
  return rucnaBlokadaPloce;
}

bool mozeSeRucnoNamjestatiPloca() {
  return UnifiedMotionStateStore::dohvatiIliMigriraj().plate_phase == FAZA_STABILNO;
}
