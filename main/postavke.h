// postavke.h - Upravljanje postavkama i EEPROM-om
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

// Doba dana i BAT raspon za otkucavanje toranjskog sata
bool jeDozvoljenoOtkucavanjeUSatu(int sat);
bool jeBATPeriodAktivanZaSatneOtkucaje(int sat, int minuta);
int dohvatiBATPeriodOdSata();
int dohvatiBATPeriodDoSata();
void postaviKompaktnePostavkeOtkucavanja(int satOd,
                                         int satDo,
                                         uint8_t modOtkucavanja,
                                         uint8_t modSlavljenja,
                                         uint8_t modMrtvackog);

// Trajanja stapica i povezanih akcija toranjskog sata
// Zvonjenje i slavljenje vracaju se i kao trajanja u ms za izvrsavanje,
// dok se odgoda slavljenja cuva i uredjuje u sekundama.
unsigned int dohvatiTrajanjeImpulsaCekica();
unsigned int dohvatiPauzuIzmeduUdaraca();
void postaviTrajanjeImpulsaCekica(unsigned int trajanjeMs);
unsigned long dohvatiTrajanjeZvonjenjaRadniMs();
unsigned long dohvatiTrajanjeZvonjenjaNedjeljaMs();
unsigned long dohvatiTrajanjeSlavljenjaMs();
uint8_t dohvatiTrajanjeZvonjenjaRadniMin();
uint8_t dohvatiTrajanjeZvonjenjaNedjeljaMin();
uint8_t dohvatiTrajanjeSlavljenjaMin();
uint8_t dohvatiOdgoduSlavljenjaSekunde();
void postaviPostavkeCavala(uint8_t trajanjeRadniMin,
                           uint8_t trajanjeNedjeljaMin,
                           uint8_t trajanjeSlavljenjaMin,
                           uint8_t odgodaSlavljenjaSekunde);

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
uint8_t dohvatiModMrtvackog();
const char* dohvatiStatickuIP();
const char* dohvatiMreznuMasku();
const char* dohvatiZadaniGateway();
const char* dohvatiNTPServer();
bool jeNTPOmogucen();
bool jeDCFOmogucen();
bool jeSuncevDogadajOmogucen(uint8_t dogadaj);
uint8_t dohvatiZvonoZaSuncevDogadaj(uint8_t dogadaj);
int dohvatiOdgoduSuncevogDogadajaMin(uint8_t dogadaj);
void postaviWiFiOmogucen(bool omogucen);
void postaviLCDPozadinskoOsvjetljenje(bool ukljuceno);
void postaviSuncevDogadaj(uint8_t dogadaj, bool omogucen, uint8_t zvono, int odgodaMin);
void postaviKonfiguracijuPloce(bool aktivna, int pocetakMinuta, int krajMinuta);
void postaviWiFiPodatkeZaSetup(const char* ssid, const char* lozinka);
void postaviNTPOmogucen(bool omogucen);
void postaviSinkronizacijskePostavke(const char* ntpServer, bool dcfOmogucen);
