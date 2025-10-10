#include "postavke.h"
#include <Arduino.h>
#include <EEPROM.h>

namespace {
const int EEPROM_ADRESA_SAT_OD = 40;
const int EEPROM_ADRESA_SAT_DO = 42;
const int EEPROM_ADRESA_TRAJANJE = 44;
const int EEPROM_ADRESA_PAUZA = 46;

const int SAT_OD_DEFAULT = 6;
const int SAT_DO_DEFAULT = 22;
const unsigned int TRAJANJE_CEKIC_DEFAULT = 150;
const unsigned int PAUZA_UDARCI_DEFAULT = 850;
}

int satOd = SAT_OD_DEFAULT;
int satDo = SAT_DO_DEFAULT;
unsigned int pauzaIzmeduUdaraca = PAUZA_UDARCI_DEFAULT;
unsigned int trajanjeImpulsaCekicaMs = TRAJANJE_CEKIC_DEFAULT;

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

void ucitajPostavke() {
    EEPROM.get(EEPROM_ADRESA_SAT_OD, satOd);
    EEPROM.get(EEPROM_ADRESA_SAT_DO, satDo);
    EEPROM.get(EEPROM_ADRESA_TRAJANJE, trajanjeImpulsaCekicaMs);
    EEPROM.get(EEPROM_ADRESA_PAUZA, pauzaIzmeduUdaraca);
    provjeriRasponSati();
    provjeriRasponTrajanja();
}

void spremiPostavke() {
    provjeriRasponSati();
    provjeriRasponTrajanja();
    EEPROM.put(EEPROM_ADRESA_SAT_OD, satOd);
    EEPROM.put(EEPROM_ADRESA_SAT_DO, satDo);
    EEPROM.put(EEPROM_ADRESA_TRAJANJE, trajanjeImpulsaCekicaMs);
    EEPROM.put(EEPROM_ADRESA_PAUZA, pauzaIzmeduUdaraca);
}

void resetPostavke() {
    satOd = SAT_OD_DEFAULT;
    satDo = SAT_DO_DEFAULT;
    trajanjeImpulsaCekicaMs = TRAJANJE_CEKIC_DEFAULT;
    pauzaIzmeduUdaraca = PAUZA_UDARCI_DEFAULT;
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
