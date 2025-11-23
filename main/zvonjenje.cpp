#include "zvonjenje.h"
#include "podesavanja_piny.h"
#include <Arduino.h>

static bool zvonoPrvo = false;
static bool zvonoDrugo = false;
static unsigned long zadnjeIskljucenjePrvo = 0;
static unsigned long zadnjeIskljucenjeDrugo = 0;

static bool slavljenje = false;
static bool mrtvacko = false;

static unsigned long slavljenjeKorakStart = 0;
static unsigned int slavljenjeKorak = 0;
static bool slavljenjeSignalAktivno = false;

static unsigned long zadnjeBrecanje = 0;
static bool mrtvackoSekvenca = false;
static bool mrtvackoPrviKorak = true;
static unsigned long mrtvackoKorakStart = 0;

static const unsigned long TRAJANJE_UDARCA_MS = 150UL;
static const unsigned long SLAVLJENJE_KORAK_MS = 150UL;

enum SlavljenjeAkcija : uint8_t {
    SLAVLJENJE_PAUZA = 0,
    SLAVLJENJE_ZENSKO,
    SLAVLJENJE_MUSKO
};

static const SlavljenjeAkcija SLAVLJENJE_UZORAK[] = {
    SLAVLJENJE_ZENSKO,
    SLAVLJENJE_PAUZA,
    SLAVLJENJE_ZENSKO,
    SLAVLJENJE_PAUZA,
    SLAVLJENJE_MUSKO,
    SLAVLJENJE_PAUZA,
};

static const size_t BROJ_KORAKA_SLAVLJENJA = sizeof(SLAVLJENJE_UZORAK) / sizeof(SLAVLJENJE_UZORAK[0]);

static void postaviCekice(bool muskoAktivan, bool zenskoAktivan) {
    digitalWrite(PIN_CEKIC_MUSKI, muskoAktivan ? HIGH : LOW);
    digitalWrite(PIN_CEKIC_ZENSKI, zenskoAktivan ? HIGH : LOW);
}

static void primijeniSlavljenjeKorak() {
    SlavljenjeAkcija akcija = SLAVLJENJE_UZORAK[slavljenjeKorak % BROJ_KORAKA_SLAVLJENJA];
    if (akcija == SLAVLJENJE_ZENSKO) {
        postaviCekice(false, true);
    } else if (akcija == SLAVLJENJE_MUSKO) {
        postaviCekice(true, false);
    } else {
        postaviCekice(false, false);
    }
}

static void zaustaviMrtvackuSekvencu() {
    mrtvackoSekvenca = false;
    mrtvackoPrviKorak = true;
    mrtvackoKorakStart = 0;
    if (!slavljenje) {
        postaviCekice(false, false);
    }
}

static void pokreniMrtvackuSekvencu(unsigned long sadaMs) {
    mrtvackoSekvenca = true;
    mrtvackoPrviKorak = true;
    mrtvackoKorakStart = sadaMs;
    postaviCekice(true, false);
}

static void azurirajMrtvackuSekvencu(unsigned long sadaMs) {
    if (!mrtvackoSekvenca) {
        return;
    }

    if (sadaMs - mrtvackoKorakStart < TRAJANJE_UDARCA_MS) {
        return;
    }

    postaviCekice(false, false);

    if (mrtvackoPrviKorak) {
        mrtvackoPrviKorak = false;
        mrtvackoKorakStart = sadaMs;
        postaviCekice(false, true);
    } else {
        zaustaviMrtvackuSekvencu();
    }
}

void inicijalizirajZvona() {
    pinMode(PIN_ZVONO_1, OUTPUT);
    pinMode(PIN_ZVONO_2, OUTPUT);
    pinMode(PIN_CEKIC_MUSKI, OUTPUT);
    pinMode(PIN_CEKIC_ZENSKI, OUTPUT);
    pinMode(PIN_SLAVLJENJE_SIGNAL, INPUT_PULLUP);
    digitalWrite(PIN_ZVONO_1, LOW);
    digitalWrite(PIN_ZVONO_2, LOW);
    postaviCekice(false, false);
    slavljenjeSignalAktivno = false;
}

void aktivirajZvonjenje(int koje) {
    if (koje == 1) {
        digitalWrite(PIN_ZVONO_1, HIGH);
        zvonoPrvo = true;
    } else if (koje == 2) {
        digitalWrite(PIN_ZVONO_2, HIGH);
        zvonoDrugo = true;
    }
}

void deaktivirajZvonjenje(int koje) {
    if (koje == 1) {
        digitalWrite(PIN_ZVONO_1, LOW);
        zvonoPrvo = false;
        zadnjeIskljucenjePrvo = millis();
    } else if (koje == 2) {
        digitalWrite(PIN_ZVONO_2, LOW);
        zvonoDrugo = false;
        zadnjeIskljucenjeDrugo = millis();
    }
}

bool jeZvonoUTijeku() {
    unsigned long m = millis();
    if (zvonoPrvo || zvonoDrugo) return true;
    if (m - zadnjeIskljucenjePrvo < 60000UL) return true;
    if (m - zadnjeIskljucenjeDrugo < 40000UL) return true;
    return false;
}

bool jeSlavljenjeUTijeku() { return slavljenje; }
bool jeMrtvackoUTijeku() { return mrtvacko; }

void upravljajZvonom() {
    unsigned long sada = millis();

    bool signalAktivan = digitalRead(PIN_SLAVLJENJE_SIGNAL) == LOW;
    if (signalAktivan != slavljenjeSignalAktivno) {
        slavljenjeSignalAktivno = signalAktivan;
        if (signalAktivan) {
            zapocniSlavljenje();
        } else if (slavljenje) {
            zaustaviSlavljenje();
        }
    }

    if (slavljenje && sada - slavljenjeKorakStart >= SLAVLJENJE_KORAK_MS) {
        slavljenjeKorak = (slavljenjeKorak + 1) % BROJ_KORAKA_SLAVLJENJA;
        slavljenjeKorakStart = sada;
        primijeniSlavljenjeKorak();
    }

    azurirajMrtvackuSekvencu(sada);

    if (mrtvacko && !slavljenje && !mrtvackoSekvenca && sada - zadnjeBrecanje >= 10000UL) {
        zadnjeBrecanje = sada;
        pokreniMrtvackuSekvencu(sada);
    }
}

void zapocniSlavljenje() {
    zaustaviMrtvackuSekvencu();
    slavljenje = true;
    slavljenjeKorak = 0;
    primijeniSlavljenjeKorak();
    slavljenjeKorakStart = millis();
}

void zaustaviSlavljenje() {
    slavljenje = false;
    slavljenjeKorak = 0;
    postaviCekice(false, false);
}

void zapocniMrtvacko() {
    mrtvacko = true;
    zadnjeBrecanje = 0;
    zaustaviMrtvackuSekvencu();
}

void zaustaviZvonjenje() {
    zaustaviSlavljenje();
    mrtvacko = false;
    zaustaviMrtvackuSekvencu();
}
