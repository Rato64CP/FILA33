#include <Arduino.h>
#include <avr/pgmspace.h>
#include <RTClib.h>
#include "kazaljke_sata.h"
#include "podesavanja_piny.h"
#include "time_glob.h"
#include "eeprom_konstante.h"
#include "pc_serial.h"
#include "postavke.h"
#include "unified_motion_state.h"
#include "watchdog.h"

namespace {
constexpr int BROJ_MINUTA_CIKLUS = 720;
constexpr int MAKS_CEKANJE_AKO_SU_KAZALJKE_NAPRIJED = 60;
constexpr uint8_t HAND_NEAKTIVNO = 0;
constexpr uint8_t HAND_AKTIVNO = 1;
constexpr uint8_t HAND_RELEJ_NIJEDAN = 0;
constexpr uint8_t HAND_RELEJ_PARNI = 1;
constexpr uint8_t HAND_RELEJ_NEPARNI = 2;

uint32_t zadnjiObradeniRtcTick = 0;
uint32_t aktivniKorakStartRtcTick = 0;
bool rucnaBlokadaKazaljki = false;

void posaljiLogKazaljkiStarta(uint8_t relej, int cilj) {
  char log[64];
  snprintf_P(log, sizeof(log), PSTR("Kazaljke: start impuls=%s cilj=%d"),
             (relej == HAND_RELEJ_PARNI) ? "PARNI" : "NEPARNI",
             cilj);
  posaljiPCLog(log);
}

void posaljiLogKazaljkiBootRecovery(const EepromLayout::UnifiedMotionState& stanje) {
  char log[96];
  snprintf_P(log, sizeof(log),
             PSTR("Kazaljke: boot recovery iz EEPROM-a, pozicija=%u active=%u relay=%u"),
             stanje.hand_position,
             stanje.hand_active,
             stanje.hand_relay);
  posaljiPCLog(log);
}

void posaljiLogKazaljkiNormalniRestart(uint16_t pozicija) {
  char log[72];
  snprintf_P(log, sizeof(log),
             PSTR("Kazaljke: normalni restart, zadrzavam EEPROM poziciju=%u"),
             pozicija);
  posaljiPCLog(log);
}

void ugasiRelejeKazaljki() {
  digitalWrite(PIN_RELEJ_PARNE_KAZALJKE, LOW);
  digitalWrite(PIN_RELEJ_NEPARNE_KAZALJKE, LOW);
}

bool jeAutomatikaKazaljkiBlokirana() {
  return rucnaBlokadaKazaljki || !jeVrijemePotvrdjenoZaAutomatiku();
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

  // Za kazaljke toranjskog sata releje preklapamo u "break-before-make"
  // redoslijedu: prvo oba OFF, pa tek onda trazeni relej ON.
  ugasiRelejeKazaljki();
  if (stanje.hand_relay == HAND_RELEJ_PARNI) {
    digitalWrite(PIN_RELEJ_PARNE_KAZALJKE, HIGH);
  } else if (stanje.hand_relay == HAND_RELEJ_NEPARNI) {
    digitalWrite(PIN_RELEJ_NEPARNE_KAZALJKE, HIGH);
  }
}

void obradiJedanKorak(EepromLayout::UnifiedMotionState& stanje) {
  const uint32_t rtcTick = dohvatiRtcSekundniBrojac();
  if (stanje.hand_active == HAND_AKTIVNO && aktivniKorakStartRtcTick == 0) {
    aktivniKorakStartRtcTick = rtcTick;
  }

  const bool istekPoSqw =
      stanje.hand_active == HAND_AKTIVNO &&
      aktivniKorakStartRtcTick != 0 &&
      (rtcTick - aktivniKorakStartRtcTick) >= 6U;
  if (istekPoSqw) {
    stanje.hand_position = static_cast<uint16_t>((stanje.hand_position + 1) % BROJ_MINUTA_CIKLUS);
    stanje.hand_active = HAND_NEAKTIVNO;
    stanje.hand_relay = HAND_RELEJ_NIJEDAN;
    stanje.hand_start_ms = 0;
    aktivniKorakStartRtcTick = 0;
    aktivirajRelejeKazaljki(stanje);
    UnifiedMotionStateStore::spremiAkoPromjena(stanje);
    posaljiPCLog(F("Kazaljke: korak dovrsen po RTC SQW taktu"));
    UnifiedMotionStateStore::logirajStanje(stanje);
  }
}

void pokreniKorakAkoTreba(EepromLayout::UnifiedMotionState& stanje) {
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
  stanje.hand_start_ms = 0;
  aktivniKorakStartRtcTick = rtcTick;
  aktivirajRelejeKazaljki(stanje);
  UnifiedMotionStateStore::spremiAkoPromjena(stanje);
  const EepromLayout::UnifiedMotionState potvrdenoStanje = UnifiedMotionStateStore::dohvatiIliMigriraj();
  if (potvrdenoStanje.hand_active == HAND_AKTIVNO &&
      potvrdenoStanje.hand_relay == stanje.hand_relay) {
    posaljiLogKazaljkiStarta(potvrdenoStanje.hand_relay, cilj);
    UnifiedMotionStateStore::logirajStanje(potvrdenoStanje);
  }
}

}  // namespace

void inicijalizirajKazaljke() {
  pinMode(PIN_RELEJ_PARNE_KAZALJKE, OUTPUT);
  pinMode(PIN_RELEJ_NEPARNE_KAZALJKE, OUTPUT);
  ugasiRelejeKazaljki();

  if (!imaKazaljkeSata()) {
    zadnjiObradeniRtcTick = 0;
    aktivniKorakStartRtcTick = 0;
    posaljiPCLog(F("Kazaljke: onemogucene u postavkama toranjskog sata"));
    return;
  }

  EepromLayout::UnifiedMotionState stanje = UnifiedMotionStateStore::dohvatiIliMigriraj();
  if (stanje.hand_active != HAND_NEAKTIVNO) {
    // Aktivni impuls iz stare sesije ne smije se direktno vratiti na relej pri bootu,
    // jer je hand_start_ms vezan uz stari millis() i moze odmah isteci kao kratki bljesak.
    stanje.hand_active = HAND_NEAKTIVNO;
    stanje.hand_relay = HAND_RELEJ_NIJEDAN;
    stanje.hand_start_ms = 0;
    UnifiedMotionStateStore::spremiAkoPromjena(stanje);
    posaljiPCLog(F("Kazaljke: pronaden aktivni impuls iz stare sesije, vracam u mirno stanje"));
  }
  // Vazno za recovery toranjskog sata: pri restartu zadrzavamo EEPROM
  // poziciju kazaljki. Ne smije se ponovno upisivati RTC vrijeme tijekom
  // inicijalizacije jer bi se time pregazila stvarna fizicka pozicija.
  if (jeBootRecoveryResetDetektiran()) {
    posaljiLogKazaljkiBootRecovery(stanje);
  } else {
    posaljiLogKazaljkiNormalniRestart(stanje.hand_position);
  }
  aktivirajRelejeKazaljki(stanje);

  zadnjiObradeniRtcTick = 0;
  aktivniKorakStartRtcTick = 0;
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
    aktivniKorakStartRtcTick = 0;
    ugasiRelejeKazaljki();
    return;
  }

  EepromLayout::UnifiedMotionState stanje = UnifiedMotionStateStore::dohvatiIliMigriraj();

  if (!jeRtcSqwAktivan()) {
    if (stanje.hand_active != HAND_NEAKTIVNO || stanje.hand_relay != HAND_RELEJ_NIJEDAN) {
      stanje.hand_active = HAND_NEAKTIVNO;
      stanje.hand_relay = HAND_RELEJ_NIJEDAN;
      stanje.hand_start_ms = 0;
      aktivniKorakStartRtcTick = 0;
      UnifiedMotionStateStore::spremiAkoPromjena(stanje);
      ugasiRelejeKazaljki();
      posaljiPCLog(F("Kazaljke: RTC SQW nije aktivan, gasim aktivni impuls bez pomaka"));
    }
    return;
  }

  if (jeAutomatikaKazaljkiBlokirana()) {
    if (stanje.hand_active != HAND_NEAKTIVNO || stanje.hand_relay != HAND_RELEJ_NIJEDAN) {
      stanje.hand_active = HAND_NEAKTIVNO;
      stanje.hand_relay = HAND_RELEJ_NIJEDAN;
      stanje.hand_start_ms = 0;
      aktivniKorakStartRtcTick = 0;
      UnifiedMotionStateStore::spremiAkoPromjena(stanje);
      if (rucnaBlokadaKazaljki) {
        posaljiPCLog(F("Kazaljke: automatika rucno blokirana za namjestanje"));
      } else {
        posaljiPCLog(F("Kazaljke: automatika blokirana dok vrijeme nije potvrdeno"));
      }
    }
    zadnjiObradeniRtcTick = 0;
    aktivirajRelejeKazaljki(stanje);
    return;
  }

  if (stanje.hand_active != HAND_NEAKTIVNO) {
    aktivirajRelejeKazaljki(stanje);
    obradiJedanKorak(stanje);
    return;
  }

  aktivirajRelejeKazaljki(stanje);
  pokreniKorakAkoTreba(stanje);
}

void pokreniBudnoKorekciju() {
  if (!imaKazaljkeSata()) {
    return;
  }
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
  aktivniKorakStartRtcTick = 0;

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
  aktivniKorakStartRtcTick = 0;
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
  aktivniKorakStartRtcTick = 0;
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
  aktivniKorakStartRtcTick = 0;
  UnifiedMotionStateStore::spremiAkoPromjena(stanje);
}

void pomakniKazaljkeNaMinutu(int ciljMinuta, bool) {
  postaviTrenutniPolozajKazaljki(ciljMinuta);
}
