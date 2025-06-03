#include "rtc_vrijeme.h"
#include <Arduino.h>
#include <EEPROM.h>
#include "vrijeme_izvor.h"
#include "dcf_decoder.h"

static RTC_DS3231 rtc;

// Zadnji izvor sinkronizacije. Spremamo u EEPROM na adresi 30
String izvorVremena = "RTC";


// Provjera za srednjoeuropsko ljetno računanje vremena
bool isDST(int dan, int mjesec, int danUTjednu) {
    if (mjesec < 3 || mjesec > 10) return false;         // sijecanj, veljaca, studeni, prosinac
    if (mjesec > 3 && mjesec < 10) return true;          // od travnja do rujna
    int zadnjaNedjelja = dan - danUTjednu;               // dan u mjesecu zadnje nedjelje
    if (mjesec == 3) return zadnjaNedjelja >= 25;        // posljednja nedjelja u ožujku
    return zadnjaNedjelja < 25;                          // prije zadnje nedjelje u listopadu
}

void syncNTP() {
    Serial1.println("REQ:NTP");
    String buffer = "";
    unsigned long start = millis();
    while (millis() - start < 5000) {                    // čekaj do 5 sekundi na odgovor
        while (Serial1.available()) {
            char c = Serial1.read();
            if (c == '\n') {
                buffer.trim();
                if (buffer.startsWith("NTP:")) {
                    DateTime dt(buffer.substring(4).c_str());
                    rtc.adjust(dt);
                    izvorVremena = "NTP";
                    EEPROM.put(30, izvorVremena);
                    setZadnjaSinkronizacija(NTP_VRIJEME, dt);
                }
                return;
            } else {
                buffer += c;
            }
        }
    }
}

void syncDCF() {
    // DCF dekoder sam postavlja vrijeme kada uhvati cijeli frame
    // Ovdje samo prosljeđujemo ulaze dekoderu određeno vrijeme
    unsigned long start = millis();
    while (millis() - start < 65000) {                   // do 1 minute za cijeli frame
        dekodirajDCFSignal();
    }
    // ako je dekoder uspješno podesio RTC, vrijeme i izvor su već spremljeni
}

