// postavke.h – Upravljanje postavkama i EEPROM
#pragma once

#include <stdint.h>

// Inicijalizacija postavki iz EEPROM-a
void ucitajPostavke();

uint8_t dohvatiBrojZvona();
uint8_t dohvatiBrojMjestaZaCavle();
uint8_t dohvatiCavaoRadniZaZvono(uint8_t zvono);
uint8_t dohvatiCavaoNedjeljaZaZvono(uint8_t zvono);
uint8_t dohvatiCavaoSlavljenja();
uint8_t dohvatiCavaoMrtvackog();

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
uint8_t dohvatiTrajanjeZvonjenjaRadniMin();
uint8_t dohvatiTrajanjeZvonjenjaNedjeljaMin();
uint8_t dohvatiTrajanjeSlavljenjaMin();
bool jeSlavljenjePrijeZvonjenja();
void postaviPostavkeCavala(uint8_t trajanjeRadniMin,
                           uint8_t trajanjeNedjeljaMin,
                           uint8_t trajanjeSlavljenjaMin,
                           bool slavljenjePrijeZvonjenja);
void postaviRasporedCavala(uint8_t brojMjestaZaCavle,
                          uint8_t brojZvona,
                          const uint8_t radni[4],
                          const uint8_t nedjelja[4],
                          uint8_t cavaoSlavljenja,
                          uint8_t cavaoMrtvackog);

// Okretna ploča
bool jePlocaKonfigurirana();
int dohvatiPocetakPloceMinute();
int dohvatiKrajPloceMinute();

// WiFi postavke
const char* dohvatiWifiSsid();
const char* dohvatiWifiLozinku();
bool jeWiFiOmogucen();
bool koristiDhcpMreza();
bool jeMQTTOmogucen();
bool jeLCDPozadinskoOsvjetljenjeUkljuceno();
bool imaKazaljkeSata();
uint8_t dohvatiModSlavljenja();
const char* dohvatiStatickuIP();
const char* dohvatiMreznuMasku();
const char* dohvatiZadaniGateway();
const char* dohvatiMQTTBroker();
uint16_t dohvatiMQTTPort();
const char* dohvatiMQTTKorisnika();
const char* dohvatiMQTTLozinku();
const char* dohvatiNTPServer();
bool jeNTPOmogucen();
bool jeDCFOmogucen();
void postaviWiFiOmogucen(bool omogucen);
void postaviMQTTOmogucen(bool omogucen);
void postaviLCDPozadinskoOsvjetljenje(bool ukljuceno);
void postaviImaKazaljkeSata(bool imaKazaljke);
void postaviModSlavljenja(uint8_t mod);
void postaviKonfiguracijuPloce(bool aktivna, int pocetakMinuta, int krajMinuta);
void postaviWiFiPodatke(const char* ssid, const char* lozinka);
void postaviMQTTPodatke(const char* broker, uint16_t port, const char* korisnik, const char* lozinka);
void postaviNTPOmogucen(bool omogucen);
void postaviSinkronizacijskePostavke(const char* ntpServer, bool dcfOmogucen);
