// misna_automatika.h - izvrsavanje misnih najava koje prema Megi salje ESP
#pragma once

#include <stdint.h>

void inicijalizirajMisnuAutomatiku();
void upravljajMisnomAutomatikom();
bool pokreniESPMisnuNajavuRadniDan();
bool pokreniESPMisnuNajavuNedjelja();
bool pokreniESPMisnuNajavuBlagdan();
