// postavke.h
#pragma once
#include <stdint.h>

void ucitajPostavke();
void spremiPostavke();

extern int satOd;
extern int satDo;
extern int plocaPocetakMinuta;
extern int plocaKrajMinuta;
extern unsigned int pauzaIzmeduUdaraca;
extern unsigned int trajanjeImpulsaCekicaMs;
extern unsigned long trajanjeZvonjenjaRadniMs;
extern unsigned long trajanjeZvonjenjaNedjeljaMs;
extern unsigned long trajanjeSlavljenjaMs;
extern uint8_t brojZvona;
extern char pristupLozinka[9];
extern char wifiSsid[33];
extern char wifiLozinka[33];
extern bool koristiDhcp;
extern char statickaIp[16];
extern char mreznaMaska[16];
extern char zadaniGateway[16];

bool jeDozvoljenoOtkucavanjeUSatu(int sat);
unsigned int dohvatiTrajanjeImpulsaCekica();
unsigned int dohvatiPauzuIzmeduUdaraca();
unsigned long dohvatiTrajanjeZvonjenjaRadniMs();
unsigned long dohvatiTrajanjeZvonjenjaNedjeljaMs();
unsigned long dohvatiTrajanjeSlavljenjaMs();
uint8_t dohvatiBrojZvona();
int dohvatiPocetakPloceMinute();
int dohvatiKrajPloceMinute();
bool jePlocaKonfigurirana();
const char* dohvatiPristupnuLozinku();
const char* dohvatiWifiSsid();
const char* dohvatiWifiLozinku();
bool koristiDhcpMreza();
const char* dohvatiStatickuIP();
const char* dohvatiMreznuMasku();
const char* dohvatiZadaniGateway();

void postaviTrajanjeImpulsaCekica(unsigned int trajanjeMs);
void postaviRasponOtkucavanja(int odSat, int doSat);
void postaviTrajanjeZvonjenjaRadni(unsigned long trajanjeMs);
void postaviTrajanjeZvonjenjaNedjelja(unsigned long trajanjeMs);
void postaviTrajanjeSlavljenja(unsigned long trajanjeMs);
void postaviBrojZvona(uint8_t broj);
void postaviRasponPloce(int pocetakMinuta, int krajMinuta);
void postaviPristupnuLozinku(const char* lozinka);
void postaviWifiSsid(const char* ssid);
void postaviWifiLozinku(const char* lozinka);
void postaviDhcp(bool omoguceno);
void postaviStatickuIP(const char* ip);
void postaviMreznuMasku(const char* maska);
void postaviZadaniGateway(const char* gateway);
