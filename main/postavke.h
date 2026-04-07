// postavke.h – Upravljanje postavkama i EEPROM
#pragma once

#include <stdint.h>

enum SunceviDogadaj {
  SUNCEVI_DOGADAJ_JUTRO = 0,
  SUNCEVI_DOGADAJ_PODNE = 1,
  SUNCEVI_DOGADAJ_VECER = 2,
  SUNCEVI_DOGADAJ_BROJ = 3
};

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
uint8_t dohvatiOdgoduSlavljenjaMin();
void postaviPostavkeCavala(uint8_t trajanjeRadniMin,
                           uint8_t trajanjeNedjeljaMin,
                           uint8_t trajanjeSlavljenjaMin,
                           uint8_t odgodaSlavljenjaMin);
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
bool jeLCDPozadinskoOsvjetljenjeUkljuceno();
bool imaKazaljkeSata();
uint8_t dohvatiModSlavljenja();
uint8_t dohvatiModOtkucavanja();
const char* dohvatiStatickuIP();
const char* dohvatiMreznuMasku();
const char* dohvatiZadaniGateway();
const char* dohvatiNTPServer();
bool jeNTPOmogucen();
bool jeDCFOmogucen();
int32_t dohvatiZemljopisnuSirinuE5();
int32_t dohvatiZemljopisnuDuzinuE5();
bool jeSuncevDogadajOmogucen(uint8_t dogadaj);
uint8_t dohvatiZvonoZaSuncevDogadaj(uint8_t dogadaj);
int dohvatiOdgoduSuncevogDogadajaMin(uint8_t dogadaj);
void postaviWiFiOmogucen(bool omogucen);
void postaviLCDPozadinskoOsvjetljenje(bool ukljuceno);
void postaviImaKazaljkeSata(bool imaKazaljke);
void postaviModSlavljenja(uint8_t mod);
void postaviModOtkucavanja(uint8_t mod);
void postaviSuncevDogadaj(uint8_t dogadaj, bool omogucen, uint8_t zvono, int odgodaMin);
void postaviKonfiguracijuPloce(bool aktivna, int pocetakMinuta, int krajMinuta);
void postaviWiFiPodatke(const char* ssid, const char* lozinka);
void postaviWiFiPodatkeZaSetup(const char* ssid, const char* lozinka);
void postaviNTPOmogucen(bool omogucen);
void postaviSinkronizacijskePostavke(const char* ntpServer, bool dcfOmogucen);
