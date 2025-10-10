// vrijeme_izvor.cpp
#include <EEPROM.h>
#include <RTClib.h>
#include "vrijeme_izvor.h"
#include "time_glob.h"

#define EEPROM_ADRESA_IZVOR 0
#define EEPROM_ADRESA_VRIJEME 4

void setZadnjaSinkronizacija(IzvorVremena izvor, const DateTime& vrijeme) {
  EEPROM.put(EEPROM_ADRESA_IZVOR, (int)izvor);
  uint32_t ts = vrijeme.unixtime();
  EEPROM.put(EEPROM_ADRESA_VRIJEME, ts);
}

IzvorVremena getZadnjiIzvor() {
  int i;
  EEPROM.get(EEPROM_ADRESA_IZVOR, i);
  if (i < 0 || i > 2) return NEPOZNATO_VRIJEME;
  return (IzvorVremena)i;
}

DateTime getZadnjeSinkroniziranoVrijeme() {
  uint32_t ts;
  EEPROM.get(EEPROM_ADRESA_VRIJEME, ts);
  return DateTime(ts);
}

bool jeSinkronizacijaZastarjela() {
  DateTime sad = dohvatiTrenutnoVrijeme();
  DateTime zadnje = getZadnjeSinkroniziranoVrijeme();
  TimeSpan razlika = sad - zadnje;
  return razlika.totalseconds() > 86400; // 24h
}
