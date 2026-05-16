#include <Arduino.h>
#include <avr/pgmspace.h>
#include <RTClib.h>
#include <string.h>

#include "mise_automatika.h"
#include "otkucavanje.h"
#include "pc_serial.h"
#include "postavke.h"
#include "prekidac_tisine.h"
#include "slavljenje_mrtvacko.h"
#include "time_glob.h"
#include "zvonjenje.h"

namespace {

constexpr unsigned long ODGODA_MISE_DO_SLIJEDECE_PROVJERE_MS = 1000UL;
constexpr uint8_t NEDJELJA_U_TJEDNU = 0;
constexpr uint8_t SEKUNDA_OKIDANJA_MISA = 25;
constexpr uint8_t MUSKO_ZVONO = 1;
constexpr uint16_t KLJUC_DATUMA_NEPOSTAVLJEN = 0xFFFFU;

enum TipMiseAutomatike : uint8_t {
  TIP_MISE_DNEVNA = 0,
  TIP_MISE_NEDJELJNA = 1,
  TIP_MISE_NEPOMICNI_BLAGDAN = 2,
  TIP_MISE_POMICNI_BLAGDAN = 3
};

struct ZakazanaMisaAutomatike {
  bool aktivna;
  TipMiseAutomatike tip;
  uint8_t indeksUnosa;
  uint16_t datumKljuc;
  uint16_t minutePrijeMise;
  unsigned long startMs;
  unsigned long trajanjeZvonaMs;
};

static ZakazanaMisaAutomatike zakazanaMisa = {
    false, TIP_MISE_DNEVNA, 0, 0, 0, 0, 0};
static uint32_t zadnjiObradeniKljucSekunde = 0xFFFFFFFFUL;
static uint16_t zadnjiOkinutiDnevnaMisa = KLJUC_DATUMA_NEPOSTAVLJEN;
static uint16_t zadnjiOkinutiNedjeljnaMisa = KLJUC_DATUMA_NEPOSTAVLJEN;
static uint16_t zadnjiOkinutiNepomicniBlagdan[BROJ_NEPOMICNIH_BLAGDANA];
static uint16_t zadnjiOkinutiPomicniBlagdan[BROJ_POMICNIH_BLAGDANA];
static uint32_t zadnjaObradenaMisaUTekucojSekundi = 0xFFFFFFFFUL;

static uint16_t napraviDatumKljuc(const DateTime& vrijeme) {
  return static_cast<uint16_t>((vrijeme.year() - 2000) * 512L +
                               vrijeme.month() * 32L +
                               vrijeme.day());
}

static uint32_t napraviKljucMinute(const DateTime& vrijeme) {
  return napraviDatumKljuc(vrijeme) * 1440UL +
         static_cast<uint32_t>(vrijeme.hour() * 60 + vrijeme.minute());
}

static uint32_t napraviKljucSekunde(const DateTime& vrijeme) {
  return napraviKljucMinute(vrijeme) * 60UL + static_cast<uint32_t>(vrijeme.second());
}

static uint16_t minuteUDanu(const DateTime& vrijeme) {
  return static_cast<uint16_t>(vrijeme.hour() * 60 + vrijeme.minute());
}

static bool vrijemeProslo(unsigned long ciljMs) {
  return static_cast<long>(millis() - ciljMs) >= 0;
}

static bool jeMehanikaZauzetaZaMisu() {
  return jeOtkucavanjeUTijeku() || jeZvonoUTijeku() || jeLiInerciaAktivna() ||
         jeSlavljenjeUTijeku() || jeMrtvackoUTijeku();
}

static bool tipMiseKoristiIndeks(TipMiseAutomatike tip) {
  return tip == TIP_MISE_NEPOMICNI_BLAGDAN || tip == TIP_MISE_POMICNI_BLAGDAN;
}

static const __FlashStringHelper* prefiksMiseZaLog(TipMiseAutomatike tip) {
  switch (tip) {
    case TIP_MISE_DNEVNA:
      return F("Mise: dnevna misa");
    case TIP_MISE_NEDJELJNA:
      return F("Mise: nedjeljna misa");
    case TIP_MISE_NEPOMICNI_BLAGDAN:
      return F("Blagdani: nepomicna misa");
    case TIP_MISE_POMICNI_BLAGDAN:
      return F("Blagdani: pomicna misa");
    default:
      return F("Mise: misa");
  }
}

static void logirajPorukuMise(TipMiseAutomatike tip,
                              const __FlashStringHelper* poruka,
                              uint8_t indeksUnosa,
                              uint16_t minutePrijeMise,
                              bool ukljuciOdmak) {
  char log[144];
  char prefiks[48];
  char porukaBuffer[48];
  char odmakBuffer[24] = "";
  const bool koristiIndeks = tipMiseKoristiIndeks(tip);
  strncpy_P(prefiks, reinterpret_cast<PGM_P>(prefiksMiseZaLog(tip)), sizeof(prefiks) - 1);
  prefiks[sizeof(prefiks) - 1] = '\0';
  strncpy_P(porukaBuffer, reinterpret_cast<PGM_P>(poruka), sizeof(porukaBuffer) - 1);
  porukaBuffer[sizeof(porukaBuffer) - 1] = '\0';

  if (ukljuciOdmak) {
    if (minutePrijeMise % 60U == 0U) {
      snprintf_P(odmakBuffer,
                 sizeof(odmakBuffer),
                 PSTR(" (%u h prije mise)"),
                 static_cast<unsigned>(minutePrijeMise / 60U));
    } else {
      snprintf_P(odmakBuffer,
                 sizeof(odmakBuffer),
                 PSTR(" (%u min prije mise)"),
                 static_cast<unsigned>(minutePrijeMise));
    }
  }

  if (koristiIndeks && ukljuciOdmak) {
    snprintf_P(log,
               sizeof(log),
               PSTR("%s %u: %s%s"),
               prefiks,
               static_cast<unsigned>(indeksUnosa + 1),
               porukaBuffer,
               odmakBuffer);
  } else if (koristiIndeks) {
    snprintf_P(log,
               sizeof(log),
               PSTR("%s %u: %s"),
               prefiks,
               static_cast<unsigned>(indeksUnosa + 1),
               porukaBuffer);
  } else if (ukljuciOdmak) {
    snprintf_P(log,
               sizeof(log),
               PSTR("%s: %s%s"),
               prefiks,
               porukaBuffer,
               odmakBuffer);
  } else {
    snprintf_P(log,
               sizeof(log),
               PSTR("%s: %s"),
               prefiks,
               porukaBuffer);
  }

  posaljiPCLog(log);
}

static bool jeNepomicniBlagdanAktivanDanas(uint8_t indeks,
                                           const DateTime& sada,
                                           NepomicniBlagdanPostavka& izlaz) {
  dohvatiNepomicniBlagdan(indeks, izlaz);
  return izlaz.omogucen &&
         izlaz.mjesec == static_cast<uint8_t>(sada.month()) &&
         izlaz.dan == static_cast<uint8_t>(sada.day());
}

static bool jePomicniBlagdanAktivanDanas(uint8_t indeks,
                                         const DateTime& sada,
                                         PomicniBlagdanPostavka& izlaz) {
  dohvatiPomicniBlagdan(indeks, izlaz);
  if (!izlaz.omogucen) {
    return false;
  }

  const DateTime uskrs = dohvatiDatumUskrsaZaGodinu(sada.year());
  const DateTime datumBlagdana =
      uskrs + TimeSpan(static_cast<int32_t>(izlaz.pomakOdUskrsaDana) * 86400L);
  return datumBlagdana.year() == sada.year() &&
         datumBlagdana.month() == sada.month() &&
         datumBlagdana.day() == sada.day();
}

static bool jeTerminMiseAktivan(const DateTime& sada,
                                uint8_t satMise,
                                uint8_t minutaMise,
                                uint16_t minutePrijeMise) {
  if (sada.second() != SEKUNDA_OKIDANJA_MISA) {
    return false;
  }

  const uint16_t terminMiseMinute =
      static_cast<uint16_t>(satMise) * 60U + static_cast<uint16_t>(minutaMise);
  const uint16_t trazenaMinutaUDanu =
      static_cast<uint16_t>((terminMiseMinute + 1440U - (minutePrijeMise % 1440U)) % 1440U);
  return minuteUDanu(sada) == trazenaMinutaUDanu;
}

static void zakaziMisu(TipMiseAutomatike tip,
                       uint8_t indeksUnosa,
                       uint16_t datumKljuc,
                       uint16_t minutePrijeMise,
                       unsigned long trajanjeZvonaMs) {
  zakazanaMisa.aktivna = true;
  zakazanaMisa.tip = tip;
  zakazanaMisa.indeksUnosa = indeksUnosa;
  zakazanaMisa.datumKljuc = datumKljuc;
  zakazanaMisa.minutePrijeMise = minutePrijeMise;
  zakazanaMisa.startMs = millis() + ODGODA_MISE_DO_SLIJEDECE_PROVJERE_MS;
  zakazanaMisa.trajanjeZvonaMs = trajanjeZvonaMs;
  logirajPorukuMise(tip, F("zakazano"), indeksUnosa, minutePrijeMise, true);
}

static void pokreniMisu(TipMiseAutomatike tip,
                        uint8_t indeksUnosa,
                        uint16_t minutePrijeMise,
                        unsigned long trajanjeZvonaMs) {
  if (tip == TIP_MISE_DNEVNA) {
    aktivirajZvonjenjeNaTrajanje(MUSKO_ZVONO, trajanjeZvonaMs);
  } else {
    const uint8_t brojZvona = dohvatiBrojZvona();
    for (uint8_t zvono = 1; zvono <= brojZvona; ++zvono) {
      aktivirajZvonjenjeNaTrajanje(zvono, trajanjeZvonaMs);
    }
  }
  logirajPorukuMise(tip, F("pokrenuto"), indeksUnosa, minutePrijeMise, true);
}

static void obradiZakazanuMisu(const DateTime& sada) {
  if (!zakazanaMisa.aktivna) {
    return;
  }

  if (zakazanaMisa.datumKljuc != napraviDatumKljuc(sada)) {
    zakazanaMisa.aktivna = false;
    return;
  }

  if (jePrekidacTisineAktivan()) {
    logirajPorukuMise(zakazanaMisa.tip,
                      F("preskoceno zbog tihog rezima"),
                      zakazanaMisa.indeksUnosa,
                      zakazanaMisa.minutePrijeMise,
                      false);
    zakazanaMisa.aktivna = false;
    return;
  }

  if (!vrijemeProslo(zakazanaMisa.startMs)) {
    return;
  }

  if (jeMehanikaZauzetaZaMisu()) {
    zakazanaMisa.startMs = millis() + ODGODA_MISE_DO_SLIJEDECE_PROVJERE_MS;
    return;
  }

  const TipMiseAutomatike tip = zakazanaMisa.tip;
  const uint8_t indeksUnosa = zakazanaMisa.indeksUnosa;
  const uint16_t minutePrijeMise = zakazanaMisa.minutePrijeMise;
  const unsigned long trajanjeZvonaMs = zakazanaMisa.trajanjeZvonaMs;
  zakazanaMisa.aktivna = false;
  pokreniMisu(tip, indeksUnosa, minutePrijeMise, trajanjeZvonaMs);
}

static void obradiJedanTerminMise(TipMiseAutomatike tip,
                                  uint8_t indeksUnosa,
                                  const DateTime& sada,
                                  uint8_t satMise,
                                  uint8_t minutaMise,
                                  uint16_t minutePrijeMise,
                                  unsigned long trajanjeZvonaMs,
                                  uint16_t& zadnjiKljucZaTip) {
  const uint16_t datumKljuc = napraviDatumKljuc(sada);
  const uint32_t kljucSekunde = napraviKljucSekunde(sada);

  if (!jeTerminMiseAktivan(sada, satMise, minutaMise, minutePrijeMise)) {
    return;
  }
  if (zadnjiKljucZaTip == kljucSekunde) {
    return;
  }

  zadnjiKljucZaTip = kljucSekunde;
  if (zadnjaObradenaMisaUTekucojSekundi == kljucSekunde) {
    return;
  }
  zadnjaObradenaMisaUTekucojSekundi = kljucSekunde;

  if (!jeVrijemePotvrdjenoZaAutomatiku()) {
    logirajPorukuMise(tip, F("preskoceno jer vrijeme nije potvrdjeno"),
                      indeksUnosa, minutePrijeMise, false);
    return;
  }

  if (jePrekidacTisineAktivan()) {
    logirajPorukuMise(tip, F("preskoceno zbog tihog rezima"),
                      indeksUnosa, minutePrijeMise, false);
    return;
  }

  if (jeMehanikaZauzetaZaMisu()) {
    zakaziMisu(tip, indeksUnosa, datumKljuc, minutePrijeMise, trajanjeZvonaMs);
    return;
  }

  pokreniMisu(tip, indeksUnosa, minutePrijeMise, trajanjeZvonaMs);
}

static void obradiBlagdanskeMise(const DateTime& sada) {
  const unsigned long trajanjeZvonaMs = dohvatiTrajanjeZvonjenjaNedjeljaMs();
  constexpr uint16_t ODMACI_BLAGDANSKE_MISE_MIN[] = {120U, 60U};

  for (uint8_t i = 0; i < BROJ_NEPOMICNIH_BLAGDANA; ++i) {
    NepomicniBlagdanPostavka blagdan;
    if (!jeNepomicniBlagdanAktivanDanas(i, sada, blagdan)) {
      continue;
    }

    for (uint8_t odmak = 0; odmak < 2; ++odmak) {
      obradiJedanTerminMise(TIP_MISE_NEPOMICNI_BLAGDAN,
                            i,
                            sada,
                            blagdan.satMise,
                            blagdan.minutaMise,
                            ODMACI_BLAGDANSKE_MISE_MIN[odmak],
                            trajanjeZvonaMs,
                            zadnjiOkinutiNepomicniBlagdan[i]);
    }
  }

  for (uint8_t i = 0; i < BROJ_POMICNIH_BLAGDANA; ++i) {
    PomicniBlagdanPostavka blagdan;
    if (!jePomicniBlagdanAktivanDanas(i, sada, blagdan)) {
      continue;
    }

    for (uint8_t odmak = 0; odmak < 2; ++odmak) {
      obradiJedanTerminMise(TIP_MISE_POMICNI_BLAGDAN,
                            i,
                            sada,
                            blagdan.satMise,
                            blagdan.minutaMise,
                            ODMACI_BLAGDANSKE_MISE_MIN[odmak],
                            trajanjeZvonaMs,
                            zadnjiOkinutiPomicniBlagdan[i]);
    }
  }
}

static void obradiRedoviteMise(const DateTime& sada) {
  RedoviteMisePostavke mise;
  dohvatiRedoviteMise(mise);

  if (sada.dayOfTheWeek() == NEDJELJA_U_TJEDNU) {
    if (mise.nedjeljnaOmogucena) {
      obradiJedanTerminMise(TIP_MISE_NEDJELJNA,
                            0,
                            sada,
                            mise.nedjeljnaSatMise,
                            mise.nedjeljnaMinutaMise,
                            120U,
                            dohvatiTrajanjeZvonjenjaNedjeljaMs(),
                            zadnjiOkinutiNedjeljnaMisa);
      obradiJedanTerminMise(TIP_MISE_NEDJELJNA,
                            0,
                            sada,
                            mise.nedjeljnaSatMise,
                            mise.nedjeljnaMinutaMise,
                            60U,
                            dohvatiTrajanjeZvonjenjaNedjeljaMs(),
                            zadnjiOkinutiNedjeljnaMisa);
    }
    return;
  }

  if (mise.dnevnaOmogucena) {
    obradiJedanTerminMise(TIP_MISE_DNEVNA,
                          0,
                          sada,
                          mise.dnevnaSatMise,
                          mise.dnevnaMinutaMise,
                          30U,
                          dohvatiTrajanjeZvonjenjaRadniMs(),
                          zadnjiOkinutiDnevnaMisa);
  }
}

}  // namespace

void inicijalizirajMiseAutomatiku() {
  zakazanaMisa.aktivna = false;
  zadnjiObradeniKljucSekunde = 0xFFFFFFFFUL;
  zadnjiOkinutiDnevnaMisa = KLJUC_DATUMA_NEPOSTAVLJEN;
  zadnjiOkinutiNedjeljnaMisa = KLJUC_DATUMA_NEPOSTAVLJEN;
  zadnjaObradenaMisaUTekucojSekundi = 0xFFFFFFFFUL;
  for (uint8_t i = 0; i < BROJ_NEPOMICNIH_BLAGDANA; ++i) {
    zadnjiOkinutiNepomicniBlagdan[i] = KLJUC_DATUMA_NEPOSTAVLJEN;
  }
  for (uint8_t i = 0; i < BROJ_POMICNIH_BLAGDANA; ++i) {
    zadnjiOkinutiPomicniBlagdan[i] = KLJUC_DATUMA_NEPOSTAVLJEN;
  }
}

void upravljajMiseAutomatikom() {
  const DateTime sada = dohvatiTrenutnoVrijeme();
  const uint32_t kljucSekunde = napraviKljucSekunde(sada);
  if (kljucSekunde == zadnjiObradeniKljucSekunde) {
    return;
  }

  zadnjiObradeniKljucSekunde = kljucSekunde;
  zadnjaObradenaMisaUTekucojSekundi = 0xFFFFFFFFUL;
  obradiBlagdanskeMise(sada);
  obradiRedoviteMise(sada);
  obradiZakazanuMisu(sada);
}
