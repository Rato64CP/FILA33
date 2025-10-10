#include "zvonjenje.h"
#include "podesavanja_piny.h"
#include <Arduino.h>

static bool zvonoMusko = false;
static bool zvonoZensko = false;
static unsigned long zadnjeIskljucenjeMusko = 0;
static unsigned long zadnjeIskljucenjeZensko = 0;

static bool slavljenje = false;
static bool mrtvacko = false;

static unsigned long slavljenjeStart = 0;
static int slavljenjeKorak = 0;

static unsigned long zadnjeBrecanje = 0;

static void udariMusko() {
    digitalWrite(PIN_CEKIC_MUSKI, HIGH);
    delay(200);
    digitalWrite(PIN_CEKIC_MUSKI, LOW);
}

static void udariZensko() {
    digitalWrite(PIN_CEKIC_ZENSKI, HIGH);
    delay(200);
    digitalWrite(PIN_CEKIC_ZENSKI, LOW);
}

void inicijalizirajZvona() {
    pinMode(PIN_ZVONO_MUSKO, OUTPUT);
    pinMode(PIN_ZVONO_ZENSKO, OUTPUT);
    pinMode(PIN_CEKIC_MUSKI, OUTPUT);
    pinMode(PIN_CEKIC_ZENSKI, OUTPUT);
    digitalWrite(PIN_ZVONO_MUSKO, LOW);
    digitalWrite(PIN_ZVONO_ZENSKO, LOW);
    digitalWrite(PIN_CEKIC_MUSKI, LOW);
    digitalWrite(PIN_CEKIC_ZENSKI, LOW);
}

void aktivirajZvonjenje(int koje) {
    if (koje == 1) {
        digitalWrite(PIN_ZVONO_MUSKO, HIGH);
        zvonoMusko = true;
    } else if (koje == 2) {
        digitalWrite(PIN_ZVONO_ZENSKO, HIGH);
        zvonoZensko = true;
    }
}

void deaktivirajZvonjenje(int koje) {
    if (koje == 1) {
        digitalWrite(PIN_ZVONO_MUSKO, LOW);
        zvonoMusko = false;
        zadnjeIskljucenjeMusko = millis();
    } else if (koje == 2) {
        digitalWrite(PIN_ZVONO_ZENSKO, LOW);
        zvonoZensko = false;
        zadnjeIskljucenjeZensko = millis();
    }
}

bool jeZvonoUTijeku() {
    unsigned long m = millis();
    if (zvonoMusko || zvonoZensko) return true;
    if (m - zadnjeIskljucenjeMusko < 60000UL) return true;
    if (m - zadnjeIskljucenjeZensko < 40000UL) return true;
    return false;
}

bool jeSlavljenjeUTijeku() { return slavljenje; }
bool jeMrtvackoUTijeku() { return mrtvacko; }

void upravljajZvonom() {
    unsigned long sada = millis();

    if (slavljenje) {
        if (sada - slavljenjeStart >= 1000UL) {
            slavljenjeStart = sada;
            slavljenjeKorak = 0;
        }

        if (slavljenjeKorak == 0) {
            udariMusko();
            slavljenjeKorak++;
        } else if (slavljenjeKorak == 1 && sada - slavljenjeStart >= 300UL) {
            udariZensko();
            slavljenjeKorak++;
        } else if (slavljenjeKorak == 2 && sada - slavljenjeStart >= 600UL) {
            udariZensko();
            slavljenjeKorak++;
        }
    }

    if (mrtvacko && sada - zadnjeBrecanje >= 10000UL) {
        zadnjeBrecanje = sada;
        udariMusko();
        udariZensko();
    }
}

void zapocniSlavljenje() {
    slavljenje = true;
    slavljenjeStart = millis();
    slavljenjeKorak = 0;
}

void zapocniMrtvacko() {
    mrtvacko = true;
    zadnjeBrecanje = 0;
}

void zaustaviZvonjenje() {
    slavljenje = false;
    mrtvacko = false;
}
