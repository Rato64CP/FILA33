// postavke.cpp – Upravljanje postavkama i EEPROM
#include <Arduino.h>
#include "postavke.h"
#include "eeprom_konstante.h"
#include "wear_leveling.h"
#include "i2c_eeprom.h"
#include "pc_serial.h"
#include <string.h>
#include <ctype.h>

// Default postavke
static EepromLayout::PostavkeSpremnik postavke = {
  EepromLayout::POSTAVKE_POTPIS, // potpis: kompatibilnost zapisa
  EepromLayout::POSTAVKE_VERZIJA,// verzija: aktivni layout
  6,              // satOd: Otkucavanje od 6h
  22,             // satDo: Otkucavanje do 22h
  22,             // tihiSatiOd: Početak tihih sati za satne otkucaje
  6,              // tihiSatiDo: Kraj tihih sati za satne otkucaje
  600,            // plocaPocetakMinuta: 10:00
  1200,           // plocaKrajMinuta: 20:00
  150,            // trajanjeImpulsaCekicaMs: 150 ms
  400,            // pauzaIzmeduUdaraca: 400 ms
  120000UL,       // trajanjeZvonjenjaRadniMs: 2 minute radni dani
  180000UL,       // trajanjeZvonjenjaNedjeljaMs: 3 minute nedjelja
  120000UL,       // trajanjeSlavljenjaMs: 2 minute slavljenje
  2,              // brojZvona: 2 zvona
  "1234",         // pristupLozinka: default lozinka
  "WiFi",         // wifiSsid: default SSID
  "password",     // wifiLozinka: default lozinka
  true,           // koristiDhcp: DHCP po defaultu
  false,          // mqttOmogucen: MQTT je po defaultu isključen
  "192.168.1.100",// statickaIp: fallback static IP
  "255.255.255.0",// mreznaMaska: standard subnet mask
  "192.168.1.1",  // zadaniGateway: standard gateway
  0               // checksum: popunjava se prije spremanja
};

static bool postavkeLCDBlinkanje = false;
static char redak1Buffer[17] = "Postavke";
static char redak2Buffer[17] = "Ucitavanje...";

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

static void pripremiIntegritetPostavki(EepromLayout::PostavkeSpremnik& cilj) {
  cilj.potpis = EepromLayout::POSTAVKE_POTPIS;
  cilj.verzija = EepromLayout::POSTAVKE_VERZIJA;
  cilj.checksum = izracunajChecksumPostavki(cilj);
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
    imaZnakova = true;
  }
  return false;  // nema '\0' unutar dozvoljene duljine
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

static void osigurajNullTerminiraneMreznePostavke() {
  postavke.pristupLozinka[sizeof(postavke.pristupLozinka) - 1] = '\0';
  postavke.wifiSsid[sizeof(postavke.wifiSsid) - 1] = '\0';
  postavke.wifiLozinka[sizeof(postavke.wifiLozinka) - 1] = '\0';
  postavke.statickaIp[sizeof(postavke.statickaIp) - 1] = '\0';
  postavke.mreznaMaska[sizeof(postavke.mreznaMaska) - 1] = '\0';
  postavke.zadaniGateway[sizeof(postavke.zadaniGateway) - 1] = '\0';
}

static bool sanitizirajMreznaPolja() {
  bool biloPromjena = false;
  osigurajNullTerminiraneMreznePostavke();

  if (!jeValjanMrezniTekst(postavke.pristupLozinka, sizeof(postavke.pristupLozinka), true)) {
    strncpy(postavke.pristupLozinka, "1234", sizeof(postavke.pristupLozinka) - 1);
    postavke.pristupLozinka[sizeof(postavke.pristupLozinka) - 1] = '\0';
    biloPromjena = true;
  }
  if (!jeValjanMrezniTekst(postavke.wifiSsid, sizeof(postavke.wifiSsid), true)) {
    strncpy(postavke.wifiSsid, "WiFi", sizeof(postavke.wifiSsid) - 1);
    postavke.wifiSsid[sizeof(postavke.wifiSsid) - 1] = '\0';
    biloPromjena = true;
  }
  if (!jeValjanMrezniTekst(postavke.wifiLozinka, sizeof(postavke.wifiLozinka), true)) {
    strncpy(postavke.wifiLozinka, "password", sizeof(postavke.wifiLozinka) - 1);
    postavke.wifiLozinka[sizeof(postavke.wifiLozinka) - 1] = '\0';
    biloPromjena = true;
  }
  if (!jeValjanIPv4Tekst(postavke.statickaIp, sizeof(postavke.statickaIp))) {
    strncpy(postavke.statickaIp, "192.168.1.100", sizeof(postavke.statickaIp) - 1);
    postavke.statickaIp[sizeof(postavke.statickaIp) - 1] = '\0';
    biloPromjena = true;
  }
  if (!jeValjanIPv4Tekst(postavke.mreznaMaska, sizeof(postavke.mreznaMaska))) {
    strncpy(postavke.mreznaMaska, "255.255.255.0", sizeof(postavke.mreznaMaska) - 1);
    postavke.mreznaMaska[sizeof(postavke.mreznaMaska) - 1] = '\0';
    biloPromjena = true;
  }
  if (!jeValjanIPv4Tekst(postavke.zadaniGateway, sizeof(postavke.zadaniGateway))) {
    strncpy(postavke.zadaniGateway, "192.168.1.1", sizeof(postavke.zadaniGateway) - 1);
    postavke.zadaniGateway[sizeof(postavke.zadaniGateway) - 1] = '\0';
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

// Inicijalizacija postavki iz EEPROM-a
void ucitajPostavke() {
  bool trebaSpremiti = false;
  bool ucitanoIzEeproma = WearLeveling::ucitaj(EepromLayout::BAZA_POSTAVKE,
                                                EepromLayout::SLOTOVI_POSTAVKE,
                                                postavke);
  // Pokušaj učitati iz EEPROM-a
  if (!ucitanoIzEeproma) {
    posaljiPCLog(F("Postavke: koriste default vrijednosti"));
    trebaSpremiti = true;
  } else {
    posaljiPCLog(F("Postavke: učitane iz EEPROM-a"));
    if (!jeKompatibilanEEPROMZapisPostavki(postavke)) {
      posaljiPCLog(F("Postavke: nekompatibilan/ostarjeli zapis -> reset na default"));
      postavke = {
        EepromLayout::POSTAVKE_POTPIS,
        EepromLayout::POSTAVKE_VERZIJA,
        6, 22, 22, 6,
        600, 1200,
        150, 400,
        120000UL, 180000UL, 120000UL,
        2, "1234", "WiFi", "password",
        true, false, "192.168.1.100", "255.255.255.0", "192.168.1.1",
        0
      };
      trebaSpremiti = true;
    }
  }
  
  // Validacija učitanih vrijednosti
  if (postavke.satOd < 0 || postavke.satOd > 23) {
    postavke.satOd = 6;
    trebaSpremiti = true;
  }
  if (postavke.satDo < 0 || postavke.satDo > 23) {
    postavke.satDo = 22;
    trebaSpremiti = true;
  }
  if (postavke.satDo <= postavke.satOd) {
    postavke.satDo = postavke.satOd + 8;
    trebaSpremiti = true;
  }
  if (postavke.satDo > 23) {
    postavke.satDo = 23;
    trebaSpremiti = true;
  }
  if (postavke.tihiSatiOd < 0 || postavke.tihiSatiOd > 23) {
    postavke.tihiSatiOd = 22;
    trebaSpremiti = true;
  }
  if (postavke.tihiSatiDo < 0 || postavke.tihiSatiDo > 23) {
    postavke.tihiSatiDo = 6;
    trebaSpremiti = true;
  }
  if (postavke.trajanjeImpulsaCekicaMs < 50) {
    postavke.trajanjeImpulsaCekicaMs = 150;
    trebaSpremiti = true;
  }
  if (postavke.pauzaIzmeduUdaraca < 100) {
    postavke.pauzaIzmeduUdaraca = 400;
    trebaSpremiti = true;
  }
  if (sanitizirajMreznaPolja()) {
    posaljiPCLog(F("Postavke: detektirano oštećenje string polja, primijenjen fallback"));
    trebaSpremiti = true;
  }
  
  pripremiIntegritetPostavki(postavke);
  
  String log = F("Postavke: sat ");
  log += postavke.satOd;
  log += F("-");
  log += postavke.satDo;
  log += F(", WiFi SSID: ");
  log += postavke.wifiSsid;
  log += F(", MQTT: ");
  log += postavke.mqttOmogucen ? F("ON") : F("OFF");
  posaljiPCLog(log);
  
  // Spremi ponovno za sigurnost
  if (trebaSpremiti) {
    WearLeveling::spremi(EepromLayout::BAZA_POSTAVKE,
                        EepromLayout::SLOTOVI_POSTAVKE,
                        postavke);
  }
}

const char* dohvatiPostavkeRedak1() {
  strncpy(redak1Buffer, "Postavke", sizeof(redak1Buffer) - 1);
  redak1Buffer[sizeof(redak1Buffer) - 1] = '\0';
  return redak1Buffer;
}

const char* dohvatiPostavkeRedak2() {
  snprintf(redak2Buffer, sizeof(redak2Buffer), "Otkl %d-%d h", postavke.satOd, postavke.satDo);
  return redak2Buffer;
}

bool dohvatiDozvoljenoZvonjenjeBell1() {
  return postavke.brojZvona >= 1;
}

bool dohvatiDozvoljenoZvonjenjeBell2() {
  return postavke.brojZvona >= 2;
}

bool jeDozvoljenoOtkucavanjeUSatu(int sat) {
  return sat >= postavke.satOd && sat < postavke.satDo;
}

bool jeTihiPeriodAktivanZaSatneOtkucaje(int sat) {
  sat = constrain(sat, 0, 23);

  // Ako su sati jednaki, tihi period je isključen.
  if (postavke.tihiSatiOd == postavke.tihiSatiDo) {
    return false;
  }

  // Raspon preko ponoći, npr. 22->6.
  if (postavke.tihiSatiOd > postavke.tihiSatiDo) {
    return sat >= postavke.tihiSatiOd || sat < postavke.tihiSatiDo;
  }

  // Standardni raspon unutar istog dana, npr. 13->16.
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
  pripremiIntegritetPostavki(postavke);

  WearLeveling::spremi(EepromLayout::BAZA_POSTAVKE,
                      EepromLayout::SLOTOVI_POSTAVKE,
                      postavke);

  String log = F("Tihi sati satnih otkucaja: ");
  log += postavke.tihiSatiOd;
  log += F("-");
  log += postavke.tihiSatiDo;
  posaljiPCLog(log);
}

unsigned int dohvatiTrajanjeImpulsaCekica() {
  return postavke.trajanjeImpulsaCekicaMs;
}

unsigned int dohvatiPauzuIzmeduUdaraca() {
  return postavke.pauzaIzmeduUdaraca;
}

unsigned long dohvatiTrajanjeZvonjenjaRadniMs() {
  return postavke.trajanjeZvonjenjaRadniMs;
}

unsigned long dohvatiTrajanjeZvonjenjaNedjeljaMs() {
  return postavke.trajanjeZvonjenjaNedjeljaMs;
}

unsigned long dohvatiTrajanjeSlavljenjaMs() {
  return postavke.trajanjeSlavljenjaMs;
}

bool jePlocaKonfigurirana() {
  // Ploca je konfigurirana ako je raspon sati validan
  return postavke.plocaPocetakMinuta != postavke.plocaKrajMinuta;
}

int dohvatiPocetakPloceMinute() {
  return postavke.plocaPocetakMinuta;
}

int dohvatiKrajPloceMinute() {
  return postavke.plocaKrajMinuta;
}

const char* dohvatiWifiSsid() {
  return postavke.wifiSsid;
}

const char* dohvatiWifiLozinku() {
  return postavke.wifiLozinka;
}

bool koristiDhcpMreza() {
  return postavke.koristiDhcp;
}

bool jeMQTTOmogucen() {
  return postavke.mqttOmogucen;
}

const char* dohvatiStatickuIP() {
  return postavke.statickaIp;
}

const char* dohvatiMreznuMasku() {
  return postavke.mreznaMaska;
}

const char* dohvatiZadaniGateway() {
  return postavke.zadaniGateway;
}

void postaviMQTTOmogucen(bool omogucen) {
  if (postavke.mqttOmogucen == omogucen) {
    return;
  }

  postavke.mqttOmogucen = omogucen;
  pripremiIntegritetPostavki(postavke);
  WearLeveling::spremi(EepromLayout::BAZA_POSTAVKE,
                      EepromLayout::SLOTOVI_POSTAVKE,
                      postavke);

  posaljiPCLog(omogucen ? F("Postavke: MQTT uključen") : F("Postavke: MQTT isključen"));
}
