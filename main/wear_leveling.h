// wear_leveling.h – EEPROM wear-leveling za dugovječnost
#pragma once

#include <stdint.h>
#include <stddef.h>

// Wear-leveling sustav koji rotira učitavanje/spremanje između višestrukih slotova
// kako bi se izbjeglo prekomjerno pisanje istih lokacija u EEPROM memoriji
namespace WearLeveling {

// Vraća veličinu slota za danu vrstu podataka
// Koristi se za izračunavanje raspored u EEPROM memoriji
template <typename T>
constexpr size_t velicinaSlota() {
  return sizeof(T);
}

// Učita vrijednost s rotacijom između slotova
// 'brojSlotova' je broj dostupnih slotova (npr. 6 za wear-leveling 6x)
template <typename T>
bool ucitaj(int baznaAdresa, int brojSlotova, T& cilj) {
  if (brojSlotova <= 0) return false;
  
  // Odaberi slot prema простом CRC ili counter mehanizmu
  // Prvo pokušaj najnoviji slot
  for (int slot = brojSlotova - 1; slot >= 0; --slot) {
    int adresa = baznaAdresa + slot * static_cast<int>(velicinaSlota<T>());
    if (procitajSlot(adresa, &cilj, sizeof(T))) {
      return true;
    }
  }
  
  return false;
}

// Spremi vrijednost s rotacijom između slotova (round-robin)
template <typename T>
bool spremi(int baznaAdresa, int brojSlotova, const T& izvor) {
  if (brojSlotova <= 0) return false;
  
  // Odaberi slot za pisanje (rotacija zbog wear-levelinga)
  static uint8_t slotCounter = 0;
  int slot = (slotCounter++) % brojSlotova;
  
  int adresa = baznaAdresa + slot * static_cast<int>(velicinaSlota<T>());
  return napisiSlot(adresa, &izvor, sizeof(T));
}

// Pomoćne funkcije za čitanje/pisanje sirovih bajtova
bool procitajSlot(int adresa, void* cilj, size_t duljina);
bool napisiSlot(int adresa, const void* izvor, size_t duljina);

}  // namespace WearLeveling