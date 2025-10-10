#include "postavke.h"
#include <Arduino.h>
#include <EEPROM.h>

namespace {
const int EEPROM_ADRESA_SAT_OD = 40;
const int EEPROM_ADRESA_SAT_DO = 42;
const int EEPROM_ADRESA_TRAJANJE = 44;
const int EEPROM_ADRESA_PAUZA = 46;
const int EEPROM_ADRESA_ZVONO_RADNI = 48;
const int EEPROM_ADRESA_ZVONO_NEDJELJA = 52;
const int EEPROM_ADRESA_SLAVLJENJE = 56;

const int SAT_OD_DEFAULT = 6;
const int SAT_DO_DEFAULT = 22;
const unsigned int TRAJANJE_CEKIC_DEFAULT = 150;
const unsigned int PAUZA_UDARCI_DEFAULT = 850;
const unsigned long TRAJANJE_ZVONJENJA_RADNI_DEFAULT = 120000UL; // 2 minute
const unsigned long TRAJANJE_ZVONJENJA_NEDJELJA_DEFAULT = 180000UL; // 3 minute
const unsigned long TRAJANJE_SLAVLJENJA_DEFAULT = 120000UL; // 2 minute
}

int satOd = SAT_OD_DEFAULT;
int satDo = SAT_DO_DEFAULT;
unsigned int pauzaIzmeduUdaraca = PAUZA_UDARCI_DEFAULT;
unsigned int trajanjeImpulsaCekicaMs = TRAJANJE_CEKIC_DEFAULT;
unsigned long trajanjeZvonjenjaRadniMs = TRAJANJE_ZVONJENJA_RADNI_DEFAULT;
unsigned long trajanjeZvonjenjaNedjeljaMs = TRAJANJE_ZVONJENJA_NEDJELJA_DEFAULT;
unsigned long trajanjeSlavljenjaMs = TRAJANJE_SLAVLJENJA_DEFAULT;

static void provjeriRasponSati() {
    if (satOd < 0 || satOd > 23) satOd = SAT_OD_DEFAULT;
    if (satDo < 0 || satDo > 23) satDo = SAT_DO_DEFAULT;
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

void ucitajPostavke() {
    EEPROM.get(EEPROM_ADRESA_SAT_OD, satOd);
    EEPROM.get(EEPROM_ADRESA_SAT_DO, satDo);
    EEPROM.get(EEPROM_ADRESA_TRAJANJE, trajanjeImpulsaCekicaMs);
    EEPROM.get(EEPROM_ADRESA_PAUZA, pauzaIzmeduUdaraca);
    EEPROM.get(EEPROM_ADRESA_ZVONO_RADNI, trajanjeZvonjenjaRadniMs);
    EEPROM.get(EEPROM_ADRESA_ZVONO_NEDJELJA, trajanjeZvonjenjaNedjeljaMs);
    EEPROM.get(EEPROM_ADRESA_SLAVLJENJE, trajanjeSlavljenjaMs);
    provjeriRasponSati();
    provjeriRasponTrajanja();
    provjeriRasponZvonjenja();
}

void spremiPostavke() {
    provjeriRasponSati();
    provjeriRasponTrajanja();
    provjeriRasponZvonjenja();
    EEPROM.put(EEPROM_ADRESA_SAT_OD, satOd);
    EEPROM.put(EEPROM_ADRESA_SAT_DO, satDo);
    EEPROM.put(EEPROM_ADRESA_TRAJANJE, trajanjeImpulsaCekicaMs);
    EEPROM.put(EEPROM_ADRESA_PAUZA, pauzaIzmeduUdaraca);
    EEPROM.put(EEPROM_ADRESA_ZVONO_RADNI, trajanjeZvonjenjaRadniMs);
    EEPROM.put(EEPROM_ADRESA_ZVONO_NEDJELJA, trajanjeZvonjenjaNedjeljaMs);
    EEPROM.put(EEPROM_ADRESA_SLAVLJENJE, trajanjeSlavljenjaMs);
}

void resetPostavke() {
    satOd = SAT_OD_DEFAULT;
    satDo = SAT_DO_DEFAULT;
    trajanjeImpulsaCekicaMs = TRAJANJE_CEKIC_DEFAULT;
    pauzaIzmeduUdaraca = PAUZA_UDARCI_DEFAULT;
    trajanjeZvonjenjaRadniMs = TRAJANJE_ZVONJENJA_RADNI_DEFAULT;
    trajanjeZvonjenjaNedjeljaMs = TRAJANJE_ZVONJENJA_NEDJELJA_DEFAULT;
    trajanjeSlavljenjaMs = TRAJANJE_SLAVLJENJA_DEFAULT;
    spremiPostavke();
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
