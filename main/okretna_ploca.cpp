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

const unsigned long FAZA_TRAJANJE_MS = 6000UL;     // Svaka faza releja traje točno 6 sekundi

const int BROJ_POZICIJA = 64;                      // 64 positions total (0-63)
const int VRIJEME_POCETKA_OPERACIJE_ZADANO = 299;  // 04:59 in minutes from midnight
const int VRIJEME_KRAJA_OPERACIJE_ZADANO = 1244;   // 20:44 in minutes from midnight
const int POZICIJA_NOCI = 63;                      // Night position (locked)
const int MINUTNI_BLOK = 15;                       // Each position covers 15 minutes

const int MAKS_PAMETNI_POMAK_KORAKA = 1;

// ==================== GLOBAL STATE VARIABLES ====================

int pozicijaPloce = 0;
int offsetMinuta = 14;
static int ciljKorakaPloce = 0;

static unsigned long vrijemeStarta = 0;           
static bool ciklusUTijeku = false;                
static bool drugaFaza = false;                    
static int zadnjaAktiviranaMinutaDana = -1;

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

struct StanjePloceEEPROM {
  char zapis[4];
};

// ==================== HELPER FUNCTIONS ====================

static void odradiJedanKorakPloceBlokirajuci();

static int izracunajCiljnuPoziciju(const DateTime& now)
{
  if (!jePlocaKonfigurirana()) {
    return pozicijaPloce;
  }
  
  int ukupnoMinuta = now.hour() * 60 + now.minute();
  int pocetak = VRIJEME_POCETKA_OPERACIJE_ZADANO;
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

static bool jeVrijemeUPlocnomIntervalu(const DateTime& now)
{
  if (!jePlocaKonfigurirana()) {
    return false;
  }
  
  int minutaDana = now.hour() * 60 + now.minute();
  int pocetak = VRIJEME_POCETKA_OPERACIJE_ZADANO;
  int kraj = VRIJEME_KRAJA_OPERACIJE_ZADANO;
  
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

static bool jeValjanZapisPloce(const StanjePloceEEPROM& stanje)
{
  if (stanje.zapis[0] < '0' || stanje.zapis[0] > '9' ||
      stanje.zapis[1] < '0' || stanje.zapis[1] > '9') {
    return false;
  }
  if (stanje.zapis[2] != 'P' && stanje.zapis[2] != 'N') {
    return false;
  }
  int poz = (stanje.zapis[0] - '0') * 10 + (stanje.zapis[1] - '0');
  return poz >= 0 && poz <= POZICIJA_NOCI;
}

static int dekodirajPoziciju(const StanjePloceEEPROM& stanje)
{
  return (stanje.zapis[0] - '0') * 10 + (stanje.zapis[1] - '0');
}

static bool spremiStanjePloceEEPROM(int pozicija, char faza)
{
  if (pozicija < 0 || pozicija > POZICIJA_NOCI) {
    return false;
  }
  if (faza != 'P' && faza != 'N') {
    return false;
  }
  StanjePloceEEPROM stanje{};
  stanje.zapis[0] = static_cast<char>('0' + (pozicija / 10));
  stanje.zapis[1] = static_cast<char>('0' + (pozicija % 10));
  stanje.zapis[2] = faza;
  stanje.zapis[3] = '\0';
  return WearLeveling::spremi(EepromLayout::BAZA_STANJE_PLOCE,
                              EepromLayout::SLOTOVI_STANJE_PLOCE,
                              stanje);
}

static bool ucitajStanjePloceEEPROM(StanjePloceEEPROM& stanje)
{
  if (!WearLeveling::ucitaj(EepromLayout::BAZA_STANJE_PLOCE,
                            EepromLayout::SLOTOVI_STANJE_PLOCE,
                            stanje)) {
    return false;
  }
  return jeValjanZapisPloce(stanje);
}

static void oporaviNedovrseniKorakPloce()
{
  StanjePloceEEPROM stanje{};
  if (!ucitajStanjePloceEEPROM(stanje)) {
    spremiStanjePloceEEPROM(pozicijaPloce, 'N');
    return;
  }

  digitalWrite(PIN_RELEJ_PARNE_PLOCE, LOW);
  digitalWrite(PIN_RELEJ_NEPARNE_PLOCE, LOW);
  ciklusUTijeku = false;
  drugaFaza = false;
  int spremljenaPozicija = dekodirajPoziciju(stanje);
  pozicijaPloce = spremljenaPozicija;
  ciljKorakaPloce = spremljenaPozicija;

  String log = F("Ploca oporavak: ");
  log += stanje.zapis;
  log += F(" -> ");

  if (stanje.zapis[2] == 'P') {
    digitalWrite(PIN_RELEJ_NEPARNE_PLOCE, HIGH);
    odradiPauzuSaLCD(FAZA_TRAJANJE_MS);
    osvjeziWatchdog();
    digitalWrite(PIN_RELEJ_NEPARNE_PLOCE, LOW);
    spremiStanjePloceEEPROM(spremljenaPozicija, 'N');
    log += F("dovrsena NEPARNI faza i potvrdeno N");
  } else {
    log += F("stabilno stanje N, nema dodatnog koraka");
  }
  posaljiPCLog(log);
}

static void sinkronizirajPlocuNaBootu()
{
  if (!jePlocaKonfigurirana()) {
    posaljiPCLog(F("Ploca boot sinkronizacija: onemogucena"));
    return;
  }

  DateTime now = dohvatiTrenutnoVrijeme();
  int ciljPozicija = izracunajCiljnuPoziciju(now);
  int razlika = izracunajBrojKorakaNaprijed(pozicijaPloce, ciljPozicija);

  String log = F("Ploca boot sinkronizacija: start=");
  if (pozicijaPloce < 10) log += '0';
  log += pozicijaPloce;
  log += F(" cilj=");
  if (ciljPozicija < 10) log += '0';
  log += ciljPozicija;
  log += F(" koraci=");
  log += razlika;
  posaljiPCLog(log);

  if (razlika == 0) {
    zadnjaAktiviranaMinutaDana = now.hour() * 60 + now.minute();
    ciklusUTijeku = false;
    drugaFaza = false;
    posaljiPCLog(F("Ploca boot sinkronizacija: vec u ciljnoj poziciji"));
    return;
  }

  const int MAX_BOOT_SYNC_KORAKA = BROJ_POZICIJA * 3;
  int odradeniKoraci = 0;

  while (true) {
    now = dohvatiTrenutnoVrijeme();
    ciljPozicija = izracunajCiljnuPoziciju(now);
    razlika = izracunajBrojKorakaNaprijed(pozicijaPloce, ciljPozicija);

    if (razlika == 0) {
      break;
    }

    odradiJedanKorakPloceBlokirajuci();

    ++odradeniKoraci;
    if (odradeniKoraci >= MAX_BOOT_SYNC_KORAKA) {
      posaljiPCLog(F("Ploca boot sinkronizacija: prekinuta zbog sigurnosne granice koraka"));
      break;
    }
  }

  now = dohvatiTrenutnoVrijeme();
  ciljPozicija = izracunajCiljnuPoziciju(now);
  pozicijaPloce = ciljPozicija;
  ciljKorakaPloce = ciljPozicija;
  spremiStanjePloceEEPROM(pozicijaPloce, 'N');
  zadnjaAktiviranaMinutaDana = now.hour() * 60 + now.minute();
  ciklusUTijeku = false;
  drugaFaza = false;
  posaljiPCLog(F("Ploca boot sinkronizacija: zavrsena"));
}

static void pokreniPrvuFazuPloce(int ciljPozicija)
{
  ciljKorakaPloce = constrain(ciljPozicija, 0, BROJ_POZICIJA - 1);
  spremiStanjePloceEEPROM(ciljKorakaPloce, 'P');
  digitalWrite(PIN_RELEJ_PARNE_PLOCE, HIGH);
  digitalWrite(PIN_RELEJ_NEPARNE_PLOCE, LOW);
  
  vrijemeStarta = millis();
  ciklusUTijeku = true;
  drugaFaza = false;
  
  String log = F("Ploca: prva faza, cilj=");
  if (ciljKorakaPloce < 10) log += '0';
  log += ciljKorakaPloce;
  log += 'P';
  posaljiPCLog(log);
}

// Complete plate rotation step
static void zavrsiCiklusPloce()
{
  digitalWrite(PIN_RELEJ_PARNE_PLOCE, LOW);
  digitalWrite(PIN_RELEJ_NEPARNE_PLOCE, LOW);
  ciklusUTijeku = false;
  drugaFaza = false;
  
  pozicijaPloce = ciljKorakaPloce;
  spremiStanjePloceEEPROM(pozicijaPloce, 'N');
  
  String log = F("Ploca: korak zavrsen, pozicija=");
  if (pozicijaPloce < 10) log += '0';
  log += pozicijaPloce;
  log += 'N';
  posaljiPCLog(log);
}

// Execute one complete rotation step in blocking mode
static void odradiJedanKorakPloceBlokirajuci()
{
  const int ciljPozicija = (pozicijaPloce + 1) % BROJ_POZICIJA;
  ciljKorakaPloce = ciljPozicija;
  spremiStanjePloceEEPROM(ciljKorakaPloce, 'P');

  // FIRST PHASE
  digitalWrite(PIN_RELEJ_PARNE_PLOCE, HIGH);
  digitalWrite(PIN_RELEJ_NEPARNE_PLOCE, LOW);
  
  String logPhase1 = F("Ploca (blok): faza 1");
  posaljiPCLog(logPhase1);
  
  odradiPauzuSaLCD(FAZA_TRAJANJE_MS);
  
  digitalWrite(PIN_RELEJ_PARNE_PLOCE, LOW);
  odradiPauzuSaLCD(200);
  osvjeziWatchdog();
  
  // SECOND PHASE
  digitalWrite(PIN_RELEJ_NEPARNE_PLOCE, HIGH);
  
  String logPhase2 = F("Ploca (blok): faza 2");
  posaljiPCLog(logPhase2);
  
  odradiPauzuSaLCD(FAZA_TRAJANJE_MS);
  osvjeziWatchdog();
  
  digitalWrite(PIN_RELEJ_PARNE_PLOCE, LOW);
  digitalWrite(PIN_RELEJ_NEPARNE_PLOCE, LOW);
  
  odradiPauzuSaLCD(400);
  osvjeziWatchdog();
  
  pozicijaPloce = ciljKorakaPloce;
  spremiStanjePloceEEPROM(pozicijaPloce, 'N');
  
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
  
  StanjePloceEEPROM stanje{};
  if (!ucitajStanjePloceEEPROM(stanje)) {
    pozicijaPloce = POZICIJA_NOCI;
    spremiStanjePloceEEPROM(pozicijaPloce, 'N');
    posaljiPCLog(F("Ploca: inicijalizirano stanje 63N"));
  } else {
    pozicijaPloce = dekodirajPoziciju(stanje);
  }
  ciljKorakaPloce = pozicijaPloce;
  
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
  zadnjaAktiviranaMinutaDana = -1;
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
  sinkronizirajPlocuNaBootu();
  
  String log = F("Ploca inicijalizirana");
  posaljiPCLog(log);
}

// ==================== MAIN OPERATION ====================

void upravljajPlocom()
{
  DateTime now = dohvatiTrenutnoVrijeme();
  unsigned long sadaMs = millis();
  const int minutaDana = now.hour() * 60 + now.minute();
  
  azurirajAutomatskaZvonjenja(sadaMs);
  
  bool plocaAktivna = jeVrijemeUPlocnomIntervalu(now);
  
  if (!plocaAktivna) {
    if (ciklusUTijeku) {
      if (drugaFaza) {
        zavrsiCiklusPloce();
        posaljiPCLog(F("Ploca: dovrsen korak prije nocnog rezima"));
      } else {
        digitalWrite(PIN_RELEJ_PARNE_PLOCE, LOW);
        digitalWrite(PIN_RELEJ_NEPARNE_PLOCE, LOW);
        ciklusUTijeku = false;
        drugaFaza = false;
        posaljiPCLog(F("Ploca: zaustavljena - izvan dozvoljenog vremena"));
      }
    } else if (plocaAktivnaRanije) {
      posaljiPCLog(F("Ploca: onemogucena - nocni rezim"));
    }
    
    plocaAktivnaRanije = false;
    if (!ciklusUTijeku && pozicijaPloce != POZICIJA_NOCI) {
      pozicijaPloce = POZICIJA_NOCI;
      ciljKorakaPloce = POZICIJA_NOCI;
      spremiStanjePloceEEPROM(POZICIJA_NOCI, 'N');
      posaljiPCLog(F("Ploca: nocni rezim - stanje 63N"));
    }
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
  
  int cilj = izracunajCiljnuPoziciju(now);
  
  int razlika = izracunajBrojKorakaNaprijed(pozicijaPloce, cilj);
  const bool jeTerminKoraka = (minutaDana >= VRIJEME_POCETKA_OPERACIJE_ZADANO) &&
                              (minutaDana <= VRIJEME_KRAJA_OPERACIJE_ZADANO) &&
                              (((minutaDana - VRIJEME_POCETKA_OPERACIJE_ZADANO) % MINUTNI_BLOK) == 0) &&
                              (now.second() == 0);
  
  bool trebaPokrenuti = (!ciklusUTijeku &&
                         minutaDana != zadnjaAktiviranaMinutaDana &&
                         jeTerminKoraka &&
                         razlika > 0);
  
  if (trebaPokrenuti) {
    String log = F("Ploca: pokretanje koraka");
    posaljiPCLog(log);
    
    const int sljedecaPozicija = (pozicijaPloce + 1) % BROJ_POZICIJA;
    pokreniPrvuFazuPloce(sljedecaPozicija);
    zadnjaAktiviranaMinutaDana = minutaDana;
  }
  
  if (!ciklusUTijeku) {
    return;
  }
  
  unsigned long proteklo = millis() - vrijemeStarta;
  
  if (!drugaFaza && proteklo >= FAZA_TRAJANJE_MS) {
    digitalWrite(PIN_RELEJ_PARNE_PLOCE, LOW);
    digitalWrite(PIN_RELEJ_NEPARNE_PLOCE, HIGH);
    
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
  ciljKorakaPloce = pozicijaPloce;
  spremiStanjePloceEEPROM(pozicijaPloce, 'N');
  zadnjaAktiviranaMinutaDana = -1;
  
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
    zadnjaAktiviranaMinutaDana = now.hour() * 60 + now.minute();
    ciklusUTijeku = false;
    drugaFaza = false;
    posaljiPCLog(F("Ploca kompenzacija: vec u sinkronu"));
    return;
  }
  
  for (int i = 0; i < razlika; i++) {
    odradiJedanKorakPloceBlokirajuci();
  }
  
  pozicijaPloce = ciljPozicija;
  ciljKorakaPloce = ciljPozicija;
  spremiStanjePloceEEPROM(pozicijaPloce, 'N');
  
  zadnjaAktiviranaMinutaDana = now.hour() * 60 + now.minute();
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
  zadnjaAktiviranaMinutaDana = now.hour() * 60 + now.minute();
  
  String log = F("Ploca: oznacena kao sinkronizirana");
  posaljiPCLog(log);
}
