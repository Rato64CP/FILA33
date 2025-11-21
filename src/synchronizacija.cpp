// synchronizacija.cpp
#include <RTClib.h>
#include "kazaljke_sata.h"
#include "okretna_ploca.h"
#include "vrijeme_izvor.h"
#include "time_glob.h"

void sinkronizirajVrijemeIzvora(const DateTime& novoVrijeme, IzvorVremena izvor) {
  switch (izvor) {
    case NTP_VRIJEME:
      postaviVrijemeIzNTP(novoVrijeme);
      break;
    case DCF_VRIJEME:
      postaviVrijemeIzDCF(novoVrijeme);
      break;
    case RTC_VRIJEME:
    case NEPOZNATO_VRIJEME:
    default:
      postaviVrijemeRucno(novoVrijeme);
      break;
  }

  setZadnjaSinkronizacija(izvor, novoVrijeme);

  // Pametna kompenzacija kazaljki i ploce
  kompenzirajKazaljke(true);
  kompenzirajPlocu(true);
}
