#pragma once

#include <EEPROM.h>
#include <stdint.h>
#include <string.h>

// Jednostavan wear-leveling sloj za EEPROM zapis povezan s toranjskim satom.
namespace WearLeveling {

template <typename T>
struct WearRecord {
  uint32_t brojac;
  T vrijednost;
  uint16_t crc;
};

uint16_t izracunajCRC(const uint8_t* podaci, size_t duljina);

// Veličina jednog slota za zadani tip.
template <typename T>
constexpr int velicinaSlota() {
  return static_cast<int>(sizeof(WearRecord<T>));
}

// Učitaj najnoviji valjani zapis iz wear-leveling bloka.
template <typename T>
bool ucitaj(int baznaAdresa, int brojSlotova, T& vrijednost) {
  const int velicina = velicinaSlota<T>();
  WearRecord<T> najbolji{};
  bool pronadjen = false;

  for (int i = 0; i < brojSlotova; ++i) {
    WearRecord<T> kandidat{};
    int adresa = baznaAdresa + i * velicina;
    EEPROM.get(adresa, kandidat);
    uint16_t crc = izracunajCRC(reinterpret_cast<uint8_t*>(&kandidat.brojac), sizeof(kandidat.brojac) + sizeof(T));
    if (crc != kandidat.crc || kandidat.brojac == 0xFFFFFFFF || kandidat.brojac == 0) {
      continue;
    }
    if (!pronadjen || kandidat.brojac > najbolji.brojac) {
      najbolji = kandidat;
      pronadjen = true;
    }
  }

  if (pronadjen) {
    memcpy(&vrijednost, &najbolji.vrijednost, sizeof(T));
  }
  return pronadjen;
}

// Spremi novi zapis u sljedeći slot kako bi se ravnomjerno trošila memorija.
template <typename T>
void spremi(int baznaAdresa, int brojSlotova, const T& vrijednost) {
  const int velicina = velicinaSlota<T>();
  uint32_t najvisiBrojac = 0;
  int zadnjiValidniSlot = -1;

  for (int i = 0; i < brojSlotova; ++i) {
    WearRecord<T> kandidat{};
    int adresa = baznaAdresa + i * velicina;
    EEPROM.get(adresa, kandidat);
    uint16_t crc = izracunajCRC(reinterpret_cast<uint8_t*>(&kandidat.brojac), sizeof(kandidat.brojac) + sizeof(T));
    if (crc != kandidat.crc || kandidat.brojac == 0xFFFFFFFF || kandidat.brojac == 0) {
      continue;
    }
    if (kandidat.brojac >= najvisiBrojac) {
      najvisiBrojac = kandidat.brojac;
      zadnjiValidniSlot = i;
    }
  }

  int sljedeciSlot = (zadnjiValidniSlot + 1) % brojSlotova;
  WearRecord<T> novi{};
  novi.brojac = najvisiBrojac + 1;
  memcpy(&novi.vrijednost, &vrijednost, sizeof(T));
  novi.crc = izracunajCRC(reinterpret_cast<uint8_t*>(&novi.brojac), sizeof(novi.brojac) + sizeof(T));

  int adresa = baznaAdresa + sljedeciSlot * velicina;
  EEPROM.put(adresa, novi);
}

}  // namespace WearLeveling

