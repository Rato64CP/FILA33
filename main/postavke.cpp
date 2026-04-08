// postavke.cpp - Upravljanje postavkama toranjskog sata i EEPROM-om
#include <Arduino.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "postavke.h"
#include "eeprom_konstante.h"
#include "wear_leveling.h"
#include "i2c_eeprom.h"
#include "pc_serial.h"
#include "postavke_migracije.h"

namespace {

constexpr int PLOCA_ZADANI_POCETAK_MINUTA = 5 * 60 + 14;
constexpr int PLOCA_ZADANI_KRAJ_MINUTA = 20 * 60 + 59;
constexpr int PLOCA_STARI_ZADANI_POCETAK_MINUTA = 10 * 60;
constexpr int PLOCA_STARI_ZADANI_KRAJ_MINUTA = 20 * 60;
constexpr int PLOCA_MINUTNI_BLOK = 15;
constexpr int PLOCA_ZADNJA_CETVRT = 23 * 60 + 59;
constexpr int32_t ZADANA_ZEMLJOPISNA_SIRINA_E5 = 4350000L;
constexpr int32_t ZADANA_ZEMLJOPISNA_DUZINA_E5 = 1695000L;
constexpr int16_t SUNCEVA_ODGODA_MIN = 180;
constexpr uint8_t MASKA_SUNCEVIH_DOGADAJA = 0x07;

struct RadnePostavke {
  int satOd;
  int satDo;
  int tihiSatiOd;
  int tihiSatiDo;
  int plocaPocetakMinuta;
  int plocaKrajMinuta;
  unsigned int trajanjeImpulsaCekicaMs;
  unsigned int pauzaIzmeduUdaraca;
  uint8_t trajanjeZvonjenjaRadniMin;
  uint8_t trajanjeZvonjenjaNedjeljaMin;
  uint8_t trajanjeSlavljenjaMin;
  uint8_t slavljenjePrijeZvonjenja;
  uint8_t brojZvona;
  uint8_t brojMjestaZaCavle;
  uint8_t cavliRadni[4];
  uint8_t cavliNedjelja[4];
  uint8_t cavaoSlavljenje;
  uint8_t cavaoMrtvacko;
  bool koristiDhcp;
  bool lcdPozadinskoOsvjetljenje;
  uint8_t modSlavljenja;
  uint8_t modOtkucavanja;
  bool ntpOmogucen;
  bool dcfOmogucen;
  bool wifiOmogucen;
  bool imaKazaljke;
  int32_t zemljopisnaSirinaE5;
  int32_t zemljopisnaDuzinaE5;
  uint8_t maskaSuncevihDogadaja;
  uint8_t zvonaSuncevihDogadaja[SUNCEVI_DOGADAJ_BROJ];
  int16_t odgodeSuncevihDogadajaMin[SUNCEVI_DOGADAJ_BROJ];
};

enum AktivnaMreznaSekcija {
  MREZNA_SEKCIJA_NISTA = 0,
  MREZNA_SEKCIJA_WIFI,
  MREZNA_SEKCIJA_SINKRONIZACIJA
};

union MrezniCache {
  struct {
    char wifiSsid[33];
    char wifiLozinka[33];
    char statickaIp[16];
    char mreznaMaska[16];
    char zadaniGateway[16];
  } wifi;
  struct {
    char ntpServer[40];
  } sinkronizacija;
};

static EepromLayout::PostavkeSpremnik napraviZadanePostavke() {
  EepromLayout::PostavkeSpremnik zadane = {
    EepromLayout::POSTAVKE_POTPIS,
    EepromLayout::POSTAVKE_VERZIJA,
    6,
    22,
    22,
    6,
    PLOCA_ZADANI_POCETAK_MINUTA,
    PLOCA_ZADANI_KRAJ_MINUTA,
    150,
    400,
    2,
    3,
    2,
    0,
    2,
    5,
    {1, 2, 0, 0},
    {3, 4, 0, 0},
    5,
    0,
    "1234",
    "SVETI PETAR",
    "cista2906",
    true,
    false,
    true,
    1,
    2,
    "192.168.8.230",
    "255.255.255.0",
    "192.168.8.1",
    "192.168.1.100",
    1883,
    "toranj",
    "toranj2024",
    "pool.ntp.org",
    true,
    true,
    true,
    0
  };
  return zadane;
}

static EepromLayout::SunceviDogadajiSpremnik napraviZadaneSunceveDogadaje() {
  EepromLayout::SunceviDogadajiSpremnik zadani = {
    EepromLayout::SUNCEVI_DOGADAJI_POTPIS,
    EepromLayout::SUNCEVI_DOGADAJI_VERZIJA,
    0,
    ZADANA_ZEMLJOPISNA_SIRINA_E5,
    ZADANA_ZEMLJOPISNA_DUZINA_E5,
    {1, 1, 1},
    {0, 0, 0},
    0
  };
  return zadani;
}

static RadnePostavke postavke = {};
static MrezniCache mrezniCache = {};
static AktivnaMreznaSekcija aktivnaMreznaSekcija = MREZNA_SEKCIJA_NISTA;

static bool jeKodiranNtpStatus(const char* ntpServer) {
  return ntpServer != nullptr &&
         (ntpServer[0] == '0' || ntpServer[0] == '1') &&
         ntpServer[1] != '\0';
}

static bool procitajNtpOmogucenostIzTeksta(const char* ntpServer) {
  if (jeKodiranNtpStatus(ntpServer)) {
    return ntpServer[0] == '1';
  }
  return true;
}

static const char* dohvatiNtpServerBezZastavice(const char* ntpServer) {
  if (jeKodiranNtpStatus(ntpServer)) {
    return ntpServer + 1;
  }
  return (ntpServer != nullptr) ? ntpServer : "";
}

static void kodirajNtpServer(char* odrediste,
                             size_t velicina,
                             const char* ntpServer,
                             bool omogucen) {
  if (odrediste == nullptr || velicina < 3) {
    return;
  }

  const char* siguranServer = dohvatiNtpServerBezZastavice(ntpServer);
  if (siguranServer[0] == '\0') {
    siguranServer = "pool.ntp.org";
  }

  odrediste[0] = omogucen ? '1' : '0';
  strncpy(odrediste + 1, siguranServer, velicina - 2);
  odrediste[velicina - 1] = '\0';
}

static int dekodirajPocetakPloceMinuta(int spremljeno) {
  if (spremljeno >= 0) {
    return spremljeno;
  }
  return -spremljeno - 1;
}

static int kodirajPocetakPloceMinuta(int pocetakMinuta, bool aktivna) {
  return aktivna ? pocetakMinuta : -(pocetakMinuta + 1);
}

static int normalizirajMinutuPloceNaBlok(int minute) {
  minute = constrain(minute, 0, PLOCA_ZADNJA_CETVRT);
  return minute;
}

static uint8_t ogranicenoTrajanjeCavla(uint8_t trajanjeMin) {
  return (trajanjeMin <= 2) ? 2 : 3;
}

static unsigned int ogranicenoTrajanjeImpulsaCekicaMs(unsigned int trajanjeMs) {
  return constrain(trajanjeMs, 50U, 150U);
}

static uint8_t ograniceniBrojZvona(uint8_t brojZvona) {
  (void)brojZvona;
  return 2;
}

static uint8_t ograniceniBrojMjestaZaCavle(uint8_t brojMjestaZaCavle) {
  (void)brojMjestaZaCavle;
  return 5;
}

static bool jeValjanSuncevDogadaj(uint8_t dogadaj) {
  return dogadaj < SUNCEVI_DOGADAJ_BROJ;
}

static uint8_t ogranicenoZvonoSuncevogDogadaja(uint8_t zvono) {
  return (zvono >= 1 && zvono <= 2) ? zvono : 1;
}

static int16_t ogranicenaOdgodaSuncevogDogadajaMin(int odgodaMin) {
  if (odgodaMin < -SUNCEVA_ODGODA_MIN) {
    return -SUNCEVA_ODGODA_MIN;
  }
  if (odgodaMin > SUNCEVA_ODGODA_MIN) {
    return SUNCEVA_ODGODA_MIN;
  }
  return static_cast<int16_t>(odgodaMin);
}

static uint8_t sanitizirajMaskuSuncevihDogadaja(uint8_t maska) {
  return maska & MASKA_SUNCEVIH_DOGADAJA;
}

static uint8_t sanitizirajOznakuCavla(uint8_t cavao, uint8_t brojMjestaZaCavle) {
  if (cavao > brojMjestaZaCavle) {
    return 0;
  }
  return cavao;
}

static uint8_t sanitizirajOznakuCavlaZvona(uint8_t cavao) {
  if (cavao > 4) {
    return 0;
  }
  return cavao;
}

static unsigned long minuteUMiliseconde(uint8_t minute) {
  return static_cast<unsigned long>(ogranicenoTrajanjeCavla(minute)) * 60000UL;
}

static uint8_t ogranicenaOdgodaSlavljenjaMin(uint8_t minute) {
  return constrain(minute, 0, 10);
}

static uint16_t izracunajChecksumPostavki(const EepromLayout::PostavkeSpremnik& ulaz) {
  EepromLayout::PostavkeSpremnik kopija = ulaz;
  kopija.checksum = 0;

  const uint8_t* podaci = reinterpret_cast<const uint8_t*>(&kopija);
  uint16_t suma = 0;
  for (size_t i = 0; i < sizeof(kopija); i++) {
    suma = static_cast<uint16_t>((suma << 1) | (suma >> 15));
    suma = static_cast<uint16_t>(suma + podaci[i] + 0x3D);
  }
  return suma;
}

static uint16_t izracunajChecksumSuncevihDogadaja(
    const EepromLayout::SunceviDogadajiSpremnik& ulaz) {
  EepromLayout::SunceviDogadajiSpremnik kopija = ulaz;
  kopija.checksum = 0;

  const uint8_t* podaci = reinterpret_cast<const uint8_t*>(&kopija);
  uint16_t suma = 0;
  for (size_t i = 0; i < sizeof(kopija); ++i) {
    suma = static_cast<uint16_t>((suma << 1) | (suma >> 15));
    suma = static_cast<uint16_t>(suma + podaci[i] + 0x27);
  }
  return suma;
}

static void pripremiIntegritetPostavki(EepromLayout::PostavkeSpremnik& cilj) {
  cilj.potpis = EepromLayout::POSTAVKE_POTPIS;
  cilj.verzija = EepromLayout::POSTAVKE_VERZIJA;
  cilj.checksum = izracunajChecksumPostavki(cilj);
}

static bool jeSpremnikNaZadanimPostavkama(const EepromLayout::PostavkeSpremnik& spremnik) {
  EepromLayout::PostavkeSpremnik kandidat = spremnik;
  EepromLayout::PostavkeSpremnik zadani = napraviZadanePostavke();
  kandidat.checksum = 0;
  zadani.checksum = 0;
  return memcmp(&kandidat, &zadani, sizeof(kandidat)) == 0;
}

static bool jeValjanMrezniTekst(const char* ulaz, size_t maxDuljina, bool dopustiPrazno) {
  bool imaZnakova = false;
  for (size_t i = 0; i < maxDuljina; i++) {
    const char c = ulaz[i];
    if (c == '\0') {
      return dopustiPrazno ? true : imaZnakova;
    }
    if (!isprint(static_cast<unsigned char>(c))) {
      return false;
    }
    if (c == '|') {
      return false;
    }
    imaZnakova = true;
  }
  return false;
}

static bool jeValjanIPv4Tekst(const char* ulaz, size_t maxDuljina) {
  bool imaZnamenku = false;
  for (size_t i = 0; i < maxDuljina; i++) {
    const char c = ulaz[i];
    if (c == '\0') {
      return imaZnamenku;
    }
    if (!isdigit(static_cast<unsigned char>(c)) && c != '.') {
      return false;
    }
    if (isdigit(static_cast<unsigned char>(c))) {
      imaZnamenku = true;
    }
  }
  return false;
}

static void osigurajNullTerminiraneMreznePostavke(EepromLayout::PostavkeSpremnik& spremnik) {
  spremnik.pristupLozinka[sizeof(spremnik.pristupLozinka) - 1] = '\0';
  spremnik.wifiSsid[sizeof(spremnik.wifiSsid) - 1] = '\0';
  spremnik.wifiLozinka[sizeof(spremnik.wifiLozinka) - 1] = '\0';
  spremnik.statickaIp[sizeof(spremnik.statickaIp) - 1] = '\0';
  spremnik.mreznaMaska[sizeof(spremnik.mreznaMaska) - 1] = '\0';
  spremnik.zadaniGateway[sizeof(spremnik.zadaniGateway) - 1] = '\0';
  spremnik.mqttBroker[sizeof(spremnik.mqttBroker) - 1] = '\0';
  spremnik.mqttKorisnik[sizeof(spremnik.mqttKorisnik) - 1] = '\0';
  spremnik.mqttLozinka[sizeof(spremnik.mqttLozinka) - 1] = '\0';
  spremnik.ntpServer[sizeof(spremnik.ntpServer) - 1] = '\0';
}

static bool sanitizirajMreznaPolja(EepromLayout::PostavkeSpremnik& spremnik) {
  bool biloPromjena = false;
  osigurajNullTerminiraneMreznePostavke(spremnik);

  if (!jeValjanMrezniTekst(spremnik.pristupLozinka, sizeof(spremnik.pristupLozinka), true)) {
    strncpy(spremnik.pristupLozinka, "1234", sizeof(spremnik.pristupLozinka) - 1);
    spremnik.pristupLozinka[sizeof(spremnik.pristupLozinka) - 1] = '\0';
    biloPromjena = true;
  }
  if (!jeValjanMrezniTekst(spremnik.wifiSsid, sizeof(spremnik.wifiSsid), true)) {
    strncpy(spremnik.wifiSsid, "WiFi", sizeof(spremnik.wifiSsid) - 1);
    spremnik.wifiSsid[sizeof(spremnik.wifiSsid) - 1] = '\0';
    biloPromjena = true;
  }
  if (!jeValjanMrezniTekst(spremnik.wifiLozinka, sizeof(spremnik.wifiLozinka), true)) {
    strncpy(spremnik.wifiLozinka, "password", sizeof(spremnik.wifiLozinka) - 1);
    spremnik.wifiLozinka[sizeof(spremnik.wifiLozinka) - 1] = '\0';
    biloPromjena = true;
  }
  if (!jeValjanIPv4Tekst(spremnik.statickaIp, sizeof(spremnik.statickaIp))) {
    strncpy(spremnik.statickaIp, "192.168.1.100", sizeof(spremnik.statickaIp) - 1);
    spremnik.statickaIp[sizeof(spremnik.statickaIp) - 1] = '\0';
    biloPromjena = true;
  }
  if (!jeValjanIPv4Tekst(spremnik.mreznaMaska, sizeof(spremnik.mreznaMaska))) {
    strncpy(spremnik.mreznaMaska, "255.255.255.0", sizeof(spremnik.mreznaMaska) - 1);
    spremnik.mreznaMaska[sizeof(spremnik.mreznaMaska) - 1] = '\0';
    biloPromjena = true;
  }
  if (!jeValjanIPv4Tekst(spremnik.zadaniGateway, sizeof(spremnik.zadaniGateway))) {
    strncpy(spremnik.zadaniGateway, "192.168.1.1", sizeof(spremnik.zadaniGateway) - 1);
    spremnik.zadaniGateway[sizeof(spremnik.zadaniGateway) - 1] = '\0';
    biloPromjena = true;
  }
  if (!jeValjanMrezniTekst(spremnik.mqttBroker, sizeof(spremnik.mqttBroker), false)) {
    strncpy(spremnik.mqttBroker, "192.168.1.100", sizeof(spremnik.mqttBroker) - 1);
    spremnik.mqttBroker[sizeof(spremnik.mqttBroker) - 1] = '\0';
    biloPromjena = true;
  }
  if (spremnik.mqttPort == 0) {
    spremnik.mqttPort = 1883;
    biloPromjena = true;
  }
  if (!jeValjanMrezniTekst(spremnik.mqttKorisnik, sizeof(spremnik.mqttKorisnik), true)) {
    strncpy(spremnik.mqttKorisnik, "toranj", sizeof(spremnik.mqttKorisnik) - 1);
    spremnik.mqttKorisnik[sizeof(spremnik.mqttKorisnik) - 1] = '\0';
    biloPromjena = true;
  }
  if (!jeValjanMrezniTekst(spremnik.mqttLozinka, sizeof(spremnik.mqttLozinka), true)) {
    strncpy(spremnik.mqttLozinka, "toranj2024", sizeof(spremnik.mqttLozinka) - 1);
    spremnik.mqttLozinka[sizeof(spremnik.mqttLozinka) - 1] = '\0';
    biloPromjena = true;
  }
  if (!jeValjanMrezniTekst(
          dohvatiNtpServerBezZastavice(spremnik.ntpServer),
          sizeof(spremnik.ntpServer) - (jeKodiranNtpStatus(spremnik.ntpServer) ? 1 : 0),
          false)) {
    kodirajNtpServer(
        spremnik.ntpServer,
        sizeof(spremnik.ntpServer),
        "pool.ntp.org",
        procitajNtpOmogucenostIzTeksta(spremnik.ntpServer));
    biloPromjena = true;
  }
  if (spremnik.dcfOmogucen > 1) {
    spremnik.dcfOmogucen = true;
    biloPromjena = true;
  }
  if (spremnik.wifiOmogucen > 1) {
    spremnik.wifiOmogucen = true;
    biloPromjena = true;
  }

  return biloPromjena;
}

static bool jeKompatibilanEEPROMZapisPostavki(const EepromLayout::PostavkeSpremnik& ucitano) {
  if (ucitano.potpis != EepromLayout::POSTAVKE_POTPIS) {
    return false;
  }
  if (ucitano.verzija != EepromLayout::POSTAVKE_VERZIJA) {
    return false;
  }
  return ucitano.checksum == izracunajChecksumPostavki(ucitano);
}

static bool ucitajAktualniSpremnikSkeniranjem(EepromLayout::PostavkeSpremnik& spremnik) {
  const int pocetniSlot = WearLeveling::odrediSlotZaCitanje(
      EepromLayout::BAZA_POSTAVKE,
      EepromLayout::SLOTOVI_POSTAVKE,
      sizeof(EepromLayout::PostavkeSpremnik));

  for (int pomak = 0; pomak < EepromLayout::SLOTOVI_POSTAVKE; ++pomak) {
    const int slot =
        (pocetniSlot - pomak + EepromLayout::SLOTOVI_POSTAVKE) % EepromLayout::SLOTOVI_POSTAVKE;
    const int adresa =
        EepromLayout::BAZA_POSTAVKE + slot * static_cast<int>(sizeof(EepromLayout::PostavkeSpremnik));
    EepromLayout::PostavkeSpremnik kandidat{};
    if (!VanjskiEEPROM::procitaj(adresa, &kandidat, sizeof(kandidat))) {
      continue;
    }
    if (!jeKompatibilanEEPROMZapisPostavki(kandidat)) {
      continue;
    }

    spremnik = kandidat;
    WearLeveling::zapamtiZadnjiSlot(
        EepromLayout::BAZA_POSTAVKE,
        EepromLayout::SLOTOVI_POSTAVKE,
        sizeof(EepromLayout::PostavkeSpremnik),
        static_cast<uint8_t>(slot));
    return true;
  }

  return false;
}

static bool ucitajKompatibilanSpremnik(EepromLayout::PostavkeSpremnik& spremnik) {
  if (!ucitajAktualniSpremnikSkeniranjem(spremnik)) {
    spremnik = napraviZadanePostavke();
    return false;
  }

  return true;
}

static bool pokusajMigriratiStarijePostavke(
    EepromLayout::PostavkeSpremnik& spremnik,
    bool prekoZadaneKopije) {
  const uint8_t verzijaIzvora =
      pokusajMigriratiLegacySpremnikPostavki(spremnik, napraviZadanePostavke());
  if (verzijaIzvora == 0) {
    return false;
  }

  char log[96];
  if (prekoZadaneKopije) {
    snprintf(log,
             sizeof(log),
             "Postavke: vracen stariji v%u zapis preko zadane v%u kopije",
             static_cast<unsigned>(verzijaIzvora),
             static_cast<unsigned>(EepromLayout::POSTAVKE_VERZIJA));
  } else {
    snprintf(log,
             sizeof(log),
             "Postavke: migrirane iz verzije %u na verziju %u",
             static_cast<unsigned>(verzijaIzvora),
             static_cast<unsigned>(EepromLayout::POSTAVKE_VERZIJA));
  }
  posaljiPCLog(log);
  return true;
}

static bool sanitizirajRadnaPolja(EepromLayout::PostavkeSpremnik& spremnik) {
  bool trebaSpremiti = false;

  if (spremnik.satOd < 0 || spremnik.satOd > 23) {
    spremnik.satOd = 6;
    trebaSpremiti = true;
  }
  if (spremnik.satDo < 0 || spremnik.satDo > 23) {
    spremnik.satDo = 22;
    trebaSpremiti = true;
  }
  if (spremnik.satDo <= spremnik.satOd) {
    spremnik.satDo = spremnik.satOd + 8;
    trebaSpremiti = true;
  }
  if (spremnik.satDo > 23) {
    spremnik.satDo = 23;
    trebaSpremiti = true;
  }
  if (spremnik.tihiSatiOd < 0 || spremnik.tihiSatiOd > 23) {
    spremnik.tihiSatiOd = 22;
    trebaSpremiti = true;
  }
  if (spremnik.tihiSatiDo < 0 || spremnik.tihiSatiDo > 23) {
    spremnik.tihiSatiDo = 6;
    trebaSpremiti = true;
  }
  {
    const bool plocaAktivna = spremnik.plocaPocetakMinuta >= 0;
    int plocaPocetak = dekodirajPocetakPloceMinuta(spremnik.plocaPocetakMinuta);
    int plocaKraj = spremnik.plocaKrajMinuta;

    if (plocaPocetak < 0 || plocaPocetak > (23 * 60 + 59) ||
        plocaKraj < 0 || plocaKraj > (23 * 60 + 59)) {
      plocaPocetak = PLOCA_ZADANI_POCETAK_MINUTA;
      plocaKraj = PLOCA_ZADANI_KRAJ_MINUTA;
      spremnik.plocaPocetakMinuta = kodirajPocetakPloceMinuta(plocaPocetak, true);
      spremnik.plocaKrajMinuta = plocaKraj;
      trebaSpremiti = true;
    } else {
      if (plocaAktivna &&
          plocaPocetak == PLOCA_STARI_ZADANI_POCETAK_MINUTA &&
          plocaKraj == PLOCA_STARI_ZADANI_KRAJ_MINUTA) {
        plocaPocetak = PLOCA_ZADANI_POCETAK_MINUTA;
        plocaKraj = PLOCA_ZADANI_KRAJ_MINUTA;
        trebaSpremiti = true;
      }

      const int normaliziraniPocetak = normalizirajMinutuPloceNaBlok(plocaPocetak);
      int normaliziraniKraj = normalizirajMinutuPloceNaBlok(plocaKraj);
      if (plocaAktivna && normaliziraniKraj <= normaliziraniPocetak) {
        normaliziraniKraj = min(normaliziraniPocetak + PLOCA_MINUTNI_BLOK, PLOCA_ZADNJA_CETVRT);
      }

      const int kodiraniPocetak = kodirajPocetakPloceMinuta(normaliziraniPocetak, plocaAktivna);
      if (spremnik.plocaPocetakMinuta != kodiraniPocetak ||
          spremnik.plocaKrajMinuta != normaliziraniKraj) {
        spremnik.plocaPocetakMinuta = kodiraniPocetak;
        spremnik.plocaKrajMinuta = normaliziraniKraj;
        trebaSpremiti = true;
      }
    }
  }
  if (spremnik.trajanjeImpulsaCekicaMs < 50 || spremnik.trajanjeImpulsaCekicaMs > 150) {
    spremnik.trajanjeImpulsaCekicaMs = 150;
    trebaSpremiti = true;
  }
  if (spremnik.pauzaIzmeduUdaraca < 100) {
    spremnik.pauzaIzmeduUdaraca = 400;
    trebaSpremiti = true;
  }
  if (spremnik.trajanjeZvonjenjaRadniMin != 2 && spremnik.trajanjeZvonjenjaRadniMin != 3) {
    spremnik.trajanjeZvonjenjaRadniMin = 2;
    trebaSpremiti = true;
  }
  if (spremnik.trajanjeZvonjenjaNedjeljaMin != 2 && spremnik.trajanjeZvonjenjaNedjeljaMin != 3) {
    spremnik.trajanjeZvonjenjaNedjeljaMin = 3;
    trebaSpremiti = true;
  }
  if (spremnik.trajanjeSlavljenjaMin != 2 && spremnik.trajanjeSlavljenjaMin != 3) {
    spremnik.trajanjeSlavljenjaMin = 2;
    trebaSpremiti = true;
  }
  if (spremnik.slavljenjePrijeZvonjenja > 10) {
    spremnik.slavljenjePrijeZvonjenja = 0;
    trebaSpremiti = true;
  }
  if (spremnik.brojZvona != 2) {
    spremnik.brojZvona = 2;
    trebaSpremiti = true;
  }
  if (spremnik.brojMjestaZaCavle != 5) {
    spremnik.brojMjestaZaCavle = 5;
    trebaSpremiti = true;
  }

  for (uint8_t i = 0; i < 4; i++) {
    const uint8_t noviRadni = sanitizirajOznakuCavlaZvona(
        sanitizirajOznakuCavla(spremnik.cavliRadni[i], spremnik.brojMjestaZaCavle));
    if (noviRadni != spremnik.cavliRadni[i]) {
      spremnik.cavliRadni[i] = noviRadni;
      trebaSpremiti = true;
    }

    const uint8_t noviNedjelja = sanitizirajOznakuCavlaZvona(
        sanitizirajOznakuCavla(spremnik.cavliNedjelja[i], spremnik.brojMjestaZaCavle));
    if (noviNedjelja != spremnik.cavliNedjelja[i]) {
      spremnik.cavliNedjelja[i] = noviNedjelja;
      trebaSpremiti = true;
    }
  }

  const uint8_t noviSlavljenje = 5;
  if (noviSlavljenje != spremnik.cavaoSlavljenje) {
    spremnik.cavaoSlavljenje = noviSlavljenje;
    trebaSpremiti = true;
  }

  const uint8_t noviMrtvacko = 0;
  if (noviMrtvacko != spremnik.cavaoMrtvacko) {
    spremnik.cavaoMrtvacko = noviMrtvacko;
    trebaSpremiti = true;
  }

  if (spremnik.modSlavljenja < 1 || spremnik.modSlavljenja > 2) {
    spremnik.modSlavljenja = 1;
    trebaSpremiti = true;
  }

  if (spremnik.modOtkucavanja < 1 || spremnik.modOtkucavanja > 2) {
    spremnik.modOtkucavanja = 2;
    trebaSpremiti = true;
  }

  if (spremnik.imaKazaljke > 1) {
    spremnik.imaKazaljke = true;
    trebaSpremiti = true;
  }

  return trebaSpremiti;
}

static void ucitajRadnePostavkeIzSpremnika(const EepromLayout::PostavkeSpremnik& spremnik) {
  postavke.satOd = spremnik.satOd;
  postavke.satDo = spremnik.satDo;
  postavke.tihiSatiOd = spremnik.tihiSatiOd;
  postavke.tihiSatiDo = spremnik.tihiSatiDo;
  postavke.plocaPocetakMinuta = spremnik.plocaPocetakMinuta;
  postavke.plocaKrajMinuta = spremnik.plocaKrajMinuta;
  postavke.trajanjeImpulsaCekicaMs = spremnik.trajanjeImpulsaCekicaMs;
  postavke.pauzaIzmeduUdaraca = spremnik.pauzaIzmeduUdaraca;
  postavke.trajanjeZvonjenjaRadniMin = spremnik.trajanjeZvonjenjaRadniMin;
  postavke.trajanjeZvonjenjaNedjeljaMin = spremnik.trajanjeZvonjenjaNedjeljaMin;
  postavke.trajanjeSlavljenjaMin = spremnik.trajanjeSlavljenjaMin;
  postavke.slavljenjePrijeZvonjenja = spremnik.slavljenjePrijeZvonjenja;
  postavke.brojZvona = spremnik.brojZvona;
  postavke.brojMjestaZaCavle = spremnik.brojMjestaZaCavle;
  memcpy(postavke.cavliRadni, spremnik.cavliRadni, sizeof(postavke.cavliRadni));
  memcpy(postavke.cavliNedjelja, spremnik.cavliNedjelja, sizeof(postavke.cavliNedjelja));
  postavke.cavaoSlavljenje = spremnik.cavaoSlavljenje;
  postavke.cavaoMrtvacko = spremnik.cavaoMrtvacko;
  postavke.koristiDhcp = spremnik.koristiDhcp;
  postavke.lcdPozadinskoOsvjetljenje = spremnik.lcdPozadinskoOsvjetljenje;
  postavke.modSlavljenja = spremnik.modSlavljenja;
  postavke.modOtkucavanja = spremnik.modOtkucavanja;
  postavke.ntpOmogucen = procitajNtpOmogucenostIzTeksta(spremnik.ntpServer);
  postavke.dcfOmogucen = spremnik.dcfOmogucen;
  postavke.wifiOmogucen = spremnik.wifiOmogucen;
  postavke.imaKazaljke = spremnik.imaKazaljke;
}

static void upisiRadnePostavkeUSpremnik(EepromLayout::PostavkeSpremnik& spremnik) {
  spremnik.satOd = postavke.satOd;
  spremnik.satDo = postavke.satDo;
  spremnik.tihiSatiOd = postavke.tihiSatiOd;
  spremnik.tihiSatiDo = postavke.tihiSatiDo;
  spremnik.plocaPocetakMinuta = postavke.plocaPocetakMinuta;
  spremnik.plocaKrajMinuta = postavke.plocaKrajMinuta;
  spremnik.trajanjeImpulsaCekicaMs = postavke.trajanjeImpulsaCekicaMs;
  spremnik.pauzaIzmeduUdaraca = postavke.pauzaIzmeduUdaraca;
  spremnik.trajanjeZvonjenjaRadniMin = postavke.trajanjeZvonjenjaRadniMin;
  spremnik.trajanjeZvonjenjaNedjeljaMin = postavke.trajanjeZvonjenjaNedjeljaMin;
  spremnik.trajanjeSlavljenjaMin = postavke.trajanjeSlavljenjaMin;
  spremnik.slavljenjePrijeZvonjenja = postavke.slavljenjePrijeZvonjenja;
  spremnik.brojZvona = postavke.brojZvona;
  spremnik.brojMjestaZaCavle = postavke.brojMjestaZaCavle;
  memcpy(spremnik.cavliRadni, postavke.cavliRadni, sizeof(spremnik.cavliRadni));
  memcpy(spremnik.cavliNedjelja, postavke.cavliNedjelja, sizeof(spremnik.cavliNedjelja));
  spremnik.cavaoSlavljenje = postavke.cavaoSlavljenje;
  spremnik.cavaoMrtvacko = postavke.cavaoMrtvacko;
  spremnik.koristiDhcp = postavke.koristiDhcp;
  spremnik.lcdPozadinskoOsvjetljenje = postavke.lcdPozadinskoOsvjetljenje;
  spremnik.modSlavljenja = postavke.modSlavljenja;
  spremnik.modOtkucavanja = postavke.modOtkucavanja;
  spremnik.dcfOmogucen = postavke.dcfOmogucen;
  spremnik.wifiOmogucen = postavke.wifiOmogucen;
  spremnik.imaKazaljke = postavke.imaKazaljke;
}

static void ucitajWifiSekcijuIzSpremnika(const EepromLayout::PostavkeSpremnik& spremnik) {
  strncpy(mrezniCache.wifi.wifiSsid, spremnik.wifiSsid, sizeof(mrezniCache.wifi.wifiSsid) - 1);
  mrezniCache.wifi.wifiSsid[sizeof(mrezniCache.wifi.wifiSsid) - 1] = '\0';
  strncpy(mrezniCache.wifi.wifiLozinka, spremnik.wifiLozinka, sizeof(mrezniCache.wifi.wifiLozinka) - 1);
  mrezniCache.wifi.wifiLozinka[sizeof(mrezniCache.wifi.wifiLozinka) - 1] = '\0';
  strncpy(mrezniCache.wifi.statickaIp, spremnik.statickaIp, sizeof(mrezniCache.wifi.statickaIp) - 1);
  mrezniCache.wifi.statickaIp[sizeof(mrezniCache.wifi.statickaIp) - 1] = '\0';
  strncpy(mrezniCache.wifi.mreznaMaska, spremnik.mreznaMaska, sizeof(mrezniCache.wifi.mreznaMaska) - 1);
  mrezniCache.wifi.mreznaMaska[sizeof(mrezniCache.wifi.mreznaMaska) - 1] = '\0';
  strncpy(mrezniCache.wifi.zadaniGateway, spremnik.zadaniGateway, sizeof(mrezniCache.wifi.zadaniGateway) - 1);
  mrezniCache.wifi.zadaniGateway[sizeof(mrezniCache.wifi.zadaniGateway) - 1] = '\0';
}

static void ucitajSinkronizacijskuSekcijuIzSpremnika(const EepromLayout::PostavkeSpremnik& spremnik) {
  strncpy(
      mrezniCache.sinkronizacija.ntpServer,
      spremnik.ntpServer,
      sizeof(mrezniCache.sinkronizacija.ntpServer) - 1);
  mrezniCache.sinkronizacija.ntpServer[sizeof(mrezniCache.sinkronizacija.ntpServer) - 1] = '\0';
}

static void invalidirajMrezniCache() {
  aktivnaMreznaSekcija = MREZNA_SEKCIJA_NISTA;
  memset(&mrezniCache, 0, sizeof(mrezniCache));
}

static void osigurajUcitanuMreznuSekciju(AktivnaMreznaSekcija trazenaSekcija) {
  if (aktivnaMreznaSekcija == trazenaSekcija) {
    return;
  }

  EepromLayout::PostavkeSpremnik spremnik = napraviZadanePostavke();
  ucitajKompatibilanSpremnik(spremnik);
  sanitizirajMreznaPolja(spremnik);

  switch (trazenaSekcija) {
    case MREZNA_SEKCIJA_WIFI:
      ucitajWifiSekcijuIzSpremnika(spremnik);
      break;
    case MREZNA_SEKCIJA_SINKRONIZACIJA:
      ucitajSinkronizacijskuSekcijuIzSpremnika(spremnik);
      break;
    case MREZNA_SEKCIJA_NISTA:
    default:
      break;
  }

  aktivnaMreznaSekcija = trazenaSekcija;
}

static void spremiSpremnikPostavki(EepromLayout::PostavkeSpremnik& spremnik) {
  sanitizirajRadnaPolja(spremnik);
  sanitizirajMreznaPolja(spremnik);
  pripremiIntegritetPostavki(spremnik);
  WearLeveling::spremi(
      EepromLayout::BAZA_POSTAVKE,
      EepromLayout::SLOTOVI_POSTAVKE,
      spremnik);
  ucitajRadnePostavkeIzSpremnika(spremnik);
  invalidirajMrezniCache();
}

static bool jeKompatibilanEEPROMZapisSuncevihDogadaja(
    const EepromLayout::SunceviDogadajiSpremnik& spremnik) {
  return spremnik.potpis == EepromLayout::SUNCEVI_DOGADAJI_POTPIS &&
         spremnik.verzija == EepromLayout::SUNCEVI_DOGADAJI_VERZIJA &&
         spremnik.checksum == izracunajChecksumSuncevihDogadaja(spremnik);
}

static bool sanitizirajSunceveDogadajeSpremnik(EepromLayout::SunceviDogadajiSpremnik& spremnik) {
  bool trebaSpremiti = false;

  if (spremnik.zemljopisnaSirinaE5 != ZADANA_ZEMLJOPISNA_SIRINA_E5) {
    spremnik.zemljopisnaSirinaE5 = ZADANA_ZEMLJOPISNA_SIRINA_E5;
    trebaSpremiti = true;
  }
  if (spremnik.zemljopisnaDuzinaE5 != ZADANA_ZEMLJOPISNA_DUZINA_E5) {
    spremnik.zemljopisnaDuzinaE5 = ZADANA_ZEMLJOPISNA_DUZINA_E5;
    trebaSpremiti = true;
  }

  const uint8_t novaMaska = sanitizirajMaskuSuncevihDogadaja(spremnik.maskaDogadaja);
  if (novaMaska != spremnik.maskaDogadaja) {
    spremnik.maskaDogadaja = novaMaska;
    trebaSpremiti = true;
  }

  for (uint8_t i = 0; i < SUNCEVI_DOGADAJ_BROJ; ++i) {
    const uint8_t novoZvono = ogranicenoZvonoSuncevogDogadaja(spremnik.zvona[i]);
    if (novoZvono != spremnik.zvona[i]) {
      spremnik.zvona[i] = novoZvono;
      trebaSpremiti = true;
    }

    const int16_t novaOdgoda =
        (i == SUNCEVI_DOGADAJ_PODNE) ? 0 : ogranicenaOdgodaSuncevogDogadajaMin(spremnik.odgodeMin[i]);
    if (novaOdgoda != spremnik.odgodeMin[i]) {
      spremnik.odgodeMin[i] = novaOdgoda;
      trebaSpremiti = true;
    }
  }

  return trebaSpremiti;
}

static void pripremiIntegritetSuncevihDogadaja(EepromLayout::SunceviDogadajiSpremnik& spremnik) {
  spremnik.potpis = EepromLayout::SUNCEVI_DOGADAJI_POTPIS;
  spremnik.verzija = EepromLayout::SUNCEVI_DOGADAJI_VERZIJA;
  spremnik.checksum = izracunajChecksumSuncevihDogadaja(spremnik);
}

static void ucitajSunceveDogadajeIzSpremnika(const EepromLayout::SunceviDogadajiSpremnik& spremnik) {
  postavke.zemljopisnaSirinaE5 = ZADANA_ZEMLJOPISNA_SIRINA_E5;
  postavke.zemljopisnaDuzinaE5 = ZADANA_ZEMLJOPISNA_DUZINA_E5;
  postavke.maskaSuncevihDogadaja = sanitizirajMaskuSuncevihDogadaja(spremnik.maskaDogadaja);
  memcpy(postavke.zvonaSuncevihDogadaja, spremnik.zvona, sizeof(postavke.zvonaSuncevihDogadaja));
  memcpy(
      postavke.odgodeSuncevihDogadajaMin, spremnik.odgodeMin, sizeof(postavke.odgodeSuncevihDogadajaMin));
}

static void upisiSunceveDogadajeUSpremnik(EepromLayout::SunceviDogadajiSpremnik& spremnik) {
  spremnik.zemljopisnaSirinaE5 = ZADANA_ZEMLJOPISNA_SIRINA_E5;
  spremnik.zemljopisnaDuzinaE5 = ZADANA_ZEMLJOPISNA_DUZINA_E5;
  spremnik.maskaDogadaja = sanitizirajMaskuSuncevihDogadaja(postavke.maskaSuncevihDogadaja);
  memcpy(spremnik.zvona, postavke.zvonaSuncevihDogadaja, sizeof(spremnik.zvona));
  memcpy(spremnik.odgodeMin, postavke.odgodeSuncevihDogadajaMin, sizeof(spremnik.odgodeMin));
}

static void spremiSunceveDogadajeSpremnik(EepromLayout::SunceviDogadajiSpremnik& spremnik) {
  sanitizirajSunceveDogadajeSpremnik(spremnik);
  pripremiIntegritetSuncevihDogadaja(spremnik);
  WearLeveling::spremi(
      EepromLayout::BAZA_SUNCEVI_DOGADAJI, EepromLayout::SLOTOVI_SUNCEVI_DOGADAJI, spremnik);
  ucitajSunceveDogadajeIzSpremnika(spremnik);
}

static bool ucitajSunceveDogadajeSkeniranjem(EepromLayout::SunceviDogadajiSpremnik& spremnik) {
  const int pocetniSlot = WearLeveling::odrediSlotZaCitanje(
      EepromLayout::BAZA_SUNCEVI_DOGADAJI,
      EepromLayout::SLOTOVI_SUNCEVI_DOGADAJI,
      sizeof(EepromLayout::SunceviDogadajiSpremnik));

  for (int pomak = 0; pomak < EepromLayout::SLOTOVI_SUNCEVI_DOGADAJI; ++pomak) {
    const int slot =
        (pocetniSlot - pomak + EepromLayout::SLOTOVI_SUNCEVI_DOGADAJI) %
        EepromLayout::SLOTOVI_SUNCEVI_DOGADAJI;
    const int adresa =
        EepromLayout::BAZA_SUNCEVI_DOGADAJI +
        slot * static_cast<int>(sizeof(EepromLayout::SunceviDogadajiSpremnik));
    EepromLayout::SunceviDogadajiSpremnik kandidat{};
    if (!VanjskiEEPROM::procitaj(adresa, &kandidat, sizeof(kandidat))) {
      continue;
    }
    if (!jeKompatibilanEEPROMZapisSuncevihDogadaja(kandidat)) {
      continue;
    }

    spremnik = kandidat;
    WearLeveling::zapamtiZadnjiSlot(
        EepromLayout::BAZA_SUNCEVI_DOGADAJI,
        EepromLayout::SLOTOVI_SUNCEVI_DOGADAJI,
        sizeof(EepromLayout::SunceviDogadajiSpremnik),
        static_cast<uint8_t>(slot));
    return true;
  }

  return false;
}

static void ucitajSunceveDogadaje() {
  EepromLayout::SunceviDogadajiSpremnik spremnik = napraviZadaneSunceveDogadaje();
  bool ucitano = ucitajSunceveDogadajeSkeniranjem(spremnik);
  bool trebaSpremiti = false;

  if (!ucitano) {
    spremnik = napraviZadaneSunceveDogadaje();
    trebaSpremiti = true;
  }

  if (sanitizirajSunceveDogadajeSpremnik(spremnik)) {
    trebaSpremiti = true;
  }

  ucitajSunceveDogadajeIzSpremnika(spremnik);

  if (trebaSpremiti) {
    spremiSunceveDogadajeSpremnik(spremnik);
  }
}

}  // namespace

void ucitajPostavke() {
  EepromLayout::PostavkeSpremnik spremnik = napraviZadanePostavke();
  bool trebaSpremiti = false;
  bool ucitanoIzEeproma = ucitajAktualniSpremnikSkeniranjem(spremnik);

  if (!ucitanoIzEeproma) {
    if (pokusajMigriratiStarijePostavke(spremnik, false)) {
      trebaSpremiti = true;
    } else {
      posaljiPCLog(F("Postavke: koriste default vrijednosti"));
      spremnik = napraviZadanePostavke();
      trebaSpremiti = true;
    }
  } else {
    posaljiPCLog(F("Postavke: ucitane iz EEPROM-a"));
    if (jeKompatibilanEEPROMZapisPostavki(spremnik)) {
      if (jeSpremnikNaZadanimPostavkama(spremnik)) {
        EepromLayout::PostavkeSpremnik legacy = spremnik;
        if (pokusajMigriratiStarijePostavke(legacy, true)) {
          spremnik = legacy;
          trebaSpremiti = true;
        }
      }
    } else {
      if (pokusajMigriratiStarijePostavke(spremnik, false)) {
        trebaSpremiti = true;
      } else {
        posaljiPCLog(F("Postavke: nekompatibilan zapis -> reset na default"));
        spremnik = napraviZadanePostavke();
        trebaSpremiti = true;
      }
    }
  }

  if (sanitizirajRadnaPolja(spremnik)) {
    trebaSpremiti = true;
  }
  if (sanitizirajMreznaPolja(spremnik)) {
    posaljiPCLog(F("Postavke: string polja popravljena fallback vrijednostima"));
    trebaSpremiti = true;
  }

  pripremiIntegritetPostavki(spremnik);
  ucitajRadnePostavkeIzSpremnika(spremnik);
  invalidirajMrezniCache();
  ucitajSunceveDogadaje();

  char log[256];
  snprintf_P(
      log,
      sizeof(log),
      PSTR("Postavke: sat %d-%d, WiFi: %s SSID=%s, NTP: %s (%s), DCF: %s, LCD: %s, Kazaljke: %s, Slavljenje: %u, Otkucavanje: %u, Cavli RD/NED/SL=%u/%u/%u odg=%u, Zvona=%u, Mjesta=%u, Sunce loc=%ld/%ld maska=%u"),
      spremnik.satOd,
      spremnik.satDo,
      spremnik.wifiOmogucen ? "ON" : "OFF",
      spremnik.wifiSsid,
      dohvatiNtpServerBezZastavice(spremnik.ntpServer),
      procitajNtpOmogucenostIzTeksta(spremnik.ntpServer) ? "ON" : "OFF",
      spremnik.dcfOmogucen ? "ON" : "OFF",
      spremnik.lcdPozadinskoOsvjetljenje ? "ON" : "OFF",
      spremnik.imaKazaljke ? "ON" : "OFF",
      spremnik.modSlavljenja,
      spremnik.modOtkucavanja,
      spremnik.trajanjeZvonjenjaRadniMin,
      spremnik.trajanjeZvonjenjaNedjeljaMin,
      spremnik.trajanjeSlavljenjaMin,
      spremnik.slavljenjePrijeZvonjenja,
      spremnik.brojZvona,
      spremnik.brojMjestaZaCavle,
      static_cast<long>(postavke.zemljopisnaSirinaE5),
      static_cast<long>(postavke.zemljopisnaDuzinaE5),
      postavke.maskaSuncevihDogadaja);
  posaljiPCLog(log);

  if (trebaSpremiti) {
    WearLeveling::spremi(
        EepromLayout::BAZA_POSTAVKE,
        EepromLayout::SLOTOVI_POSTAVKE,
        spremnik);
  }
}

uint8_t dohvatiBrojZvona() {
  return ograniceniBrojZvona(postavke.brojZvona);
}

uint8_t dohvatiBrojMjestaZaCavle() {
  return ograniceniBrojMjestaZaCavle(postavke.brojMjestaZaCavle);
}

uint8_t dohvatiCavaoRadniZaZvono(uint8_t zvono) {
  if (zvono < 1 || zvono > 2) {
    return 0;
  }
  return sanitizirajOznakuCavla(postavke.cavliRadni[zvono - 1], dohvatiBrojMjestaZaCavle());
}

uint8_t dohvatiCavaoNedjeljaZaZvono(uint8_t zvono) {
  if (zvono < 1 || zvono > 2) {
    return 0;
  }
  return sanitizirajOznakuCavla(postavke.cavliNedjelja[zvono - 1], dohvatiBrojMjestaZaCavle());
}

uint8_t dohvatiCavaoSlavljenja() {
  return 5;
}

uint8_t dohvatiCavaoMrtvackog() {
  // Mrtvacko zvono je namjerno odvojeno od okretne ploce i cavala.
  // Polje ostaje samo radi kompatibilnosti spremljenih postavki toranjskog sata.
  return 0;
}

bool jeDozvoljenoOtkucavanjeUSatu(int sat) {
  return sat >= postavke.satOd && sat < postavke.satDo;
}

bool jeTihiPeriodAktivanZaSatneOtkucaje(int sat) {
  sat = constrain(sat, 0, 23);

  if (postavke.tihiSatiOd == postavke.tihiSatiDo) {
    return false;
  }

  if (postavke.tihiSatiOd > postavke.tihiSatiDo) {
    return sat >= postavke.tihiSatiOd || sat < postavke.tihiSatiDo;
  }

  return sat >= postavke.tihiSatiOd && sat < postavke.tihiSatiDo;
}

int dohvatiTihiPeriodOdSata() {
  return postavke.tihiSatiOd;
}

int dohvatiTihiPeriodDoSata() {
  return postavke.tihiSatiDo;
}

void postaviTihiPeriodSatnihOtkucaja(int satOd, int satDo) {
  satOd = constrain(satOd, 0, 23);
  satDo = constrain(satDo, 0, 23);

  if (postavke.tihiSatiOd == satOd && postavke.tihiSatiDo == satDo) {
    return;
  }

  postavke.tihiSatiOd = satOd;
  postavke.tihiSatiDo = satDo;

  EepromLayout::PostavkeSpremnik spremnik = napraviZadanePostavke();
  ucitajKompatibilanSpremnik(spremnik);
  upisiRadnePostavkeUSpremnik(spremnik);
  spremiSpremnikPostavki(spremnik);

  char log[48];
  snprintf(log, sizeof(log), "Tihi sati satnih otkucaja: %d-%d", postavke.tihiSatiOd, postavke.tihiSatiDo);
  posaljiPCLog(log);
}

unsigned int dohvatiTrajanjeImpulsaCekica() {
  return ogranicenoTrajanjeImpulsaCekicaMs(postavke.trajanjeImpulsaCekicaMs);
}

unsigned int dohvatiPauzuIzmeduUdaraca() {
  return postavke.pauzaIzmeduUdaraca;
}

void postaviTrajanjeImpulsaCekica(unsigned int trajanjeMs) {
  const unsigned int novoTrajanjeMs = ogranicenoTrajanjeImpulsaCekicaMs(trajanjeMs);
  if (postavke.trajanjeImpulsaCekicaMs == novoTrajanjeMs) {
    return;
  }

  postavke.trajanjeImpulsaCekicaMs = novoTrajanjeMs;

  EepromLayout::PostavkeSpremnik spremnik = napraviZadanePostavke();
  ucitajKompatibilanSpremnik(spremnik);
  upisiRadnePostavkeUSpremnik(spremnik);
  spremiSpremnikPostavki(spremnik);

  char log[64];
  snprintf(log,
           sizeof(log),
           "Postavke: trajanje impulsa cekica postavljeno na %u ms",
           static_cast<unsigned>(postavke.trajanjeImpulsaCekicaMs));
  posaljiPCLog(log);
}

unsigned long dohvatiTrajanjeZvonjenjaRadniMs() {
  return minuteUMiliseconde(postavke.trajanjeZvonjenjaRadniMin);
}

unsigned long dohvatiTrajanjeZvonjenjaNedjeljaMs() {
  return minuteUMiliseconde(postavke.trajanjeZvonjenjaNedjeljaMin);
}

unsigned long dohvatiTrajanjeSlavljenjaMs() {
  return minuteUMiliseconde(postavke.trajanjeSlavljenjaMin);
}

uint8_t dohvatiTrajanjeZvonjenjaRadniMin() {
  return ogranicenoTrajanjeCavla(postavke.trajanjeZvonjenjaRadniMin);
}

uint8_t dohvatiTrajanjeZvonjenjaNedjeljaMin() {
  return ogranicenoTrajanjeCavla(postavke.trajanjeZvonjenjaNedjeljaMin);
}

uint8_t dohvatiTrajanjeSlavljenjaMin() {
  return ogranicenoTrajanjeCavla(postavke.trajanjeSlavljenjaMin);
}

uint8_t dohvatiOdgoduSlavljenjaMin() {
  return ogranicenaOdgodaSlavljenjaMin(postavke.slavljenjePrijeZvonjenja);
}

bool jePlocaKonfigurirana() {
  return postavke.plocaPocetakMinuta >= 0 &&
         dekodirajPocetakPloceMinuta(postavke.plocaPocetakMinuta) != postavke.plocaKrajMinuta;
}

int dohvatiPocetakPloceMinute() {
  return dekodirajPocetakPloceMinuta(postavke.plocaPocetakMinuta);
}

int dohvatiKrajPloceMinute() {
  return postavke.plocaKrajMinuta;
}

const char* dohvatiWifiSsid() {
  osigurajUcitanuMreznuSekciju(MREZNA_SEKCIJA_WIFI);
  return mrezniCache.wifi.wifiSsid;
}

const char* dohvatiWifiLozinku() {
  osigurajUcitanuMreznuSekciju(MREZNA_SEKCIJA_WIFI);
  return mrezniCache.wifi.wifiLozinka;
}

bool jeWiFiOmogucen() {
  return postavke.wifiOmogucen;
}

bool koristiDhcpMreza() {
  return postavke.koristiDhcp;
}

bool jeLCDPozadinskoOsvjetljenjeUkljuceno() {
  return postavke.lcdPozadinskoOsvjetljenje;
}

bool imaKazaljkeSata() {
  return postavke.imaKazaljke;
}

uint8_t dohvatiModSlavljenja() {
  return postavke.modSlavljenja;
}

uint8_t dohvatiModOtkucavanja() {
  return postavke.modOtkucavanja;
}

const char* dohvatiStatickuIP() {
  osigurajUcitanuMreznuSekciju(MREZNA_SEKCIJA_WIFI);
  return mrezniCache.wifi.statickaIp;
}

const char* dohvatiMreznuMasku() {
  osigurajUcitanuMreznuSekciju(MREZNA_SEKCIJA_WIFI);
  return mrezniCache.wifi.mreznaMaska;
}

const char* dohvatiZadaniGateway() {
  osigurajUcitanuMreznuSekciju(MREZNA_SEKCIJA_WIFI);
  return mrezniCache.wifi.zadaniGateway;
}

const char* dohvatiNTPServer() {
  osigurajUcitanuMreznuSekciju(MREZNA_SEKCIJA_SINKRONIZACIJA);
  return dohvatiNtpServerBezZastavice(mrezniCache.sinkronizacija.ntpServer);
}

bool jeNTPOmogucen() {
  return postavke.ntpOmogucen;
}

bool jeDCFOmogucen() {
  return postavke.dcfOmogucen;
}

int32_t dohvatiZemljopisnuSirinuE5() {
  return ZADANA_ZEMLJOPISNA_SIRINA_E5;
}

int32_t dohvatiZemljopisnuDuzinuE5() {
  return ZADANA_ZEMLJOPISNA_DUZINA_E5;
}

bool jeSuncevDogadajOmogucen(uint8_t dogadaj) {
  if (!jeValjanSuncevDogadaj(dogadaj)) {
    return false;
  }
  return (postavke.maskaSuncevihDogadaja & (1U << dogadaj)) != 0;
}

uint8_t dohvatiZvonoZaSuncevDogadaj(uint8_t dogadaj) {
  if (!jeValjanSuncevDogadaj(dogadaj)) {
    return 1;
  }
  return ogranicenoZvonoSuncevogDogadaja(postavke.zvonaSuncevihDogadaja[dogadaj]);
}

int dohvatiOdgoduSuncevogDogadajaMin(uint8_t dogadaj) {
  if (!jeValjanSuncevDogadaj(dogadaj)) {
    return 0;
  }
  if (dogadaj == SUNCEVI_DOGADAJ_PODNE) {
    return 0;
  }
  return ogranicenaOdgodaSuncevogDogadajaMin(postavke.odgodeSuncevihDogadajaMin[dogadaj]);
}

void postaviWiFiOmogucen(bool omogucen) {
  if (postavke.wifiOmogucen == omogucen) {
    return;
  }

  postavke.wifiOmogucen = omogucen;

  EepromLayout::PostavkeSpremnik spremnik = napraviZadanePostavke();
  ucitajKompatibilanSpremnik(spremnik);
  upisiRadnePostavkeUSpremnik(spremnik);
  spremiSpremnikPostavki(spremnik);

  posaljiPCLog(omogucen ? F("Postavke: WiFi ukljucen") : F("Postavke: WiFi iskljucen"));
}

void postaviSuncevDogadaj(uint8_t dogadaj, bool omogucen, uint8_t zvono, int odgodaMin) {
  if (!jeValjanSuncevDogadaj(dogadaj)) {
    return;
  }

  const uint8_t novoZvono = ogranicenoZvonoSuncevogDogadaja(zvono);
  const int16_t novaOdgoda =
      (dogadaj == SUNCEVI_DOGADAJ_PODNE) ? 0 : ogranicenaOdgodaSuncevogDogadajaMin(odgodaMin);
  const uint8_t bitDogadaja = static_cast<uint8_t>(1U << dogadaj);
  const bool prethodnoOmogucen = (postavke.maskaSuncevihDogadaja & bitDogadaja) != 0;

  if (prethodnoOmogucen == omogucen &&
      postavke.zvonaSuncevihDogadaja[dogadaj] == novoZvono &&
      postavke.odgodeSuncevihDogadajaMin[dogadaj] == novaOdgoda) {
    return;
  }

  if (omogucen) {
    postavke.maskaSuncevihDogadaja |= bitDogadaja;
  } else {
    postavke.maskaSuncevihDogadaja &= static_cast<uint8_t>(~bitDogadaja);
  }
  postavke.maskaSuncevihDogadaja =
      sanitizirajMaskuSuncevihDogadaja(postavke.maskaSuncevihDogadaja);
  postavke.zvonaSuncevihDogadaja[dogadaj] = novoZvono;
  postavke.odgodeSuncevihDogadajaMin[dogadaj] = novaOdgoda;

  EepromLayout::SunceviDogadajiSpremnik spremnik = napraviZadaneSunceveDogadaje();
  if (!ucitajSunceveDogadajeSkeniranjem(spremnik)) {
    spremnik = napraviZadaneSunceveDogadaje();
  }
  upisiSunceveDogadajeUSpremnik(spremnik);
  spremiSunceveDogadajeSpremnik(spremnik);

  const char* naziv = "nepoznato";
  if (dogadaj == SUNCEVI_DOGADAJ_JUTRO) {
    naziv = "jutro";
  } else if (dogadaj == SUNCEVI_DOGADAJ_PODNE) {
    naziv = "podne";
  } else if (dogadaj == SUNCEVI_DOGADAJ_VECER) {
    naziv = "vecer";
  }

  char log[96];
  snprintf(log,
           sizeof(log),
           "Suncevi dogadaj %s: %s, zvono=%u, odgoda=%d min",
           naziv,
           jeSuncevDogadajOmogucen(dogadaj) ? "ON" : "OFF",
           dohvatiZvonoZaSuncevDogadaj(dogadaj),
           dohvatiOdgoduSuncevogDogadajaMin(dogadaj));
  posaljiPCLog(log);
}

void postaviLCDPozadinskoOsvjetljenje(bool ukljuceno) {
  if (postavke.lcdPozadinskoOsvjetljenje == ukljuceno) {
    return;
  }

  postavke.lcdPozadinskoOsvjetljenje = ukljuceno;

  EepromLayout::PostavkeSpremnik spremnik = napraviZadanePostavke();
  ucitajKompatibilanSpremnik(spremnik);
  upisiRadnePostavkeUSpremnik(spremnik);
  spremiSpremnikPostavki(spremnik);

  posaljiPCLog(ukljuceno ? F("Postavke: LCD osvjetljenje ukljuceno")
                         : F("Postavke: LCD osvjetljenje iskljuceno"));
}

void postaviImaKazaljkeSata(bool imaKazaljke) {
  if (postavke.imaKazaljke == imaKazaljke) {
    return;
  }

  postavke.imaKazaljke = imaKazaljke;

  EepromLayout::PostavkeSpremnik spremnik = napraviZadanePostavke();
  ucitajKompatibilanSpremnik(spremnik);
  upisiRadnePostavkeUSpremnik(spremnik);
  spremiSpremnikPostavki(spremnik);

  posaljiPCLog(imaKazaljke ? F("Postavke: kazaljke ukljucene")
                           : F("Postavke: kazaljke iskljucene"));
}

void postaviModSlavljenja(uint8_t mod) {
  if (mod < 1 || mod > 2) {
    mod = 1;
  }

  if (postavke.modSlavljenja == mod) {
    return;
  }

  postavke.modSlavljenja = mod;

  EepromLayout::PostavkeSpremnik spremnik = napraviZadanePostavke();
  ucitajKompatibilanSpremnik(spremnik);
  upisiRadnePostavkeUSpremnik(spremnik);
  spremiSpremnikPostavki(spremnik);

  char log[48];
  snprintf(log, sizeof(log), "Postavke: mod slavljenja postavljen na %u", postavke.modSlavljenja);
  posaljiPCLog(log);
}

void postaviModOtkucavanja(uint8_t mod) {
  if (mod < 1 || mod > 2) {
    mod = 2;
  }

  if (postavke.modOtkucavanja == mod) {
    return;
  }

  postavke.modOtkucavanja = mod;

  EepromLayout::PostavkeSpremnik spremnik = napraviZadanePostavke();
  ucitajKompatibilanSpremnik(spremnik);
  upisiRadnePostavkeUSpremnik(spremnik);
  spremiSpremnikPostavki(spremnik);

  char log[56];
  snprintf(log, sizeof(log), "Postavke: mod otkucavanja postavljen na %u", postavke.modOtkucavanja);
  posaljiPCLog(log);
}

void postaviKonfiguracijuPloce(bool aktivna, int pocetakMinuta, int krajMinuta) {
  pocetakMinuta = normalizirajMinutuPloceNaBlok(pocetakMinuta);
  krajMinuta = normalizirajMinutuPloceNaBlok(krajMinuta);

  if (aktivna && krajMinuta <= pocetakMinuta) {
    krajMinuta = min(pocetakMinuta + PLOCA_MINUTNI_BLOK, PLOCA_ZADNJA_CETVRT);
  }

  const int kodiraniPocetak = kodirajPocetakPloceMinuta(pocetakMinuta, aktivna);
  if (postavke.plocaPocetakMinuta == kodiraniPocetak &&
      postavke.plocaKrajMinuta == krajMinuta) {
    return;
  }

  postavke.plocaPocetakMinuta = kodiraniPocetak;
  postavke.plocaKrajMinuta = krajMinuta;

  EepromLayout::PostavkeSpremnik spremnik = napraviZadanePostavke();
  ucitajKompatibilanSpremnik(spremnik);
  upisiRadnePostavkeUSpremnik(spremnik);
  spremiSpremnikPostavki(spremnik);

  char log[96];
  snprintf(
      log,
      sizeof(log),
      "Postavke ploce: %s %02d:%02d-%02d:%02d",
      aktivna ? "ON" : "OFF",
      pocetakMinuta / 60,
      pocetakMinuta % 60,
      krajMinuta / 60,
      krajMinuta % 60);
  posaljiPCLog(log);
}

void postaviPostavkeCavala(uint8_t trajanjeRadniMin,
                           uint8_t trajanjeNedjeljaMin,
                           uint8_t trajanjeSlavljenjaMin,
                           uint8_t odgodaSlavljenjaMin) {
  const uint8_t novoTrajanjeRadniMin = ogranicenoTrajanjeCavla(trajanjeRadniMin);
  const uint8_t novoTrajanjeNedjeljaMin = ogranicenoTrajanjeCavla(trajanjeNedjeljaMin);
  const uint8_t novoTrajanjeSlavljenjaMin = ogranicenoTrajanjeCavla(trajanjeSlavljenjaMin);
  const uint8_t novaOdgodaSlavljenjaMin = ogranicenaOdgodaSlavljenjaMin(odgodaSlavljenjaMin);

  if (postavke.trajanjeZvonjenjaRadniMin == novoTrajanjeRadniMin &&
      postavke.trajanjeZvonjenjaNedjeljaMin == novoTrajanjeNedjeljaMin &&
      postavke.trajanjeSlavljenjaMin == novoTrajanjeSlavljenjaMin &&
      postavke.slavljenjePrijeZvonjenja == novaOdgodaSlavljenjaMin) {
    return;
  }

  postavke.trajanjeZvonjenjaRadniMin = novoTrajanjeRadniMin;
  postavke.trajanjeZvonjenjaNedjeljaMin = novoTrajanjeNedjeljaMin;
  postavke.trajanjeSlavljenjaMin = novoTrajanjeSlavljenjaMin;
  postavke.slavljenjePrijeZvonjenja = novaOdgodaSlavljenjaMin;

  EepromLayout::PostavkeSpremnik spremnik = napraviZadanePostavke();
  ucitajKompatibilanSpremnik(spremnik);
  upisiRadnePostavkeUSpremnik(spremnik);
  spremiSpremnikPostavki(spremnik);

  char log[80];
  snprintf(
      log,
      sizeof(log),
      "Postavke cavala: RD=%u NED=%u SL=%u ODG=%u",
      postavke.trajanjeZvonjenjaRadniMin,
      postavke.trajanjeZvonjenjaNedjeljaMin,
      postavke.trajanjeSlavljenjaMin,
      postavke.slavljenjePrijeZvonjenja);
  posaljiPCLog(log);
}

void postaviRasporedCavala(uint8_t brojMjestaZaCavle,
                           uint8_t brojZvona,
                           const uint8_t radni[4],
                           const uint8_t nedjelja[4],
                           uint8_t cavaoSlavljenja,
                           uint8_t cavaoMrtvackog) {
  const uint8_t brojMjesta = ograniceniBrojMjestaZaCavle(brojMjestaZaCavle);
  const uint8_t noviBrojZvona = ograniceniBrojZvona(brojZvona);
  uint8_t noviCavliRadni[4];
  uint8_t noviCavliNedjelja[4];

  for (uint8_t i = 0; i < 4; i++) {
    noviCavliRadni[i] = sanitizirajOznakuCavlaZvona(
        sanitizirajOznakuCavla((radni != nullptr && i < 2) ? radni[i] : 0, brojMjesta));
    noviCavliNedjelja[i] = sanitizirajOznakuCavlaZvona(
        sanitizirajOznakuCavla((nedjelja != nullptr && i < 2) ? nedjelja[i] : 0, brojMjesta));
  }

  if (postavke.brojMjestaZaCavle == brojMjesta &&
      postavke.brojZvona == noviBrojZvona &&
      memcmp(postavke.cavliRadni, noviCavliRadni, sizeof(noviCavliRadni)) == 0 &&
      memcmp(postavke.cavliNedjelja, noviCavliNedjelja, sizeof(noviCavliNedjelja)) == 0 &&
      postavke.cavaoSlavljenje == 5 &&
      postavke.cavaoMrtvacko == 0) {
    return;
  }

  postavke.brojMjestaZaCavle = brojMjesta;
  postavke.brojZvona = noviBrojZvona;
  memcpy(postavke.cavliRadni, noviCavliRadni, sizeof(noviCavliRadni));
  memcpy(postavke.cavliNedjelja, noviCavliNedjelja, sizeof(noviCavliNedjelja));

  postavke.cavaoSlavljenje = 5;
  postavke.cavaoMrtvacko = 0;

  EepromLayout::PostavkeSpremnik spremnik = napraviZadanePostavke();
  ucitajKompatibilanSpremnik(spremnik);
  upisiRadnePostavkeUSpremnik(spremnik);
  spremiSpremnikPostavki(spremnik);

  char log[96];
  snprintf(
      log,
      sizeof(log),
      "Raspored cavala: mjesta=%u zvona=%u RD=%u%u%u%u NED=%u%u%u%u SL=%u MRT=%u",
      postavke.brojMjestaZaCavle,
      postavke.brojZvona,
      postavke.cavliRadni[0],
      postavke.cavliRadni[1],
      postavke.cavliRadni[2],
      postavke.cavliRadni[3],
      postavke.cavliNedjelja[0],
      postavke.cavliNedjelja[1],
      postavke.cavliNedjelja[2],
      postavke.cavliNedjelja[3],
      postavke.cavaoSlavljenje,
      postavke.cavaoMrtvacko);
  posaljiPCLog(log);
}

void postaviWiFiPodatke(const char* ssid, const char* lozinka) {
  if (ssid == nullptr || lozinka == nullptr) {
    return;
  }

  char noviSsid[33];
  char novaLozinka[33];
  strncpy(noviSsid, ssid, sizeof(noviSsid) - 1);
  noviSsid[sizeof(noviSsid) - 1] = '\0';
  strncpy(novaLozinka, lozinka, sizeof(novaLozinka) - 1);
  novaLozinka[sizeof(novaLozinka) - 1] = '\0';

  if (strcmp(dohvatiWifiSsid(), noviSsid) == 0 &&
      strcmp(dohvatiWifiLozinku(), novaLozinka) == 0) {
    return;
  }

  EepromLayout::PostavkeSpremnik spremnik = napraviZadanePostavke();
  ucitajKompatibilanSpremnik(spremnik);
  upisiRadnePostavkeUSpremnik(spremnik);

  strncpy(spremnik.wifiSsid, noviSsid, sizeof(spremnik.wifiSsid) - 1);
  spremnik.wifiSsid[sizeof(spremnik.wifiSsid) - 1] = '\0';
  strncpy(spremnik.wifiLozinka, novaLozinka, sizeof(spremnik.wifiLozinka) - 1);
  spremnik.wifiLozinka[sizeof(spremnik.wifiLozinka) - 1] = '\0';

  spremiSpremnikPostavki(spremnik);

  strncpy(mrezniCache.wifi.wifiSsid, spremnik.wifiSsid, sizeof(mrezniCache.wifi.wifiSsid) - 1);
  mrezniCache.wifi.wifiSsid[sizeof(mrezniCache.wifi.wifiSsid) - 1] = '\0';
  strncpy(mrezniCache.wifi.wifiLozinka, spremnik.wifiLozinka, sizeof(mrezniCache.wifi.wifiLozinka) - 1);
  mrezniCache.wifi.wifiLozinka[sizeof(mrezniCache.wifi.wifiLozinka) - 1] = '\0';
  strncpy(mrezniCache.wifi.statickaIp, spremnik.statickaIp, sizeof(mrezniCache.wifi.statickaIp) - 1);
  mrezniCache.wifi.statickaIp[sizeof(mrezniCache.wifi.statickaIp) - 1] = '\0';
  strncpy(mrezniCache.wifi.mreznaMaska, spremnik.mreznaMaska, sizeof(mrezniCache.wifi.mreznaMaska) - 1);
  mrezniCache.wifi.mreznaMaska[sizeof(mrezniCache.wifi.mreznaMaska) - 1] = '\0';
  strncpy(mrezniCache.wifi.zadaniGateway, spremnik.zadaniGateway, sizeof(mrezniCache.wifi.zadaniGateway) - 1);
  mrezniCache.wifi.zadaniGateway[sizeof(mrezniCache.wifi.zadaniGateway) - 1] = '\0';
  aktivnaMreznaSekcija = MREZNA_SEKCIJA_WIFI;

  char log[80];
  snprintf(log, sizeof(log), "Postavke: WiFi spremljen SSID=%s", spremnik.wifiSsid);
  posaljiPCLog(log);
}

void postaviWiFiPodatkeZaSetup(const char* ssid, const char* lozinka) {
  if (ssid == nullptr || lozinka == nullptr) {
    return;
  }

  char noviSsid[33];
  char novaLozinka[33];
  strncpy(noviSsid, ssid, sizeof(noviSsid) - 1);
  noviSsid[sizeof(noviSsid) - 1] = '\0';
  strncpy(novaLozinka, lozinka, sizeof(novaLozinka) - 1);
  novaLozinka[sizeof(novaLozinka) - 1] = '\0';

  if (strcmp(dohvatiWifiSsid(), noviSsid) == 0 &&
      strcmp(dohvatiWifiLozinku(), novaLozinka) == 0 &&
      postavke.koristiDhcp) {
    return;
  }

  EepromLayout::PostavkeSpremnik spremnik = napraviZadanePostavke();
  ucitajKompatibilanSpremnik(spremnik);
  upisiRadnePostavkeUSpremnik(spremnik);

  strncpy(spremnik.wifiSsid, noviSsid, sizeof(spremnik.wifiSsid) - 1);
  spremnik.wifiSsid[sizeof(spremnik.wifiSsid) - 1] = '\0';
  strncpy(spremnik.wifiLozinka, novaLozinka, sizeof(spremnik.wifiLozinka) - 1);
  spremnik.wifiLozinka[sizeof(spremnik.wifiLozinka) - 1] = '\0';
  spremnik.koristiDhcp = true;
  postavke.koristiDhcp = true;

  spremiSpremnikPostavki(spremnik);

  strncpy(mrezniCache.wifi.wifiSsid, spremnik.wifiSsid, sizeof(mrezniCache.wifi.wifiSsid) - 1);
  mrezniCache.wifi.wifiSsid[sizeof(mrezniCache.wifi.wifiSsid) - 1] = '\0';
  strncpy(mrezniCache.wifi.wifiLozinka, spremnik.wifiLozinka, sizeof(mrezniCache.wifi.wifiLozinka) - 1);
  mrezniCache.wifi.wifiLozinka[sizeof(mrezniCache.wifi.wifiLozinka) - 1] = '\0';
  strncpy(mrezniCache.wifi.statickaIp, spremnik.statickaIp, sizeof(mrezniCache.wifi.statickaIp) - 1);
  mrezniCache.wifi.statickaIp[sizeof(mrezniCache.wifi.statickaIp) - 1] = '\0';
  strncpy(mrezniCache.wifi.mreznaMaska, spremnik.mreznaMaska, sizeof(mrezniCache.wifi.mreznaMaska) - 1);
  mrezniCache.wifi.mreznaMaska[sizeof(mrezniCache.wifi.mreznaMaska) - 1] = '\0';
  strncpy(mrezniCache.wifi.zadaniGateway, spremnik.zadaniGateway, sizeof(mrezniCache.wifi.zadaniGateway) - 1);
  mrezniCache.wifi.zadaniGateway[sizeof(mrezniCache.wifi.zadaniGateway) - 1] = '\0';
  aktivnaMreznaSekcija = MREZNA_SEKCIJA_WIFI;

  char log[96];
  snprintf(log,
           sizeof(log),
           "Setup WiFi: spremljen SSID=%s i aktiviran DHCP za novu mrezu",
           spremnik.wifiSsid);
  posaljiPCLog(log);
}

void postaviNTPOmogucen(bool omogucen) {
  if (postavke.ntpOmogucen == omogucen) {
    return;
  }

  EepromLayout::PostavkeSpremnik spremnik = napraviZadanePostavke();
  ucitajKompatibilanSpremnik(spremnik);
  upisiRadnePostavkeUSpremnik(spremnik);
  kodirajNtpServer(
      spremnik.ntpServer,
      sizeof(spremnik.ntpServer),
      dohvatiNtpServerBezZastavice(spremnik.ntpServer),
      omogucen);

  spremiSpremnikPostavki(spremnik);

  posaljiPCLog(omogucen ? F("Postavke: NTP ukljucen")
                        : F("Postavke: NTP iskljucen"));
}

void postaviSinkronizacijskePostavke(const char* ntpServer, bool dcfOmogucen) {
  if (ntpServer == nullptr) {
    return;
  }

  if (strcmp(dohvatiNTPServer(), ntpServer) == 0 &&
      postavke.dcfOmogucen == dcfOmogucen) {
    return;
  }

  EepromLayout::PostavkeSpremnik spremnik = napraviZadanePostavke();
  ucitajKompatibilanSpremnik(spremnik);
  upisiRadnePostavkeUSpremnik(spremnik);

  kodirajNtpServer(
      spremnik.ntpServer,
      sizeof(spremnik.ntpServer),
      ntpServer,
      procitajNtpOmogucenostIzTeksta(spremnik.ntpServer));
  spremnik.dcfOmogucen = dcfOmogucen;

  spremiSpremnikPostavki(spremnik);

  char log[96];
  snprintf(
      log,
      sizeof(log),
      "Postavke: sinkronizacija NTP=%s (%s), DCF=%s",
      dohvatiNtpServerBezZastavice(spremnik.ntpServer),
      procitajNtpOmogucenostIzTeksta(spremnik.ntpServer) ? "ON" : "OFF",
      spremnik.dcfOmogucen ? "ON" : "OFF");
  posaljiPCLog(log);
}
