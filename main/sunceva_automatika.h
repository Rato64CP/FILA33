#pragma once

#include <stdint.h>

void inicijalizirajSuncevuAutomatiku();
void upravljajSuncevomAutomatikom();

bool jeSuncevaLokacijaValjana();
bool dohvatiDanasnjeVrijemeSuncevogDogadajaMin(uint8_t dogadaj, int& minute);
