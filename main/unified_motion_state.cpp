#include "unified_motion_state.h"

#include <Arduino.h>
#include <string.h>

#include "i2c_eeprom.h"
#include "pc_serial.h"
#include "time_glob.h"
#include "wear_leveling.h"

namespace UnifiedMotionStateStore {
namespace {
constexpr int BROJ_MINUTA_CIKLUS = 720;
constexpr int BROJ_POZICIJA_PLOCE = 64;
constexpr uint8_t HAND_NEAKTIVNO = 0;
constexpr uint8_t HAND_RELEJ_NIJEDAN = 0;
constexpr uint8_t FAZA_STABILNO = 0;
constexpr uint8_t UNIFIED_VERZIJA = EepromLayout::UNIFIED_STANJE_VERZIJA;
constexpr uint8_t RESERVED_SEKVENCA_POCETNA = 1;
bool cacheInicijaliziran = false;
EepromLayout::UnifiedMotionState cacheStanje{};

bool jeValjanoStanje(const EepromLayout::UnifiedMotionState& stanje) {
  return stanje.hand_position < BROJ_MINUTA_CIKLUS &&
         stanje.hand_active <= 1 &&
         stanje.hand_relay <= 2 &&
         stanje.plate_position < BROJ_POZICIJA_PLOCE &&
         stanje.plate_phase <= 2 &&
         stanje.version == UNIFIED_VERZIJA;
}

int izracunajDvanaestSatneMinute() {
  const DateTime vrijeme = dohvatiTrenutnoVrijeme();
  return (vrijeme.hour() % 12) * 60 + vrijeme.minute();
}

bool jeSekvencaNovija(uint8_t kandidat, uint8_t referenca) {
  const uint8_t razlika = static_cast<uint8_t>(kandidat - referenca);
  return razlika != 0 && razlika < 128;
}

bool ucitajNajnovijeStanje(EepromLayout::UnifiedMotionState& stanje, int* slotNajnoviji = nullptr) {
  bool pronadeno = false;
  uint8_t najboljaSekvenca = 0;
  int najboljiSlot = -1;

  for (int slot = 0; slot < EepromLayout::SLOTOVI_UNIFIED_STANJE; ++slot) {
    EepromLayout::UnifiedMotionState kandidat{};
    const int adresa =
      EepromLayout::BAZA_UNIFIED_STANJE + slot * static_cast<int>(sizeof(EepromLayout::UnifiedMotionState));
    if (!VanjskiEEPROM::procitaj(adresa, &kandidat, sizeof(kandidat)) || !jeValjanoStanje(kandidat)) {
      continue;
    }

    if (!pronadeno ||
        jeSekvencaNovija(kandidat.reserved, najboljaSekvenca) ||
        (kandidat.reserved == najboljaSekvenca && slot > najboljiSlot)) {
      stanje = kandidat;
      najboljaSekvenca = kandidat.reserved;
      najboljiSlot = slot;
      pronadeno = true;
    }
  }

  if (pronadeno && slotNajnoviji != nullptr) {
    *slotNajnoviji = najboljiSlot;
  }
  return pronadeno;
}

bool jednakoLogickoStanje(EepromLayout::UnifiedMotionState lijevo,
                          EepromLayout::UnifiedMotionState desno) {
  lijevo.version = 0;
  lijevo.reserved = 0;
  desno.version = 0;
  desno.reserved = 0;
  return memcmp(&lijevo, &desno, sizeof(lijevo)) == 0;
}

void ucitajLegacyKazaljke(uint16_t& handPosition) {
  int legacyK = 0;
  if (!WearLeveling::ucitaj(EepromLayout::BAZA_KAZALJKE,
                            EepromLayout::SLOTOVI_KAZALJKE,
                            legacyK)) {
    legacyK = izracunajDvanaestSatneMinute();
  }
  handPosition = static_cast<uint16_t>((legacyK % BROJ_MINUTA_CIKLUS + BROJ_MINUTA_CIKLUS) % BROJ_MINUTA_CIKLUS);
}

void ucitajLegacyPlocu(uint8_t& platePosition, uint8_t& platePhase) {
  struct LegacyPloce { char zapis[4]; } legacy{};
  if (!WearLeveling::ucitaj(EepromLayout::BAZA_STANJE_PLOCE,
                            EepromLayout::SLOTOVI_STANJE_PLOCE,
                            legacy)) {
    return;
  }

  if (legacy.zapis[0] >= '0' && legacy.zapis[0] <= '9' &&
      legacy.zapis[1] >= '0' && legacy.zapis[1] <= '9') {
    const int poz = (legacy.zapis[0] - '0') * 10 + (legacy.zapis[1] - '0');
    if (poz >= 0 && poz < BROJ_POZICIJA_PLOCE) {
      platePosition = static_cast<uint8_t>(poz);
      platePhase = (legacy.zapis[2] == 'P') ? 1 : FAZA_STABILNO;
    }
  }
}

EepromLayout::UnifiedMotionState inicijalnoStanje() {
  EepromLayout::UnifiedMotionState stanje{};
  ucitajLegacyKazaljke(stanje.hand_position);
  stanje.hand_active = HAND_NEAKTIVNO;
  stanje.hand_relay = HAND_RELEJ_NIJEDAN;
  stanje.hand_start_ms = 0;
  stanje.plate_position = 63;
  stanje.plate_phase = FAZA_STABILNO;
  ucitajLegacyPlocu(stanje.plate_position, stanje.plate_phase);
  stanje.version = UNIFIED_VERZIJA;
  stanje.reserved = RESERVED_SEKVENCA_POCETNA;
  return stanje;
}

String formatStanja(const EepromLayout::UnifiedMotionState& stanje) {
  String log = F("STANJE: hand=");
  log += stanje.hand_position;
  log += F(" active=");
  log += stanje.hand_active;
  log += F(" relay=");
  log += stanje.hand_relay;
  log += F(" plate=");
  log += stanje.plate_position;
  log += F(" phase=");
  log += stanje.plate_phase;
  return log;
}
}  // namespace

bool ucitaj(EepromLayout::UnifiedMotionState& stanje) {
  if (cacheInicijaliziran) {
    stanje = cacheStanje;
    return true;
  }

  if (!ucitajNajnovijeStanje(stanje)) {
    return false;
  }

  cacheStanje = stanje;
  cacheInicijaliziran = true;
  return true;
}

EepromLayout::UnifiedMotionState dohvatiIliMigriraj() {
  EepromLayout::UnifiedMotionState stanje{};
  if (ucitaj(stanje)) {
    return stanje;
  }

  stanje = inicijalnoStanje();
  spremiAkoPromjena(stanje);
  return stanje;
}

void spremiAkoPromjena(const EepromLayout::UnifiedMotionState& stanje) {
  if (cacheInicijaliziran && jednakoLogickoStanje(cacheStanje, stanje)) {
    return;
  }

  EepromLayout::UnifiedMotionState trenutno{};
  if (!cacheInicijaliziran && ucitaj(trenutno) &&
      jednakoLogickoStanje(trenutno, stanje)) {
    return;
  }

  EepromLayout::UnifiedMotionState stanjeZaSpremanje = stanje;
  stanjeZaSpremanje.version = UNIFIED_VERZIJA;
  const uint8_t zadnjaSekvenca = cacheInicijaliziran ? cacheStanje.reserved : trenutno.reserved;
  stanjeZaSpremanje.reserved =
    (zadnjaSekvenca == 0) ? RESERVED_SEKVENCA_POCETNA : static_cast<uint8_t>(zadnjaSekvenca + 1);

  WearLeveling::spremi(EepromLayout::BAZA_UNIFIED_STANJE,
                       EepromLayout::SLOTOVI_UNIFIED_STANJE,
                       stanjeZaSpremanje);
  cacheStanje = stanjeZaSpremanje;
  cacheInicijaliziran = true;
}

void logirajStanje(const EepromLayout::UnifiedMotionState& stanje) {
  posaljiPCLog(formatStanja(stanje));
}

}  // namespace UnifiedMotionStateStore
