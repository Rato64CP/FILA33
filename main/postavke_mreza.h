// postavke_mreza.h - Mrezni helperi postavki toranjskog sata
#pragma once

#include <stddef.h>

#include "eeprom_konstante.h"

bool jeKodiranNtpStatus(const char* ntpServer);
bool procitajNtpOmogucenostIzTeksta(const char* ntpServer);
const char* dohvatiNtpServerBezZastavice(const char* ntpServer);
void kodirajNtpServer(char* odrediste,
                      size_t velicina,
                      const char* ntpServer,
                      bool omogucen);
bool sanitizirajMreznaPolja(EepromLayout::PostavkeSpremnik& spremnik);
