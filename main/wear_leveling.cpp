#include <Arduino.h>
#include <avr/pgmspace.h>
#include <string.h>

#include "wear_leveling.h"

#include "eeprom_konstante.h"
#include "i2c_eeprom.h"
#include "pc_serial.h"

namespace WearLeveling {
namespace {
constexpr int EEPROM_KAPACITET = EepromLayout::LOGICKI_KAPACITET_KOMPATIBILNOG_RASPOREDA;
// Zadnji dio zadrzanog 4 KB kompatibilnog rasporeda rezerviran je za metapodatke wear-levelinga.
// Tu pamtimo koji je slot zadnji uspješno zapisan za pojedini segment
// kako bi recovery u modulima iz main/ nakon boota čitao najnovije stanje.
constexpr int BAZA_META = EepromLayout::BAZA_WEAR_LEVELING_META;
constexpr int BROJ_META_ZAPISA = 16;
constexpr unsigned long ODGODA_PONOVNOG_WEAR_LEVELING_LOGA_MS = 10000UL;

struct StanjeWearLevelingLoga {
  int adresa;
  size_t duljina;
  unsigned long zadnjiLogMs;
  bool biloPrije;
};

StanjeWearLevelingLoga zadnjiLogProcitaj = {-1, 0, 0UL, false};
StanjeWearLevelingLoga zadnjiLogZapisi = {-1, 0, 0UL, false};

struct MetaWearLeveling {
  uint16_t baznaAdresa;
  uint8_t brojSlotova;
  uint8_t zadnjiSlot;
  uint16_t velicinaSlota;
  uint16_t checksum;
};

struct SazetakMetaSegmenta {
  bool zauzet;
  MetaWearLeveling meta;
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

bool jeMetaPrazna(const MetaWearLeveling& meta) {
  const uint8_t* bajtovi = reinterpret_cast<const uint8_t*>(&meta);
  for (size_t i = 0; i < sizeof(MetaWearLeveling); ++i) {
    if (bajtovi[i] != 0xFF) {
      return false;
    }
  }
  return true;
}

bool jeIstiLogickiSegment(const MetaWearLeveling& meta,
                          int baznaAdresa,
                          int brojSlotova) {
  return jeMetaValjana(meta) &&
         meta.baznaAdresa == baznaAdresa &&
         meta.brojSlotova == brojSlotova;
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
  int zadnjiPronadeni = -1;
  for (int i = 0; i < BROJ_META_ZAPISA; ++i) {
    MetaWearLeveling meta{};
    if (!procitajMeta(i, meta)) {
      continue;
    }
    if (jeMetaValjana(meta) &&
        meta.baznaAdresa == baznaAdresa &&
        meta.brojSlotova == brojSlotova &&
        meta.velicinaSlota == velicinaSlota) {
      zadnjiPronadeni = i;
    }
  }
  return zadnjiPronadeni;
}

int pronadiKompatibilanMetaIndex(int baznaAdresa, int brojSlotova) {
  int zadnjiPronadeni = -1;
  for (int i = 0; i < BROJ_META_ZAPISA; ++i) {
    MetaWearLeveling meta{};
    if (!procitajMeta(i, meta)) {
      continue;
    }
    if (jeIstiLogickiSegment(meta, baznaAdresa, brojSlotova)) {
      // Dopusti da nova verzija firmwarea za isti logicki segment
      // preuzme zadnji kompatibilni meta zapis i kad se velicina slota promijeni.
      zadnjiPronadeni = i;
    }
  }
  return zadnjiPronadeni;
}

int pronadiIndeksZaAppendMetaZapisa() {
  int zadnjiNePrazan = -1;
  for (int i = 0; i < BROJ_META_ZAPISA; ++i) {
    MetaWearLeveling meta{};
    if (!procitajMeta(i, meta)) {
      continue;
    }
    if (!jeMetaPrazna(meta)) {
      zadnjiNePrazan = i;
    }
  }

  if (zadnjiNePrazan < 0) {
    return 0;
  }

  const int kandidat = zadnjiNePrazan + 1;
  if (kandidat >= BROJ_META_ZAPISA) {
    return -1;
  }

  MetaWearLeveling meta{};
  if (!procitajMeta(kandidat, meta) || !jeMetaPrazna(meta)) {
    return -1;
  }

  return kandidat;
}

bool obrisiSveMetapodatkeBezLoga() {
  uint8_t prazno[32];
  memset(prazno, 0xFF, sizeof(prazno));

  const int krajMeta = BAZA_META + BROJ_META_ZAPISA * static_cast<int>(sizeof(MetaWearLeveling));
  for (int adresa = BAZA_META; adresa < krajMeta; adresa += static_cast<int>(sizeof(prazno))) {
    const size_t blok =
        static_cast<size_t>(min(static_cast<int>(sizeof(prazno)), krajMeta - adresa));
    if (!VanjskiEEPROM::zapisi(adresa, prazno, blok)) {
      return false;
    }
  }

  return true;
}

bool kompaktirajMetaZapiseINadopuni(const MetaWearLeveling& noviMeta) {
  SazetakMetaSegmenta sazetci[BROJ_META_ZAPISA];
  for (int i = 0; i < BROJ_META_ZAPISA; ++i) {
    sazetci[i].zauzet = false;
  }

  int brojSazetaka = 0;
  for (int i = 0; i < BROJ_META_ZAPISA; ++i) {
    MetaWearLeveling meta{};
    if (!procitajMeta(i, meta) || !jeMetaValjana(meta) || !jeMetaZaAktivniSegment(meta)) {
      continue;
    }

    int postojeci = -1;
    for (int j = 0; j < brojSazetaka; ++j) {
      if (sazetci[j].zauzet &&
          sazetci[j].meta.baznaAdresa == meta.baznaAdresa &&
          sazetci[j].meta.brojSlotova == meta.brojSlotova) {
        postojeci = j;
        break;
      }
    }

    if (postojeci >= 0) {
      sazetci[postojeci].meta = meta;
    } else if (brojSazetaka < BROJ_META_ZAPISA) {
      sazetci[brojSazetaka].zauzet = true;
      sazetci[brojSazetaka].meta = meta;
      ++brojSazetaka;
    }
  }

  int postojeciNovi = -1;
  for (int i = 0; i < brojSazetaka; ++i) {
    if (sazetci[i].zauzet &&
        sazetci[i].meta.baznaAdresa == noviMeta.baznaAdresa &&
        sazetci[i].meta.brojSlotova == noviMeta.brojSlotova) {
      postojeciNovi = i;
      break;
    }
  }

  if (postojeciNovi >= 0) {
    sazetci[postojeciNovi].meta = noviMeta;
  } else if (brojSazetaka < BROJ_META_ZAPISA) {
    sazetci[brojSazetaka].zauzet = true;
    sazetci[brojSazetaka].meta = noviMeta;
    ++brojSazetaka;
  } else {
    return false;
  }

  if (!obrisiSveMetapodatkeBezLoga()) {
    return false;
  }

  int indeksZapisa = 0;
  for (int i = 0; i < brojSazetaka; ++i) {
    if (!sazetci[i].zauzet) {
      continue;
    }
    if (!zapisiMeta(indeksZapisa, sazetci[i].meta)) {
      return false;
    }
    ++indeksZapisa;
  }

  return true;
}
void logirajWearLevelingGreskuAkoTreba(StanjeWearLevelingLoga& stanje,
                                       PGM_P predlozak,
                                       int adresa,
                                       size_t duljina) {
  const unsigned long sadaMs = millis();
  const bool istaGreska =
      stanje.biloPrije &&
      stanje.adresa == adresa &&
      stanje.duljina == duljina;

  if (istaGreska &&
      (sadaMs - stanje.zadnjiLogMs) < ODGODA_PONOVNOG_WEAR_LEVELING_LOGA_MS) {
    return;
  }

  stanje.adresa = adresa;
  stanje.duljina = duljina;
  stanje.zadnjiLogMs = sadaMs;
  stanje.biloPrije = true;

  char log[72];
  snprintf_P(log, sizeof(log), predlozak, adresa, static_cast<unsigned>(duljina));
  posaljiPCLog(log);
}
}  // namespace

bool procitajSlot(int adresa, void* cilj, size_t duljina) {
  if (adresa < 0 || duljina == 0) {
    return false;
  }

  const bool uspjeh = VanjskiEEPROM::procitaj(adresa, cilj, duljina);
  if (!uspjeh) {
    logirajWearLevelingGreskuAkoTreba(
        zadnjiLogProcitaj,
        PSTR("WearLeveling: procitaj FAILED adresa=%d duljina=%u"),
        adresa,
        duljina);
  }
  return uspjeh;
}

bool napisiSlot(int adresa, const void* izvor, size_t duljina) {
  if (adresa < 0 || duljina == 0 || izvor == nullptr) {
    return false;
  }

  const bool uspjeh = VanjskiEEPROM::zapisi(adresa, izvor, duljina);
  if (!uspjeh) {
    logirajWearLevelingGreskuAkoTreba(
        zadnjiLogZapisi,
        PSTR("WearLeveling: zapisi FAILED adresa=%d duljina=%u"),
        adresa,
        duljina);
  }
  return uspjeh;
}

int odrediSlotZaCitanje(int baznaAdresa, int brojSlotova, size_t velicinaSlota) {
  const int metaIndex = pronadiTocniMetaIndex(baznaAdresa, brojSlotova, velicinaSlota);
  if (metaIndex >= 0) {
    MetaWearLeveling meta{};
    if (procitajMeta(metaIndex, meta) && jeMetaValjana(meta)) {
      return meta.zadnjiSlot;
    }
  }

  const int kompatibilniMetaIndex = pronadiKompatibilanMetaIndex(baznaAdresa, brojSlotova);
  if (kompatibilniMetaIndex >= 0) {
    MetaWearLeveling meta{};
    if (procitajMeta(kompatibilniMetaIndex, meta) && jeMetaValjana(meta)) {
      return meta.zadnjiSlot;
    }
  }

  return brojSlotova - 1;
}

int odrediSlotZaPisanje(int baznaAdresa, int brojSlotova, size_t velicinaSlota) {
  const int metaIndex = pronadiTocniMetaIndex(baznaAdresa, brojSlotova, velicinaSlota);
  if (metaIndex >= 0) {
    MetaWearLeveling meta{};
    if (procitajMeta(metaIndex, meta) && jeMetaValjana(meta)) {
      return (meta.zadnjiSlot + 1) % brojSlotova;
    }
  }

  const int kompatibilniMetaIndex = pronadiKompatibilanMetaIndex(baznaAdresa, brojSlotova);
  if (kompatibilniMetaIndex >= 0) {
    MetaWearLeveling meta{};
    if (procitajMeta(kompatibilniMetaIndex, meta) && jeMetaValjana(meta)) {
      return (meta.zadnjiSlot + 1) % brojSlotova;
    }
  }

  return 0;
}

void zapamtiZadnjiSlot(int baznaAdresa, int brojSlotova, size_t velicinaSlota, uint8_t slot) {
  MetaWearLeveling meta{};
  meta.baznaAdresa = static_cast<uint16_t>(baznaAdresa);
  meta.brojSlotova = static_cast<uint8_t>(brojSlotova);
  meta.zadnjiSlot = slot;
  meta.velicinaSlota = static_cast<uint16_t>(velicinaSlota);
  meta.checksum = izracunajChecksumMeta(meta);

  const int indeksAppend = pronadiIndeksZaAppendMetaZapisa();
  if (indeksAppend >= 0) {
    if (!zapisiMeta(indeksAppend, meta)) {
      posaljiPCLog(F("WearLeveling: upis meta zapisa nije uspio"));
    }
    return;
  }

  if (!kompaktirajMetaZapiseINadopuni(meta)) {
    posaljiPCLog(F("WearLeveling: kompaktiranje meta zapisa nije uspjelo"));
  }
}

bool obrisiSveMetapodatke() {
  const bool uspjeh = obrisiSveMetapodatkeBezLoga();
  posaljiPCLog(uspjeh
                   ? F("WearLeveling: metapodaci obrisani")
                   : F("WearLeveling: brisanje metapodataka nije uspjelo"));
  return uspjeh;
}

}  // namespace WearLeveling
