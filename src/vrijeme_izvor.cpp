// vrijeme_izvor.cpp
#include <RTClib.h>
#include "vrijeme_izvor.h"
#include "time_glob.h"
#include "eeprom_konstante.h"
#include "wear_leveling.h"

void setZadnjaSinkronizacija(IzvorVremena izvor, const DateTime& vrijeme) {
  EepromLayout::ZadnjaSinkronizacija zapis{};
  zapis.izvor = static_cast<int>(izvor);
  zapis.timestamp = vrijeme.unixtime();
  WearLeveling::spremi(EepromLayout::BAZA_ZADNJA_SINKRONIZACIJA, EepromLayout::SLOTOVI_ZADNJA_SINKRONIZACIJA, zapis);
}

DateTime getZadnjeSinkroniziranoVrijeme() {
  EepromLayout::ZadnjaSinkronizacija zapis{};
  if (!WearLeveling::ucitaj(EepromLayout::BAZA_ZADNJA_SINKRONIZACIJA, EepromLayout::SLOTOVI_ZADNJA_SINKRONIZACIJA, zapis)) {
    return DateTime((uint32_t)0);
  }
  return DateTime(zapis.timestamp);
}

bool jeSinkronizacijaZastarjela() {
  DateTime sad = dohvatiTrenutnoVrijeme();
  DateTime zadnje = getZadnjeSinkroniziranoVrijeme();
  TimeSpan razlika = sad - zadnje;
  return razlika.totalseconds() > 86400; // 24h
}
