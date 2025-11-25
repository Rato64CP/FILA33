#include "postavke.h"
#include <Arduino.h>
#include <ctype.h>
#include <string.h>
#include "eeprom_konstante.h"
#include "wear_leveling.h"

namespace {
const int SAT_OD_DEFAULT = 6;
const int SAT_DO_DEFAULT = 22;
const int PLOCA_POCETAK_DEFAULT = 4 * 60 + 59;  // 04:59
const int PLOCA_KRAJ_DEFAULT = 20 * 60 + 44;     // 20:44
const unsigned int TRAJANJE_CEKIC_DEFAULT = 150;
const unsigned int PAUZA_UDARCI_DEFAULT = 850;
const unsigned long TRAJANJE_ZVONJENJA_RADNI_DEFAULT = 120000UL; // 2 minute
const unsigned long TRAJANJE_ZVONJENJA_NEDJELJA_DEFAULT = 180000UL; // 3 minute
const unsigned long TRAJANJE_SLAVLJENJA_DEFAULT = 120000UL; // 2 minute
const uint8_t BROJ_ZVONA_DEFAULT = 2;
const char PRISTUP_LOZINKA_DEFAULT[] = "12345678";
const char WIFI_SSID_DEFAULT[] = "TORANJ_NET";
const char WIFI_LOZINKA_DEFAULT[] = "TORANJ123";
const bool DHCP_DEFAULT = true;
const char STATICKA_IP_DEFAULT[] = "192.168.000.050";
const char MREZNA_MASKA_DEFAULT[] = "255.255.255.000";
const char ZADANI_GATEWAY_DEFAULT[] = "192.168.000.001";
constexpr size_t IPV4_DULJINA = 15;
constexpr char IPV4_PREDLOZAK[] = "000.000.000.000";

void postaviTekstualnoPolje(const char* izvor, char* odrediste, size_t velicina, const char* zamjena, size_t tocnaDuljina = 0) {
    memset(odrediste, 0, velicina);
    const char* odabrano = (izvor != nullptr && izvor[0] != '\0') ? izvor : zamjena;
    size_t duljina = strnlen(odabrano, velicina - 1);
    if (tocnaDuljina != 0 && duljina != tocnaDuljina) {
        odabrano = zamjena;
        duljina = strnlen(zamjena, velicina - 1);
    }
    memcpy(odrediste, odabrano, duljina);
    odrediste[duljina] = '\0';
}

void normalizirajRazmake(char* polje) {
    size_t duljina = strlen(polje);
    while (duljina > 0 && polje[duljina - 1] == ' ') {
        polje[--duljina] = '\0';
    }
}

bool jeIspravnaIPv4Sekcija(const char* pocetak) {
    char segment[4];
    memcpy(segment, pocetak, 3);
    segment[3] = '\0';
    for (int i = 0; i < 3; ++i) {
        if (!isdigit(segment[i])) return false;
    }
    int vrijednost = atoi(segment);
    return vrijednost >= 0 && vrijednost <= 255;
}

bool jeIspravnaIPv4Adresa(const char* adresa) {
    if (strlen(adresa) != IPV4_DULJINA) return false;
    for (size_t i = 0; i < IPV4_DULJINA; ++i) {
        if (IPV4_PREDLOZAK[i] == '.') {
            if (adresa[i] != '.') return false;
        } else if (!isdigit(adresa[i])) {
            return false;
        }
    }
    return jeIspravnaIPv4Sekcija(adresa) && jeIspravnaIPv4Sekcija(adresa + 4) &&
           jeIspravnaIPv4Sekcija(adresa + 8) && jeIspravnaIPv4Sekcija(adresa + 12);
}

void postaviIPv4Polje(const char* izvor, char* odrediste, const char* zamjena) {
    if (izvor != nullptr && jeIspravnaIPv4Adresa(izvor)) {
        strncpy(odrediste, izvor, IPV4_DULJINA);
        odrediste[IPV4_DULJINA] = '\0';
        return;
    }
    strncpy(odrediste, zamjena, IPV4_DULJINA);
    odrediste[IPV4_DULJINA] = '\0';
}

EepromLayout::PostavkeSpremnik napraviSpremnik() {
    EepromLayout::PostavkeSpremnik spremnik{};
    spremnik.satOd = satOd;
    spremnik.satDo = satDo;
    spremnik.plocaPocetakMinuta = plocaPocetakMinuta;
    spremnik.plocaKrajMinuta = plocaKrajMinuta;
    spremnik.trajanjeImpulsaCekicaMs = trajanjeImpulsaCekicaMs;
    spremnik.pauzaIzmeduUdaraca = pauzaIzmeduUdaraca;
    spremnik.trajanjeZvonjenjaRadniMs = trajanjeZvonjenjaRadniMs;
    spremnik.trajanjeZvonjenjaNedjeljaMs = trajanjeZvonjenjaNedjeljaMs;
    spremnik.trajanjeSlavljenjaMs = trajanjeSlavljenjaMs;
    spremnik.brojZvona = brojZvona;
    strncpy(spremnik.pristupLozinka, pristupLozinka, sizeof(spremnik.pristupLozinka));
    spremnik.pristupLozinka[sizeof(spremnik.pristupLozinka) - 1] = '\0';
    strncpy(spremnik.wifiSsid, wifiSsid, sizeof(spremnik.wifiSsid));
    spremnik.wifiSsid[sizeof(spremnik.wifiSsid) - 1] = '\0';
    strncpy(spremnik.wifiLozinka, wifiLozinka, sizeof(spremnik.wifiLozinka));
    spremnik.wifiLozinka[sizeof(spremnik.wifiLozinka) - 1] = '\0';
    spremnik.koristiDhcp = koristiDhcp;
    strncpy(spremnik.statickaIp, statickaIp, sizeof(spremnik.statickaIp));
    spremnik.statickaIp[sizeof(spremnik.statickaIp) - 1] = '\0';
    strncpy(spremnik.mreznaMaska, mreznaMaska, sizeof(spremnik.mreznaMaska));
    spremnik.mreznaMaska[sizeof(spremnik.mreznaMaska) - 1] = '\0';
    strncpy(spremnik.zadaniGateway, zadaniGateway, sizeof(spremnik.zadaniGateway));
    spremnik.zadaniGateway[sizeof(spremnik.zadaniGateway) - 1] = '\0';
    return spremnik;
}

void primijeniSpremnik(const EepromLayout::PostavkeSpremnik& spremnik) {
    satOd = spremnik.satOd;
    satDo = spremnik.satDo;
    plocaPocetakMinuta = spremnik.plocaPocetakMinuta;
    plocaKrajMinuta = spremnik.plocaKrajMinuta;
    trajanjeImpulsaCekicaMs = spremnik.trajanjeImpulsaCekicaMs;
    pauzaIzmeduUdaraca = spremnik.pauzaIzmeduUdaraca;
    trajanjeZvonjenjaRadniMs = spremnik.trajanjeZvonjenjaRadniMs;
    trajanjeZvonjenjaNedjeljaMs = spremnik.trajanjeZvonjenjaNedjeljaMs;
    trajanjeSlavljenjaMs = spremnik.trajanjeSlavljenjaMs;
    brojZvona = spremnik.brojZvona;
    strncpy(pristupLozinka, spremnik.pristupLozinka, sizeof(pristupLozinka));
    pristupLozinka[sizeof(pristupLozinka) - 1] = '\0';
    strncpy(wifiSsid, spremnik.wifiSsid, sizeof(wifiSsid));
    wifiSsid[sizeof(wifiSsid) - 1] = '\0';
    strncpy(wifiLozinka, spremnik.wifiLozinka, sizeof(wifiLozinka));
    wifiLozinka[sizeof(wifiLozinka) - 1] = '\0';
    koristiDhcp = spremnik.koristiDhcp;
    strncpy(statickaIp, spremnik.statickaIp, sizeof(statickaIp));
    statickaIp[sizeof(statickaIp) - 1] = '\0';
    strncpy(mreznaMaska, spremnik.mreznaMaska, sizeof(mreznaMaska));
    mreznaMaska[sizeof(mreznaMaska) - 1] = '\0';
    strncpy(zadaniGateway, spremnik.zadaniGateway, sizeof(zadaniGateway));
    zadaniGateway[sizeof(zadaniGateway) - 1] = '\0';
}
}

int satOd = SAT_OD_DEFAULT;
int satDo = SAT_DO_DEFAULT;
int plocaPocetakMinuta = PLOCA_POCETAK_DEFAULT;
int plocaKrajMinuta = PLOCA_KRAJ_DEFAULT;
unsigned int pauzaIzmeduUdaraca = PAUZA_UDARCI_DEFAULT;
unsigned int trajanjeImpulsaCekicaMs = TRAJANJE_CEKIC_DEFAULT;
unsigned long trajanjeZvonjenjaRadniMs = TRAJANJE_ZVONJENJA_RADNI_DEFAULT;
unsigned long trajanjeZvonjenjaNedjeljaMs = TRAJANJE_ZVONJENJA_NEDJELJA_DEFAULT;
unsigned long trajanjeSlavljenjaMs = TRAJANJE_SLAVLJENJA_DEFAULT;
uint8_t brojZvona = BROJ_ZVONA_DEFAULT;
char pristupLozinka[9] = "";
char wifiSsid[33] = "";
char wifiLozinka[33] = "";
bool koristiDhcp = DHCP_DEFAULT;
char statickaIp[16] = "";
char mreznaMaska[16] = "";
char zadaniGateway[16] = "";

static void provjeriRasponSati() {
    if (satOd < 0 || satOd > 23) satOd = SAT_OD_DEFAULT;
    if (satDo < 0 || satDo > 23) satDo = SAT_DO_DEFAULT;
}

static void provjeriRasponPloce() {
    auto ogranicenaMinuta = [](int vrijednost) {
        if (vrijednost < 0 || vrijednost > 1439) return -1;
        return vrijednost;
    };

    plocaPocetakMinuta = ogranicenaMinuta(plocaPocetakMinuta);
    plocaKrajMinuta = ogranicenaMinuta(plocaKrajMinuta);

    if (plocaPocetakMinuta == -1 || plocaKrajMinuta == -1) {
        plocaPocetakMinuta = PLOCA_POCETAK_DEFAULT;
        plocaKrajMinuta = PLOCA_KRAJ_DEFAULT;
    }
}

static void provjeriRasponTrajanja() {
    if (trajanjeImpulsaCekicaMs < 50 || trajanjeImpulsaCekicaMs > 2000) {
        trajanjeImpulsaCekicaMs = TRAJANJE_CEKIC_DEFAULT;
    }
    if (pauzaIzmeduUdaraca < 200 || pauzaIzmeduUdaraca > 5000) {
        pauzaIzmeduUdaraca = PAUZA_UDARCI_DEFAULT;
    }
}

static void provjeriRasponZvonjenja() {
    if (trajanjeZvonjenjaRadniMs < 10000UL || trajanjeZvonjenjaRadniMs > 600000UL) {
        trajanjeZvonjenjaRadniMs = TRAJANJE_ZVONJENJA_RADNI_DEFAULT;
    }
    if (trajanjeZvonjenjaNedjeljaMs < 10000UL || trajanjeZvonjenjaNedjeljaMs > 600000UL) {
        trajanjeZvonjenjaNedjeljaMs = TRAJANJE_ZVONJENJA_NEDJELJA_DEFAULT;
    }
    if (trajanjeSlavljenjaMs < 10000UL || trajanjeSlavljenjaMs > 600000UL) {
        trajanjeSlavljenjaMs = TRAJANJE_SLAVLJENJA_DEFAULT;
    }
}

static void provjeriRasponZvona() {
    if (brojZvona < 1 || brojZvona > 5) {
        brojZvona = BROJ_ZVONA_DEFAULT;
    }
}

static void provjeriTekstualnePostavke() {
    char privPristup[sizeof(pristupLozinka)];
    strncpy(privPristup, pristupLozinka, sizeof(privPristup));
    privPristup[sizeof(privPristup) - 1] = '\0';
    postaviTekstualnoPolje(privPristup, pristupLozinka, sizeof(pristupLozinka), PRISTUP_LOZINKA_DEFAULT, 8);

    char privSsid[sizeof(wifiSsid)];
    strncpy(privSsid, wifiSsid, sizeof(privSsid));
    privSsid[sizeof(privSsid) - 1] = '\0';
    postaviTekstualnoPolje(privSsid, wifiSsid, sizeof(wifiSsid), WIFI_SSID_DEFAULT);
    normalizirajRazmake(wifiSsid);

    char privWifiLozinka[sizeof(wifiLozinka)];
    strncpy(privWifiLozinka, wifiLozinka, sizeof(privWifiLozinka));
    privWifiLozinka[sizeof(privWifiLozinka) - 1] = '\0';
    postaviTekstualnoPolje(privWifiLozinka, wifiLozinka, sizeof(wifiLozinka), WIFI_LOZINKA_DEFAULT);
    if (strlen(wifiLozinka) < 8) {
        postaviTekstualnoPolje(WIFI_LOZINKA_DEFAULT, wifiLozinka, sizeof(wifiLozinka), WIFI_LOZINKA_DEFAULT);
    }
    normalizirajRazmake(wifiLozinka);
}

static void provjeriMrezneAdrese() {
    koristiDhcp = koristiDhcp ? true : false;

    char privIp[sizeof(statickaIp)];
    strncpy(privIp, statickaIp, sizeof(privIp));
    privIp[sizeof(privIp) - 1] = '\0';
    postaviIPv4Polje(privIp, statickaIp, STATICKA_IP_DEFAULT);

    char privMaska[sizeof(mreznaMaska)];
    strncpy(privMaska, mreznaMaska, sizeof(privMaska));
    privMaska[sizeof(privMaska) - 1] = '\0';
    postaviIPv4Polje(privMaska, mreznaMaska, MREZNA_MASKA_DEFAULT);

    char privGateway[sizeof(zadaniGateway)];
    strncpy(privGateway, zadaniGateway, sizeof(privGateway));
    privGateway[sizeof(privGateway) - 1] = '\0';
    postaviIPv4Polje(privGateway, zadaniGateway, ZADANI_GATEWAY_DEFAULT);
}

void ucitajPostavke() {
    EepromLayout::PostavkeSpremnik spremnik{};
    if (WearLeveling::ucitaj(EepromLayout::BAZA_POSTAVKE, EepromLayout::SLOTOVI_POSTAVKE, spremnik)) {
        primijeniSpremnik(spremnik);
    }
    provjeriRasponSati();
    provjeriRasponPloce();
    provjeriRasponTrajanja();
    provjeriRasponZvonjenja();
    provjeriRasponZvona();
    provjeriTekstualnePostavke();
    provjeriMrezneAdrese();
}

void spremiPostavke() {
    provjeriRasponSati();
    provjeriRasponPloce();
    provjeriRasponTrajanja();
    provjeriRasponZvonjenja();
    provjeriTekstualnePostavke();
    provjeriMrezneAdrese();
    EepromLayout::PostavkeSpremnik spremnik = napraviSpremnik();
    WearLeveling::spremi(EepromLayout::BAZA_POSTAVKE, EepromLayout::SLOTOVI_POSTAVKE, spremnik);
}

bool jeDozvoljenoOtkucavanjeUSatu(int sat) {
    sat %= 24;
    provjeriRasponSati();
    if (satOd == satDo) return true; // radi cijeli dan
    if (satOd < satDo) {
        return sat >= satOd && sat < satDo;
    }
    // raspon prelazi preko ponoci
    return sat >= satOd || sat < satDo;
}

unsigned int dohvatiTrajanjeImpulsaCekica() {
    provjeriRasponTrajanja();
    return trajanjeImpulsaCekicaMs;
}

unsigned int dohvatiPauzuIzmeduUdaraca() {
    provjeriRasponTrajanja();
    return pauzaIzmeduUdaraca;
}

unsigned long dohvatiTrajanjeZvonjenjaRadniMs() {
    provjeriRasponZvonjenja();
    return trajanjeZvonjenjaRadniMs;
}

unsigned long dohvatiTrajanjeZvonjenjaNedjeljaMs() {
    provjeriRasponZvonjenja();
    return trajanjeZvonjenjaNedjeljaMs;
}

unsigned long dohvatiTrajanjeSlavljenjaMs() {
    provjeriRasponZvonjenja();
    return trajanjeSlavljenjaMs;
}

uint8_t dohvatiBrojZvona() {
    provjeriRasponZvona();
    return brojZvona;
}

int dohvatiPocetakPloceMinute() {
    provjeriRasponPloce();
    return plocaPocetakMinuta;
}

int dohvatiKrajPloceMinute() {
    provjeriRasponPloce();
    return plocaKrajMinuta;
}

bool jePlocaKonfigurirana() {
    provjeriRasponPloce();
    return !(plocaPocetakMinuta == 0 && plocaKrajMinuta == 0);
}

void postaviTrajanjeImpulsaCekica(unsigned int trajanjeMs) {
    trajanjeImpulsaCekicaMs = constrain(trajanjeMs, 50U, 2000U);
    spremiPostavke();
}

void postaviRasponOtkucavanja(int odSat, int doSat) {
    satOd = constrain(odSat, 0, 23);
    satDo = constrain(doSat, 0, 23);
    spremiPostavke();
}

void postaviTrajanjeZvonjenjaRadni(unsigned long trajanjeMs) {
    trajanjeZvonjenjaRadniMs = constrain(trajanjeMs, 10000UL, 600000UL);
    spremiPostavke();
}

void postaviTrajanjeZvonjenjaNedjelja(unsigned long trajanjeMs) {
    trajanjeZvonjenjaNedjeljaMs = constrain(trajanjeMs, 10000UL, 600000UL);
    spremiPostavke();
}

void postaviTrajanjeSlavljenja(unsigned long trajanjeMs) {
    trajanjeSlavljenjaMs = constrain(trajanjeMs, 10000UL, 600000UL);
    spremiPostavke();
}

void postaviBrojZvona(uint8_t broj) {
    brojZvona = constrain(broj, static_cast<uint8_t>(1), static_cast<uint8_t>(5));
    spremiPostavke();
}

void postaviRasponPloce(int pocetakMinuta, int krajMinuta) {
    plocaPocetakMinuta = constrain(pocetakMinuta, 0, 1439);
    plocaKrajMinuta = constrain(krajMinuta, 0, 1439);
    spremiPostavke();
}

const char* dohvatiPristupnuLozinku() {
    provjeriTekstualnePostavke();
    return pristupLozinka;
}

const char* dohvatiWifiSsid() {
    provjeriTekstualnePostavke();
    return wifiSsid;
}

const char* dohvatiWifiLozinku() {
    provjeriTekstualnePostavke();
    return wifiLozinka;
}

bool koristiDhcpMreza() {
    provjeriMrezneAdrese();
    return koristiDhcp;
}

const char* dohvatiStatickuIP() {
    provjeriMrezneAdrese();
    return statickaIp;
}

const char* dohvatiMreznuMasku() {
    provjeriMrezneAdrese();
    return mreznaMaska;
}

const char* dohvatiZadaniGateway() {
    provjeriMrezneAdrese();
    return zadaniGateway;
}

void postaviPristupnuLozinku(const char* lozinka) {
    postaviTekstualnoPolje(lozinka, pristupLozinka, sizeof(pristupLozinka), PRISTUP_LOZINKA_DEFAULT, 8);
    spremiPostavke();
}

void postaviWifiSsid(const char* ssid) {
    postaviTekstualnoPolje(ssid, wifiSsid, sizeof(wifiSsid), WIFI_SSID_DEFAULT);
    normalizirajRazmake(wifiSsid);
    spremiPostavke();
}

void postaviWifiLozinku(const char* lozinka) {
    postaviTekstualnoPolje(lozinka, wifiLozinka, sizeof(wifiLozinka), WIFI_LOZINKA_DEFAULT);
    if (strlen(wifiLozinka) < 8) {
        postaviTekstualnoPolje(WIFI_LOZINKA_DEFAULT, wifiLozinka, sizeof(wifiLozinka), WIFI_LOZINKA_DEFAULT);
    }
    normalizirajRazmake(wifiLozinka);
    spremiPostavke();
}

void postaviDhcp(bool omoguceno) {
    koristiDhcp = omoguceno;
    spremiPostavke();
}

void postaviStatickuIP(const char* ip) {
    postaviIPv4Polje(ip, statickaIp, STATICKA_IP_DEFAULT);
    spremiPostavke();
}

void postaviMreznuMasku(const char* maska) {
    postaviIPv4Polje(maska, mreznaMaska, MREZNA_MASKA_DEFAULT);
    spremiPostavke();
}

void postaviZadaniGateway(const char* gateway) {
    postaviIPv4Polje(gateway, zadaniGateway, ZADANI_GATEWAY_DEFAULT);
    spremiPostavke();
}
