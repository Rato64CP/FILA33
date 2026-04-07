#include <Arduino.h>
#include <RTClib.h>
#include "kazaljke_sata.h"
#include "podesavanja_piny.h"
#include "time_glob.h"
#include "eeprom_konstante.h"
#include "pc_serial.h"
#include "postavke.h"
#include "unified_motion_state.h"

namespace {
constexpr unsigned long TRAJANJE_FAZE_MS = 6000UL;
constexpr int BROJ_MINUTA_CIKLUS = 720;
constexpr int MAKS_CEKANJE_AKO_SU_KAZALJKE_NAPRIJED = 60;
constexpr uint8_t HAND_NEAKTIVNO = 0;
constexpr uint8_t HAND_AKTIVNO = 1;
constexpr uint8_t HAND_RELEJ_NIJEDAN = 0;
constexpr uint8_t HAND_RELEJ_PARNI = 1;
constexpr uint8_t HAND_RELEJ_NEPARNI = 2;

unsigned long zadnjaProvjeraMs = 0;
uint32_t zadnjiObradeniRtcTick = 0;
uint32_t handStartTick = 0;
bool rucnaBlokadaKazaljki = false;

void ugasiRelejeKazaljki() {
  digitalWrite(PIN_RELEJ_PARNE_KAZALJKE, LOW);
  digitalWrite(PIN_RELEJ_NEPARNE_KAZALJKE, LOW);
}

bool jeAutomatikaKazaljkiBlokirana() {
  return rucnaBlokadaKazaljki || !jeVrijemePotvrdjenoZaAutomatiku();
}

bool jeBootRecoveryAktivan() {
  const uint8_t resetFlags = MCUSR;
  const bool watchdogReset = (resetFlags & (1 << WDRF)) != 0;
  const bool powerLossReset = (resetFlags & ((1 << BORF) | (1 << PORF))) != 0;
  const bool vanjskiReset = (resetFlags & (1 << EXTRF)) != 0;
  return watchdogReset || (powerLossReset && !vanjskiReset);
}

int izracunajDvanaestSatneMinute(const DateTime& vrijeme) {
  return (vrijeme.hour() % 12) * 60 + vrijeme.minute();
}

int izracunajMinuteNaprijedOdTrenutnogVremena(int polozajKazaljki, int ciljVrijeme) {
  return (polozajKazaljki - ciljVrijeme + BROJ_MINUTA_CIKLUS) % BROJ_MINUTA_CIKLUS;
}

bool trebajuKazaljkeSamoCekati(int polozajKazaljki, int ciljVrijeme) {
  const int minuteNaprijed = izracunajMinuteNaprijedOdTrenutnogVremena(polozajKazaljki, ciljVrijeme);
  return minuteNaprijed > 0 && minuteNaprijed <= MAKS_CEKANJE_AKO_SU_KAZALJKE_NAPRIJED;
}

uint8_t odrediRelejKazaljki(const EepromLayout::UnifiedMotionState& stanje) {
  // Vazno za toranjski sat: relej se bira prema trenutno memoriranoj
  // K-minuta poziciji. Ovo je potvrdeno na stvarnoj mehanici; ne mijenjati
  // na "sljedecu minutu" bez ponovne provjere na satu.
  return (stanje.hand_position % 2 == 0) ? HAND_RELEJ_PARNI : HAND_RELEJ_NEPARNI;
}

void aktivirajRelejeKazaljki(const EepromLayout::UnifiedMotionState& stanje) {
  if (!imaKazaljkeSata() || jeAutomatikaKazaljkiBlokirana()) {
    ugasiRelejeKazaljki();
    return;
  }

  if (stanje.hand_active != HAND_AKTIVNO) {
    ugasiRelejeKazaljki();
    return;
  }

  if (stanje.hand_relay == HAND_RELEJ_PARNI) {
    digitalWrite(PIN_RELEJ_PARNE_KAZALJKE, HIGH);
    digitalWrite(PIN_RELEJ_NEPARNE_KAZALJKE, LOW);
  } else if (stanje.hand_relay == HAND_RELEJ_NEPARNI) {
    digitalWrite(PIN_RELEJ_PARNE_KAZALJKE, LOW);
    digitalWrite(PIN_RELEJ_NEPARNE_KAZALJKE, HIGH);
  } else {
    ugasiRelejeKazaljki();
  }
}

void obradiJedanKorak(EepromLayout::UnifiedMotionState& stanje, unsigned long sadaMs) {
  const uint32_t rtcTick = dohvatiRtcSekundniBrojac();
  if (stanje.hand_active == HAND_AKTIVNO &&
      (rtcTick - handStartTick) >= (TRAJANJE_FAZE_MS / 1000UL)) {
    stanje.hand_position = static_cast<uint16_t>((stanje.hand_position + 1) % BROJ_MINUTA_CIKLUS);
    stanje.hand_active = HAND_NEAKTIVNO;
    stanje.hand_relay = HAND_RELEJ_NIJEDAN;
    stanje.hand_start_ms = 0;
    aktivirajRelejeKazaljki(stanje);
    UnifiedMotionStateStore::spremiAkoPromjena(stanje);
    posaljiPCLog(F("Kazaljke: korak dovrsen"));
    UnifiedMotionStateStore::logirajStanje(stanje);
  }
}

void pokreniKorakAkoTreba(EepromLayout::UnifiedMotionState& stanje, unsigned long sadaMs) {
  const EepromLayout::UnifiedMotionState najnovijeStanje = UnifiedMotionStateStore::dohvatiIliMigriraj();
  stanje = najnovijeStanje;
  const uint32_t rtcTick = dohvatiRtcSekundniBrojac();
  if (stanje.hand_active != HAND_NEAKTIVNO) {
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
  zadnjaProvjeraMs = sadaMs;

  const int cilj = izracunajDvanaestSatneMinute(rtcVrijeme);
  if (stanje.hand_position == cilj) {
    return;
  }

  // Ako su kazaljke toranjskog sata malo naprijed, ne forsiramo puni krug
  // nego pustimo da ih stvarno vrijeme sustigne.
  if (trebajuKazaljkeSamoCekati(stanje.hand_position, cilj)) {
    return;
  }

  stanje.hand_active = HAND_AKTIVNO;
  stanje.hand_relay = odrediRelejKazaljki(stanje);
  stanje.hand_start_ms = sadaMs;
  handStartTick = rtcTick;
  aktivirajRelejeKazaljki(stanje);
  UnifiedMotionStateStore::spremiAkoPromjena(stanje);
  const EepromLayout::UnifiedMotionState potvrdenoStanje = UnifiedMotionStateStore::dohvatiIliMigriraj();
  if (potvrdenoStanje.hand_active == HAND_AKTIVNO &&
      potvrdenoStanje.hand_start_ms == stanje.hand_start_ms) {
    String log = F("Kazaljke: start impuls=");
    log += (potvrdenoStanje.hand_relay == HAND_RELEJ_PARNI) ? F("PARNI") : F("NEPARNI");
    log += F(" cilj=");
    log += cilj;
    posaljiPCLog(log);
    UnifiedMotionStateStore::logirajStanje(potvrdenoStanje);
  }
}

}  // namespace

void inicijalizirajKazaljke() {
  pinMode(PIN_RELEJ_PARNE_KAZALJKE, OUTPUT);
  pinMode(PIN_RELEJ_NEPARNE_KAZALJKE, OUTPUT);
  ugasiRelejeKazaljki();

  if (!imaKazaljkeSata()) {
    zadnjaProvjeraMs = millis();
    zadnjiObradeniRtcTick = 0;
    handStartTick = 0;
    posaljiPCLog(F("Kazaljke: onemogucene u postavkama toranjskog sata"));
    return;
  }

  EepromLayout::UnifiedMotionState stanje = UnifiedMotionStateStore::dohvatiIliMigriraj();
  // Vazno za recovery toranjskog sata: pri restartu zadrzavamo EEPROM
  // poziciju kazaljki. Ne smije se ponovno upisivati RTC vrijeme tijekom
  // inicijalizacije jer bi se time pregazila stvarna fizicka pozicija.
  if (jeBootRecoveryAktivan()) {
    String log = F("Kazaljke: boot recovery iz EEPROM-a, pozicija=");
    log += stanje.hand_position;
    log += F(" active=");
    log += stanje.hand_active;
    log += F(" relay=");
    log += stanje.hand_relay;
    posaljiPCLog(log);
  } else {
    String log = F("Kazaljke: normalni restart, zadrzavam EEPROM poziciju=");
    log += stanje.hand_position;
    posaljiPCLog(log);
  }
  aktivirajRelejeKazaljki(stanje);

  zadnjaProvjeraMs = millis();
  zadnjiObradeniRtcTick = 0;
  handStartTick = 0;
  rucnaBlokadaKazaljki = false;
  UnifiedMotionStateStore::logirajStanje(stanje);
  posaljiPCLog(F("Kazaljke: inicijalizirane kroz jedinstveni model stanja"));
}

void upravljajKazaljkama() {
  upravljajKorekcijomKazaljki();
}

void upravljajKorekcijomKazaljki() {
  if (!imaKazaljkeSata()) {
    zadnjiObradeniRtcTick = 0;
    handStartTick = 0;
    ugasiRelejeKazaljki();
    return;
  }

  EepromLayout::UnifiedMotionState stanje = UnifiedMotionStateStore::dohvatiIliMigriraj();
  const unsigned long sadaMs = millis();

  if (jeAutomatikaKazaljkiBlokirana()) {
    if (stanje.hand_active != HAND_NEAKTIVNO || stanje.hand_relay != HAND_RELEJ_NIJEDAN) {
      stanje.hand_active = HAND_NEAKTIVNO;
      stanje.hand_relay = HAND_RELEJ_NIJEDAN;
      stanje.hand_start_ms = 0;
      UnifiedMotionStateStore::spremiAkoPromjena(stanje);
      if (rucnaBlokadaKazaljki) {
        posaljiPCLog(F("Kazaljke: automatika rucno blokirana za namjestanje"));
      } else {
        posaljiPCLog(F("Kazaljke: automatika blokirana dok vrijeme nije potvrdeno"));
      }
    }
    zadnjiObradeniRtcTick = 0;
    handStartTick = 0;
    aktivirajRelejeKazaljki(stanje);
    return;
  }

  if (stanje.hand_active != HAND_NEAKTIVNO) {
    aktivirajRelejeKazaljki(stanje);
    obradiJedanKorak(stanje, sadaMs);
    return;
  }

  aktivirajRelejeKazaljki(stanje);
  pokreniKorakAkoTreba(stanje, sadaMs);
}

void pokreniBudnoKorekciju() {
  if (!imaKazaljkeSata()) {
    return;
  }
  zadnjaProvjeraMs = 0;
  zadnjiObradeniRtcTick = 0;
}

void zatraziPoravnanjeTaktaKazaljki() {
  if (!imaKazaljkeSata()) {
    return;
  }
  zadnjiObradeniRtcTick = 0;
}

void postaviRucnuBlokaduKazaljki(bool blokirano) {
  if (rucnaBlokadaKazaljki == blokirano) {
    return;
  }

  rucnaBlokadaKazaljki = blokirano;
  zadnjiObradeniRtcTick = 0;
  handStartTick = 0;

  EepromLayout::UnifiedMotionState stanje = UnifiedMotionStateStore::dohvatiIliMigriraj();
  if (blokirano) {
    stanje.hand_active = HAND_NEAKTIVNO;
    stanje.hand_relay = HAND_RELEJ_NIJEDAN;
    stanje.hand_start_ms = 0;
    UnifiedMotionStateStore::spremiAkoPromjena(stanje);
    ugasiRelejeKazaljki();
    posaljiPCLog(F("Kazaljke: ukljucena rucna blokada za namjestanje"));
  } else {
    aktivirajRelejeKazaljki(stanje);
    pokreniBudnoKorekciju();
    posaljiPCLog(F("Kazaljke: iskljucena rucna blokada"));
  }
}

bool jeRucnaBlokadaKazaljkiAktivna() {
  return rucnaBlokadaKazaljki;
}

bool mozeSeRucnoNamjestatiKazaljke() {
  if (!imaKazaljkeSata()) {
    return true;
  }
  return UnifiedMotionStateStore::dohvatiIliMigriraj().hand_active == HAND_NEAKTIVNO;
}

void postaviRucnuPozicijuKazaljki(int satKazaljke, int minutaKazaljke) {
  if (!imaKazaljkeSata()) {
    return;
  }
  satKazaljke = constrain(satKazaljke, 0, 11);
  minutaKazaljke = constrain(minutaKazaljke, 0, 59);

  EepromLayout::UnifiedMotionState stanje = UnifiedMotionStateStore::dohvatiIliMigriraj();
  stanje.hand_position = static_cast<uint16_t>((satKazaljke * 60 + minutaKazaljke) % BROJ_MINUTA_CIKLUS);
  stanje.hand_active = HAND_NEAKTIVNO;
  stanje.hand_relay = HAND_RELEJ_NIJEDAN;
  stanje.hand_start_ms = 0;
  aktivirajRelejeKazaljki(stanje);
  UnifiedMotionStateStore::spremiAkoPromjena(stanje);
  pokreniBudnoKorekciju();
}

void pomakniKazaljkeZa(int brojMinuta) {
  if (!imaKazaljkeSata()) {
    return;
  }
  EepromLayout::UnifiedMotionState stanje = UnifiedMotionStateStore::dohvatiIliMigriraj();
  int nova = static_cast<int>(stanje.hand_position) + brojMinuta;
  while (nova < 0) nova += BROJ_MINUTA_CIKLUS;
  stanje.hand_position = static_cast<uint16_t>(nova % BROJ_MINUTA_CIKLUS);
  stanje.hand_active = HAND_NEAKTIVNO;
  stanje.hand_relay = HAND_RELEJ_NIJEDAN;
  stanje.hand_start_ms = 0;
  UnifiedMotionStateStore::spremiAkoPromjena(stanje);
}

bool suKazaljkeUSinkronu() {
  if (!imaKazaljkeSata()) {
    return true;
  }

  if (!jeVrijemePotvrdjenoZaAutomatiku()) {
    return false;
  }

  const EepromLayout::UnifiedMotionState stanje = UnifiedMotionStateStore::dohvatiIliMigriraj();
  return stanje.hand_active == HAND_NEAKTIVNO &&
         stanje.hand_position == izracunajDvanaestSatneMinute(dohvatiTrenutnoVrijeme());
}

int dohvatiMemoriraneKazaljkeMinuta() {
  return UnifiedMotionStateStore::dohvatiIliMigriraj().hand_position;
}

void oznaciKazaljkeKaoSinkronizirane() {
  if (!imaKazaljkeSata()) {
    return;
  }
  pokreniBudnoKorekciju();
}

void obavijestiKazaljkeDSTPromjena(int) {
  if (!imaKazaljkeSata()) {
    return;
  }
  pokreniBudnoKorekciju();
}

void postaviTrenutniPolozajKazaljki(int trenutnaMinuta) {
  if (!imaKazaljkeSata()) {
    return;
  }
  trenutnaMinuta = constrain(trenutnaMinuta, 0, BROJ_MINUTA_CIKLUS - 1);
  EepromLayout::UnifiedMotionState stanje = UnifiedMotionStateStore::dohvatiIliMigriraj();
  stanje.hand_position = static_cast<uint16_t>(trenutnaMinuta);
  stanje.hand_active = HAND_NEAKTIVNO;
  stanje.hand_relay = HAND_RELEJ_NIJEDAN;
  stanje.hand_start_ms = 0;
  UnifiedMotionStateStore::spremiAkoPromjena(stanje);
}

void pomakniKazaljkeNaMinutu(int ciljMinuta, bool) {
  postaviTrenutniPolozajKazaljki(ciljMinuta);
}
