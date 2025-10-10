// synchronizacija.cpp
#include <RTClib.h>
#include <EEPROM.h>
#include "kazaljke_sata.h"
#include "okretna_ploca.h"
#include "vrijeme_izvor.h"
#include "time_glob.h"

void sinkronizirajVrijemeIzvora(const DateTime& novoVrijeme, IzvorVremena izvor) {
  switch (izvor) {
    case NTP_VRIJEME:
      postaviVrijemeIzNTP(novoVrijeme);
      break;
    case RTC_VRIJEME:
    case NEPOZNATO_VRIJEME:
    default:
      postaviVrijemeRucno(novoVrijeme);
      break;
  }

  // Spremi izvor
  EEPROM.put(0, (int)izvor);

  // Pametna kompenzacija kazaljki i ploce
  kompenzirajKazaljke(true);
  kompenzirajPlocu(true);
}
