#include "sunceva_automatika.h"

#include <Arduino.h>
#include <avr/pgmspace.h>
#include <RTClib.h>
#include <math.h>
#include "otkucavanje.h"
#include "podesavanja_piny.h"
#include "postavke.h"
#include "time_glob.h"
#include "zvonjenje.h"
#include "slavljenje_mrtvacko.h"
#include "pc_serial.h"
#include "prekidac_tisine.h"
#include "debouncing.h"

namespace {

constexpr float PI_F = 3.14159265f;
constexpr float SUNCEV_ZENIT_RAD = 90.833f * (PI_F / 180.0f);
constexpr int32_t SUNCEVA_LOKACIJA_SIRINA_E5 = 4350000L;
constexpr int32_t SUNCEVA_LOKACIJA_DUZINA_E5 = 1695000L;
constexpr int NAJRANIJE_JUTARNJE_ZVONJENJE_MINUTA = 5 * 60;
constexpr int FIKSNO_PODNE_MINUTA = 12 * 60;
constexpr unsigned long ODGODA_SUNCA_DO_SLIJEDECE_PROVJERE_MS = 1000UL;
constexpr unsigned long TREPTANJE_LAMPICE_SUNCA_MS = 500UL;

struct DnevniRasporedSuncevihDogadaja {
  bool valjano;
  uint32_t datumKljuc;
  int minute[SUNCEVI_DOGADAJ_BROJ];
};

struct ZakazanoSuncevoZvonjenje {
  bool aktivno;
  uint8_t dogadaj;
  uint8_t zvono;
  uint32_t datumKljuc;
  unsigned long startMs;
  unsigned long trajanjeMs;
};

struct ZakazanoBlagdanskoSlavljenje {
  bool aktivno;
  bool pokrenuto;
  uint8_t dogadaj;
  uint32_t datumKljuc;
  unsigned long startMs;
  unsigned long trajanjeMs;
  unsigned long krajMs;
};

static DnevniRasporedSuncevihDogadaja raspored = {false, 0, {-1, -1, -1}};
static ZakazanoSuncevoZvonjenje zakazanoZvonjenje = {false, 0, 0, 0, 0, 0};
static ZakazanoBlagdanskoSlavljenje zakazanoBlagdanskoSlavljenje = {
    false, false, 0, 0, 0, 0, 0};
static uint32_t zadnjiObradeniKljucMinute = 0xFFFFFFFFUL;
static uint32_t zadnjiObradeniKljucSekunde = 0xFFFFFFFFUL;
static uint32_t zadnjiOkinutiDatum[SUNCEVI_DOGADAJ_BROJ] = {0, 0, 0};
static uint32_t datumJutarnjegStartaOtkucavanja = 0;
static int minutaJutarnjegStartaOtkucavanja = -1;
static bool sviSvetiMrtvackoAktivno = false;
static bool nocnaRasvjetaUkljucena = false;
static int32_t zadnjaSirinaE5 = 0x7FFFFFFFL;
static int32_t zadnjaDuzinaE5 = 0x7FFFFFFFL;
static uint8_t zadnjaMaskaDogadaja = 0xFF;
static uint8_t zadnjaZvona[SUNCEVI_DOGADAJ_BROJ] = {0, 0, 0};
static int16_t zadnjeOdgode[SUNCEVI_DOGADAJ_BROJ] = {0, 0, 0};
static bool aktivnoSuncevoZvonjenje[SUNCEVI_DOGADAJ_BROJ] = {false, false, false};
static uint8_t aktivnoSuncevoZvono[SUNCEVI_DOGADAJ_BROJ] = {0, 0, 0};
static unsigned long krajSuncevogZvonjenjaMs[SUNCEVI_DOGADAJ_BROJ] = {0, 0, 0};

static uint32_t napraviDatumKljuc(const DateTime& vrijeme) {
  return static_cast<uint32_t>((vrijeme.year() - 2000) * 512L +
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

static bool jePrijestupnaGodina(int godina) {
  if ((godina % 400) == 0) {
    return true;
  }
  if ((godina % 100) == 0) {
    return false;
  }
  return (godina % 4) == 0;
}

static uint16_t izracunajDanUGodini(const DateTime& datum) {
  static const uint8_t DANI_U_MJESECIMA[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  uint16_t danUGodini = datum.day();
  for (uint8_t mjesec = 1; mjesec < datum.month(); ++mjesec) {
    danUGodini += DANI_U_MJESECIMA[mjesec - 1];
    if (mjesec == 2 && jePrijestupnaGodina(datum.year())) {
      ++danUGodini;
    }
  }
  return danUGodini;
}

static float stupnjeviURadijane(float stupnjevi) {
  return stupnjevi * (PI_F / 180.0f);
}

static float radijaniUStupnjeve(float radijani) {
  return radijani * (180.0f / PI_F);
}

static int normalizirajMinuteUDanu(int minute) {
  while (minute < 0) {
    minute += 1440;
  }
  while (minute >= 1440) {
    minute -= 1440;
  }
  return minute;
}

static int izracunajJutarnjeVrijemeZvonjenja(int izlazMinute) {
  const int jutroSPrilagodbom =
      izlazMinute + dohvatiOdgoduSuncevogDogadajaMin(SUNCEVI_DOGADAJ_JUTRO);

  // Jutarnje suncevo zvonjenje toranjskog sata ne dopustamo prije 05:00,
  // cak ni kad negativni pomak gura dogadaj ranije.
  if (jutroSPrilagodbom < NAJRANIJE_JUTARNJE_ZVONJENJE_MINUTA) {
    return NAJRANIJE_JUTARNJE_ZVONJENJE_MINUTA;
  }

  return normalizirajMinuteUDanu(jutroSPrilagodbom);
}

static bool jeLokacijaValjana(int32_t sirinaE5, int32_t duzinaE5) {
  return sirinaE5 >= -9000000L && sirinaE5 <= 9000000L &&
         duzinaE5 >= -18000000L && duzinaE5 <= 18000000L &&
         !(sirinaE5 == 0 && duzinaE5 == 0);
}

static bool jeRucnoUpravljivSuncevDogadaj(uint8_t dogadaj) {
  return dogadaj == SUNCEVI_DOGADAJ_JUTRO || dogadaj == SUNCEVI_DOGADAJ_VECER;
}

static uint8_t dohvatiPinLampiceSuncevogDogadaja(uint8_t dogadaj) {
  return (dogadaj == SUNCEVI_DOGADAJ_JUTRO) ? PIN_LAMPICA_SUNCE_JUTRO : PIN_LAMPICA_SUNCE_VECER;
}

static uint8_t dohvatiPinTipkeSuncevogDogadaja(uint8_t dogadaj) {
  return (dogadaj == SUNCEVI_DOGADAJ_JUTRO) ? PIN_TIPKA_SUNCE_JUTRO : PIN_TIPKA_SUNCE_VECER;
}

static bool izracunajSunceveMinuteZaDatum(const DateTime& datum,
                                          int32_t sirinaE5,
                                          int32_t duzinaE5,
                                          int& izlazMinute,
                                          int& zalazMinute) {
  if (!jeLokacijaValjana(sirinaE5, duzinaE5)) {
    return false;
  }

  const float sirina = static_cast<float>(sirinaE5) / 100000.0f;
  const float duzina = static_cast<float>(duzinaE5) / 100000.0f;
  const uint16_t danUGodini = izracunajDanUGodini(datum);
  const float gama = 2.0f * PI_F / 365.0f * (static_cast<float>(danUGodini) - 1.0f);

  const float jednadzbaVremena =
      229.18f * (0.000075f +
                 0.001868f * cosf(gama) -
                 0.032077f * sinf(gama) -
                 0.014615f * cosf(2.0f * gama) -
                 0.040849f * sinf(2.0f * gama));

  const float deklinacija =
      0.006918f -
      0.399912f * cosf(gama) +
      0.070257f * sinf(gama) -
      0.006758f * cosf(2.0f * gama) +
      0.000907f * sinf(2.0f * gama) -
      0.002697f * cosf(3.0f * gama) +
      0.001480f * sinf(3.0f * gama);

  const float sirinaRad = stupnjeviURadijane(sirina);
  const float nazivnik = cosf(sirinaRad) * cosf(deklinacija);
  if (fabsf(nazivnik) < 0.000001f) {
    return false;
  }

  const float cosKutnogSata =
      (cosf(SUNCEV_ZENIT_RAD) / nazivnik) - tanf(sirinaRad) * tanf(deklinacija);
  if (cosKutnogSata < -1.0f || cosKutnogSata > 1.0f) {
    return false;
  }

  const float kutniSatStupnjevi = radijaniUStupnjeve(acosf(cosKutnogSata));
  // Za sunceve dogadaje trebamo civilni UTC offset koji vrijedi za taj dan
  // u trenutku stvarnog dogadaja. Referentno lokalno podne daje stabilan
  // odgovor i na dan prijelaza CET/CEST, pa jutro/vecer ne ovise o tome je li
  // raspored racunat prije ili nakon samog DST prijelaza.
  const DateTime referentnoPodne(datum.year(), datum.month(), datum.day(), 12, 0, 0);
  const int utcOffsetMinute = dohvatiUTCOffsetMinuteZaLokalnoVrijeme(referentnoPodne);
  const float solarnoPodne = 720.0f - (4.0f * duzina) - jednadzbaVremena + utcOffsetMinute;
  const float delta = kutniSatStupnjevi * 4.0f;

  izlazMinute = normalizirajMinuteUDanu(static_cast<int>((solarnoPodne - delta) + 0.5f));
  zalazMinute = normalizirajMinuteUDanu(static_cast<int>((solarnoPodne + delta) + 0.5f));
  return true;
}

static bool jeKonfiguracijaPromijenjena() {
  if (zadnjaSirinaE5 != SUNCEVA_LOKACIJA_SIRINA_E5 ||
      zadnjaDuzinaE5 != SUNCEVA_LOKACIJA_DUZINA_E5) {
    return true;
  }

  uint8_t maska = 0;
  for (uint8_t i = 0; i < SUNCEVI_DOGADAJ_BROJ; ++i) {
    if (jeSuncevDogadajOmogucen(i)) {
      maska |= static_cast<uint8_t>(1U << i);
    }
    if (zadnjaZvona[i] != dohvatiZvonoZaSuncevDogadaj(i) ||
        zadnjeOdgode[i] != dohvatiOdgoduSuncevogDogadajaMin(i)) {
      return true;
    }
  }

  return zadnjaMaskaDogadaja != maska;
}

static void zapamtiKonfiguraciju() {
  zadnjaSirinaE5 = SUNCEVA_LOKACIJA_SIRINA_E5;
  zadnjaDuzinaE5 = SUNCEVA_LOKACIJA_DUZINA_E5;
  zadnjaMaskaDogadaja = 0;
  for (uint8_t i = 0; i < SUNCEVI_DOGADAJ_BROJ; ++i) {
    if (jeSuncevDogadajOmogucen(i)) {
      zadnjaMaskaDogadaja |= static_cast<uint8_t>(1U << i);
    }
    zadnjaZvona[i] = dohvatiZvonoZaSuncevDogadaj(i);
    zadnjeOdgode[i] = static_cast<int16_t>(dohvatiOdgoduSuncevogDogadajaMin(i));
  }
}

static const char* nazivDogadajaTekst(uint8_t dogadaj) {
  if (dogadaj == SUNCEVI_DOGADAJ_JUTRO) {
    return "jutro";
  }
  if (dogadaj == SUNCEVI_DOGADAJ_PODNE) {
    return "podne";
  }
  return "vecer";
}

static const char* nazivBlagdanskogRazdobljaTekst(uint8_t razdoblje) {
  if (razdoblje == BLAGDAN_ANTE) {
    return "sv. Ante";
  }
  if (razdoblje == BLAGDAN_PETAR) {
    return "sv. Petra";
  }
  return "Velike Gospe";
}

static bool jeDatumURasponu(const DateTime& sada,
                            uint8_t mjesec,
                            uint8_t prviDan,
                            uint8_t zadnjiDan) {
  return sada.month() == mjesec && sada.day() >= prviDan && sada.day() <= zadnjiDan;
}

static bool dohvatiAktivnoBlagdanskoRazdoblje(const DateTime& sada, uint8_t& razdoblje) {
  if (jeBlagdanskoRazdobljeOmoguceno(BLAGDAN_ANTE) &&
      jeDatumURasponu(sada, 6, 6, 13)) {
    razdoblje = BLAGDAN_ANTE;
    return true;
  }

  if (jeBlagdanskoRazdobljeOmoguceno(BLAGDAN_PETAR) &&
      jeDatumURasponu(sada, 6, 22, 28)) {
    razdoblje = BLAGDAN_PETAR;
    return true;
  }

  if (jeBlagdanskoRazdobljeOmoguceno(BLAGDAN_VELIKA_GOSPA) &&
      jeDatumURasponu(sada, 8, 8, 15)) {
    razdoblje = BLAGDAN_VELIKA_GOSPA;
    return true;
  }

  return false;
}

static bool trebaSlavitiBlagdanNakonDogadaja(uint8_t dogadaj,
                                             const DateTime& sada,
                                             uint8_t& razdoblje) {
  if (!jeBlagdanskoSlavljenjeOmoguceno(dogadaj)) {
    return false;
  }

  return dohvatiAktivnoBlagdanskoRazdoblje(sada, razdoblje);
}

static bool jeDanSvihSvetih(const DateTime& sada) {
  return sada.month() == 11 && sada.day() == 1;
}

static bool jeDusniDan(const DateTime& sada) {
  return sada.month() == 11 && sada.day() == 2;
}

static bool trebaPreskocitiSuncevDogadajZbogSvihSvetih(uint8_t dogadaj, const DateTime& sada) {
  if (!jeSviSvetiMrtvackoOmoguceno()) {
    return false;
  }

  return (jeDanSvihSvetih(sada) && dogadaj == SUNCEVI_DOGADAJ_VECER) ||
         (jeDusniDan(sada) && dogadaj == SUNCEVI_DOGADAJ_JUTRO);
}

static bool jeSviSvetiMrtvackoProzorAktivan(const DateTime& sada) {
  if (!jeSviSvetiMrtvackoOmoguceno()) {
    return false;
  }

  const int minutaUDanu = sada.hour() * 60 + sada.minute();
  if (jeDanSvihSvetih(sada)) {
    return minutaUDanu >= static_cast<int>(dohvatiSviSvetiPocetakSat()) * 60 &&
           minutaUDanu < 21 * 60;
  }

  if (jeDusniDan(sada)) {
    return minutaUDanu >= 6 * 60 &&
           minutaUDanu < static_cast<int>(dohvatiSviSvetiZavrsetakSat()) * 60;
  }

  return false;
}

static void postaviRelejNocneRasvjete(bool ukljuceno) {
  digitalWrite(PIN_RELEJ_NOCNE_RASVJETE, ukljuceno ? HIGH : LOW);
}

static void postaviNocnuRasvjetu(bool ukljuceno) {
  if (nocnaRasvjetaUkljucena == ukljuceno) {
    return;
  }

  nocnaRasvjetaUkljucena = ukljuceno;
  postaviRelejNocneRasvjete(ukljuceno);
  posaljiPCLog(ukljuceno ? F("Sunce: nocna rasvjeta ukljucena")
                         : F("Sunce: nocna rasvjeta iskljucena"));
}

static bool jeVrijemeUProzoru(int trenutnaMinutaUDanu, int pocetakMinuta, int krajMinuta) {
  if (pocetakMinuta == krajMinuta) {
    return false;
  }
  if (pocetakMinuta < krajMinuta) {
    return trenutnaMinutaUDanu >= pocetakMinuta && trenutnaMinutaUDanu < krajMinuta;
  }
  return trenutnaMinutaUDanu >= pocetakMinuta || trenutnaMinutaUDanu < krajMinuta;
}

static void osvjeziNocnuRasvjetu(const DateTime& sada) {
  if (!jeNocnaRasvjetaOmogucena() || !jeVrijemePotvrdjenoZaAutomatiku() || !raspored.valjano) {
    postaviNocnuRasvjetu(false);
    return;
  }

  const int jutroMinuta = raspored.minute[SUNCEVI_DOGADAJ_JUTRO];
  const int vecerMinuta = raspored.minute[SUNCEVI_DOGADAJ_VECER];
  if (jutroMinuta < 0 || vecerMinuta < 0) {
    postaviNocnuRasvjetu(false);
    return;
  }

  const int trenutnaMinutaUDanu = sada.hour() * 60 + sada.minute();
  postaviNocnuRasvjetu(jeVrijemeUProzoru(trenutnaMinutaUDanu, vecerMinuta, jutroMinuta));
}

static bool vrijemeProslo(unsigned long ciljMs) {
  return static_cast<long>(millis() - ciljMs) >= 0;
}

static void oznaciSuncevoZvonjenjeAktivnim(uint8_t dogadaj,
                                           uint8_t zvono,
                                           unsigned long trajanjeMs) {
  if (!jeRucnoUpravljivSuncevDogadaj(dogadaj)) {
    return;
  }

  aktivnoSuncevoZvonjenje[dogadaj] = true;
  aktivnoSuncevoZvono[dogadaj] = zvono;
  krajSuncevogZvonjenjaMs[dogadaj] = millis() + trajanjeMs;
}

static void osvjeziAktivnaSuncevaZvonjenja() {
  for (uint8_t dogadaj = 0; dogadaj < SUNCEVI_DOGADAJ_BROJ; ++dogadaj) {
    if (!aktivnoSuncevoZvonjenje[dogadaj]) {
      continue;
    }

    if (!jeZvonoAktivno(aktivnoSuncevoZvono[dogadaj]) ||
        vrijemeProslo(krajSuncevogZvonjenjaMs[dogadaj])) {
      aktivnoSuncevoZvonjenje[dogadaj] = false;
      aktivnoSuncevoZvono[dogadaj] = 0;
      krajSuncevogZvonjenjaMs[dogadaj] = 0;
    }
  }
}

static void postaviLampicuSuncevogDogadaja(uint8_t dogadaj, bool ukljuceno) {
  digitalWrite(dohvatiPinLampiceSuncevogDogadaja(dogadaj), ukljuceno ? HIGH : LOW);
}

static void osvjeziLampiceSunceveAutomatike() {
  const bool treptajUpaljen =
      ((millis() / TREPTANJE_LAMPICE_SUNCA_MS) % 2UL) == 0UL;

  for (uint8_t dogadaj = 0; dogadaj < SUNCEVI_DOGADAJ_BROJ; ++dogadaj) {
    if (!jeRucnoUpravljivSuncevDogadaj(dogadaj)) {
      continue;
    }

    if (aktivnoSuncevoZvonjenje[dogadaj]) {
      postaviLampicuSuncevogDogadaja(dogadaj, treptajUpaljen);
      continue;
    }

    postaviLampicuSuncevogDogadaja(dogadaj, jeSuncevDogadajOmogucen(dogadaj));
  }
}

static void prebaciRucnoUpravljanjeSuncevimDogadajem(uint8_t dogadaj) {
  if (!jeRucnoUpravljivSuncevDogadaj(dogadaj)) {
    return;
  }

  const bool novoStanje = !jeSuncevDogadajOmogucen(dogadaj);
  postaviSuncevDogadaj(dogadaj,
                       novoStanje,
                       dohvatiZvonoZaSuncevDogadaj(dogadaj),
                       dohvatiOdgoduSuncevogDogadajaMin(dogadaj));

  char log[96];
  snprintf_P(log,
             sizeof(log),
             PSTR("Sunce tipka: %s automatika %s"),
             nazivDogadajaTekst(dogadaj),
             novoStanje ? "ukljucena" : "iskljucena");
  posaljiPCLog(log);
}

static void obradiRucneTipkeSunceveAutomatike() {
  for (uint8_t dogadaj = 0; dogadaj < SUNCEVI_DOGADAJ_BROJ; ++dogadaj) {
    if (!jeRucnoUpravljivSuncevDogadaj(dogadaj)) {
      continue;
    }

    SwitchState novoStanje = SWITCH_RELEASED;
    if (!obradiDebouncedInput(dohvatiPinTipkeSuncevogDogadaja(dogadaj), 30, &novoStanje)) {
      continue;
    }

    if (novoStanje == SWITCH_PRESSED) {
      prebaciRucnoUpravljanjeSuncevimDogadajem(dogadaj);
    }
  }
}

static bool jeMinutaKolizijeSOtkucavanjem(int minutaUDanu) {
  const uint8_t modOtkucavanja = dohvatiModOtkucavanja();
  if (modOtkucavanja == 0) {
    return false;
  }

  const int minutaUSatu = minutaUDanu % 60;
  if (modOtkucavanja == 2) {
    return minutaUSatu == 0 || minutaUSatu == 15 || minutaUSatu == 30 || minutaUSatu == 45;
  }

  return minutaUSatu == 0 || minutaUSatu == 30;
}

static void zakaziSuncevoZvonjenje(uint8_t dogadaj,
                                   uint8_t zvono,
                                   uint32_t datumKljuc,
                                   unsigned long trajanjeMs) {
  zakazanoZvonjenje.aktivno = true;
  zakazanoZvonjenje.dogadaj = dogadaj;
  zakazanoZvonjenje.zvono = zvono;
  zakazanoZvonjenje.datumKljuc = datumKljuc;
  zakazanoZvonjenje.startMs = millis() + ODGODA_SUNCA_DO_SLIJEDECE_PROVJERE_MS;
  zakazanoZvonjenje.trajanjeMs = trajanjeMs;

  char log[96];
  snprintf_P(log, sizeof(log),
             PSTR("Suncevo zvonjenje odgodeno do kraja otkucavanja: %s -> ZVONO%u"),
             nazivDogadajaTekst(dogadaj),
             zvono);
  posaljiPCLog(log);
}

static void zakaziBlagdanskoSlavljenjeAkoTreba(uint8_t dogadaj, const DateTime& sada) {
  uint8_t razdoblje = 0;
  if (!trebaSlavitiBlagdanNakonDogadaja(dogadaj, sada, razdoblje)) {
    return;
  }

  zakazanoBlagdanskoSlavljenje.aktivno = true;
  zakazanoBlagdanskoSlavljenje.pokrenuto = false;
  zakazanoBlagdanskoSlavljenje.dogadaj = dogadaj;
  zakazanoBlagdanskoSlavljenje.datumKljuc = napraviDatumKljuc(sada);
  zakazanoBlagdanskoSlavljenje.startMs =
      millis() + static_cast<unsigned long>(dohvatiOdgoduSlavljenjaSekunde()) * 1000UL;
  zakazanoBlagdanskoSlavljenje.trajanjeMs = dohvatiTrajanjeSlavljenjaMs();
  zakazanoBlagdanskoSlavljenje.krajMs = 0;

  char log[112];
  snprintf_P(log,
             sizeof(log),
             PSTR("Blagdani: slavljenje nakon %s (%s) zakazano"),
             nazivDogadajaTekst(dogadaj),
             nazivBlagdanskogRazdobljaTekst(razdoblje));
  posaljiPCLog(log);
}

static void evidentirajJutarnjiStartOtkucavanja(uint8_t dogadaj, const DateTime& sada) {
  if (dogadaj != SUNCEVI_DOGADAJ_JUTRO) {
    return;
  }

  datumJutarnjegStartaOtkucavanja = napraviDatumKljuc(sada);
  minutaJutarnjegStartaOtkucavanja = sada.hour() * 60 + sada.minute();
  posaljiPCLog(F("Suncevo jutro: otkucavanje prelazi u dnevni rezim"));
}

static void obradiZakazanoSuncevoZvonjenje(const DateTime& sada) {
  if (!zakazanoZvonjenje.aktivno) {
    return;
  }

  if (zakazanoZvonjenje.datumKljuc != napraviDatumKljuc(sada)) {
    zakazanoZvonjenje.aktivno = false;
    return;
  }

  if (!vrijemeProslo(zakazanoZvonjenje.startMs)) {
    return;
  }

  if (jeOtkucavanjeUTijeku() || jeZvonoUTijeku() || jeLiInerciaAktivna()) {
    zakazanoZvonjenje.startMs = millis() + ODGODA_SUNCA_DO_SLIJEDECE_PROVJERE_MS;
    return;
  }

  if (jePrekidacTisineAktivan()) {
    zakazanoZvonjenje.aktivno = false;
    posaljiPCLog(F("Suncevo zvonjenje nakon odgode preskoceno (tihi rezim)"));
    return;
  }

  aktivirajZvonjenjeNaTrajanje(zakazanoZvonjenje.zvono, zakazanoZvonjenje.trajanjeMs);
  if (jeZvonoAktivno(zakazanoZvonjenje.zvono)) {
    oznaciSuncevoZvonjenjeAktivnim(zakazanoZvonjenje.dogadaj,
                                   zakazanoZvonjenje.zvono,
                                   zakazanoZvonjenje.trajanjeMs);
  }
  evidentirajJutarnjiStartOtkucavanja(zakazanoZvonjenje.dogadaj, sada);
  zakaziBlagdanskoSlavljenjeAkoTreba(zakazanoZvonjenje.dogadaj, sada);

  char log[96];
  snprintf_P(log, sizeof(log),
             PSTR("Suncevo zvonjenje: %s -> ZVONO%u nakon odgode zbog otkucavanja"),
             nazivDogadajaTekst(zakazanoZvonjenje.dogadaj),
             zakazanoZvonjenje.zvono);
  posaljiPCLog(log);

  zakazanoZvonjenje.aktivno = false;
}

static void obradiZakazanoBlagdanskoSlavljenje(const DateTime& sada) {
  if (!zakazanoBlagdanskoSlavljenje.aktivno) {
    return;
  }

  if (zakazanoBlagdanskoSlavljenje.datumKljuc != napraviDatumKljuc(sada)) {
    zakazanoBlagdanskoSlavljenje.aktivno = false;
    zakazanoBlagdanskoSlavljenje.pokrenuto = false;
    return;
  }

  if (jePrekidacTisineAktivan()) {
    zakazanoBlagdanskoSlavljenje.aktivno = false;
    zakazanoBlagdanskoSlavljenje.pokrenuto = false;
    posaljiPCLog(F("Blagdani: slavljenje preskoceno zbog tihog rezima"));
    return;
  }

  if (!zakazanoBlagdanskoSlavljenje.pokrenuto) {
    if (!vrijemeProslo(zakazanoBlagdanskoSlavljenje.startMs)) {
      return;
    }

    if (jeOtkucavanjeUTijeku() || jeZvonoUTijeku() || jeLiInerciaAktivna() ||
        jeSlavljenjeUTijeku() || jeMrtvackoUTijeku()) {
      zakazanoBlagdanskoSlavljenje.startMs =
          millis() + ODGODA_SUNCA_DO_SLIJEDECE_PROVJERE_MS;
      return;
    }

    zapocniSlavljenje();
    if (!jeSlavljenjeUTijeku()) {
      zakazanoBlagdanskoSlavljenje.startMs =
          millis() + ODGODA_SUNCA_DO_SLIJEDECE_PROVJERE_MS;
      return;
    }

    zakazanoBlagdanskoSlavljenje.pokrenuto = true;
    zakazanoBlagdanskoSlavljenje.krajMs =
        millis() + zakazanoBlagdanskoSlavljenje.trajanjeMs;
    posaljiPCLog(F("Blagdani: slavljenje pokrenuto nakon suncevog dogadaja"));
    return;
  }

  if (!vrijemeProslo(zakazanoBlagdanskoSlavljenje.krajMs)) {
    return;
  }

  zaustaviSlavljenje();
  if (!jeSlavljenjeUTijeku()) {
    zakazanoBlagdanskoSlavljenje.aktivno = false;
    zakazanoBlagdanskoSlavljenje.pokrenuto = false;
    posaljiPCLog(F("Blagdani: slavljenje zavrseno"));
  } else {
    zakazanoBlagdanskoSlavljenje.krajMs =
        millis() + ODGODA_SUNCA_DO_SLIJEDECE_PROVJERE_MS;
  }
}

static void upravljajMrtvackimZaSveSvete(const DateTime& sada) {
  if (!jeVrijemePotvrdjenoZaAutomatiku()) {
    if (sviSvetiMrtvackoAktivno) {
      zaustaviMrtvacko();
      sviSvetiMrtvackoAktivno = false;
      posaljiPCLog(F("Svi sveti: mrtvacko zaustavljeno jer vrijeme nije potvrdeno"));
    }
    return;
  }

  const bool prozorAktivan = jeSviSvetiMrtvackoProzorAktivan(sada);
  if (!prozorAktivan) {
    if (sviSvetiMrtvackoAktivno) {
      zaustaviMrtvacko();
      sviSvetiMrtvackoAktivno = false;
      posaljiPCLog(F("Svi sveti: mrtvacko zaustavljeno izvan zadanog prozora"));
    }
    return;
  }

  if (jePrekidacTisineAktivan()) {
    if (sviSvetiMrtvackoAktivno) {
      zaustaviMrtvacko();
      sviSvetiMrtvackoAktivno = false;
      posaljiPCLog(F("Svi sveti: mrtvacko zaustavljeno zbog tihog rezima"));
    }
    return;
  }

  if (!jeMrtvackoUTijeku()) {
    if (pokusajZapocetiMrtvackoBezAutoStopa()) {
      sviSvetiMrtvackoAktivno = true;
      posaljiPCLog(F("Svi sveti: mrtvacko pokrenuto prema blagdanskom rasporedu"));
    }
    return;
  }

  if (!sviSvetiMrtvackoAktivno) {
    // Mrtvacko je vec moglo biti pokrenuto rucno; ne preuzimamo ga za automatsko gasenje.
    return;
  }
}

static void osvjeziDnevniRaspored(const DateTime& sada) {
  raspored.valjano = true;
  raspored.datumKljuc = napraviDatumKljuc(sada);
  raspored.minute[SUNCEVI_DOGADAJ_JUTRO] = -1;
  raspored.minute[SUNCEVI_DOGADAJ_PODNE] = FIKSNO_PODNE_MINUTA;
  raspored.minute[SUNCEVI_DOGADAJ_VECER] = -1;

  int izlazMinute = -1;
  int zalazMinute = -1;
  if (!izracunajSunceveMinuteZaDatum(
          sada,
          SUNCEVA_LOKACIJA_SIRINA_E5,
          SUNCEVA_LOKACIJA_DUZINA_E5,
          izlazMinute,
          zalazMinute)) {
    return;
  }

  raspored.minute[SUNCEVI_DOGADAJ_JUTRO] = izracunajJutarnjeVrijemeZvonjenja(izlazMinute);
  raspored.minute[SUNCEVI_DOGADAJ_VECER] =
      normalizirajMinuteUDanu(zalazMinute + dohvatiOdgoduSuncevogDogadajaMin(SUNCEVI_DOGADAJ_VECER));
}

static void osigurajDnevniRaspored(const DateTime& sada) {
  const uint32_t datumKljuc = napraviDatumKljuc(sada);
  if (raspored.datumKljuc != datumKljuc || jeKonfiguracijaPromijenjena()) {
    osvjeziDnevniRaspored(sada);
    zapamtiKonfiguraciju();
  }
}

static unsigned long dohvatiTrajanjeSuncevogZvonjenja(const DateTime& sada) {
  return (sada.dayOfTheWeek() == 0)
      ? dohvatiTrajanjeZvonjenjaNedjeljaMs()
      : dohvatiTrajanjeZvonjenjaRadniMs();
}

static void obradiSuncevDogadaj(uint8_t dogadaj, const DateTime& sada) {
  if (!jeSuncevDogadajOmogucen(dogadaj)) {
    return;
  }

  const uint32_t datumKljuc = napraviDatumKljuc(sada);
  if (zadnjiOkinutiDatum[dogadaj] == datumKljuc) {
    return;
  }

  const int trazenaMinuta = raspored.minute[dogadaj];
  if (trazenaMinuta < 0) {
    return;
  }

  const int trenutnaMinutaUDanu = sada.hour() * 60 + sada.minute();
  if (trazenaMinuta != trenutnaMinutaUDanu) {
    return;
  }

  if (trebaPreskocitiSuncevDogadajZbogSvihSvetih(dogadaj, sada)) {
    char log[88];
    snprintf_P(log,
               sizeof(log),
               PSTR("Suncevo zvonjenje preskoceno zbog Svih svetih: %s"),
               nazivDogadajaTekst(dogadaj));
    posaljiPCLog(log);
    zadnjiOkinutiDatum[dogadaj] = datumKljuc;
    return;
  }

  zadnjiOkinutiDatum[dogadaj] = datumKljuc;

  if (!jeVrijemePotvrdjenoZaAutomatiku()) {
    char log[96];
    snprintf_P(log, sizeof(log),
               PSTR("Suncevo zvonjenje preskoceno (vrijeme nije potvrdjeno): %s"),
               nazivDogadajaTekst(dogadaj));
    posaljiPCLog(log);
    return;
  }

  if (jePrekidacTisineAktivan()) {
    char log[80];
    snprintf_P(log, sizeof(log),
               PSTR("Suncevo zvonjenje preskoceno (tihi rezim): %s"),
               nazivDogadajaTekst(dogadaj));
    posaljiPCLog(log);
    return;
  }

  const uint8_t zvono = dohvatiZvonoZaSuncevDogadaj(dogadaj);
  const unsigned long trajanjeZvona = dohvatiTrajanjeSuncevogZvonjenja(sada);
  if (jeMinutaKolizijeSOtkucavanjem(trazenaMinuta)) {
    zakaziSuncevoZvonjenje(dogadaj, zvono, datumKljuc, trajanjeZvona);
    return;
  }

  aktivirajZvonjenjeNaTrajanje(zvono, trajanjeZvona);
  if (jeZvonoAktivno(zvono)) {
    oznaciSuncevoZvonjenjeAktivnim(dogadaj, zvono, trajanjeZvona);
  }
  evidentirajJutarnjiStartOtkucavanja(dogadaj, sada);
  zakaziBlagdanskoSlavljenjeAkoTreba(dogadaj, sada);

  char log[96];
  snprintf_P(log, sizeof(log),
             PSTR("Suncevo zvonjenje: %s -> ZVONO%u u %02u:%02u:%02u"),
             nazivDogadajaTekst(dogadaj),
             zvono,
             sada.hour(),
             sada.minute(),
             sada.second());
  posaljiPCLog(log);
}

static void obradiFiksnoPodnevnoZvonjenje(const DateTime& sada) {
  if (!jeSuncevDogadajOmogucen(SUNCEVI_DOGADAJ_PODNE)) {
    return;
  }

  const uint32_t datumKljuc = napraviDatumKljuc(sada);
  if (zadnjiOkinutiDatum[SUNCEVI_DOGADAJ_PODNE] == datumKljuc) {
    return;
  }

  // Podnevno zvono toranjskog sata okidamo cim udemo u 12:00 minutu.
  // Ako je satno otkucavanje jos aktivno, koristi se isti mehanizam zakazivanja
  // kao i za ostale kolizije pa zvono krece odmah nakon dovrsetka otkucavanja,
  // bez fiksne odgode od 30 s.
  if (sada.hour() != 12 || sada.minute() != 0) {
    return;
  }

  zadnjiOkinutiDatum[SUNCEVI_DOGADAJ_PODNE] = datumKljuc;

  if (!jeVrijemePotvrdjenoZaAutomatiku()) {
    posaljiPCLog(F("Suncevo zvonjenje preskoceno (vrijeme nije potvrdeno): podne"));
    return;
  }

  if (jePrekidacTisineAktivan()) {
    posaljiPCLog(F("Suncevo zvonjenje preskoceno (tihi rezim): podne"));
    return;
  }

  const uint8_t zvono = dohvatiZvonoZaSuncevDogadaj(SUNCEVI_DOGADAJ_PODNE);
  const unsigned long trajanjeZvona = dohvatiTrajanjeSuncevogZvonjenja(sada);
  if (jeMinutaKolizijeSOtkucavanjem(FIKSNO_PODNE_MINUTA) || jeZvonoUTijeku() ||
      jeLiInerciaAktivna()) {
    zakaziSuncevoZvonjenje(SUNCEVI_DOGADAJ_PODNE, zvono, datumKljuc, trajanjeZvona);
    return;
  }

  aktivirajZvonjenjeNaTrajanje(zvono, trajanjeZvona);
  zakaziBlagdanskoSlavljenjeAkoTreba(SUNCEVI_DOGADAJ_PODNE, sada);

  char log[64];
  snprintf_P(log,
             sizeof(log),
             PSTR("Suncevo zvonjenje: podne -> ZVONO%u u %02u:%02u:%02u"),
             zvono,
             sada.hour(),
             sada.minute(),
             sada.second());
  posaljiPCLog(log);
}

}  // namespace

void inicijalizirajSuncevuAutomatiku() {
  pinMode(PIN_RELEJ_NOCNE_RASVJETE, OUTPUT);
  postaviRelejNocneRasvjete(false);
  pinMode(PIN_TIPKA_SUNCE_JUTRO, INPUT_PULLUP);
  pinMode(PIN_TIPKA_SUNCE_VECER, INPUT_PULLUP);
  pinMode(PIN_LAMPICA_SUNCE_JUTRO, OUTPUT);
  pinMode(PIN_LAMPICA_SUNCE_VECER, OUTPUT);
  postaviLampicuSuncevogDogadaja(SUNCEVI_DOGADAJ_JUTRO, false);
  postaviLampicuSuncevogDogadaja(SUNCEVI_DOGADAJ_VECER, false);
  nocnaRasvjetaUkljucena = false;
  raspored.valjano = false;
  raspored.datumKljuc = 0;
  zakazanoZvonjenje.aktivno = false;
  zakazanoBlagdanskoSlavljenje.aktivno = false;
  zakazanoBlagdanskoSlavljenje.pokrenuto = false;
  sviSvetiMrtvackoAktivno = false;
  for (uint8_t i = 0; i < SUNCEVI_DOGADAJ_BROJ; ++i) {
    raspored.minute[i] = -1;
    zadnjiOkinutiDatum[i] = 0;
    zadnjaZvona[i] = 0;
    zadnjeOdgode[i] = 0;
    aktivnoSuncevoZvonjenje[i] = false;
    aktivnoSuncevoZvono[i] = 0;
    krajSuncevogZvonjenjaMs[i] = 0;
  }
  zadnjiObradeniKljucMinute = 0xFFFFFFFFUL;
  zadnjiObradeniKljucSekunde = 0xFFFFFFFFUL;
  datumJutarnjegStartaOtkucavanja = 0;
  minutaJutarnjegStartaOtkucavanja = -1;
  zadnjaSirinaE5 = 0x7FFFFFFFL;
  zadnjaDuzinaE5 = 0x7FFFFFFFL;
  zadnjaMaskaDogadaja = 0xFF;

  // Ako se toranjski sat podigne usred noci, nocna rasvjeta ne smije cekati
  // sljedeci puni ciklus automatike nego odmah zauzeti ispravno stanje.
  const DateTime sada = dohvatiTrenutnoVrijeme();
  if (sada.unixtime() != 0) {
    osigurajDnevniRaspored(sada);
    osvjeziNocnuRasvjetu(sada);
  }
  osvjeziLampiceSunceveAutomatike();
}

void upravljajSuncevomAutomatikom() {
  obradiRucneTipkeSunceveAutomatike();
  osvjeziAktivnaSuncevaZvonjenja();
  osvjeziLampiceSunceveAutomatike();

  const DateTime sada = dohvatiTrenutnoVrijeme();
  if (sada.unixtime() == 0) {
    return;
  }

  osigurajDnevniRaspored(sada);
  osvjeziNocnuRasvjetu(sada);
  upravljajMrtvackimZaSveSvete(sada);

  if (!raspored.valjano) {
    return;
  }

  const uint32_t kljucMinute = napraviKljucMinute(sada);
  if (kljucMinute != zadnjiObradeniKljucMinute) {
    zadnjiObradeniKljucMinute = kljucMinute;
    obradiSuncevDogadaj(SUNCEVI_DOGADAJ_JUTRO, sada);
    obradiSuncevDogadaj(SUNCEVI_DOGADAJ_VECER, sada);
  }

  const uint32_t kljucSekunde = napraviKljucSekunde(sada);
  if (kljucSekunde == zadnjiObradeniKljucSekunde) {
    return;
  }
  zadnjiObradeniKljucSekunde = kljucSekunde;

  obradiZakazanoSuncevoZvonjenje(sada);
  obradiFiksnoPodnevnoZvonjenje(sada);
  obradiZakazanoBlagdanskoSlavljenje(sada);
}

bool dohvatiDanasnjeVrijemeSuncevogDogadajaMin(uint8_t dogadaj, int& minute) {
  if (dogadaj >= SUNCEVI_DOGADAJ_BROJ) {
    return false;
  }

  const DateTime sada = dohvatiTrenutnoVrijeme();
  if (sada.unixtime() == 0) {
    return false;
  }

  osigurajDnevniRaspored(sada);
  if (!raspored.valjano || raspored.minute[dogadaj] < 0) {
    return false;
  }

  minute = raspored.minute[dogadaj];
  return true;
}

bool jeJutarnjeZvonjenjeOtvoriloOtkucavanje(const DateTime& vrijeme) {
  if (vrijeme.unixtime() == 0) {
    return false;
  }

  if (datumJutarnjegStartaOtkucavanja != napraviDatumKljuc(vrijeme) ||
      minutaJutarnjegStartaOtkucavanja < 0) {
    return false;
  }

  const int trenutnaMinutaUDanu = vrijeme.hour() * 60 + vrijeme.minute();
  const int batPocetakMinuta = dohvatiBATPeriodOdSata() * 60;
  if (trenutnaMinutaUDanu < minutaJutarnjegStartaOtkucavanja) {
    return false;
  }

  return trenutnaMinutaUDanu < batPocetakMinuta;
}
