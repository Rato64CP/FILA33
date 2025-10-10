#include "postavke.h"
#include <Arduino.h>
#include <EEPROM.h>

namespace {
const int EEPROM_ADRESA_SAT_OD = 40;
const int EEPROM_ADRESA_SAT_DO = 42;
const int EEPROM_ADRESA_TRAJANJE = 44;
const int EEPROM_ADRESA_PAUZA = 46;
const int EEPROM_ADRESA_ZVONO_TRAJANJE = 48;

const int SAT_OD_DEFAULT = 6;
const int SAT_DO_DEFAULT = 22;
const unsigned int TRAJANJE_CEKIC_DEFAULT = 150;
const unsigned int PAUZA_UDARCI_DEFAULT = 850;
const unsigned long TRAJANJE_ZVONJENJA_DEFAULT = 120000UL; // 2 minute
}

int satOd = SAT_OD_DEFAULT;
int satDo = SAT_DO_DEFAULT;
unsigned int pauzaIzmeduUdaraca = PAUZA_UDARCI_DEFAULT;
unsigned int trajanjeImpulsaCekicaMs = TRAJANJE_CEKIC_DEFAULT;
unsigned long trajanjeZvonjenjaMs = TRAJANJE_ZVONJENJA_DEFAULT;

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
    if (trajanjeZvonjenjaMs < 10000UL || trajanjeZvonjenjaMs > 600000UL) {
        trajanjeZvonjenjaMs = TRAJANJE_ZVONJENJA_DEFAULT;
    }
}

void ucitajPostavke() {
    EEPROM.get(EEPROM_ADRESA_SAT_OD, satOd);
    EEPROM.get(EEPROM_ADRESA_SAT_DO, satDo);
    EEPROM.get(EEPROM_ADRESA_TRAJANJE, trajanjeImpulsaCekicaMs);
    EEPROM.get(EEPROM_ADRESA_PAUZA, pauzaIzmeduUdaraca);
    EEPROM.get(EEPROM_ADRESA_ZVONO_TRAJANJE, trajanjeZvonjenjaMs);
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
    EEPROM.put(EEPROM_ADRESA_ZVONO_TRAJANJE, trajanjeZvonjenjaMs);
}

void resetPostavke() {
    satOd = SAT_OD_DEFAULT;
    satDo = SAT_DO_DEFAULT;
    trajanjeImpulsaCekicaMs = TRAJANJE_CEKIC_DEFAULT;
    pauzaIzmeduUdaraca = PAUZA_UDARCI_DEFAULT;
    trajanjeZvonjenjaMs = TRAJANJE_ZVONJENJA_DEFAULT;
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

unsigned long dohvatiTrajanjeZvonjenjaMs() {
    provjeriRasponZvonjenja();
    return trajanjeZvonjenjaMs;
}
