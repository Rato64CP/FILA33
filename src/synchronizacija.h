// synchronizacija.h
#ifndef SYNCHRONIZACIJA_H
#define SYNCHRONIZACIJA_H

#include <RTClib.h>
#include "vrijeme_izvor.h"

void sinkronizirajVrijemeIzvora(const DateTime& novoVrijeme, IzvorVremena izvor);

#endif
