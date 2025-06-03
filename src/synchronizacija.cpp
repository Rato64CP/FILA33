// synchronizacija.cpp
#include <RTClib.h>
#include <EEPROM.h>
#include "kazaljke_sata.h"
#include "okretna_ploca.h"
#include "vrijeme_izvor.h"
#include "time_glob.h"

void sinkronizirajVrijemeIzvora(const DateTime& novoVrijeme, IzvorVremena izvor) {
  rtc.adjust(novoVrijeme);

  // Spremi izvor
  EEPROM.put(0, (int)izvor);

  // Pametna kompenzacija kazaljki i ploce
  kompenzirajKazaljke(true);
  kompenzirajPlocu(true);
}
