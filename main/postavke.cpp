#include "postavke.h"
#include <Arduino.h>
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
}

void spremiPostavke() {
    provjeriRasponSati();
    provjeriRasponPloce();
    provjeriRasponTrajanja();
    provjeriRasponZvonjenja();
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
