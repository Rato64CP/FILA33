// dcf_sync.cpp - nocni DCF77 fallback za toranjski sat
#include <Arduino.h>
#include <string.h>
#include "dcf_sync.h"
#include "podesavanja_piny.h"
#include "time_glob.h"
#include "pc_serial.h"
#include "kazaljke_sata.h"
#include "okretna_ploca.h"
#include "postavke.h"
#include "esp_serial.h"

namespace {

static const uint8_t DCF_SAT_NOC_OD = 22;
static const uint8_t DCF_SAT_NOC_DO = 6;
static const unsigned long DCF_CEKANJE_WIFI_MS = 300000UL;
static const unsigned long DCF_VRIJEME_STABILIZACIJE_MS = 60000UL;
static const unsigned long DCF_MAX_TRAJANJE_POKUSAJA_PO_SATU_MS = 20UL * 60000UL;
static const unsigned long DCF_PRODUZENJE_ZA_DRUGU_POTVRDU_MS = 120000UL;
static const unsigned long DCF_MINUTA_MARKER_MS = 1500UL;

static const unsigned long PULS_0_MIN_MS = 70UL;
static const unsigned long PULS_0_MAX_MS = 140UL;
static const unsigned long PULS_1_MIN_MS = 160UL;
static const unsigned long PULS_1_MAX_MS = 260UL;

static bool dcfInicijaliziran = false;
static bool dcfPrijemAktivan = false;
static bool dcfUlazJeNizak = false;
static uint8_t dcfBitovi[59] = {};
static uint8_t dcfBrojBita = 0;
static unsigned long dcfPocetakSesijeMs = 0;
static unsigned long dcfKrajSesijeMs = 0;
static unsigned long dcfPocetakPulsaMs = 0;
static unsigned long dcfZadnjiKrajPulsaMs = 0;
static unsigned long dcfPocetakCekanjaNaWiFiMs = 0;
static bool dcfCekanjeNaWiFiAktivno = false;
static uint32_t zadnjiSatniPokusajKljuc = 0;
static uint32_t zadnjaUspjesnaNocDcfKljuc = 0;
static uint32_t dcfVizualniBrojac = 0;
static DateTime zadnjeDCF((uint32_t)0);
static bool rucniDcfZahtjev = false;
static bool rucniDcfAktivan = false;

static void oznaciPromjenuDcfVizualizacije() {
  dcfVizualniBrojac++;
}

static bool vrijemeProslo(unsigned long sadaMs, unsigned long ciljMs) {
  return static_cast<long>(sadaMs - ciljMs) >= 0;
}

static void resetirajCekanjeNaWiFiZaDCF() {
  dcfPocetakCekanjaNaWiFiMs = 0;
  dcfCekanjeNaWiFiAktivno = false;
}

static bool jeNocniDCFInterval(const DateTime& sada) {
  const uint8_t sat = sada.hour();
  if (DCF_SAT_NOC_OD == DCF_SAT_NOC_DO) {
    return false;
  }

  if (DCF_SAT_NOC_OD > DCF_SAT_NOC_DO) {
    return sat >= DCF_SAT_NOC_OD || sat < DCF_SAT_NOC_DO;
  }

  return sat >= DCF_SAT_NOC_OD && sat < DCF_SAT_NOC_DO;
}

static uint32_t izracunajKljucNocnogPokusaja(const DateTime& sada) {
  DateTime referenca = sada;
  if (sada.hour() < DCF_SAT_NOC_DO && sada.unixtime() >= 86400UL) {
    referenca = DateTime(sada.unixtime() - 86400UL);
  }

  return static_cast<uint32_t>(referenca.year()) * 10000UL +
         static_cast<uint32_t>(referenca.month()) * 100UL +
         static_cast<uint32_t>(referenca.day());
}

static uint32_t izracunajKljucSatnogPokusaja(const DateTime& sada, uint32_t nocniKljuc) {
  return nocniKljuc * 100UL + static_cast<uint32_t>(sada.hour());
}

static unsigned long izracunajPreostaliProzorPokusajaMs(const DateTime& sada) {
  const unsigned long protekloUSatuMs =
      static_cast<unsigned long>(sada.minute()) * 60000UL +
      static_cast<unsigned long>(sada.second()) * 1000UL;
  if (protekloUSatuMs >= DCF_MAX_TRAJANJE_POKUSAJA_PO_SATU_MS) {
    return 0;
  }
  return DCF_MAX_TRAJANJE_POKUSAJA_PO_SATU_MS - protekloUSatuMs;
}

static void ocistiDcfOkvir() {
  memset(dcfBitovi, 0, sizeof(dcfBitovi));
  dcfBrojBita = 0;
}

static void postaviDcfPrijemnikAktivan(bool aktivan) {
  digitalWrite(PIN_DCF_AKTIVACIJA, aktivan ? LOW : HIGH);
}

static void resetirajDcfSesiju(bool ugasiPrijemnik) {
  const bool bioAktivan = dcfPrijemAktivan;
  ocistiDcfOkvir();
  dcfPrijemAktivan = false;
  dcfPocetakSesijeMs = 0;
  dcfKrajSesijeMs = 0;
  dcfPocetakPulsaMs = 0;
  dcfZadnjiKrajPulsaMs = 0;
  dcfUlazJeNizak = false;
  if (ugasiPrijemnik) {
    postaviDcfPrijemnikAktivan(false);
  }
  if (bioAktivan) {
    oznaciPromjenuDcfVizualizacije();
  }
}

static void pokreniRucniPokusaj() {
  postaviDcfPrijemnikAktivan(true);
  ocistiDcfOkvir();
  dcfPrijemAktivan = true;
  dcfPocetakSesijeMs = millis();
  dcfKrajSesijeMs = dcfPocetakSesijeMs + DCF_MAX_TRAJANJE_POKUSAJA_PO_SATU_MS;
  dcfPocetakPulsaMs = 0;
  dcfZadnjiKrajPulsaMs = 0;
  dcfUlazJeNizak = (digitalRead(PIN_DCF_SIGNAL) == LOW);
  rucniDcfZahtjev = false;
  rucniDcfAktivan = true;
  oznaciPromjenuDcfVizualizacije();
  posaljiPCLog(F("DCF77: pokrenut rucni prijem iz izbornika toranjskog sata"));
}

static bool trebaNocniFallback() {
  if (!jeDCFOmogucen()) {
    resetirajCekanjeNaWiFiZaDCF();
    return false;
  }

  if (jeZadnjaSvjezaSinkronizacijaIzNTP()) {
    resetirajCekanjeNaWiFiZaDCF();
    return false;
  }

  if (!jeWiFiOmogucen()) {
    resetirajCekanjeNaWiFiZaDCF();
    return true;
  }

  if (jeWiFiPovezanNaESP()) {
    resetirajCekanjeNaWiFiZaDCF();
    return false;
  }

  const unsigned long sadaMs = millis();
  if (!dcfCekanjeNaWiFiAktivno) {
    dcfCekanjeNaWiFiAktivno = true;
    dcfPocetakCekanjaNaWiFiMs = sadaMs;
    return false;
  }

  return (sadaMs - dcfPocetakCekanjaNaWiFiMs) >= DCF_CEKANJE_WIFI_MS;
}

static bool dcfJeStabiliziran() {
  return dcfPrijemAktivan &&
         (millis() - dcfPocetakSesijeMs) >= DCF_VRIJEME_STABILIZACIJE_MS;
}

static int procitajDCFBit(unsigned long trajanjePulsaMs) {
  if (trajanjePulsaMs >= PULS_0_MIN_MS && trajanjePulsaMs <= PULS_0_MAX_MS) {
    return 0;
  }
  if (trajanjePulsaMs >= PULS_1_MIN_MS && trajanjePulsaMs <= PULS_1_MAX_MS) {
    return 1;
  }
  return -1;
}

static bool provjeriParitet(uint8_t od, uint8_t doUkljucivo, uint8_t indeksPariteta) {
  uint8_t paritet = 0;
  for (uint8_t i = od; i <= doUkljucivo; ++i) {
    paritet ^= dcfBitovi[i];
  }
  return paritet == dcfBitovi[indeksPariteta];
}

static uint8_t procitajVrijednost(const uint8_t* indeksi, const uint8_t* tezine, uint8_t brojIndeksa) {
  uint8_t vrijednost = 0;
  for (uint8_t i = 0; i < brojIndeksa; ++i) {
    if (dcfBitovi[indeksi[i]] != 0) {
      vrijednost = static_cast<uint8_t>(vrijednost + tezine[i]);
    }
  }
  return vrijednost;
}

static bool dekodirajDCFVrijeme(DateTime& dekodiranoVrijeme, bool& dstAktivan) {
  static const uint8_t minuteIndeksi[] = {21, 22, 23, 24, 25, 26, 27};
  static const uint8_t minuteTezine[] = {1, 2, 4, 8, 10, 20, 40};
  static const uint8_t satIndeksi[] = {29, 30, 31, 32, 33, 34};
  static const uint8_t satTezine[] = {1, 2, 4, 8, 10, 20};
  static const uint8_t danIndeksi[] = {36, 37, 38, 39, 40, 41};
  static const uint8_t danTezine[] = {1, 2, 4, 8, 10, 20};
  static const uint8_t mjesecIndeksi[] = {45, 46, 47, 48, 49};
  static const uint8_t mjesecTezine[] = {1, 2, 4, 8, 10};
  static const uint8_t godinaIndeksi[] = {50, 51, 52, 53, 54, 55, 56, 57};
  static const uint8_t godinaTezine[] = {1, 2, 4, 8, 10, 20, 40, 80};

  if (dcfBrojBita != 59) {
    return false;
  }

  if (dcfBitovi[20] != 1) {
    return false;
  }

  const bool cet = dcfBitovi[17] != 0;
  const bool cest = dcfBitovi[18] != 0;
  if (cet == cest) {
    return false;
  }

  if (!provjeriParitet(21, 27, 28) ||
      !provjeriParitet(29, 34, 35) ||
      !provjeriParitet(36, 57, 58)) {
    return false;
  }

  const uint8_t minuta = procitajVrijednost(minuteIndeksi, minuteTezine, sizeof(minuteIndeksi));
  const uint8_t sat = procitajVrijednost(satIndeksi, satTezine, sizeof(satIndeksi));
  const uint8_t dan = procitajVrijednost(danIndeksi, danTezine, sizeof(danIndeksi));
  const uint8_t mjesec = procitajVrijednost(mjesecIndeksi, mjesecTezine, sizeof(mjesecIndeksi));
  const uint8_t godina = procitajVrijednost(godinaIndeksi, godinaTezine, sizeof(godinaIndeksi));

  if (minuta > 59 || sat > 23 || dan == 0 || dan > 31 || mjesec == 0 || mjesec > 12) {
    return false;
  }

  DateTime dekodirano(2000 + godina, mjesec, dan, sat, minuta, 0);
  if (dekodirano.year() != 2000 + godina ||
      dekodirano.month() != mjesec ||
      dekodirano.day() != dan ||
      dekodirano.hour() != sat ||
      dekodirano.minute() != minuta) {
    return false;
  }

  dekodiranoVrijeme = dekodirano;
  dstAktivan = cest;
  return true;
}

static void zavrsiSatniPokusaj(uint32_t satniKljuc, const __FlashStringHelper* poruka) {
  zadnjiSatniPokusajKljuc = satniKljuc;
  resetirajDcfSesiju(true);
  if (poruka != nullptr) {
    posaljiPCLog(poruka);
  }
}

static void pokreniSatniPokusaj(const DateTime& sada, uint32_t satniKljuc) {
  const unsigned long preostaliProzorMs = izracunajPreostaliProzorPokusajaMs(sada);
  if (preostaliProzorMs == 0) {
    zadnjiSatniPokusajKljuc = satniKljuc;
    return;
  }

  postaviDcfPrijemnikAktivan(true);
  ocistiDcfOkvir();
  dcfPrijemAktivan = true;
  dcfPocetakSesijeMs = millis();
  dcfKrajSesijeMs = dcfPocetakSesijeMs + preostaliProzorMs;
  dcfPocetakPulsaMs = 0;
  dcfZadnjiKrajPulsaMs = 0;
  dcfUlazJeNizak = (digitalRead(PIN_DCF_SIGNAL) == LOW);
  oznaciPromjenuDcfVizualizacije();

  char log[96];
  snprintf(
      log,
      sizeof(log),
      "DCF77: satni pokusaj %02d:%02d-%02d:20, cekam valjani minutni okvir",
      sada.hour(),
      0,
      sada.hour());
  posaljiPCLog(log);
}

static void produziSesijuZaDruguPotvrdu() {
  const unsigned long sadaMs = millis();
  const unsigned long predlozeniKraj = sadaMs + DCF_PRODUZENJE_ZA_DRUGU_POTVRDU_MS;
  const long preostaloDoTrenutnogKraja = static_cast<long>(dcfKrajSesijeMs - sadaMs);
  const long preostaloDoPredlozenogKraja = static_cast<long>(predlozeniKraj - sadaMs);
  if (preostaloDoPredlozenogKraja > preostaloDoTrenutnogKraja) {
    dcfKrajSesijeMs = predlozeniKraj;
  }
}

static void obradiDovrseniOkvir(uint32_t nocniKljuc, uint32_t satniKljuc) {
  if (dcfBrojBita != 59) {
    ocistiDcfOkvir();
    return;
  }

  DateTime novoVrijeme((uint32_t)0);
  bool dstAktivan = false;
  if (!dekodirajDCFVrijeme(novoVrijeme, dstAktivan) ||
      novoVrijeme.unixtime() == 0 ||
      novoVrijeme.year() < 2024) {
    posaljiPCLog(F("DCF77: primljen okvir nije valjan, nastavljam slusanje"));
    ocistiDcfOkvir();
    return;
  }

  const RezultatProvjereSinkronizacije rezultatSinkronizacije =
      azurirajVrijemeIzDCF(novoVrijeme, true, dstAktivan);
  if (rezultatSinkronizacije == SINKRONIZACIJA_CEKA_DODATNU_POTVRDU) {
    produziSesijuZaDruguPotvrdu();
    posaljiPCLog(F("DCF77: prvi valjani okvir trazi jos jednu potvrdu, nastavljam slusanje"));
    ocistiDcfOkvir();
    return;
  }

  if (rezultatSinkronizacije != SINKRONIZACIJA_PRIHVACENA) {
    posaljiPCLog(F("DCF77: valjani okvir nije prihvacen, nastavljam slusanje"));
    ocistiDcfOkvir();
    return;
  }

  zadnjeDCF = novoVrijeme;
  if (!rucniDcfAktivan) {
    zadnjaUspjesnaNocDcfKljuc = nocniKljuc;
    zadnjiSatniPokusajKljuc = satniKljuc;
  }
  zatraziPoravnanjeTaktaKazaljki();
  zatraziPoravnanjeTaktaPloce();

  char log[80];
  snprintf(
      log,
      sizeof(log),
      "%s %04d-%02d-%02d %02d:%02d",
      rucniDcfAktivan ? "DCF77: rucna sinkronizacija uspjesna" :
                        "DCF77: nocna sinkronizacija uspjesna",
      novoVrijeme.year(),
      novoVrijeme.month(),
      novoVrijeme.day(),
      novoVrijeme.hour(),
      novoVrijeme.minute());
  posaljiPCLog(log);

  resetirajDcfSesiju(true);
  rucniDcfAktivan = false;
}

static void obradiDcfSignal(uint32_t nocniKljuc, uint32_t satniKljuc) {
  const unsigned long sadaMs = millis();
  const bool ulazJeNizak = (digitalRead(PIN_DCF_SIGNAL) == LOW);

  if (ulazJeNizak == dcfUlazJeNizak) {
    return;
  }

  dcfUlazJeNizak = ulazJeNizak;
  oznaciPromjenuDcfVizualizacije();

  if (ulazJeNizak) {
    if (dcfZadnjiKrajPulsaMs != 0 &&
        (sadaMs - dcfZadnjiKrajPulsaMs) >= DCF_MINUTA_MARKER_MS) {
      obradiDovrseniOkvir(nocniKljuc, satniKljuc);
      if (!dcfPrijemAktivan) {
        return;
      }
    }
    dcfPocetakPulsaMs = sadaMs;
    return;
  }

  if (dcfPocetakPulsaMs == 0) {
    return;
  }

  const unsigned long trajanjePulsaMs = sadaMs - dcfPocetakPulsaMs;
  dcfPocetakPulsaMs = 0;
  dcfZadnjiKrajPulsaMs = sadaMs;

  const int bit = procitajDCFBit(trajanjePulsaMs);
  if (bit < 0) {
    ocistiDcfOkvir();
    return;
  }

  if (dcfBrojBita < sizeof(dcfBitovi)) {
    dcfBitovi[dcfBrojBita++] = static_cast<uint8_t>(bit);
  } else {
    ocistiDcfOkvir();
  }
}

}  // namespace

void inicijalizirajDCF() {
  pinMode(PIN_DCF_SIGNAL, INPUT);
  pinMode(PIN_DCF_AKTIVACIJA, OUTPUT);
  postaviDcfPrijemnikAktivan(false);

  dcfInicijaliziran = true;
  zadnjeDCF = DateTime((uint32_t)0);
  zadnjiSatniPokusajKljuc = 0;
  zadnjaUspjesnaNocDcfKljuc = 0;
  resetirajDcfSesiju(false);

  if (!jeDCFOmogucen()) {
    posaljiPCLog(F("DCF77: onemogucen u postavkama toranjskog sata"));
    return;
  }

  posaljiPCLog(F("DCF77: inicijaliziran, fallback radi po satnim nocnim prozorima"));
}

void osvjeziDCFSinkronizaciju() {
  if (!dcfInicijaliziran) {
    inicijalizirajDCF();
  }

  if (!jeDCFOmogucen()) {
    if (dcfPrijemAktivan) {
      resetirajDcfSesiju(true);
    }
    rucniDcfZahtjev = false;
    rucniDcfAktivan = false;
    return;
  }

  if (rucniDcfZahtjev && !dcfPrijemAktivan) {
    pokreniRucniPokusaj();
  }

  if (rucniDcfAktivan) {
    if (!dcfPrijemAktivan) {
      rucniDcfAktivan = false;
      return;
    }

    if (vrijemeProslo(millis(), dcfKrajSesijeMs)) {
      resetirajDcfSesiju(true);
      rucniDcfAktivan = false;
      posaljiPCLog(F("DCF77: rucni prijem istekao bez valjane sinkronizacije"));
      return;
    }

    if (!dcfJeStabiliziran()) {
      return;
    }

    obradiDcfSignal(0, 0);
    return;
  }

  const DateTime sada = dohvatiTrenutnoVrijeme();
  if (!jeNocniDCFInterval(sada)) {
    if (dcfPrijemAktivan) {
      resetirajDcfSesiju(true);
    }
    return;
  }

  const uint32_t nocniKljuc = izracunajKljucNocnogPokusaja(sada);
  const uint32_t satniKljuc = izracunajKljucSatnogPokusaja(sada, nocniKljuc);
  if (zadnjaUspjesnaNocDcfKljuc == nocniKljuc) {
    if (dcfPrijemAktivan) {
      resetirajDcfSesiju(true);
    }
    return;
  }

  if (!trebaNocniFallback()) {
    if (dcfPrijemAktivan) {
      resetirajDcfSesiju(true);
    }
    return;
  }

  if (!dcfPrijemAktivan) {
    if (izracunajPreostaliProzorPokusajaMs(sada) == 0) {
      return;
    }
    if (zadnjiSatniPokusajKljuc == satniKljuc) {
      return;
    }
    pokreniSatniPokusaj(sada, satniKljuc);
    return;
  }

  if (vrijemeProslo(millis(), dcfKrajSesijeMs)) {
    char log[80];
    snprintf(
        log,
        sizeof(log),
        "DCF77: satni pokusaj %02d:00 istekao bez valjane sinkronizacije",
        sada.hour());
    zavrsiSatniPokusaj(satniKljuc, nullptr);
    posaljiPCLog(log);
    return;
  }

  if (!dcfJeStabiliziran()) {
    return;
  }

  obradiDcfSignal(nocniKljuc, satniKljuc);
}

bool jeDCFSinkronizacijaUTijeku() {
  return dcfPrijemAktivan;
}

bool jeDCFImpulsAktivan() {
  return dcfPrijemAktivan && dcfUlazJeNizak;
}

uint32_t dohvatiDcfVizualniBrojac() {
  return dcfVizualniBrojac;
}

void pokreniRucniDCFPrijem() {
  if (!jeDCFOmogucen()) {
    posaljiPCLog(F("DCF77: rucni prijem odbijen jer je DCF iskljucen u postavkama"));
    return;
  }

  if (!dcfInicijaliziran) {
    inicijalizirajDCF();
  }

  if (dcfPrijemAktivan) {
    posaljiPCLog(F("DCF77: prijem je vec aktivan"));
    return;
  }

  rucniDcfZahtjev = true;
}
