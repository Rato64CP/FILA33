// postavke.h – Upravljanje postavkama i EEPROM
#pragma once

#include <stdint.h>

// Inicijalizacija postavki iz EEPROM-a
void ucitajPostavke();

// Dozvoljenost zvona
bool dohvatiDozvoljenoZvonjenjeBell1();
bool dohvatiDozvoljenoZvonjenjeBell2();

// Doba dana za zvona
bool jeDozvoljenoOtkucavanjeUSatu(int sat);
bool jeTihiPeriodAktivanZaSatneOtkucaje(int sat);
int dohvatiTihiPeriodOdSata();
int dohvatiTihiPeriodDoSata();
void postaviTihiPeriodSatnihOtkucaja(int satOd, int satDo);

// Trajanja raznih zvona i akcija (ms)
unsigned int dohvatiTrajanjeImpulsaCekica();
unsigned int dohvatiPauzuIzmeduUdaraca();
unsigned long dohvatiTrajanjeZvonjenjaRadniMs();
unsigned long dohvatiTrajanjeZvonjenjaNedjeljaMs();
unsigned long dohvatiTrajanjeSlavljenjaMs();

// Okretna ploča
bool jePlocaKonfigurirana();
int dohvatiPocetakPloceMinute();
int dohvatiKrajPloceMinute();

// WiFi postavke
const char* dohvatiWifiSsid();
const char* dohvatiWifiLozinku();
bool koristiDhcpMreza();
bool jeMQTTOmogucen();
const char* dohvatiStatickuIP();
const char* dohvatiMreznuMasku();
const char* dohvatiZadaniGateway();
void postaviMQTTOmogucen(bool omogucen);
void postaviWiFiPodatke(const char* ssid, const char* lozinka);
