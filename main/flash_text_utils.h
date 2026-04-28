#pragma once

#include <avr/pgmspace.h>
#include <string.h>

namespace FlashTekst {

inline void kopirajLiteral(char* odrediste, size_t velicina, PGM_P literal) {
  strncpy_P(odrediste, literal, velicina - 1);
  odrediste[velicina - 1] = '\0';
}

inline void ucitajIzNiza(const char* const* niz, int indeks, char* odrediste, size_t velicina) {
  strncpy_P(odrediste, reinterpret_cast<PGM_P>(pgm_read_ptr(&niz[indeks])), velicina - 1);
  odrediste[velicina - 1] = '\0';
}

}  // namespace FlashTekst
