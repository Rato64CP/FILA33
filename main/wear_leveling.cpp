#include <Arduino.h>

#include "wear_leveling.h"

#include "i2c_eeprom.h"
#include "pc_serial.h"

namespace WearLeveling {
namespace {
constexpr int EEPROM_KAPACITET = 4096;
// Zadnji dio 24C32 rezerviran je za metapodatke wear-levelinga.
// Tu pamtimo koji je slot zadnji uspješno zapisan za pojedini segment
// kako bi recovery u modulima iz main/ nakon boota čitao najnovije stanje.
constexpr int BAZA_META = 3968;
constexpr int BROJ_META_ZAPISA = 16;

struct MetaWearLeveling {
  uint16_t baznaAdresa;
  uint8_t brojSlotova;
  uint8_t zadnjiSlot;
  uint16_t velicinaSlota;
  uint16_t checksum;
};

static_assert(
  BAZA_META + BROJ_META_ZAPISA * static_cast<int>(sizeof(MetaWearLeveling)) <= EEPROM_KAPACITET,
  "Wear leveling metapodaci izlaze izvan EEPROM kapaciteta"
);

uint16_t izracunajChecksumMeta(const MetaWearLeveling& meta) {
  uint16_t checksum = 0;
  checksum += meta.baznaAdresa;
  checksum += meta.brojSlotova;
  checksum += meta.zadnjiSlot;
  checksum += meta.velicinaSlota;
  return checksum;
}

bool jeMetaValjana(const MetaWearLeveling& meta) {
  return meta.baznaAdresa != 0xFFFF &&
         meta.brojSlotova > 0 &&
         meta.zadnjiSlot < meta.brojSlotova &&
         meta.velicinaSlota > 0 &&
         meta.checksum == izracunajChecksumMeta(meta);
}

int adresaMetaZapisa(int index) {
  return BAZA_META + index * static_cast<int>(sizeof(MetaWearLeveling));
}

bool procitajMeta(int index, MetaWearLeveling& meta) {
  return VanjskiEEPROM::procitaj(adresaMetaZapisa(index), &meta, sizeof(meta));
}

bool zapisiMeta(int index, const MetaWearLeveling& meta) {
  return VanjskiEEPROM::zapisi(adresaMetaZapisa(index), &meta, sizeof(meta));
}

int pronadiMetaIndex(int baznaAdresa, int brojSlotova, size_t velicinaSlota) {
  int prazanIndex = -1;
  for (int i = 0; i < BROJ_META_ZAPISA; ++i) {
    MetaWearLeveling meta{};
    if (!procitajMeta(i, meta)) {
      continue;
    }
    if (jeMetaValjana(meta) &&
        meta.baznaAdresa == baznaAdresa &&
        meta.brojSlotova == brojSlotova &&
        meta.velicinaSlota == velicinaSlota) {
      return i;
    }
    if (!jeMetaValjana(meta) && prazanIndex < 0) {
      prazanIndex = i;
    }
  }
  return prazanIndex;
}
}  // namespace

bool procitajSlot(int adresa, void* cilj, size_t duljina) {
  if (adresa < 0 || duljina == 0) {
    return false;
  }

  const bool uspjeh = VanjskiEEPROM::procitaj(adresa, cilj, duljina);
  if (!uspjeh) {
    String log = F("WearLeveling: procitaj FAILED adresa=");
    log += adresa;
    log += F(" duljina=");
    log += duljina;
    posaljiPCLog(log);
  }
  return uspjeh;
}

bool napisiSlot(int adresa, const void* izvor, size_t duljina) {
  if (adresa < 0 || duljina == 0 || izvor == nullptr) {
    return false;
  }

  const bool uspjeh = VanjskiEEPROM::zapisi(adresa, izvor, duljina);
  if (!uspjeh) {
    String log = F("WearLeveling: zapisi FAILED adresa=");
    log += adresa;
    log += F(" duljina=");
    log += duljina;
    posaljiPCLog(log);
  }
  return uspjeh;
}

int odrediSlotZaCitanje(int baznaAdresa, int brojSlotova, size_t velicinaSlota) {
  const int metaIndex = pronadiMetaIndex(baznaAdresa, brojSlotova, velicinaSlota);
  if (metaIndex < 0) {
    return brojSlotova - 1;
  }

  MetaWearLeveling meta{};
  if (!procitajMeta(metaIndex, meta) || !jeMetaValjana(meta)) {
    return brojSlotova - 1;
  }
  return meta.zadnjiSlot;
}

int odrediSlotZaPisanje(int baznaAdresa, int brojSlotova, size_t velicinaSlota) {
  const int metaIndex = pronadiMetaIndex(baznaAdresa, brojSlotova, velicinaSlota);
  if (metaIndex < 0) {
    return 0;
  }

  MetaWearLeveling meta{};
  if (!procitajMeta(metaIndex, meta) || !jeMetaValjana(meta)) {
    return 0;
  }
  return (meta.zadnjiSlot + 1) % brojSlotova;
}

void zapamtiZadnjiSlot(int baznaAdresa, int brojSlotova, size_t velicinaSlota, uint8_t slot) {
  const int metaIndex = pronadiMetaIndex(baznaAdresa, brojSlotova, velicinaSlota);
  if (metaIndex < 0) {
    posaljiPCLog(F("WearLeveling: nema slobodnog meta zapisa za segment"));
    return;
  }

  MetaWearLeveling postojeci{};
  if (procitajMeta(metaIndex, postojeci) &&
      jeMetaValjana(postojeci) &&
      postojeci.baznaAdresa == baznaAdresa &&
      postojeci.brojSlotova == brojSlotova) {
    MetaWearLeveling meta = postojeci;
    meta.zadnjiSlot = slot;
    meta.checksum = izracunajChecksumMeta(meta);
    zapisiMeta(metaIndex, meta);
    return;
  }

  MetaWearLeveling meta{};
  meta.baznaAdresa = static_cast<uint16_t>(baznaAdresa);
  meta.brojSlotova = static_cast<uint8_t>(brojSlotova);
  meta.zadnjiSlot = slot;
  meta.velicinaSlota = static_cast<uint16_t>(velicinaSlota);
  meta.checksum = izracunajChecksumMeta(meta);
  zapisiMeta(metaIndex, meta);
}

}  // namespace WearLeveling
