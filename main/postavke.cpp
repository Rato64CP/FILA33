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

namespace {

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
  bool mqttOmogucen;
  bool lcdPozadinskoOsvjetljenje;
  uint8_t modSlavljenja;
  uint16_t mqttPort;
  bool ntpOmogucen;
  bool dcfOmogucen;
  bool wifiOmogucen;
  bool imaKazaljke;
};

struct PostavkeSpremnikV8 {
  uint16_t potpis;
  uint8_t verzija;
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
  char pristupLozinka[9];
  char wifiSsid[33];
  char wifiLozinka[33];
  bool koristiDhcp;
  bool mqttOmogucen;
  bool lcdPozadinskoOsvjetljenje;
  uint8_t modSlavljenja;
  char statickaIp[16];
  char mreznaMaska[16];
  char zadaniGateway[16];
  char mqttBroker[40];
  uint16_t mqttPort;
  char mqttKorisnik[33];
  char mqttLozinka[33];
  char ntpServer[40];
  bool dcfOmogucen;
  uint16_t checksum;
};

struct PostavkeSpremnikV9 {
  uint16_t potpis;
  uint8_t verzija;
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
  char pristupLozinka[9];
  char wifiSsid[33];
  char wifiLozinka[33];
  bool koristiDhcp;
  bool mqttOmogucen;
  bool lcdPozadinskoOsvjetljenje;
  uint8_t modSlavljenja;
  char statickaIp[16];
  char mreznaMaska[16];
  char zadaniGateway[16];
  char mqttBroker[40];
  uint16_t mqttPort;
  char mqttKorisnik[33];
  char mqttLozinka[33];
  char ntpServer[40];
  bool dcfOmogucen;
  bool wifiOmogucen;
  uint16_t checksum;
};

enum AktivnaMreznaSekcija {
  MREZNA_SEKCIJA_NISTA = 0,
  MREZNA_SEKCIJA_WIFI,
  MREZNA_SEKCIJA_MQTT,
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
    char mqttBroker[40];
    char mqttKorisnik[33];
    char mqttLozinka[33];
  } mqtt;
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
    600,
    1200,
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

static uint8_t ogranicenoTrajanjeCavla(uint8_t trajanjeMin) {
  return constrain(trajanjeMin, 1, 4);
}

static uint8_t ograniceniBrojZvona(uint8_t brojZvona) {
  (void)brojZvona;
  return 2;
}

static uint8_t ograniceniBrojMjestaZaCavle(uint8_t brojMjestaZaCavle) {
  (void)brojMjestaZaCavle;
  return 5;
}

static uint8_t sanitizirajOznakuCavla(uint8_t cavao, uint8_t brojMjestaZaCavle) {
  if (cavao > brojMjestaZaCavle) {
    return 0;
  }
  return cavao;
}

static unsigned long minuteUMiliseconde(uint8_t minute) {
  return static_cast<unsigned long>(ogranicenoTrajanjeCavla(minute)) * 60000UL;
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

static uint16_t izracunajChecksumPostavkiV8(const PostavkeSpremnikV8& ulaz) {
  PostavkeSpremnikV8 kopija = ulaz;
  kopija.checksum = 0;

  const uint8_t* podaci = reinterpret_cast<const uint8_t*>(&kopija);
  uint16_t suma = 0;
  for (size_t i = 0; i < sizeof(kopija); i++) {
    suma = static_cast<uint16_t>((suma << 1) | (suma >> 15));
    suma = static_cast<uint16_t>(suma + podaci[i] + 0x3D);
  }
  return suma;
}

static uint16_t izracunajChecksumPostavkiV9(const PostavkeSpremnikV9& ulaz) {
  PostavkeSpremnikV9 kopija = ulaz;
  kopija.checksum = 0;

  const uint8_t* podaci = reinterpret_cast<const uint8_t*>(&kopija);
  uint16_t suma = 0;
  for (size_t i = 0; i < sizeof(kopija); i++) {
    suma = static_cast<uint16_t>((suma << 1) | (suma >> 15));
    suma = static_cast<uint16_t>(suma + podaci[i] + 0x3D);
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

static bool ucitajV10SpremnikSkeniranjem(EepromLayout::PostavkeSpremnik& spremnik) {
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
  if (!ucitajV10SpremnikSkeniranjem(spremnik)) {
    spremnik = napraviZadanePostavke();
    return false;
  }

  return true;
}

static bool jeKompatibilanEEPROMZapisPostavkiV8(const PostavkeSpremnikV8& ucitano) {
  if (ucitano.potpis != EepromLayout::POSTAVKE_POTPIS) {
    return false;
  }
  if (ucitano.verzija != 8) {
    return false;
  }
  return ucitano.checksum == izracunajChecksumPostavkiV8(ucitano);
}

static bool jeKompatibilanEEPROMZapisPostavkiV9(const PostavkeSpremnikV9& ucitano) {
  if (ucitano.potpis != EepromLayout::POSTAVKE_POTPIS) {
    return false;
  }
  if (ucitano.verzija != 9) {
    return false;
  }
  return ucitano.checksum == izracunajChecksumPostavkiV9(ucitano);
}

static bool ucitajV8SpremnikSkeniranjem(PostavkeSpremnikV8& spremnik) {
  for (int slot = EepromLayout::SLOTOVI_POSTAVKE - 1; slot >= 0; --slot) {
    const int adresa =
      EepromLayout::BAZA_POSTAVKE + slot * static_cast<int>(sizeof(PostavkeSpremnikV8));
    PostavkeSpremnikV8 kandidat{};
    if (!VanjskiEEPROM::procitaj(adresa, &kandidat, sizeof(kandidat))) {
      continue;
    }
    if (jeKompatibilanEEPROMZapisPostavkiV8(kandidat)) {
      spremnik = kandidat;
      return true;
    }
  }
  return false;
}

static bool ucitajV9SpremnikSkeniranjem(PostavkeSpremnikV9& spremnik) {
  for (int slot = EepromLayout::SLOTOVI_POSTAVKE - 1; slot >= 0; --slot) {
    const int adresa =
      EepromLayout::BAZA_POSTAVKE + slot * static_cast<int>(sizeof(PostavkeSpremnikV9));
    PostavkeSpremnikV9 kandidat{};
    if (!VanjskiEEPROM::procitaj(adresa, &kandidat, sizeof(kandidat))) {
      continue;
    }
    if (jeKompatibilanEEPROMZapisPostavkiV9(kandidat)) {
      spremnik = kandidat;
      return true;
    }
  }
  return false;
}

static bool migrirajV8Spremnik(EepromLayout::PostavkeSpremnik& cilj) {
  PostavkeSpremnikV8 staro{};
  if (!ucitajV8SpremnikSkeniranjem(staro)) {
    return false;
  }

  cilj = napraviZadanePostavke();
  cilj.satOd = staro.satOd;
  cilj.satDo = staro.satDo;
  cilj.tihiSatiOd = staro.tihiSatiOd;
  cilj.tihiSatiDo = staro.tihiSatiDo;
  cilj.plocaPocetakMinuta = staro.plocaPocetakMinuta;
  cilj.plocaKrajMinuta = staro.plocaKrajMinuta;
  cilj.trajanjeImpulsaCekicaMs = staro.trajanjeImpulsaCekicaMs;
  cilj.pauzaIzmeduUdaraca = staro.pauzaIzmeduUdaraca;
  cilj.trajanjeZvonjenjaRadniMin = staro.trajanjeZvonjenjaRadniMin;
  cilj.trajanjeZvonjenjaNedjeljaMin = staro.trajanjeZvonjenjaNedjeljaMin;
  cilj.trajanjeSlavljenjaMin = staro.trajanjeSlavljenjaMin;
  cilj.slavljenjePrijeZvonjenja = staro.slavljenjePrijeZvonjenja;
  cilj.brojZvona = staro.brojZvona;
  cilj.brojMjestaZaCavle = staro.brojMjestaZaCavle;
  memcpy(cilj.cavliRadni, staro.cavliRadni, sizeof(cilj.cavliRadni));
  memcpy(cilj.cavliNedjelja, staro.cavliNedjelja, sizeof(cilj.cavliNedjelja));
  cilj.cavaoSlavljenje = staro.cavaoSlavljenje;
  cilj.cavaoMrtvacko = staro.cavaoMrtvacko;
  memcpy(cilj.pristupLozinka, staro.pristupLozinka, sizeof(cilj.pristupLozinka));
  memcpy(cilj.wifiSsid, staro.wifiSsid, sizeof(cilj.wifiSsid));
  memcpy(cilj.wifiLozinka, staro.wifiLozinka, sizeof(cilj.wifiLozinka));
  cilj.koristiDhcp = staro.koristiDhcp;
  cilj.mqttOmogucen = staro.mqttOmogucen;
  cilj.lcdPozadinskoOsvjetljenje = staro.lcdPozadinskoOsvjetljenje;
  cilj.modSlavljenja = staro.modSlavljenja;
  memcpy(cilj.statickaIp, staro.statickaIp, sizeof(cilj.statickaIp));
  memcpy(cilj.mreznaMaska, staro.mreznaMaska, sizeof(cilj.mreznaMaska));
  memcpy(cilj.zadaniGateway, staro.zadaniGateway, sizeof(cilj.zadaniGateway));
  memcpy(cilj.mqttBroker, staro.mqttBroker, sizeof(cilj.mqttBroker));
  cilj.mqttPort = staro.mqttPort;
  memcpy(cilj.mqttKorisnik, staro.mqttKorisnik, sizeof(cilj.mqttKorisnik));
  memcpy(cilj.mqttLozinka, staro.mqttLozinka, sizeof(cilj.mqttLozinka));
  memcpy(cilj.ntpServer, staro.ntpServer, sizeof(cilj.ntpServer));
  cilj.dcfOmogucen = staro.dcfOmogucen;
  cilj.wifiOmogucen = true;
  cilj.imaKazaljke = true;
  osigurajNullTerminiraneMreznePostavke(cilj);
  cilj.pristupLozinka[sizeof(cilj.pristupLozinka) - 1] = '\0';
  return true;
}

static bool migrirajV9Spremnik(EepromLayout::PostavkeSpremnik& cilj) {
  PostavkeSpremnikV9 staro{};
  if (!ucitajV9SpremnikSkeniranjem(staro)) {
    return false;
  }

  cilj = napraviZadanePostavke();
  cilj.satOd = staro.satOd;
  cilj.satDo = staro.satDo;
  cilj.tihiSatiOd = staro.tihiSatiOd;
  cilj.tihiSatiDo = staro.tihiSatiDo;
  cilj.plocaPocetakMinuta = staro.plocaPocetakMinuta;
  cilj.plocaKrajMinuta = staro.plocaKrajMinuta;
  cilj.trajanjeImpulsaCekicaMs = staro.trajanjeImpulsaCekicaMs;
  cilj.pauzaIzmeduUdaraca = staro.pauzaIzmeduUdaraca;
  cilj.trajanjeZvonjenjaRadniMin = staro.trajanjeZvonjenjaRadniMin;
  cilj.trajanjeZvonjenjaNedjeljaMin = staro.trajanjeZvonjenjaNedjeljaMin;
  cilj.trajanjeSlavljenjaMin = staro.trajanjeSlavljenjaMin;
  cilj.slavljenjePrijeZvonjenja = staro.slavljenjePrijeZvonjenja;
  cilj.brojZvona = staro.brojZvona;
  cilj.brojMjestaZaCavle = staro.brojMjestaZaCavle;
  memcpy(cilj.cavliRadni, staro.cavliRadni, sizeof(cilj.cavliRadni));
  memcpy(cilj.cavliNedjelja, staro.cavliNedjelja, sizeof(cilj.cavliNedjelja));
  cilj.cavaoSlavljenje = staro.cavaoSlavljenje;
  cilj.cavaoMrtvacko = staro.cavaoMrtvacko;
  memcpy(cilj.pristupLozinka, staro.pristupLozinka, sizeof(cilj.pristupLozinka));
  memcpy(cilj.wifiSsid, staro.wifiSsid, sizeof(cilj.wifiSsid));
  memcpy(cilj.wifiLozinka, staro.wifiLozinka, sizeof(cilj.wifiLozinka));
  cilj.koristiDhcp = staro.koristiDhcp;
  cilj.mqttOmogucen = staro.mqttOmogucen;
  cilj.lcdPozadinskoOsvjetljenje = staro.lcdPozadinskoOsvjetljenje;
  cilj.modSlavljenja = staro.modSlavljenja;
  memcpy(cilj.statickaIp, staro.statickaIp, sizeof(cilj.statickaIp));
  memcpy(cilj.mreznaMaska, staro.mreznaMaska, sizeof(cilj.mreznaMaska));
  memcpy(cilj.zadaniGateway, staro.zadaniGateway, sizeof(cilj.zadaniGateway));
  memcpy(cilj.mqttBroker, staro.mqttBroker, sizeof(cilj.mqttBroker));
  cilj.mqttPort = staro.mqttPort;
  memcpy(cilj.mqttKorisnik, staro.mqttKorisnik, sizeof(cilj.mqttKorisnik));
  memcpy(cilj.mqttLozinka, staro.mqttLozinka, sizeof(cilj.mqttLozinka));
  memcpy(cilj.ntpServer, staro.ntpServer, sizeof(cilj.ntpServer));
  cilj.dcfOmogucen = staro.dcfOmogucen;
  cilj.wifiOmogucen = staro.wifiOmogucen;
  cilj.imaKazaljke = true;
  osigurajNullTerminiraneMreznePostavke(cilj);
  cilj.pristupLozinka[sizeof(cilj.pristupLozinka) - 1] = '\0';
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
  if (spremnik.trajanjeImpulsaCekicaMs < 50) {
    spremnik.trajanjeImpulsaCekicaMs = 150;
    trebaSpremiti = true;
  }
  if (spremnik.trajanjeImpulsaCekicaMs > 150) {
    spremnik.trajanjeImpulsaCekicaMs = 150;
    trebaSpremiti = true;
  }
  if (spremnik.pauzaIzmeduUdaraca < 100) {
    spremnik.pauzaIzmeduUdaraca = 400;
    trebaSpremiti = true;
  }
  if (spremnik.trajanjeZvonjenjaRadniMin < 1 || spremnik.trajanjeZvonjenjaRadniMin > 4) {
    spremnik.trajanjeZvonjenjaRadniMin = 2;
    trebaSpremiti = true;
  }
  if (spremnik.trajanjeZvonjenjaNedjeljaMin < 1 || spremnik.trajanjeZvonjenjaNedjeljaMin > 4) {
    spremnik.trajanjeZvonjenjaNedjeljaMin = 3;
    trebaSpremiti = true;
  }
  if (spremnik.trajanjeSlavljenjaMin < 1 || spremnik.trajanjeSlavljenjaMin > 4) {
    spremnik.trajanjeSlavljenjaMin = 2;
    trebaSpremiti = true;
  }
  if (spremnik.slavljenjePrijeZvonjenja > 1) {
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
    const uint8_t noviRadni = sanitizirajOznakuCavla(spremnik.cavliRadni[i], spremnik.brojMjestaZaCavle);
    if (noviRadni != spremnik.cavliRadni[i]) {
      spremnik.cavliRadni[i] = noviRadni;
      trebaSpremiti = true;
    }

    const uint8_t noviNedjelja =
        sanitizirajOznakuCavla(spremnik.cavliNedjelja[i], spremnik.brojMjestaZaCavle);
    if (noviNedjelja != spremnik.cavliNedjelja[i]) {
      spremnik.cavliNedjelja[i] = noviNedjelja;
      trebaSpremiti = true;
    }
  }

  const uint8_t noviSlavljenje =
      sanitizirajOznakuCavla(spremnik.cavaoSlavljenje, spremnik.brojMjestaZaCavle);
  if (noviSlavljenje != spremnik.cavaoSlavljenje) {
    spremnik.cavaoSlavljenje = noviSlavljenje;
    trebaSpremiti = true;
  }

  const uint8_t noviMrtvacko =
      sanitizirajOznakuCavla(spremnik.cavaoMrtvacko, spremnik.brojMjestaZaCavle);
  if (noviMrtvacko != spremnik.cavaoMrtvacko) {
    spremnik.cavaoMrtvacko = noviMrtvacko;
    trebaSpremiti = true;
  }

  if (spremnik.modSlavljenja < 1 || spremnik.modSlavljenja > 2) {
    spremnik.modSlavljenja = 1;
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
  postavke.mqttOmogucen = spremnik.mqttOmogucen;
  postavke.lcdPozadinskoOsvjetljenje = spremnik.lcdPozadinskoOsvjetljenje;
  postavke.modSlavljenja = spremnik.modSlavljenja;
  postavke.mqttPort = spremnik.mqttPort;
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
  spremnik.mqttOmogucen = postavke.mqttOmogucen;
  spremnik.lcdPozadinskoOsvjetljenje = postavke.lcdPozadinskoOsvjetljenje;
  spremnik.modSlavljenja = postavke.modSlavljenja;
  spremnik.mqttPort = postavke.mqttPort;
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

static void ucitajMQTTSekcijuIzSpremnika(const EepromLayout::PostavkeSpremnik& spremnik) {
  strncpy(mrezniCache.mqtt.mqttBroker, spremnik.mqttBroker, sizeof(mrezniCache.mqtt.mqttBroker) - 1);
  mrezniCache.mqtt.mqttBroker[sizeof(mrezniCache.mqtt.mqttBroker) - 1] = '\0';
  strncpy(mrezniCache.mqtt.mqttKorisnik, spremnik.mqttKorisnik, sizeof(mrezniCache.mqtt.mqttKorisnik) - 1);
  mrezniCache.mqtt.mqttKorisnik[sizeof(mrezniCache.mqtt.mqttKorisnik) - 1] = '\0';
  strncpy(mrezniCache.mqtt.mqttLozinka, spremnik.mqttLozinka, sizeof(mrezniCache.mqtt.mqttLozinka) - 1);
  mrezniCache.mqtt.mqttLozinka[sizeof(mrezniCache.mqtt.mqttLozinka) - 1] = '\0';
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
    case MREZNA_SEKCIJA_MQTT:
      ucitajMQTTSekcijuIzSpremnika(spremnik);
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

}  // namespace

void ucitajPostavke() {
  EepromLayout::PostavkeSpremnik spremnik = napraviZadanePostavke();
  bool trebaSpremiti = false;
  bool ucitanoIzEeproma = ucitajV10SpremnikSkeniranjem(spremnik);

  if (!ucitanoIzEeproma) {
    posaljiPCLog(F("Postavke: koriste default vrijednosti"));
    spremnik = napraviZadanePostavke();
    trebaSpremiti = true;
  } else {
    posaljiPCLog(F("Postavke: ucitane iz EEPROM-a"));
    if (jeKompatibilanEEPROMZapisPostavki(spremnik)) {
      if (jeSpremnikNaZadanimPostavkama(spremnik)) {
        EepromLayout::PostavkeSpremnik legacy = spremnik;
        if (migrirajV9Spremnik(legacy)) {
          spremnik = legacy;
          posaljiPCLog(F("Postavke: vracen stariji v9 zapis preko zadane v10 kopije"));
          trebaSpremiti = true;
        } else if (migrirajV8Spremnik(legacy)) {
          spremnik = legacy;
          posaljiPCLog(F("Postavke: vracen stariji v8 zapis preko zadane v10 kopije"));
          trebaSpremiti = true;
        }
      }
    } else {
      if (migrirajV9Spremnik(spremnik)) {
        posaljiPCLog(F("Postavke: migrirane iz verzije 9 na verziju 10"));
        trebaSpremiti = true;
      } else if (migrirajV8Spremnik(spremnik)) {
        posaljiPCLog(F("Postavke: migrirane iz verzije 8 na verziju 10"));
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

  char log[256];
  snprintf(
      log,
      sizeof(log),
      "Postavke: sat %d-%d, WiFi: %s SSID=%s, MQTT: %s @%s:%u, NTP: %s (%s), DCF: %s, LCD: %s, Kazaljke: %s, Slavljenje: %u, Cavli RD/NED/SL=%u/%u/%u %s, Zvona=%u, Mjesta=%u",
      spremnik.satOd,
      spremnik.satDo,
      spremnik.wifiOmogucen ? "ON" : "OFF",
      spremnik.wifiSsid,
      spremnik.mqttOmogucen ? "ON" : "OFF",
      spremnik.mqttBroker,
      spremnik.mqttPort,
      dohvatiNtpServerBezZastavice(spremnik.ntpServer),
      procitajNtpOmogucenostIzTeksta(spremnik.ntpServer) ? "ON" : "OFF",
      spremnik.dcfOmogucen ? "ON" : "OFF",
      spremnik.lcdPozadinskoOsvjetljenje ? "ON" : "OFF",
      spremnik.imaKazaljke ? "ON" : "OFF",
      spremnik.modSlavljenja,
      spremnik.trajanjeZvonjenjaRadniMin,
      spremnik.trajanjeZvonjenjaNedjeljaMin,
      spremnik.trajanjeSlavljenjaMin,
      spremnik.slavljenjePrijeZvonjenja ? "PRIJE" : "POSLIJE",
      spremnik.brojZvona,
      spremnik.brojMjestaZaCavle);
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
  return sanitizirajOznakuCavla(postavke.cavaoSlavljenje, dohvatiBrojMjestaZaCavle());
}

uint8_t dohvatiCavaoMrtvackog() {
  return sanitizirajOznakuCavla(postavke.cavaoMrtvacko, dohvatiBrojMjestaZaCavle());
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
  return postavke.trajanjeImpulsaCekicaMs;
}

unsigned int dohvatiPauzuIzmeduUdaraca() {
  return postavke.pauzaIzmeduUdaraca;
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

bool jeSlavljenjePrijeZvonjenja() {
  return postavke.slavljenjePrijeZvonjenja != 0;
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

bool jeMQTTOmogucen() {
  return postavke.mqttOmogucen;
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

const char* dohvatiMQTTBroker() {
  osigurajUcitanuMreznuSekciju(MREZNA_SEKCIJA_MQTT);
  return mrezniCache.mqtt.mqttBroker;
}

uint16_t dohvatiMQTTPort() {
  return postavke.mqttPort;
}

const char* dohvatiMQTTKorisnika() {
  osigurajUcitanuMreznuSekciju(MREZNA_SEKCIJA_MQTT);
  return mrezniCache.mqtt.mqttKorisnik;
}

const char* dohvatiMQTTLozinku() {
  osigurajUcitanuMreznuSekciju(MREZNA_SEKCIJA_MQTT);
  return mrezniCache.mqtt.mqttLozinka;
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

void postaviMQTTOmogucen(bool omogucen) {
  if (postavke.mqttOmogucen == omogucen) {
    return;
  }

  postavke.mqttOmogucen = omogucen;

  EepromLayout::PostavkeSpremnik spremnik = napraviZadanePostavke();
  ucitajKompatibilanSpremnik(spremnik);
  upisiRadnePostavkeUSpremnik(spremnik);
  spremiSpremnikPostavki(spremnik);

  posaljiPCLog(omogucen ? F("Postavke: MQTT ukljucen") : F("Postavke: MQTT iskljucen"));
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

void postaviKonfiguracijuPloce(bool aktivna, int pocetakMinuta, int krajMinuta) {
  pocetakMinuta = constrain(pocetakMinuta, 0, 23 * 60 + 59);
  krajMinuta = constrain(krajMinuta, 0, 23 * 60 + 59);

  if (aktivna && krajMinuta <= pocetakMinuta) {
    krajMinuta = min(pocetakMinuta + 15, 23 * 60 + 59);
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
                           bool slavljenjePrijeZvonjenja) {
  postavke.trajanjeZvonjenjaRadniMin = ogranicenoTrajanjeCavla(trajanjeRadniMin);
  postavke.trajanjeZvonjenjaNedjeljaMin = ogranicenoTrajanjeCavla(trajanjeNedjeljaMin);
  postavke.trajanjeSlavljenjaMin = ogranicenoTrajanjeCavla(trajanjeSlavljenjaMin);
  postavke.slavljenjePrijeZvonjenja = slavljenjePrijeZvonjenja ? 1 : 0;

  EepromLayout::PostavkeSpremnik spremnik = napraviZadanePostavke();
  ucitajKompatibilanSpremnik(spremnik);
  upisiRadnePostavkeUSpremnik(spremnik);
  spremiSpremnikPostavki(spremnik);

  char log[80];
  snprintf(
      log,
      sizeof(log),
      "Postavke cavala: RD=%u NED=%u SL=%u %s",
      postavke.trajanjeZvonjenjaRadniMin,
      postavke.trajanjeZvonjenjaNedjeljaMin,
      postavke.trajanjeSlavljenjaMin,
      postavke.slavljenjePrijeZvonjenja ? "PRIJE" : "POSLIJE");
  posaljiPCLog(log);
}

void postaviRasporedCavala(uint8_t brojMjestaZaCavle,
                           uint8_t brojZvona,
                           const uint8_t radni[4],
                           const uint8_t nedjelja[4],
                           uint8_t cavaoSlavljenja,
                           uint8_t cavaoMrtvackog) {
  const uint8_t brojMjesta = ograniceniBrojMjestaZaCavle(brojMjestaZaCavle);

  postavke.brojMjestaZaCavle = brojMjesta;
  postavke.brojZvona = ograniceniBrojZvona(brojZvona);

  for (uint8_t i = 0; i < 4; i++) {
    postavke.cavliRadni[i] =
        sanitizirajOznakuCavla((radni != nullptr && i < 2) ? radni[i] : 0, brojMjesta);
    postavke.cavliNedjelja[i] =
        sanitizirajOznakuCavla((nedjelja != nullptr && i < 2) ? nedjelja[i] : 0, brojMjesta);
  }

  postavke.cavaoSlavljenje = sanitizirajOznakuCavla(cavaoSlavljenja, brojMjesta);
  postavke.cavaoMrtvacko = sanitizirajOznakuCavla(cavaoMrtvackog, brojMjesta);

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

  EepromLayout::PostavkeSpremnik spremnik = napraviZadanePostavke();
  ucitajKompatibilanSpremnik(spremnik);
  upisiRadnePostavkeUSpremnik(spremnik);

  strncpy(spremnik.wifiSsid, ssid, sizeof(spremnik.wifiSsid) - 1);
  spremnik.wifiSsid[sizeof(spremnik.wifiSsid) - 1] = '\0';
  strncpy(spremnik.wifiLozinka, lozinka, sizeof(spremnik.wifiLozinka) - 1);
  spremnik.wifiLozinka[sizeof(spremnik.wifiLozinka) - 1] = '\0';

  spremiSpremnikPostavki(spremnik);

  char log[80];
  snprintf(log, sizeof(log), "Postavke: WiFi spremljen SSID=%s", spremnik.wifiSsid);
  posaljiPCLog(log);
}

void postaviMQTTPodatke(const char* broker, uint16_t port, const char* korisnik, const char* lozinka) {
  if (broker == nullptr || korisnik == nullptr || lozinka == nullptr) {
    return;
  }

  EepromLayout::PostavkeSpremnik spremnik = napraviZadanePostavke();
  ucitajKompatibilanSpremnik(spremnik);
  upisiRadnePostavkeUSpremnik(spremnik);

  strncpy(spremnik.mqttBroker, broker, sizeof(spremnik.mqttBroker) - 1);
  spremnik.mqttBroker[sizeof(spremnik.mqttBroker) - 1] = '\0';
  spremnik.mqttPort = (port == 0) ? 1883 : port;
  strncpy(spremnik.mqttKorisnik, korisnik, sizeof(spremnik.mqttKorisnik) - 1);
  spremnik.mqttKorisnik[sizeof(spremnik.mqttKorisnik) - 1] = '\0';
  strncpy(spremnik.mqttLozinka, lozinka, sizeof(spremnik.mqttLozinka) - 1);
  spremnik.mqttLozinka[sizeof(spremnik.mqttLozinka) - 1] = '\0';

  spremiSpremnikPostavki(spremnik);

  char log[128];
  snprintf(
      log,
      sizeof(log),
      "Postavke: MQTT spremljen broker=%s:%u, korisnik=%s",
      spremnik.mqttBroker,
      spremnik.mqttPort,
      spremnik.mqttKorisnik);
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
