#pragma once

#include <stddef.h>
#include <stdint.h>

namespace WearLeveling {

// Trajno pamti zadnji zapisani slot za pojedini EEPROM segment.
// To je važno za recovery toranjskog sata nakon boota, kako bi
// moduli u main/ dohvatili stvarno najnovije stanje, a ne stariji slot.
int odrediSlotZaCitanje(int baznaAdresa, int brojSlotova, size_t velicinaSlota);
int odrediSlotZaPisanje(int baznaAdresa, int brojSlotova, size_t velicinaSlota);
void zapamtiZadnjiSlot(int baznaAdresa, int brojSlotova, size_t velicinaSlota, uint8_t slot);
bool obrisiSveMetapodatke();

template <typename T>
constexpr size_t velicinaSlota() {
  return sizeof(T);
}

template <typename T>
bool ucitaj(int baznaAdresa, int brojSlotova, T& cilj) {
  if (brojSlotova <= 0) return false;

  const size_t velicina = velicinaSlota<T>();
  // Prvo pokušaj od zadnjeg potvrđeno upisanog slota.
  const int pocetniSlot = odrediSlotZaCitanje(baznaAdresa, brojSlotova, velicina);
  if (pocetniSlot >= 0) {
    for (int pomak = 0; pomak < brojSlotova; ++pomak) {
      const int slot = (pocetniSlot - pomak + brojSlotova) % brojSlotova;
      const int adresa = baznaAdresa + slot * static_cast<int>(velicina);
      if (procitajSlot(adresa, &cilj, sizeof(T))) {
        return true;
      }
    }
  }

  // Fallback za stare zapise koji još nemaju metapodatke.
  for (int slot = brojSlotova - 1; slot >= 0; --slot) {
    const int adresa = baznaAdresa + slot * static_cast<int>(velicina);
    if (procitajSlot(adresa, &cilj, sizeof(T))) {
      return true;
    }
  }

  return false;
}

template <typename T>
bool spremi(int baznaAdresa, int brojSlotova, const T& izvor) {
  if (brojSlotova <= 0) return false;

  const size_t velicina = velicinaSlota<T>();
  const int slot = odrediSlotZaPisanje(baznaAdresa, brojSlotova, velicina);
  if (slot < 0) {
    return false;
  }

  const int adresa = baznaAdresa + slot * static_cast<int>(velicina);
  const bool uspjeh = napisiSlot(adresa, &izvor, sizeof(T));
  if (uspjeh) {
    // Nakon uspješnog zapisa ažuriraj trajni pokazivač najnovijeg slota.
    zapamtiZadnjiSlot(baznaAdresa, brojSlotova, velicina, static_cast<uint8_t>(slot));
  }
  return uspjeh;
}

bool procitajSlot(int adresa, void* cilj, size_t duljina);
bool napisiSlot(int adresa, const void* izvor, size_t duljina);

}  // namespace WearLeveling
