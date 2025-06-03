#include "otkucavanje.h"
#include "zvonjenje.h"
#include "podesavanja_piny.h"
#include "time_glob.h"
#include <Arduino.h>

static DateTime zadnjeOtkucavanje;

void upravljajOtkucavanjem() {
    DateTime sada = dohvatiTrenutnoVrijeme();
    if (sada == zadnjeOtkucavanje) return; // provjera jednom u sekundi
    zadnjeOtkucavanje = sada;

    if (jeZvonoUTijeku() || jeSlavljenjeUTijeku() || jeMrtvackoUTijeku()) return;

    int sat = sada.hour();
    int minuta = sada.minute();
    int sekunda = sada.second();

    if (minuta == 0 && sekunda == 0 && sat >= 6 && sat <= 22) {
        int broj = sat % 12;
        if (broj == 0) broj = 12;
        otkucajSate(broj);
    }
    else if (minuta == 30 && sekunda == 0 && sat >= 6 && sat <= 22) {
        otkucajPolasata();
    }
}

void otkucajSate(int broj) {
    for (int i = 0; i < broj; i++) {
        digitalWrite(PIN_CEKIC_MUSKI, HIGH);
        delay(200);
        digitalWrite(PIN_CEKIC_MUSKI, LOW);
        delay(800);
    }
}

void otkucajPolasata() {
    digitalWrite(PIN_CEKIC_ZENSKI, HIGH);
    delay(200);
    digitalWrite(PIN_CEKIC_ZENSKI, LOW);
}
