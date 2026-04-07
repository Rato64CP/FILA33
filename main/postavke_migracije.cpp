// postavke_migracije.cpp - Migracije starijih EEPROM zapisa toranjskog sata
#include "postavke_migracije.h"

#include <stddef.h>
#include <string.h>

#include "i2c_eeprom.h"

namespace {

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

struct PostavkeSpremnikV10 {
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
  bool imaKazaljke;
  uint16_t checksum;
};

template <typename T>
static uint16_t izracunajChecksumLegacyPostavki(const T& ulaz) {
  T kopija = ulaz;
  kopija.checksum = 0;

  const uint8_t* podaci = reinterpret_cast<const uint8_t*>(&kopija);
  uint16_t suma = 0;
  for (size_t i = 0; i < sizeof(kopija); ++i) {
    suma = static_cast<uint16_t>((suma << 1) | (suma >> 15));
    suma = static_cast<uint16_t>(suma + podaci[i] + 0x3D);
  }
  return suma;
}

template <typename T>
static bool jeKompatibilanLegacySpremnik(const T& ucitano, uint8_t verzija) {
  return ucitano.potpis == EepromLayout::POSTAVKE_POTPIS &&
         ucitano.verzija == verzija &&
         ucitano.checksum == izracunajChecksumLegacyPostavki(ucitano);
}

template <typename T>
static bool ucitajLegacySpremnikSkeniranjem(T& spremnik, uint8_t verzija) {
  for (int slot = EepromLayout::SLOTOVI_POSTAVKE - 1; slot >= 0; --slot) {
    const int adresa =
        EepromLayout::BAZA_POSTAVKE + slot * static_cast<int>(sizeof(T));
    T kandidat{};
    if (!VanjskiEEPROM::procitaj(adresa, &kandidat, sizeof(kandidat))) {
      continue;
    }
    if (!jeKompatibilanLegacySpremnik(kandidat, verzija)) {
      continue;
    }
    spremnik = kandidat;
    return true;
  }
  return false;
}

static void osigurajNullTerminiraneMreznePostavke(
    EepromLayout::PostavkeSpremnik& spremnik) {
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

static bool migrirajV8Spremnik(EepromLayout::PostavkeSpremnik& cilj,
                               const EepromLayout::PostavkeSpremnik& zadane) {
  PostavkeSpremnikV8 staro{};
  if (!ucitajLegacySpremnikSkeniranjem(staro, 8)) {
    return false;
  }

  cilj = zadane;
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

static bool migrirajV9Spremnik(EepromLayout::PostavkeSpremnik& cilj,
                               const EepromLayout::PostavkeSpremnik& zadane) {
  PostavkeSpremnikV9 staro{};
  if (!ucitajLegacySpremnikSkeniranjem(staro, 9)) {
    return false;
  }

  cilj = zadane;
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

static bool migrirajV10Spremnik(EepromLayout::PostavkeSpremnik& cilj,
                                const EepromLayout::PostavkeSpremnik& zadane) {
  PostavkeSpremnikV10 staro{};
  if (!ucitajLegacySpremnikSkeniranjem(staro, 10)) {
    return false;
  }

  cilj = zadane;
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
  cilj.modOtkucavanja = 2;
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
  cilj.imaKazaljke = staro.imaKazaljke;
  osigurajNullTerminiraneMreznePostavke(cilj);
  cilj.pristupLozinka[sizeof(cilj.pristupLozinka) - 1] = '\0';
  return true;
}

}  // namespace

uint8_t pokusajMigriratiLegacySpremnikPostavki(
    EepromLayout::PostavkeSpremnik& cilj,
    const EepromLayout::PostavkeSpremnik& zadane) {
  if (migrirajV10Spremnik(cilj, zadane)) {
    return 10;
  }
  if (migrirajV9Spremnik(cilj, zadane)) {
    return 9;
  }
  if (migrirajV8Spremnik(cilj, zadane)) {
    return 8;
  }
  return 0;
}
