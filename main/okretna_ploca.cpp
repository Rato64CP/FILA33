// okretna_ploca.cpp – Complete rotating plate control system
// 64 positions covering 15-minute intervals in operating window (04:59-20:44)
// Dual relay sequencing with wear-leveling EEPROM persistence
// Mechanical cam input processing for automatic bells/celebrations

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
#include "watchdog.h"

// ==================== CONSTANTS ====================

const unsigned long FAZA_TRAJANJE_MS = 3000UL;    // Each phase is 3 seconds

const int BROJ_POZICIJA = 64;                      // 64 positions total (0-63)
const int VRIJEME_POCETKA_OPERACIJE_ZADANO = 299;  // 04:59 in minutes from midnight
const int VRIJEME_KRAJA_OPERACIJE_ZADANO = 1244;   // 20:44 in minutes from midnight
const int POZICIJA_NOCI = 63;                      // Night position (locked)
const int MINUTNI_BLOK = 15;                       // Each position covers 15 minutes

const int MAKS_PAMETNI_POMAK_KORAKA = 1;

// ==================== GLOBAL STATE VARIABLES ====================

int pozicijaPloce = 0;
int offsetMinuta = 14;

static unsigned long vrijemeStarta = 0;           
static bool ciklusUTijeku = false;                
static bool drugaFaza = false;                    
static int zadnjaAktiviranaMinuta = -1;           

static const uint8_t BROJ_ULAZA_PLOCE = 5;
static const uint8_t PIN_ULAZA_PLOCE[BROJ_ULAZA_PLOCE] = {
  PIN_ULAZA_PLOCE_1,
  PIN_ULAZA_PLOCE_2,
  PIN_ULAZA_PLOCE_3,
  PIN_ULAZA_PLOCE_4,
  PIN_ULAZA_PLOCE_5
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

enum MarkerFazePloce {
  MARKER_PLOCA_NEMA_AKTIVNOG_KORAKA = 0,
  MARKER_PLOCA_PRIJE_FAZE_1 = 1,
  MARKER_PLOCA_FAZA_1_U_TIJEKU = 2,
  MARKER_PLOCA_IZMEDU_FAZA = 3,
  MARKER_PLOCA_FAZA_2_U_TIJEKU = 4,
  MARKER_PLOCA_NAKON_FAZE_2 = 5
};

// ==================== HELPER FUNCTIONS ====================

static int dohvatiPocetakOperacijeMinute()
{
  int pocetak = dohvatiPocetakPloceMinute();
  if (pocetak < 0 || pocetak > 1439) {
    return VRIJEME_POCETKA_OPERACIJE_ZADANO;
  }
  return pocetak;
}

static int dohvatiKrajOperacijeMinute()
{
  int kraj = dohvatiKrajPloceMinute();
  if (kraj < 0 || kraj > 1439) {
    return VRIJEME_KRAJA_OPERACIJE_ZADANO;
  }
  return kraj;
}

// Calculate expected plate position based on RTC time
static int izracunajCiljnuPoziciju(const DateTime& now)
{
  if (!jePlocaKonfigurirana()) {
    return pozicijaPloce;
  }
  
  int ukupnoMinuta = now.hour() * 60 + now.minute();
  int pocetak = dohvatiPocetakOperacijeMinute();
  int diff = ukupnoMinuta - pocetak;
  
  if (diff < 0) {
    return POZICIJA_NOCI;
  }
  
  int pozicija = diff / MINUTNI_BLOK;
  
  if (pozicija > POZICIJA_NOCI) {
    pozicija = POZICIJA_NOCI;
  }
  if (pozicija < 0) {
    pozicija = 0;
  }
  
  return pozicija;
}

// Check if current time is within operating window (04:59-20:44)
static bool jeVrijemeUPlocnomIntervalu(const DateTime& now)
{
  if (!jePlocaKonfigurirana()) {
    return false;
  }
  
  int minutaDana = now.hour() * 60 + now.minute();
  int pocetak = dohvatiPocetakOperacijeMinute();
  int kraj = dohvatiKrajOperacijeMinute();
  
  return (minutaDana >= pocetak && minutaDana <= kraj);
}

// Calculate number of steps needed to move from current to target position
static int izracunajBrojKorakaNaprijed(int trenutnaPozicija, int ciljnaPozicija)
{
  int razlika = (ciljnaPozicija - trenutnaPozicija) % BROJ_POZICIJA;
  
  if (razlika < 0) {
    razlika += BROJ_POZICIJA;
  }
  
  return razlika;
}

static void spremiMarkerFazePloce(int marker)
{
  WearLeveling::spremi(EepromLayout::BAZA_MARKER_FAZE_PLOCE,
                       EepromLayout::SLOTOVI_MARKER_FAZE_PLOCE,
                       marker);
}

static int ucitajMarkerFazePloce()
{
  int marker = MARKER_PLOCA_NEMA_AKTIVNOG_KORAKA;
  if (!WearLeveling::ucitaj(EepromLayout::BAZA_MARKER_FAZE_PLOCE,
                            EepromLayout::SLOTOVI_MARKER_FAZE_PLOCE,
                            marker)) {
    return MARKER_PLOCA_NEMA_AKTIVNOG_KORAKA;
  }
  if (marker < MARKER_PLOCA_NEMA_AKTIVNOG_KORAKA || marker > MARKER_PLOCA_NAKON_FAZE_2) {
    return MARKER_PLOCA_NEMA_AKTIVNOG_KORAKA;
  }
  return marker;
}

static void oporaviNedovrseniKorakPloce()
{
  int marker = ucitajMarkerFazePloce();
  if (marker == MARKER_PLOCA_NEMA_AKTIVNOG_KORAKA) {
    return;
  }

  digitalWrite(PIN_RELEJ_PARNE_PLOCE, LOW);
  digitalWrite(PIN_RELEJ_NEPARNE_PLOCE, LOW);
  ciklusUTijeku = false;
  drugaFaza = false;

  String log = F("Ploca oporavak: marker=");
  log += marker;
  log += F(" -> ");

  if (marker == MARKER_PLOCA_PRIJE_FAZE_1) {
    log += F("korak nije zapocet, ostaje pozicija");
  } else if (marker == MARKER_PLOCA_FAZA_1_U_TIJEKU || marker == MARKER_PLOCA_IZMEDU_FAZA) {
    log += F("prekid prije faze 2, vracam na pocetnu poziciju");
  } else if (marker == MARKER_PLOCA_FAZA_2_U_TIJEKU || marker == MARKER_PLOCA_NAKON_FAZE_2) {
    pozicijaPloce = (pozicijaPloce + 1) % BROJ_POZICIJA;
    WearLeveling::spremi(EepromLayout::BAZA_POZICIJA_PLOCE,
                         EepromLayout::SLOTOVI_POZICIJA_PLOCE,
                         pozicijaPloce);
    log += F("faza 2 zapoceta/zavrsena, potvrdujem sljedecu poziciju=");
    log += pozicijaPloce;
  }

  spremiMarkerFazePloce(MARKER_PLOCA_NEMA_AKTIVNOG_KORAKA);
  posaljiPCLog(log);
}

// Activate first phase of plate rotation
static void pokreniPrvuFazuPloce()
{
  spremiMarkerFazePloce(MARKER_PLOCA_PRIJE_FAZE_1);
  WearLeveling::spremi(EepromLayout::BAZA_POZICIJA_PLOCE,
                       EepromLayout::SLOTOVI_POZICIJA_PLOCE,
                       pozicijaPloce);

  spremiMarkerFazePloce(MARKER_PLOCA_FAZA_1_U_TIJEKU);
  digitalWrite(PIN_RELEJ_PARNE_PLOCE, HIGH);
  digitalWrite(PIN_RELEJ_NEPARNE_PLOCE, LOW);
  
  vrijemeStarta = millis();
  ciklusUTijeku = true;
  drugaFaza = false;
  
  String log = F("Ploca: prva faza, pozicija=");
  log += pozicijaPloce;
  posaljiPCLog(log);
}

// Complete plate rotation step
static void zavrsiCiklusPloce()
{
  spremiMarkerFazePloce(MARKER_PLOCA_NAKON_FAZE_2);

  digitalWrite(PIN_RELEJ_PARNE_PLOCE, LOW);
  digitalWrite(PIN_RELEJ_NEPARNE_PLOCE, LOW);
  ciklusUTijeku = false;
  drugaFaza = false;
  
  pozicijaPloce = (pozicijaPloce + 1) % BROJ_POZICIJA;
  WearLeveling::spremi(EepromLayout::BAZA_POZICIJA_PLOCE,
                       EepromLayout::SLOTOVI_POZICIJA_PLOCE,
                       pozicijaPloce);
  spremiMarkerFazePloce(MARKER_PLOCA_NEMA_AKTIVNOG_KORAKA);
  
  String log = F("Ploca: korak zavrsen, pozicija=");
  log += pozicijaPloce;
  posaljiPCLog(log);
}

// Execute one complete rotation step in blocking mode
static void odradiJedanKorakPloceBlokirajuci()
{
  spremiMarkerFazePloce(MARKER_PLOCA_PRIJE_FAZE_1);
  WearLeveling::spremi(EepromLayout::BAZA_POZICIJA_PLOCE,
                       EepromLayout::SLOTOVI_POZICIJA_PLOCE,
                       pozicijaPloce);

  // FIRST PHASE
  spremiMarkerFazePloce(MARKER_PLOCA_FAZA_1_U_TIJEKU);
  digitalWrite(PIN_RELEJ_PARNE_PLOCE, HIGH);
  digitalWrite(PIN_RELEJ_NEPARNE_PLOCE, LOW);
  
  String logPhase1 = F("Ploca (blok): faza 1");
  posaljiPCLog(logPhase1);
  
  odradiPauzuSaLCD(FAZA_TRAJANJE_MS);
  
  spremiMarkerFazePloce(MARKER_PLOCA_IZMEDU_FAZA);
  digitalWrite(PIN_RELEJ_PARNE_PLOCE, LOW);
  odradiPauzuSaLCD(200);
  osvjeziWatchdog();
  
  // SECOND PHASE
  spremiMarkerFazePloce(MARKER_PLOCA_FAZA_2_U_TIJEKU);
  digitalWrite(PIN_RELEJ_NEPARNE_PLOCE, HIGH);
  
  String logPhase2 = F("Ploca (blok): faza 2");
  posaljiPCLog(logPhase2);
  
  odradiPauzuSaLCD(FAZA_TRAJANJE_MS);
  osvjeziWatchdog();
  
  spremiMarkerFazePloce(MARKER_PLOCA_NAKON_FAZE_2);
  digitalWrite(PIN_RELEJ_PARNE_PLOCE, LOW);
  digitalWrite(PIN_RELEJ_NEPARNE_PLOCE, LOW);
  
  odradiPauzuSaLCD(400);
  osvjeziWatchdog();
  
  pozicijaPloce = (pozicijaPloce + 1) % BROJ_POZICIJA;
  WearLeveling::spremi(EepromLayout::BAZA_POZICIJA_PLOCE,
                       EepromLayout::SLOTOVI_POZICIJA_PLOCE,
                       pozicijaPloce);
  spremiMarkerFazePloce(MARKER_PLOCA_NEMA_AKTIVNOG_KORAKA);
  
  String logComplete = F("Ploca (blok): korak zavrsen");
  posaljiPCLog(logComplete);
}

// Check if enough time has passed
static bool vrijemeProslo(unsigned long sada, unsigned long cilj)
{
  return static_cast<long>(sada - cilj) >= 0;
}

// ==================== AUTOMATIC BELL/HAMMER TRIGGERING ====================

static void azurirajAutomatskaZvonjenja(unsigned long sadaMs)
{
  for (int i = 0; i < 2; ++i) {
    if (autoZvonoAktivno[i] && vrijemeProslo(sadaMs, autoZvonoKraj[i])) {
      deaktivirajZvonjenje(i + 1);
      autoZvonoAktivno[i] = false;
      
      String log = F("Ploca auto-zvono: zaustavljeno");
      posaljiPCLog(log);
    }
  }
  
  if (autoSlavljenjeAktivno && !jeSlavljenjeUTijeku()) {
    autoSlavljenjeAktivno = false;
    String log = F("Ploca auto-slavljenje: zaustavljeno");
    posaljiPCLog(log);
  }
  
  if (autoSlavljenjeZakazano && vrijemeProslo(sadaMs, autoSlavljenjeStart)) {
    autoSlavljenjeZakazano = false;
    
    if (!jeSlavljenjeUTijeku()) {
      zapocniSlavljenje();
      autoSlavljenjeAktivno = true;
      autoSlavljenjeKraj = sadaMs + autoSlavljenjeTrajanje;
      
      String log = F("Ploca auto-slavljenje: pokrenuto");
      posaljiPCLog(log);
    }
  }
  
  if (autoSlavljenjeAktivno && vrijemeProslo(sadaMs, autoSlavljenjeKraj)) {
    zaustaviSlavljenje();
    autoSlavljenjeAktivno = false;
    
    String log = F("Ploca auto-slavljenje: zaustavljeno");
    posaljiPCLog(log);
  }
}

static void pokreniAutomatskoZvonjenje(int index, unsigned long sadaMs, unsigned long trajanjeMs)
{
  if (index < 0 || index > 1) {
    return;
  }
  
  if (!autoZvonoAktivno[index]) {
    aktivirajZvonjenje(index + 1);
    
    String log = F("Ploca auto-zvono: pokrenuto");
    posaljiPCLog(log);
  }
  
  autoZvonoAktivno[index] = true;
  autoZvonoKraj[index] = sadaMs + trajanjeMs;
}

static void zakaziSlavljenje(unsigned long startMs, unsigned long trajanjeMs)
{
  autoSlavljenjeZakazano = true;
  autoSlavljenjeStart = startMs;
  autoSlavljenjeTrajanje = trajanjeMs;
  
  String log = F("Ploca auto-slavljenje: zakazano");
  posaljiPCLog(log);
}

static void pokreniSlavljenjeOdmah(unsigned long sadaMs, unsigned long trajanjeMs)
{
  autoSlavljenjeZakazano = false;
  
  if (!jeSlavljenjeUTijeku()) {
    zapocniSlavljenje();
    autoSlavljenjeAktivno = true;
    autoSlavljenjeTrajanje = trajanjeMs;
    autoSlavljenjeKraj = sadaMs + trajanjeMs;
    
    String log = F("Ploca auto-slavljenje: odmah");
    posaljiPCLog(log);
  }
}

// ==================== MECHANICAL INPUT PROCESSING ====================

static void obradiUlazePloce(const DateTime& now, unsigned long sadaMs)
{
  bool ulaziAktivni[BROJ_ULAZA_PLOCE];
  for (uint8_t i = 0; i < BROJ_ULAZA_PLOCE; ++i) {
    ulaziAktivni[i] = (digitalRead(PIN_ULAZA_PLOCE[i]) == LOW);
  }
  
  String dbg = F("Ploca ulazi: ");
  for (uint8_t i = 0; i < BROJ_ULAZA_PLOCE; ++i) {
    dbg += ulaziAktivni[i] ? '1' : '0';
  }
  posaljiPCLog(dbg);
  
  bool jeNedjelja = (now.dayOfTheWeek() == 0);
  
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

// ==================== INITIALIZATION ====================

void inicijalizirajPlocu()
{
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
    posaljiPCLog(F("Ploca: pozicija inicijalizirana na 0"));
  } else {
    if (pozicijaPloce < 0 || pozicijaPloce >= BROJ_POZICIJA) {
      pozicijaPloce = 0;
      String log = F("Ploca: pozicija van dosega, resetirano");
      posaljiPCLog(log);
    }
  }
  
  if (!WearLeveling::ucitaj(EepromLayout::BAZA_OFFSET_MINUTA,
                            EepromLayout::SLOTOVI_OFFSET_MINUTA,
                            offsetMinuta)) {
    offsetMinuta = 14;
    posaljiPCLog(F("Ploca: offsetMinuta inicijalizirana na 14"));
  } else {
    if (offsetMinuta < 0 || offsetMinuta > 14) {
      offsetMinuta = 14;
      String log = F("Ploca: offset van dosega, resetirano");
      posaljiPCLog(log);
    }
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
  plocaAktivnaRanije = true;
  oporaviNedovrseniKorakPloce();
  
  String log = F("Ploca inicijalizirana");
  posaljiPCLog(log);
}

// ==================== MAIN OPERATION ====================

void upravljajPlocom()
{
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
      spremiMarkerFazePloce(MARKER_PLOCA_NEMA_AKTIVNOG_KORAKA);
      
      posaljiPCLog(F("Ploca: zaustavljena - izvan dozvoljenog vremena"));
    } else if (plocaAktivnaRanije) {
      posaljiPCLog(F("Ploca: onemogucena - nocni rezim"));
    }
    
    plocaAktivnaRanije = false;
    zadnjiSlotUlaza = -1;
    return;
  }
  
  if (!plocaAktivnaRanije) {
    posaljiPCLog(F("Ploca: ponovno aktivna - dnevni rezim"));
  }
  plocaAktivnaRanije = true;
  
  if (now.minute() % 15 == 0 && now.second() >= 30) {
    int slot = now.hour() * 4 + (now.minute() / 15);
    
    if (slot != zadnjiSlotUlaza) {
      zadnjiSlotUlaza = slot;
      
      String logSlot = F("Ploca: obrada ulaza");
      posaljiPCLog(logSlot);
      
      obradiUlazePloce(now, sadaMs);
    }
  }
  
  int minuta = now.minute();
  int cilj = izracunajCiljnuPoziciju(now);
  
  int razlika = izracunajBrojKorakaNaprijed(pozicijaPloce, cilj);
  
  bool trebaPokrenuti = (!ciklusUTijeku &&
                         minuta != zadnjaAktiviranaMinuta &&
                         (minuta % MINUTNI_BLOK == offsetMinuta) &&
                         razlika > 0);
  
  if (trebaPokrenuti) {
    String log = F("Ploca: pokretanje koraka");
    posaljiPCLog(log);
    
    pokreniPrvuFazuPloce();
    zadnjaAktiviranaMinuta = minuta;
  }
  
  if (!ciklusUTijeku) {
    return;
  }
  
  unsigned long proteklo = millis() - vrijemeStarta;
  
  if (!drugaFaza && proteklo >= FAZA_TRAJANJE_MS) {
    digitalWrite(PIN_RELEJ_PARNE_PLOCE, LOW);
    digitalWrite(PIN_RELEJ_NEPARNE_PLOCE, HIGH);
    spremiMarkerFazePloce(MARKER_PLOCA_IZMEDU_FAZA);
    spremiMarkerFazePloce(MARKER_PLOCA_FAZA_2_U_TIJEKU);
    
    vrijemeStarta = millis();
    drugaFaza = true;
    
    String logPhase2 = F("Ploca: faza 2");
    posaljiPCLog(logPhase2);
  }
  else if (drugaFaza && proteklo >= FAZA_TRAJANJE_MS) {
    zavrsiCiklusPloce();
  }
}

// ==================== PUBLIC API ====================

void postaviTrenutniPolozajPloce(int pozicija)
{
  pozicijaPloce = constrain(pozicija, 0, BROJ_POZICIJA - 1);
  
  WearLeveling::spremi(EepromLayout::BAZA_POZICIJA_PLOCE,
                       EepromLayout::SLOTOVI_POZICIJA_PLOCE,
                       pozicijaPloce);
  
  zadnjaAktiviranaMinuta = -1;
  
  String log = F("Ploca: postavljena pozicija=");
  log += pozicijaPloce;
  posaljiPCLog(log);
}

void postaviOffsetMinuta(int offset)
{
  offsetMinuta = constrain(offset, 0, 14);
  
  WearLeveling::spremi(EepromLayout::BAZA_OFFSET_MINUTA,
                       EepromLayout::SLOTOVI_OFFSET_MINUTA,
                       offsetMinuta);
  
  String log = F("Ploca: postavljen offset=");
  log += offsetMinuta;
  posaljiPCLog(log);
}

int dohvatiPozicijuPloce()
{
  return pozicijaPloce;
}

int dohvatiOffsetMinuta()
{
  return offsetMinuta;
}

void kompenzirajPlocu(bool pametniMod)
{
  if (!jePlocaKonfigurirana()) {
    posaljiPCLog(F("Ploca kompenzacija: onemogucena"));
    return;
  }
  
  DateTime now = dohvatiTrenutnoVrijeme();
  int ciljPozicija = izracunajCiljnuPoziciju(now);
  int razlika = izracunajBrojKorakaNaprijed(pozicijaPloce, ciljPozicija);
  
  String log = F("Ploca kompenzacija: razlika=");
  log += razlika;
  posaljiPCLog(log);
  
  if (pametniMod && razlika <= MAKS_PAMETNI_POMAK_KORAKA) {
    posaljiPCLog(F("Ploca kompenzacija: pametni mod - cekam"));
    return;
  }
  
  if (razlika == 0) {
    zadnjaAktiviranaMinuta = now.minute();
    ciklusUTijeku = false;
    drugaFaza = false;
    posaljiPCLog(F("Ploca kompenzacija: vec u sinkronu"));
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
  log = F("Ploca kompenzacija: zavrsena");
  posaljiPCLog(log);
}

bool jePlocaUSinkronu()
{
  if (!jePlocaKonfigurirana()) {
    return true;
  }
  
  DateTime now = dohvatiTrenutnoVrijeme();
  int cilj = izracunajCiljnuPoziciju(now);
  bool sink = (pozicijaPloce == cilj);
  
  return sink;
}

void oznaciPlocuKaoSinkroniziranu()
{
  DateTime now = dohvatiTrenutnoVrijeme();
  zadnjaAktiviranaMinuta = now.minute();
  
  String log = F("Ploca: oznacena kao sinkronizirana");
  posaljiPCLog(log);
}
