// okretna_ploca.cpp
#include <Arduino.h>
#include <RTClib.h>
#include "okretna_ploca.h"
#include "podesavanja_piny.h"
#include "time_glob.h"
#include "lcd_display.h"
#include "zvonjenje.h"
#include "postavke.h"
#include "eeprom_konstante.h"
#include "wear_leveling.h"
#include "pc_serial.h"   

const unsigned long POLA_OKRETA_MS = 6000UL;
const int MAKS_PAMETNI_POMAK_MINUTA = 15; // maksimalno za čekanje umjesto rotacije
const int MAKS_OFFSET_MINUTA = 14;        // offset mora ostati u okviru 0-14 kako bi modulo provjera uspjela
const int MINUTA_U_DANU = 24 * 60;

static unsigned long vrijemeStarta = 0;
static bool ciklusUTijeku = false;
static bool drugaFaza = false;
static int zadnjaAktiviranaMinuta = -1;

static const uint8_t BROJ_ULAZA_PLOCE = 5;
static const uint8_t PIN_ULAZA_PLOCE[BROJ_ULAZA_PLOCE] = {
  PIN_PLOCA_ULAZ_1,
  PIN_PLOCA_ULAZ_2,
  PIN_PLOCA_ULAZ_3,
  PIN_PLOCA_ULAZ_4,
  PIN_PLOCA_ULAZ_5
};

static int zadnjiSlotUlaza = -1;
static bool autoZvonoAktivno[2] = {false, false};
static unsigned long autoZvonoKraj[2] = {0, 0};
static bool autoSlavljenjeZakazano = false;
static unsigned long autoSlavljenjeStart = 0;
static unsigned long autoSlavljenjeTrajanje = 0;
static bool autoSlavljenjeAktivno = false;
static unsigned long autoSlavljenjeKraj = 0;
static bool plocaAktivnaRanije = true;

int pozicijaPloce = 0; // 0-63
int offsetMinuta = 14; // podesivo u postavkama, 0–14

// -------------------- POMOĆNE FUNKCIJE --------------------

static int izracunajCiljnuPoziciju(const DateTime& now) {
  if (!jePlocaKonfigurirana()) return pozicijaPloce;
  int ukupnoMinuta = now.hour() * 60 + now.minute();
  int pocetak = (dohvatiPocetakPloceMinute() + offsetMinuta) % MINUTA_U_DANU;
  int diff = ukupnoMinuta - pocetak;
  if (diff < 0) diff += MINUTA_U_DANU;
  int pozicija = diff / 15;
  if (pozicija > 63) pozicija = 63;
  if (pozicija < 0)  pozicija = 0;
  return pozicija;
}

static bool jeVrijemeUPlocnomIntervalu(const DateTime& now) {
  if (!jePlocaKonfigurirana()) return false;

  int minutaDana = now.hour() * 60 + now.minute();
  int pocetak = dohvatiPocetakPloceMinute();
  int kraj = dohvatiKrajPloceMinute();

  if (pocetak == kraj) return true; // puni dan osim ako je oba nula (pokrito je gore)

  if (pocetak < kraj) {
    return minutaDana >= pocetak && minutaDana <= kraj;
  }

  return minutaDana >= pocetak || minutaDana <= kraj;
}

static int izracunajBrojKorakaNaprijed(int trenutnaPozicija, int ciljnaPozicija) {
  int razlika = (ciljnaPozicija - trenutnaPozicija) % 64;
  if (razlika < 0) razlika += 64;
  return razlika;
}

static void pokreniPrvuFazuPloce() {
  digitalWrite(PIN_RELEJ_PARNE_PLOCE, HIGH);
  digitalWrite(PIN_RELEJ_NEPARNE_PLOCE, LOW);
  vrijemeStarta = millis();
  ciklusUTijeku = true;
  drugaFaza = false;
}

static void zavrsiCiklusPloce() {
  digitalWrite(PIN_RELEJ_PARNE_PLOCE, LOW);
  digitalWrite(PIN_RELEJ_NEPARNE_PLOCE, LOW);
  ciklusUTijeku = false;
  drugaFaza = false;
  pozicijaPloce = (pozicijaPloce + 1) % 64;
  WearLeveling::spremi(EepromLayout::BAZA_POZICIJA_PLOCE,
                       EepromLayout::SLOTOVI_POZICIJA_PLOCE,
                       pozicijaPloce);

  String log = F("Ploca: zavrsen jedan korak, nova pozicija=");
  log += pozicijaPloce;
  posaljiPCLog(log);
}

static void odradiJedanKorakPloceBlokirajuci() {
  digitalWrite(PIN_RELEJ_PARNE_PLOCE, HIGH);
  odradiPauzuSaLCD(POLA_OKRETA_MS);
  digitalWrite(PIN_RELEJ_PARNE_PLOCE, LOW);
  odradiPauzuSaLCD(200);
  digitalWrite(PIN_RELEJ_NEPARNE_PLOCE, HIGH);
  odradiPauzuSaLCD(POLA_OKRETA_MS);
  digitalWrite(PIN_RELEJ_NEPARNE_PLOCE, LOW);
  odradiPauzuSaLCD(400);
  pozicijaPloce = (pozicijaPloce + 1) % 64;
  WearLeveling::spremi(EepromLayout::BAZA_POZICIJA_PLOCE,
                       EepromLayout::SLOTOVI_POZICIJA_PLOCE,
                       pozicijaPloce);

  String log = F("Ploca (blokirajuci): odradjen korak, pozicija=");
  log += pozicijaPloce;
  posaljiPCLog(log);
}

static bool vrijemeProslo(unsigned long sada, unsigned long cilj) {
  return static_cast<long>(sada - cilj) >= 0;
}

// -------------------- AUTO ZVONA / SLAVLJENJE --------------------

static void azurirajAutomatskaZvonjenja(unsigned long sadaMs) {
  // završetak auto zvona
  for (int i = 0; i < 2; ++i) {
    if (autoZvonoAktivno[i] && vrijemeProslo(sadaMs, autoZvonoKraj[i])) {
      deaktivirajZvonjenje(i + 1);
      autoZvonoAktivno[i] = false;
      String log = F("Ploca auto-zvono: zvono ");
      log += (i + 1);
      log += F(" zaustavljeno");
      posaljiPCLog(log);
    }
  }

  // sinkronizacija stanja slavljenja
  if (autoSlavljenjeAktivno && !jeSlavljenjeUTijeku()) {
    autoSlavljenjeAktivno = false;
    posaljiPCLog(F("Ploca auto-slavljenje: otkriveno da slavljenje vise ne traje (vanjski stop)."));
  }

  // start zakazanog slavljenja
  if (autoSlavljenjeZakazano && vrijemeProslo(sadaMs, autoSlavljenjeStart)) {
    autoSlavljenjeZakazano = false;
    if (!jeSlavljenjeUTijeku()) {
      zapocniSlavljenje();
      autoSlavljenjeAktivno = true;
      autoSlavljenjeKraj = sadaMs + autoSlavljenjeTrajanje;

      String log = F("Ploca auto-slavljenje: pokretanje zakazanog slavljenja, trajanje(ms)=");
      log += autoSlavljenjeTrajanje;
      posaljiPCLog(log);
    } else if (autoSlavljenjeAktivno) {
      autoSlavljenjeKraj = sadaMs + autoSlavljenjeTrajanje;

      String log = F("Ploca auto-slavljenje: produzenje trajanja zbog vec aktivnog slavljenja, novo trajanje(ms)=");
      log += autoSlavljenjeTrajanje;
      posaljiPCLog(log);
    }
  }

  // završetak slavljenja
  if (autoSlavljenjeAktivno && vrijemeProslo(sadaMs, autoSlavljenjeKraj)) {
    zaustaviSlavljenje();
    autoSlavljenjeAktivno = false;
    posaljiPCLog(F("Ploca auto-slavljenje: zaustavljeno po isteku vremena."));
  }
}

static void pokreniAutomatskoZvonjenje(int index, unsigned long sadaMs, unsigned long trajanjeMs) {
  if (index < 0 || index > 1) return;
  if (!autoZvonoAktivno[index]) {
    aktivirajZvonjenje(index + 1);
    String log = F("Ploca auto-zvono: pokretanje zvona ");
    log += (index + 1);
    log += F(", trajanje(ms)=");
    log += trajanjeMs;
    posaljiPCLog(log);
  }
  autoZvonoAktivno[index] = true;
  autoZvonoKraj[index] = sadaMs + trajanjeMs;
}

static void zakaziSlavljenje(unsigned long startMs, unsigned long trajanjeMs) {
  autoSlavljenjeZakazano = true;
  autoSlavljenjeStart = startMs;
  autoSlavljenjeTrajanje = trajanjeMs;

  String log = F("Ploca auto-slavljenje: zakazano odg adjeno slavljenje, startMs=");
  log += startMs;
  log += F(" trajanje(ms)=");
  log += trajanjeMs;
  posaljiPCLog(log);
}

static void pokreniSlavljenjeOdmah(unsigned long sadaMs, unsigned long trajanjeMs) {
  autoSlavljenjeZakazano = false;
  if (!jeSlavljenjeUTijeku()) {
    zapocniSlavljenje();
    autoSlavljenjeAktivno = true;
    autoSlavljenjeTrajanje = trajanjeMs;
    autoSlavljenjeKraj = sadaMs + trajanjeMs;

    String log = F("Ploca auto-slavljenje: pokrenuto odmah, trajanje(ms)=");
    log += trajanjeMs;
    posaljiPCLog(log);
  } else if (autoSlavljenjeAktivno) {
    autoSlavljenjeTrajanje = trajanjeMs;
    autoSlavljenjeKraj = sadaMs + trajanjeMs;

    String log = F("Ploca auto-slavljenje: vec aktivno, produzeno trajanje(ms)=");
    log += trajanjeMs;
    posaljiPCLog(log);
  }
}

// -------------------- OBRADA ULAZA PLOČE --------------------

static void obradiUlazePloce(const DateTime& now, unsigned long sadaMs) {
  bool ulaziAktivni[BROJ_ULAZA_PLOCE];
  for (uint8_t i = 0; i < BROJ_ULAZA_PLOCE; ++i) {
    ulaziAktivni[i] = (digitalRead(PIN_ULAZA_PLOCE[i]) == LOW);
  }

  String dbg = F("Ploca ulazi: ");
  for (uint8_t i = 0; i < BROJ_ULAZA_PLOCE; ++i) {
    dbg += ulaziAktivni[i] ? '1' : '0';
  }
  dbg += F(" (sat=");
  dbg += now.hour();
  dbg += F(" min=");
  dbg += now.minute();
  dbg += F(")");
  posaljiPCLog(dbg);

  bool jeNedjelja = now.dayOfTheWeek() == 0;
  bool pokreniZvono1 = jeNedjelja ? ulaziAktivni[2] : ulaziAktivni[0];
  bool pokreniZvono2 = jeNedjelja ? ulaziAktivni[3] : ulaziAktivni[1];
  bool pokreniSlavljenjePin = ulaziAktivni[4];

  unsigned long trajanjeMs = jeNedjelja ? dohvatiTrajanjeZvonjenjaNedjeljaMs()
                                        : dohvatiTrajanjeZvonjenjaRadniMs();
  if (trajanjeMs == 0) {
    trajanjeMs = jeNedjelja ? 180000UL : 120000UL;
  }
  unsigned long trajanjeSlavljenja = dohvatiTrajanjeSlavljenjaMs();
  if (trajanjeSlavljenja == 0) {
    trajanjeSlavljenja = 120000UL;
  }

  bool imaZvono = false;
  if (pokreniZvono1) {
    pokreniAutomatskoZvonjenje(0, sadaMs, trajanjeMs);
    imaZvono = true;
  }
  if (pokreniZvono2) {
    pokreniAutomatskoZvonjenje(1, sadaMs, trajanjeMs);
    imaZvono = true;
  }

  if (pokreniSlavljenjePin) {
    if (imaZvono) {
      unsigned long pocetak = sadaMs + trajanjeMs;
      for (int i = 0; i < 2; ++i) {
        if (autoZvonoAktivno[i] && autoZvonoKraj[i] > pocetak) {
          pocetak = autoZvonoKraj[i];
        }
      }
      zakaziSlavljenje(pocetak, trajanjeSlavljenja);
    } else {
      pokreniSlavljenjeOdmah(sadaMs, trajanjeSlavljenja);
    }
  }
}

// -------------------- JAVNE FUNKCIJE --------------------

void inicijalizirajPlocu() {
  pinMode(PIN_RELEJ_PARNE_PLOCE, OUTPUT);
  pinMode(PIN_RELEJ_NEPARNE_PLOCE, OUTPUT);
  digitalWrite(PIN_RELEJ_PARNE_PLOCE, LOW);
  digitalWrite(PIN_RELEJ_NEPARNE_PLOCE, LOW);

  for (uint8_t i = 0; i < BROJ_ULAZA_PLOCE; ++i) {
    pinMode(PIN_ULAZA_PLOCE[i], INPUT_PULLUP);
  }

  if (!WearLeveling::ucitaj(EepromLayout::BAZA_POZICIJA_PLOCE,
                            EepromLayout::SLOTOVI_POZICIJA_PLOCE,
                            pozicijaPloce)) {
    pozicijaPloce = 0;
  }
  if (pozicijaPloce < 0 || pozicijaPloce > 63) pozicijaPloce = 0;

  if (!WearLeveling::ucitaj(EepromLayout::BAZA_OFFSET_MINUTA,
                            EepromLayout::SLOTOVI_OFFSET_MINUTA,
                            offsetMinuta)) {
    offsetMinuta = MAKS_OFFSET_MINUTA;
  }
  if (offsetMinuta < 0 || offsetMinuta > MAKS_OFFSET_MINUTA) {
    offsetMinuta = MAKS_OFFSET_MINUTA;
  }

  vrijemeStarta = 0;
  ciklusUTijeku = false;
  drugaFaza = false;
  zadnjaAktiviranaMinuta = -1;
  zadnjiSlotUlaza = -1;
  for (uint8_t i = 0; i < 2; ++i) {
    autoZvonoAktivno[i] = false;
    autoZvonoKraj[i] = 0;
  }
  autoSlavljenjeZakazano = false;
  autoSlavljenjeStart = 0;
  autoSlavljenjeTrajanje = 0;
  autoSlavljenjeAktivno = false;
  autoSlavljenjeKraj = 0;

  String log = F("Ploca init: pozicija=");
  log += pozicijaPloce;
  log += F(" offsetMin=");
  log += offsetMinuta;
  posaljiPCLog(log);
}

void upravljajPlocom() {
  DateTime now = dohvatiTrenutnoVrijeme();
  unsigned long sadaMs = millis();

  azurirajAutomatskaZvonjenja(sadaMs);

  bool plocaAktivna = jeVrijemeUPlocnomIntervalu(now);
  if (!plocaAktivna) {
    if (ciklusUTijeku) {
      digitalWrite(PIN_RELEJ_PARNE_PLOCE, LOW);
      digitalWrite(PIN_RELEJ_NEPARNE_PLOCE, LOW);
      ciklusUTijeku = false;
      drugaFaza = false;
      posaljiPCLog(F("Ploca: zaustavljena jer je izvan dozvoljenog termina."));
    } else if (plocaAktivnaRanije) {
      posaljiPCLog(F("Ploca: onemogucena (raspon 00:00-00:00) ili izvan zadanog intervala."));
    }
    plocaAktivnaRanije = false;
    zadnjiSlotUlaza = -1;
    return;
  }

  if (!plocaAktivnaRanije) {
    posaljiPCLog(F("Ploca: ponovno aktivna u dozvoljenom intervalu."));
  }
  plocaAktivnaRanije = true;

  // svakih 15 min, nakon 30. sekunde, očitaj ploču (čavle)
  if (now.minute() % 15 == 0 && now.second() >= 30) {
    int slot = now.hour() * 4 + (now.minute() / 15);
    if (slot != zadnjiSlotUlaza) {
      zadnjiSlotUlaza = slot;
      posaljiPCLog(String(F("Ploca: obrada ulaza za slot=")) + slot);
      obradiUlazePloce(now, sadaMs);
    }
  }

  int minuta = now.minute();

  int cilj = izracunajCiljnuPoziciju(now);
  int razlika = cilj - pozicijaPloce;
  if (razlika < 0) razlika += 64;

  // STARI UVJET je imao now.second() <= 1, što je bilo nesigurno
  // NOVI, ROBUSTNIJI UVJET: reagiramo na novu minutu s odgovarajućim offsetom
  bool trebaPokrenuti =
      (!ciklusUTijeku &&
       minuta != zadnjaAktiviranaMinuta &&
       (minuta % 15 == offsetMinuta) &&
       razlika > 0);

  if (trebaPokrenuti) {
    String log = F("Ploca: pokretanje koraka – sat=");
    log += now.hour();
    log += F(" min=");
    log += minuta;
    log += F(" pozicija=");
    log += pozicijaPloce;
    log += F(" cilj=");
    log += cilj;
    log += F(" razlika=");
    log += razlika;
    posaljiPCLog(log);

    pokreniPrvuFazuPloce();
    zadnjaAktiviranaMinuta = minuta;
  }

  if (!ciklusUTijeku) return;

  unsigned long proteklo = millis() - vrijemeStarta;
  if (!drugaFaza && proteklo >= POLA_OKRETA_MS) {
    digitalWrite(PIN_RELEJ_PARNE_PLOCE, LOW);
    digitalWrite(PIN_RELEJ_NEPARNE_PLOCE, HIGH);
    vrijemeStarta = millis();
    drugaFaza = true;
    posaljiPCLog(F("Ploca: faza 2 koraka (druga polovica okreta)."));
  } else if (drugaFaza && proteklo >= POLA_OKRETA_MS) {
    zavrsiCiklusPloce();
  }
}

void postaviTrenutniPolozajPloce(int pozicija) {
  pozicijaPloce = constrain(pozicija, 0, 63);
  WearLeveling::spremi(EepromLayout::BAZA_POZICIJA_PLOCE,
                       EepromLayout::SLOTOVI_POZICIJA_PLOCE,
                       pozicijaPloce);

  zadnjaAktiviranaMinuta = -1; // dopusti odmah ručnu korekciju ako je potrebno

  String log = F("Ploca: rucno postavljena pozicija=");
  log += pozicijaPloce;
  posaljiPCLog(log);
}

void postaviOffsetMinuta(int offset) {
  offsetMinuta = constrain(offset, 0, MAKS_OFFSET_MINUTA);
  WearLeveling::spremi(EepromLayout::BAZA_OFFSET_MINUTA,
                       EepromLayout::SLOTOVI_OFFSET_MINUTA,
                       offsetMinuta);

  String log = F("Ploca: postavljen offsetMinuta=");
  log += offsetMinuta;
  posaljiPCLog(log);
}

int dohvatiPozicijuPloce() {
  return pozicijaPloce;
}

int dohvatiOffsetMinuta() {
  return offsetMinuta;
}

bool jePlocaUSinkronu() {
  if (!jePlocaKonfigurirana()) return true;
  DateTime now = dohvatiTrenutnoVrijeme();
  bool sink = (pozicijaPloce == izracunajCiljnuPoziciju(now));
  return sink;
}

void kompenzirajPlocu(bool pametniMod) {
  if (!jePlocaKonfigurirana()) {
    posaljiPCLog(F("Ploca kompenzacija: ploca onemogucena postavkama."));
    return;
  }
  DateTime now = dohvatiTrenutnoVrijeme();
  int ciljPozicija = izracunajCiljnuPoziciju(now);
  int razlika = izracunajBrojKorakaNaprijed(pozicijaPloce, ciljPozicija);

  String log = F("Ploca kompenzacija: poz=");
  log += pozicijaPloce;
  log += F(" cilj=");
  log += ciljPozicija;
  log += F(" razlika=");
  log += razlika;
  log += F(" pametniMod=");
  log += pametniMod ? '1' : '0';
  posaljiPCLog(log);

  if (pametniMod && razlika <= MAKS_PAMETNI_POMAK_MINUTA) {
    posaljiPCLog(F("Ploca kompenzacija: pametniMod aktivan, razlika mala – cekam prirodno dovodenje."));
    return; // čekaj sljedeći impuls ako je blizu
  }

  if (razlika == 0) {
    zadnjaAktiviranaMinuta = now.minute();
    ciklusUTijeku = false;
    drugaFaza = false;
    posaljiPCLog(F("Ploca kompenzacija: vec u sinkronu, nema rotacije."));
    return;
  }

  for (int i = 0; i < razlika; i++) {
    odradiJedanKorakPloceBlokirajuci();
  }

  pozicijaPloce = ciljPozicija;
  WearLeveling::spremi(EepromLayout::BAZA_POZICIJA_PLOCE,
                       EepromLayout::SLOTOVI_POZICIJA_PLOCE,
                       pozicijaPloce);

  zadnjaAktiviranaMinuta = now.minute();
  ciklusUTijeku = false;
  drugaFaza = false;

  log = F("Ploca kompenzacija: zavrsena, nova pozicija=");
  log += pozicijaPloce;
  posaljiPCLog(log);
}

void oznaciPlocuKaoSinkroniziranu() {
  DateTime now = dohvatiTrenutnoVrijeme();
  zadnjaAktiviranaMinuta = now.minute();
  posaljiPCLog(F("Ploca: oznacena kao sinkronizirana."));
}