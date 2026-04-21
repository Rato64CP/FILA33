#include <Arduino.h>
#include <avr/pgmspace.h>
#include <string.h>

#include "wear_leveling.h"

#include "eeprom_konstante.h"
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

bool jeMetaZaAktivniSegment(const MetaWearLeveling& meta) {
  if (!jeMetaValjana(meta)) {
    return false;
  }

  return
    (meta.baznaAdresa == EepromLayout::BAZA_ZADNJA_SINKRONIZACIJA &&
     meta.brojSlotova == EepromLayout::SLOTOVI_ZADNJA_SINKRONIZACIJA &&
     meta.velicinaSlota == EepromLayout::SLOT_SIZE_ZADNJA_SINKRONIZACIJA) ||
    (meta.baznaAdresa == EepromLayout::BAZA_POSTAVKE &&
     meta.brojSlotova == EepromLayout::SLOTOVI_POSTAVKE &&
     meta.velicinaSlota == EepromLayout::SLOT_SIZE_POSTAVKE) ||
    (meta.baznaAdresa == EepromLayout::BAZA_UNIFIED_STANJE &&
     meta.brojSlotova == EepromLayout::SLOTOVI_UNIFIED_STANJE &&
     meta.velicinaSlota == EepromLayout::SLOT_SIZE_UNIFIED_STANJE) ||
    (meta.baznaAdresa == EepromLayout::BAZA_DST_STATUS &&
     meta.brojSlotova == EepromLayout::SLOTOVI_DST_STATUS &&
     meta.velicinaSlota == EepromLayout::SLOT_SIZE_DST_STATUS) ||
    (meta.baznaAdresa == EepromLayout::BAZA_SUNCEVI_DOGADAJI &&
     meta.brojSlotova == EepromLayout::SLOTOVI_SUNCEVI_DOGADAJI &&
     meta.velicinaSlota == EepromLayout::SLOT_SIZE_SUNCEVI_DOGADAJI);
}

int pronadiTocniMetaIndex(int baznaAdresa, int brojSlotova, size_t velicinaSlota) {
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
  }
  return -1;
}

int pronadiMetaIndexZaPisanje(int baznaAdresa, int brojSlotova, size_t velicinaSlota) {
  int prazanIndex = -1;
  int kompatibilanIndex = -1;
  int zastarjeliIndex = -1;
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
    if (jeMetaValjana(meta) &&
        meta.baznaAdresa == baznaAdresa &&
        meta.brojSlotova == brojSlotova &&
        kompatibilanIndex < 0) {
      // Dopusti da nova verzija firmwarea za isti logicki segment
      // preuzme postojeci meta zapis, npr. nakon promjene velicine
      // strukture postavki toranjskog sata.
      kompatibilanIndex = i;
    }
    if (!jeMetaValjana(meta) && prazanIndex < 0) {
      prazanIndex = i;
    } else if (jeMetaValjana(meta) && !jeMetaZaAktivniSegment(meta) && zastarjeliIndex < 0) {
      // Ako je meta prostor pun starim rasporedima iz prijasnjih firmwarea,
      // recikliraj prvi zapis koji vise ne pripada nijednom aktivnom segmentu.
      zastarjeliIndex = i;
    }
  }
  if (kompatibilanIndex >= 0) {
    return kompatibilanIndex;
  }
  if (zastarjeliIndex >= 0) {
    return zastarjeliIndex;
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
    char log[72];
    snprintf_P(log, sizeof(log), PSTR("WearLeveling: procitaj FAILED adresa=%d duljina=%u"),
               adresa, static_cast<unsigned>(duljina));
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
    char log[72];
    snprintf_P(log, sizeof(log), PSTR("WearLeveling: zapisi FAILED adresa=%d duljina=%u"),
               adresa, static_cast<unsigned>(duljina));
    posaljiPCLog(log);
  }
  return uspjeh;
}

int odrediSlotZaCitanje(int baznaAdresa, int brojSlotova, size_t velicinaSlota) {
  const int metaIndex = pronadiTocniMetaIndex(baznaAdresa, brojSlotova, velicinaSlota);
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
  const int metaIndex = pronadiMetaIndexZaPisanje(baznaAdresa, brojSlotova, velicinaSlota);
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
  const int metaIndex = pronadiMetaIndexZaPisanje(baznaAdresa, brojSlotova, velicinaSlota);
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
    meta.velicinaSlota = static_cast<uint16_t>(velicinaSlota);
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

bool obrisiSveMetapodatke() {
  uint8_t prazno[32];
  memset(prazno, 0xFF, sizeof(prazno));

  const int krajMeta = BAZA_META + BROJ_META_ZAPISA * static_cast<int>(sizeof(MetaWearLeveling));
  for (int adresa = BAZA_META; adresa < krajMeta; adresa += static_cast<int>(sizeof(prazno))) {
    const size_t blok =
        static_cast<size_t>(min(static_cast<int>(sizeof(prazno)), krajMeta - adresa));
    if (!VanjskiEEPROM::zapisi(adresa, prazno, blok)) {
      posaljiPCLog(F("WearLeveling: brisanje metapodataka nije uspjelo"));
      return false;
    }
  }

  posaljiPCLog(F("WearLeveling: razvojni metapodaci obrisani"));
  return true;
}

}  // namespace WearLeveling
