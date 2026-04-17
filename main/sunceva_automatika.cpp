#include "sunceva_automatika.h"

#include <Arduino.h>
#include <RTClib.h>
#include <math.h>
#include "otkucavanje.h"
#include "postavke.h"
#include "time_glob.h"
#include "zvonjenje.h"
#include "pc_serial.h"

namespace {

constexpr float PI_F = 3.14159265f;
constexpr float SUNCEV_ZENIT_RAD = 90.833f * (PI_F / 180.0f);
constexpr int FIKSNO_PODNE_MINUTA = 12 * 60;
constexpr uint8_t FIKSNO_PODNE_SEKUNDA = 30;
constexpr unsigned long ODGODA_SUNCA_DO_SLIJEDECE_PROVJERE_MS = 1000UL;

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

static DnevniRasporedSuncevihDogadaja raspored = {false, 0, {-1, -1, -1}};
static ZakazanoSuncevoZvonjenje zakazanoZvonjenje = {false, 0, 0, 0, 0, 0};
static uint32_t zadnjiObradeniKljucMinute = 0xFFFFFFFFUL;
static uint32_t zadnjiObradeniKljucSekunde = 0xFFFFFFFFUL;
static uint32_t zadnjiOkinutiDatum[SUNCEVI_DOGADAJ_BROJ] = {0, 0, 0};
static int32_t zadnjaSirinaE5 = 0x7FFFFFFFL;
static int32_t zadnjaDuzinaE5 = 0x7FFFFFFFL;
static uint8_t zadnjaMaskaDogadaja = 0xFF;
static uint8_t zadnjaZvona[SUNCEVI_DOGADAJ_BROJ] = {0, 0, 0};
static int16_t zadnjeOdgode[SUNCEVI_DOGADAJ_BROJ] = {0, 0, 0};

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

static bool jeLokacijaValjana(int32_t sirinaE5, int32_t duzinaE5) {
  return sirinaE5 >= -9000000L && sirinaE5 <= 9000000L &&
         duzinaE5 >= -18000000L && duzinaE5 <= 18000000L &&
         !(sirinaE5 == 0 && duzinaE5 == 0);
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
  const int utcOffsetMinute = dohvatiUTCOffsetMinuteZaLokalnoVrijeme(datum);
  const float solarnoPodne = 720.0f - (4.0f * duzina) - jednadzbaVremena + utcOffsetMinute;
  const float delta = kutniSatStupnjevi * 4.0f;

  izlazMinute = normalizirajMinuteUDanu(static_cast<int>((solarnoPodne - delta) + 0.5f));
  zalazMinute = normalizirajMinuteUDanu(static_cast<int>((solarnoPodne + delta) + 0.5f));
  return true;
}

static bool jeKonfiguracijaPromijenjena() {
  if (zadnjaSirinaE5 != dohvatiZemljopisnuSirinuE5() ||
      zadnjaDuzinaE5 != dohvatiZemljopisnuDuzinuE5()) {
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
  zadnjaSirinaE5 = dohvatiZemljopisnuSirinuE5();
  zadnjaDuzinaE5 = dohvatiZemljopisnuDuzinuE5();
  zadnjaMaskaDogadaja = 0;
  for (uint8_t i = 0; i < SUNCEVI_DOGADAJ_BROJ; ++i) {
    if (jeSuncevDogadajOmogucen(i)) {
      zadnjaMaskaDogadaja |= static_cast<uint8_t>(1U << i);
    }
    zadnjaZvona[i] = dohvatiZvonoZaSuncevDogadaj(i);
    zadnjeOdgode[i] = static_cast<int16_t>(dohvatiOdgoduSuncevogDogadajaMin(i));
  }
}

static const __FlashStringHelper* nazivDogadaja(uint8_t dogadaj) {
  if (dogadaj == SUNCEVI_DOGADAJ_JUTRO) {
    return F("jutro");
  }
  if (dogadaj == SUNCEVI_DOGADAJ_PODNE) {
    return F("podne");
  }
  return F("vecer");
}

static bool vrijemeProslo(unsigned long ciljMs) {
  return static_cast<long>(millis() - ciljMs) >= 0;
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

  String log = F("Suncevo zvonjenje odgodeno do kraja otkucavanja: ");
  log += nazivDogadaja(dogadaj);
  log += F(" -> ZVONO");
  log += zvono;
  posaljiPCLog(log);
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

  aktivirajZvonjenjeNaTrajanje(zakazanoZvonjenje.zvono, zakazanoZvonjenje.trajanjeMs);

  String log = F("Suncevo zvonjenje: ");
  log += nazivDogadaja(zakazanoZvonjenje.dogadaj);
  log += F(" -> ZVONO");
  log += zakazanoZvonjenje.zvono;
  log += F(" nakon odgode zbog otkucavanja");
  posaljiPCLog(log);

  zakazanoZvonjenje.aktivno = false;
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
          dohvatiZemljopisnuSirinuE5(),
          dohvatiZemljopisnuDuzinuE5(),
          izlazMinute,
          zalazMinute)) {
    return;
  }

  raspored.minute[SUNCEVI_DOGADAJ_JUTRO] =
      normalizirajMinuteUDanu(izlazMinute + dohvatiOdgoduSuncevogDogadajaMin(SUNCEVI_DOGADAJ_JUTRO));
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

  zadnjiOkinutiDatum[dogadaj] = datumKljuc;

  if (!jeVrijemePotvrdjenoZaAutomatiku()) {
    String log = F("Suncevo zvonjenje preskoceno (vrijeme nije potvrdeno): ");
    log += nazivDogadaja(dogadaj);
    posaljiPCLog(log);
    return;
  }

  if (jeUskrsnaTisinaAktivna(sada)) {
    String log = F("Suncevo zvonjenje preskoceno (uskrsna tisina): ");
    log += nazivDogadaja(dogadaj);
    posaljiPCLog(log);
    return;
  }

  if (!jeBATPeriodAktivanZaSatneOtkucaje(sada.hour(), sada.minute())) {
    String log = F("Suncevo zvonjenje preskoceno (izvan BAT raspona): ");
    log += nazivDogadaja(dogadaj);
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

  String log = F("Suncevo zvonjenje: ");
  log += nazivDogadaja(dogadaj);
  log += F(" -> ZVONO");
  log += zvono;
  log += F(" u ");
  if (sada.hour() < 10) {
    log += '0';
  }
  log += sada.hour();
  log += ':';
  if (sada.minute() < 10) {
    log += '0';
  }
  log += sada.minute();
  log += ':';
  if (sada.second() < 10) {
    log += '0';
  }
  log += sada.second();
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

  if (sada.hour() != 12 || sada.minute() != 0 || sada.second() != FIKSNO_PODNE_SEKUNDA) {
    return;
  }

  zadnjiOkinutiDatum[SUNCEVI_DOGADAJ_PODNE] = datumKljuc;

  if (!jeVrijemePotvrdjenoZaAutomatiku()) {
    posaljiPCLog(F("Suncevo zvonjenje preskoceno (vrijeme nije potvrdeno): podne"));
    return;
  }

  if (jeUskrsnaTisinaAktivna(sada)) {
    posaljiPCLog(F("Suncevo zvonjenje preskoceno (uskrsna tisina): podne"));
    return;
  }

  if (!jeBATPeriodAktivanZaSatneOtkucaje(sada.hour(), sada.minute())) {
    posaljiPCLog(F("Suncevo zvonjenje preskoceno (izvan BAT raspona): podne"));
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

  String log = F("Suncevo zvonjenje: podne -> ZVONO");
  log += zvono;
  log += F(" u 12:00:30");
  posaljiPCLog(log);
}

}  // namespace

void inicijalizirajSuncevuAutomatiku() {
  raspored.valjano = false;
  raspored.datumKljuc = 0;
  zakazanoZvonjenje.aktivno = false;
  for (uint8_t i = 0; i < SUNCEVI_DOGADAJ_BROJ; ++i) {
    raspored.minute[i] = -1;
    zadnjiOkinutiDatum[i] = 0;
    zadnjaZvona[i] = 0;
    zadnjeOdgode[i] = 0;
  }
  zadnjiObradeniKljucMinute = 0xFFFFFFFFUL;
  zadnjiObradeniKljucSekunde = 0xFFFFFFFFUL;
  zadnjaSirinaE5 = 0x7FFFFFFFL;
  zadnjaDuzinaE5 = 0x7FFFFFFFL;
  zadnjaMaskaDogadaja = 0xFF;
}

void upravljajSuncevomAutomatikom() {
  const DateTime sada = dohvatiTrenutnoVrijeme();
  if (sada.unixtime() == 0) {
    return;
  }

  osigurajDnevniRaspored(sada);

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
}

bool jeSuncevaLokacijaValjana() {
  return jeLokacijaValjana(dohvatiZemljopisnuSirinuE5(), dohvatiZemljopisnuDuzinuE5());
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
