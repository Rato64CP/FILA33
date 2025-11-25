#pragma once

#include "wear_leveling.h"

// Raspored wear-leveling blokova za EEPROM toranjskog sata.
namespace EepromLayout {

constexpr int SLOTOVI_KAZALJKE = 6;
constexpr int BAZA_KAZALJKE = 0;

constexpr int SLOTOVI_POZICIJA_PLOCE = 6;
constexpr int BAZA_POZICIJA_PLOCE = BAZA_KAZALJKE + WearLeveling::velicinaSlota<int>() * SLOTOVI_KAZALJKE;

constexpr int SLOTOVI_OFFSET_MINUTA = 4;
constexpr int BAZA_OFFSET_MINUTA = BAZA_POZICIJA_PLOCE + WearLeveling::velicinaSlota<int>() * SLOTOVI_POZICIJA_PLOCE;

constexpr int SLOTOVI_IZVOR_VREMENA = 6;
constexpr int BAZA_IZVOR_VREMENA = BAZA_OFFSET_MINUTA + WearLeveling::velicinaSlota<int>() * SLOTOVI_OFFSET_MINUTA;

struct ZadnjaSinkronizacija {
  int izvor;
  uint32_t timestamp;
};

constexpr int SLOTOVI_ZADNJA_SINKRONIZACIJA = 6;
constexpr int BAZA_ZADNJA_SINKRONIZACIJA = BAZA_IZVOR_VREMENA + WearLeveling::velicinaSlota<char[4]>() * SLOTOVI_IZVOR_VREMENA;

struct PostavkeSpremnik {
  int satOd;
  int satDo;
  int plocaPocetakMinuta;
  int plocaKrajMinuta;
  unsigned int trajanjeImpulsaCekicaMs;
  unsigned int pauzaIzmeduUdaraca;
  unsigned long trajanjeZvonjenjaRadniMs;
  unsigned long trajanjeZvonjenjaNedjeljaMs;
  unsigned long trajanjeSlavljenjaMs;
  uint8_t brojZvona;
  char pristupLozinka[9];
  char wifiSsid[33];
  char wifiLozinka[33];
  bool koristiDhcp;
  char statickaIp[16];
  char mreznaMaska[16];
  char zadaniGateway[16];
};

constexpr int SLOTOVI_POSTAVKE = 6;
constexpr int BAZA_POSTAVKE =
    BAZA_ZADNJA_SINKRONIZACIJA + WearLeveling::velicinaSlota<ZadnjaSinkronizacija>() * SLOTOVI_ZADNJA_SINKRONIZACIJA;

}  // namespace EepromLayout

