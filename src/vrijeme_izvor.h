// vrijeme_izvor.h
#ifndef VRIJEME_IZVOR_H
#define VRIJEME_IZVOR_H

#include <RTClib.h>

enum IzvorVremena {
  RTC_VRIJEME = 0,
  NTP_VRIJEME = 1,
  NEPOZNATO_VRIJEME = 2
};

void setZadnjaSinkronizacija(IzvorVremena izvor, const DateTime& vrijeme);
IzvorVremena getZadnjiIzvor();
DateTime getZadnjeSinkroniziranoVrijeme();
bool jeSinkronizacijaZastarjela();

#endif
