// postavke.cpp – Upravljanje postavkama i EEPROM
#include <Arduino.h>
#include "postavke.h"
#include "eeprom_konstante.h"
#include "wear_leveling.h"
#include "i2c_eeprom.h"
#include "pc_serial.h"

// Default postavke
static EepromLayout::PostavkeSpremnik postavke = {
  6,              // satOd: Otkucavanje od 6h
  22,             // satDo: Otkucavanje do 22h
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
  "192.168.1.100",// statickaIp: fallback static IP
  "255.255.255.0",// mreznaMaska: standard subnet mask
  "192.168.1.1"   // zadaniGateway: standard gateway
};

static bool postavkeLCDBlinkanje = false;
static char redak1Buffer[17] = "Postavke";
static char redak2Buffer[17] = "Ucitavanje...";

static void osigurajNullTerminiraneMreznePostavke() {
  postavke.pristupLozinka[sizeof(postavke.pristupLozinka) - 1] = '\0';
  postavke.wifiSsid[sizeof(postavke.wifiSsid) - 1] = '\0';
  postavke.wifiLozinka[sizeof(postavke.wifiLozinka) - 1] = '\0';
  postavke.statickaIp[sizeof(postavke.statickaIp) - 1] = '\0';
  postavke.mreznaMaska[sizeof(postavke.mreznaMaska) - 1] = '\0';
  postavke.zadaniGateway[sizeof(postavke.zadaniGateway) - 1] = '\0';
}

// Inicijalizacija postavki iz EEPROM-a
void ucitajPostavke() {
  // Pokušaj učitati iz EEPROM-a
  if (!WearLeveling::ucitaj(EepromLayout::BAZA_POSTAVKE,
                           EepromLayout::SLOTOVI_POSTAVKE,
                           postavke)) {
    posaljiPCLog(F("Postavke: koriste default vrijednosti"));
  } else {
    posaljiPCLog(F("Postavke: učitane iz EEPROM-a"));
  }
  
  // Validacija učitanih vrijednosti
  if (postavke.satOd < 0) postavke.satOd = 6;
  if (postavke.satDo > 23) postavke.satDo = 22;
  if (postavke.satDo <= postavke.satOd) postavke.satDo = postavke.satOd + 8;
  osigurajNullTerminiraneMreznePostavke();
  
  if (postavke.trajanjeImpulsaCekicaMs < 50) postavke.trajanjeImpulsaCekicaMs = 150;
  if (postavke.pauzaIzmeduUdaraca < 100) postavke.pauzaIzmeduUdaraca = 400;
  
  String log = F("Postavke: sat ");
  log += postavke.satOd;
  log += F("-");
  log += postavke.satDo;
  log += F(", WiFi SSID: ");
  log += postavke.wifiSsid;
  posaljiPCLog(log);
  
  // Spremi ponovno za sigurnost
  WearLeveling::spremi(EepromLayout::BAZA_POSTAVKE,
                      EepromLayout::SLOTOVI_POSTAVKE,
                      postavke);
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

const char* dohvatiStatickuIP() {
  return postavke.statickaIp;
}

const char* dohvatiMreznuMasku() {
  return postavke.mreznaMaska;
}

const char* dohvatiZadaniGateway() {
  return postavke.zadaniGateway;
}
