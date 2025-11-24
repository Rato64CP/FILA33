#include "otkucavanje.h"
#include "zvonjenje.h"
#include "podesavanja_piny.h"
#include "time_glob.h"
#include "postavke.h"
#include "pc_serial.h"      // ➕ dodano za logiranje
#include <Arduino.h>

namespace {
enum VrstaOtkucavanja {
    OTKUCAVANJE_NONE,
    OTKUCAVANJE_SATI,
    OTKUCAVANJE_POLA
};

VrstaOtkucavanja aktivnoOtkucavanje = OTKUCAVANJE_NONE;
int preostaliUdarci = 0;
unsigned long fazaStart = 0;
bool cekicAktivan = false;
int aktivniPin = -1;
DateTime zadnjeMjereneVrijeme;
bool blokadaOtkucavanja = false;

bool jeUIntervalu(int sat) {
    return jeDozvoljenoOtkucavanjeUSatu(sat);
}

void ponistiAktivnoOtkucavanje() {
    if (aktivniPin >= 0) {
        digitalWrite(aktivniPin, LOW);
    }
    aktivnoOtkucavanje = OTKUCAVANJE_NONE;
    preostaliUdarci = 0;
    cekicAktivan = false;
    aktivniPin = -1;
    fazaStart = 0;
}

void pokreniSljedeciUdarac() {
    if (preostaliUdarci <= 0) {
        ponistiAktivnoOtkucavanje();
        return;
    }
    digitalWrite(aktivniPin, HIGH);
    cekicAktivan = true;
    preostaliUdarci--;
    fazaStart = millis();
}

void zapocniOtkucavanje(VrstaOtkucavanja vrsta, int brojUdaraca, int pin) {
    if (brojUdaraca <= 0 || blokadaOtkucavanja) return;
    aktivnoOtkucavanje = vrsta;
    preostaliUdarci = brojUdaraca;
    aktivniPin = pin;
    cekicAktivan = false;
    fazaStart = 0;
    pokreniSljedeciUdarac();
}
} // namespace

void upravljajOtkucavanjem() {
    DateTime sada = dohvatiTrenutnoVrijeme();
    if (sada != zadnjeMjereneVrijeme) {
        zadnjeMjereneVrijeme = sada;

        if (!blokadaOtkucavanja && aktivnoOtkucavanje == OTKUCAVANJE_NONE &&
            !jeZvonoUTijeku() && !jeSlavljenjeUTijeku() && !jeMrtvackoUTijeku()) {

            if (sada.second() == 0 && jeUIntervalu(sada.hour())) {
                if (sada.minute() == 0) {
                    int broj = sada.hour() % 12;
                    if (broj == 0) broj = 12;
                    // ➕ log prije pokretanja otkucaja
                    posaljiPCLog(String(F("Otkucavanje: puni sat, broj udaraca = ")) + broj);
                    otkucajSate(broj);
                } else if (sada.minute() == 30) {
                    // ➕ log za pol sata
                    posaljiPCLog(F("Otkucavanje: pola sata, 1 udarac"));
                    otkucajPolasata();
                }
            }
        }
    }

    if (blokadaOtkucavanja) {
        if (aktivnoOtkucavanje != OTKUCAVANJE_NONE) {
            posaljiPCLog(F("Otkucavanje: prekinuto zbog blokade"));
            ponistiAktivnoOtkucavanje();
        }
        return;
    }

    if (aktivnoOtkucavanje == OTKUCAVANJE_NONE) {
        return;
    }

    unsigned long sadaMs = millis();
    unsigned long trajanjeImpulsa = dohvatiTrajanjeImpulsaCekica();
    unsigned long pauza = dohvatiPauzuIzmeduUdaraca();

    if (cekicAktivan) {
        if (sadaMs - fazaStart >= trajanjeImpulsa) {
            digitalWrite(aktivniPin, LOW);
            cekicAktivan = false;
            fazaStart = millis();
        }
    } else {
        if (preostaliUdarci > 0) {
            if (fazaStart == 0 || sadaMs - fazaStart >= pauza) {
                pokreniSljedeciUdarac();
            }
        } else {
            aktivnoOtkucavanje = OTKUCAVANJE_NONE;
            aktivniPin = -1;
        }
    }
}

void otkucajSate(int broj) {
    if (aktivnoOtkucavanje != OTKUCAVANJE_NONE) return;
    zapocniOtkucavanje(OTKUCAVANJE_SATI, broj, PIN_CEKIC_MUSKI);
}

void otkucajPolasata() {
    if (aktivnoOtkucavanje != OTKUCAVANJE_NONE) return;
    zapocniOtkucavanje(OTKUCAVANJE_POLA, 1, PIN_CEKIC_ZENSKI);
}

void postaviBlokaduOtkucavanja(bool blokiraj) {
    if (blokadaOtkucavanja == blokiraj) return;

    blokadaOtkucavanja = blokiraj;

    if (blokadaOtkucavanja) {
        posaljiPCLog(F("Blokada otkucavanja: UKLJUČENA"));
        ponistiAktivnoOtkucavanje();
    } else {
        posaljiPCLog(F("Blokada otkucavanja: ISKLJUČENA"));
    }
}