#pragma once

#include <stdint.h>
#include <RTClib.h>

void inicijalizirajSuncevuAutomatiku();
void upravljajSuncevomAutomatikom();

bool dohvatiDanasnjeVrijemeSuncevogDogadajaMin(uint8_t dogadaj, int& minute);
bool jeJutarnjeZvonjenjeOtvoriloOtkucavanje(const DateTime& vrijeme);
