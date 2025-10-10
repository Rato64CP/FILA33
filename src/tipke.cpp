#include "tipke.h"
#include <Arduino.h>
#include <RTClib.h>
#include <cstring>
#include <cstdio>
#include "podesavanja_piny.h"
#include "time_glob.h"
#include "kazaljke_sata.h"
#include "postavke.h"

namespace {

constexpr unsigned long DEBOUNCE_MS = 40UL;

enum TipkaIndex : uint8_t {
    TIPKA_GORE = 0,
    TIPKA_DOLJE,
    TIPKA_LIJEVO,
    TIPKA_DESNO,
    TIPKA_DA,
    TIPKA_NE,
    TIPKA_BROJ
};

const uint8_t PINOVI[TIPKA_BROJ] = {
    PIN_TIPKA_GORE,
    PIN_TIPKA_DOLJE,
    PIN_TIPKA_LIJEVO,
    PIN_TIPKA_DESNO,
    PIN_TIPKA_DA,
    PIN_TIPKA_NE
};

bool stabilnoStanje[TIPKA_BROJ] = {false};
bool zadnjeOcitano[TIPKA_BROJ] = {false};
unsigned long vrijemePromjene[TIPKA_BROJ] = {0};

enum PostavkeEkran : uint8_t {
    EKRAN_KAZALJKE = 0,
    EKRAN_VRIJEME,
    EKRAN_CEKICI,
    EKRAN_OTKUCAVANJE,
    EKRAN_ZVONO_RADNI,
    EKRAN_ZVONO_NEDJELJA,
    EKRAN_SLAVLJENJE,
    EKRAN_REZERVA,
    EKRAN_BROJ
};

bool postavkeMode = false;
PostavkeEkran aktivniEkran = EKRAN_KAZALJKE;
bool uEditModu = false;
bool sekundarnoPolje = false;

int privKazaljkeMinuta = 0;
int privSat = 0;
int privMinuta = 0;
unsigned int privTrajanjeCekic = 0;
int privSatOd = 0;
int privSatDo = 0;
unsigned long privRadniZvono = 0;
unsigned long privNedjeljaZvono = 0;
unsigned long privSlavljenje = 0;

char prikazRedak1[17];
char prikazRedak2[17];
bool prikazAzuriran = false;

bool ocitajPritisak(TipkaIndex index) {
    bool trenutno = digitalRead(PINOVI[index]) == LOW;
    if (trenutno != zadnjeOcitano[index]) {
        zadnjeOcitano[index] = trenutno;
        vrijemePromjene[index] = millis();
    }

    if ((millis() - vrijemePromjene[index]) > DEBOUNCE_MS) {
        if (stabilnoStanje[index] != trenutno) {
            stabilnoStanje[index] = trenutno;
            if (trenutno) {
                return true;
            }
        }
    }
    return false;
}

void resetirajUredjivanje() {
    uEditModu = false;
    sekundarnoPolje = false;
}

void aktivirajPostavke() {
    postavkeMode = true;
    aktivniEkran = EKRAN_KAZALJKE;
    resetirajUredjivanje();
}

void promijeniEkran(int pomak) {
    int nova = static_cast<int>(aktivniEkran) + pomak;
    while (nova < 0) nova += EKRAN_BROJ;
    nova %= EKRAN_BROJ;
    aktivniEkran = static_cast<PostavkeEkran>(nova);
    resetirajUredjivanje();
}

void zapocniUredjivanjeTrenutnogEkrana() {
    // Preuzmi početne vrijednosti iz modula prije ručne izmjene (kazaljke_sata.cpp, time_glob.cpp, postavke.cpp)
    switch (aktivniEkran) {
        case EKRAN_KAZALJKE:
            privKazaljkeMinuta = dohvatiMemoriraneKazaljkeMinuta();
            break;
        case EKRAN_VRIJEME: {
            DateTime sada = dohvatiTrenutnoVrijeme();
            privSat = sada.hour();
            privMinuta = sada.minute();
            sekundarnoPolje = false;
            break;
        }
        case EKRAN_CEKICI:
            privTrajanjeCekic = dohvatiTrajanjeImpulsaCekica();
            break;
        case EKRAN_OTKUCAVANJE:
            privSatOd = satOd;
            privSatDo = satDo;
            sekundarnoPolje = false;
            break;
        case EKRAN_ZVONO_RADNI:
            privRadniZvono = dohvatiTrajanjeZvonjenjaRadniMs();
            break;
        case EKRAN_ZVONO_NEDJELJA:
            privNedjeljaZvono = dohvatiTrajanjeZvonjenjaNedjeljaMs();
            break;
        case EKRAN_SLAVLJENJE:
            privSlavljenje = dohvatiTrajanjeSlavljenjaMs();
            break;
        case EKRAN_REZERVA:
            break;
    }
    uEditModu = true;
}

void spremiPromjene() {
    // Spremi potvrđene vrijednosti natrag u odgovarajuće module toranjskog sustava
    switch (aktivniEkran) {
        case EKRAN_KAZALJKE:
            postaviTrenutniPolozajKazaljki(privKazaljkeMinuta);
            break;
        case EKRAN_VRIJEME: {
            DateTime sada = dohvatiTrenutnoVrijeme();
            DateTime novoVrijeme(sada.year(), sada.month(), sada.day(), privSat, privMinuta, 0);
            postaviVrijemeRucno(novoVrijeme);
            break;
        }
        case EKRAN_CEKICI:
            postaviTrajanjeImpulsaCekica(privTrajanjeCekic);
            break;
        case EKRAN_OTKUCAVANJE:
            postaviRasponOtkucavanja(privSatOd, privSatDo);
            break;
        case EKRAN_ZVONO_RADNI:
            postaviTrajanjeZvonjenjaRadni(privRadniZvono);
            break;
        case EKRAN_ZVONO_NEDJELJA:
            postaviTrajanjeZvonjenjaNedjelja(privNedjeljaZvono);
            break;
        case EKRAN_SLAVLJENJE:
            postaviTrajanjeSlavljenja(privSlavljenje);
            break;
        case EKRAN_REZERVA:
            break;
    }
    resetirajUredjivanje();
}

void otkaziUredjivanje() {
    resetirajUredjivanje();
}

void azurirajKazaljkeNaTipkama(bool lijevo, bool desno) {
    if (!uEditModu) return;
    if (lijevo) {
        privKazaljkeMinuta = (privKazaljkeMinuta + 719) % 720;
    }
    if (desno) {
        privKazaljkeMinuta = (privKazaljkeMinuta + 1) % 720;
    }
}

void azurirajVrijemeNaTipkama(bool lijevo, bool desno, bool gore, bool dolje) {
    if (!uEditModu) return;
    if (gore || dolje) {
        sekundarnoPolje = !sekundarnoPolje;
    }
    if (lijevo) {
        if (!sekundarnoPolje) {
            privSat = (privSat + 23) % 24;
        } else {
            privMinuta = (privMinuta + 59) % 60;
        }
    }
    if (desno) {
        if (!sekundarnoPolje) {
            privSat = (privSat + 1) % 24;
        } else {
            privMinuta = (privMinuta + 1) % 60;
        }
    }
}

void azurirajCekicNaTipkama(bool lijevo, bool desno) {
    if (!uEditModu) return;
    const unsigned int KORAK = 10;
    if (lijevo) {
        if (privTrajanjeCekic > KORAK) {
            privTrajanjeCekic -= KORAK;
        }
    }
    if (desno) {
        privTrajanjeCekic += KORAK;
        if (privTrajanjeCekic > 2000U) privTrajanjeCekic = 2000U;
    }
    if (privTrajanjeCekic < 50U) privTrajanjeCekic = 50U;
}

void azurirajOtkucavanjeNaTipkama(bool lijevo, bool desno, bool gore, bool dolje) {
    if (!uEditModu) return;
    if (gore || dolje) {
        sekundarnoPolje = !sekundarnoPolje;
    }
    if (lijevo) {
        if (!sekundarnoPolje) {
            privSatOd = (privSatOd + 23) % 24;
        } else {
            privSatDo = (privSatDo + 23) % 24;
        }
    }
    if (desno) {
        if (!sekundarnoPolje) {
            privSatOd = (privSatOd + 1) % 24;
        } else {
            privSatDo = (privSatDo + 1) % 24;
        }
    }
}

void azurirajTrajanjeMs(unsigned long& vrijednost, bool lijevo, bool desno, unsigned long korak, unsigned long minimum) {
    if (!uEditModu) return;
    if (lijevo) {
        if (vrijednost > minimum) {
            if (vrijednost - minimum < korak) {
                vrijednost = minimum;
            } else {
                vrijednost -= korak;
            }
        }
    }
    if (desno) {
        vrijednost += korak;
        if (vrijednost > 600000UL) vrijednost = 600000UL;
    }
    if (vrijednost < minimum) vrijednost = minimum;
}

void pripremiPrikaz() {
    // Sastavi dvoredni prikaz koji lcd_display.cpp prikazuje dok su postavke aktivne
    memset(prikazRedak1, ' ', sizeof(prikazRedak1));
    memset(prikazRedak2, ' ', sizeof(prikazRedak2));
    prikazRedak1[sizeof(prikazRedak1) - 1] = '\0';
    prikazRedak2[sizeof(prikazRedak2) - 1] = '\0';

    switch (aktivniEkran) {
        case EKRAN_KAZALJKE: {
            int minute = uEditModu ? privKazaljkeMinuta : dohvatiMemoriraneKazaljkeMinuta();
            int sat = (minute / 60) % 12;
            if (sat == 0) sat = 12;
            int min = minute % 60;
            snprintf(prikazRedak1, sizeof(prikazRedak1), "Kazaljke %02d:%02d%c", sat, min, uEditModu ? '*' : ' ');
            if (uEditModu) {
                snprintf(prikazRedak2, sizeof(prikazRedak2), "L/R +/-1m DA=Sp");
            } else {
                snprintf(prikazRedak2, sizeof(prikazRedak2), "DA=Uredi NE=Izl");
            }
            break;
        }
        case EKRAN_VRIJEME: {
            DateTime prikazVrijeme = dohvatiTrenutnoVrijeme();
            int sat = uEditModu ? privSat : prikazVrijeme.hour();
            int min = uEditModu ? privMinuta : prikazVrijeme.minute();
            snprintf(prikazRedak1, sizeof(prikazRedak1), "Vrijeme %02d:%02d%c", sat, min, uEditModu ? '*' : ' ');
            if (uEditModu) {
                if (!sekundarnoPolje) {
                    snprintf(prikazRedak2, sizeof(prikazRedak2), ">Sat L/R DA=Sp");
                } else {
                    snprintf(prikazRedak2, sizeof(prikazRedak2), ">Min L/R DA=Sp");
                }
            } else {
                snprintf(prikazRedak2, sizeof(prikazRedak2), "DA=Uredi NE=Izl");
            }
            break;
        }
        case EKRAN_CEKICI: {
            unsigned int trajanje = uEditModu ? privTrajanjeCekic : dohvatiTrajanjeImpulsaCekica();
            snprintf(prikazRedak1, sizeof(prikazRedak1), "Cekic %4ums%c", trajanje, uEditModu ? '*' : ' ');
            if (uEditModu) {
                snprintf(prikazRedak2, sizeof(prikazRedak2), "L/R +/-10 DA=Sp");
            } else {
                snprintf(prikazRedak2, sizeof(prikazRedak2), "DA=Uredi NE=Izl");
            }
            break;
        }
        case EKRAN_OTKUCAVANJE: {
            int od = uEditModu ? privSatOd : satOd;
            int d = uEditModu ? privSatDo : satDo;
            snprintf(prikazRedak1, sizeof(prikazRedak1), "Otk %02d-%02d h%c", od, d, uEditModu ? '*' : ' ');
            if (uEditModu) {
                if (!sekundarnoPolje) {
                    snprintf(prikazRedak2, sizeof(prikazRedak2), ">Od L/R DA=Sp");
                } else {
                    snprintf(prikazRedak2, sizeof(prikazRedak2), ">Do L/R DA=Sp");
                }
            } else {
                snprintf(prikazRedak2, sizeof(prikazRedak2), "DA=Uredi NE=Izl");
            }
            break;
        }
        case EKRAN_ZVONO_RADNI: {
            unsigned long trajanje = uEditModu ? privRadniZvono : dohvatiTrajanjeZvonjenjaRadniMs();
            unsigned long sek = trajanje / 1000UL;
            snprintf(prikazRedak1, sizeof(prikazRedak1), "Radni zv %4lus%c", sek, uEditModu ? '*' : ' ');
            if (uEditModu) {
                snprintf(prikazRedak2, sizeof(prikazRedak2), "L/R +/-10s DA");
            } else {
                snprintf(prikazRedak2, sizeof(prikazRedak2), "DA=Uredi NE=Izl");
            }
            break;
        }
        case EKRAN_ZVONO_NEDJELJA: {
            unsigned long trajanje = uEditModu ? privNedjeljaZvono : dohvatiTrajanjeZvonjenjaNedjeljaMs();
            unsigned long sek = trajanje / 1000UL;
            snprintf(prikazRedak1, sizeof(prikazRedak1), "Ned zv %5lus%c", sek, uEditModu ? '*' : ' ');
            if (uEditModu) {
                snprintf(prikazRedak2, sizeof(prikazRedak2), "L/R +/-10s DA");
            } else {
                snprintf(prikazRedak2, sizeof(prikazRedak2), "DA=Uredi NE=Izl");
            }
            break;
        }
        case EKRAN_SLAVLJENJE: {
            unsigned long trajanje = uEditModu ? privSlavljenje : dohvatiTrajanjeSlavljenjaMs();
            unsigned long sek = trajanje / 1000UL;
            snprintf(prikazRedak1, sizeof(prikazRedak1), "Slavlje %5lus%c", sek, uEditModu ? '*' : ' ');
            if (uEditModu) {
                snprintf(prikazRedak2, sizeof(prikazRedak2), "L/R +/-5s DA");
            } else {
                snprintf(prikazRedak2, sizeof(prikazRedak2), "DA=Uredi NE=Izl");
            }
            break;
        }
        case EKRAN_REZERVA:
            snprintf(prikazRedak1, sizeof(prikazRedak1), "Rezerva     ");
            snprintf(prikazRedak2, sizeof(prikazRedak2), "NE=Izlaz    ");
            break;
    }
}

} // namespace

void inicijalizirajTipke() {
    for (uint8_t i = 0; i < TIPKA_BROJ; ++i) {
        pinMode(PINOVI[i], INPUT_PULLUP);
        stabilnoStanje[i] = false;
        zadnjeOcitano[i] = false;
        vrijemePromjene[i] = millis();
    }
}

void provjeriTipke() {
    bool gore = ocitajPritisak(TIPKA_GORE);
    bool dolje = ocitajPritisak(TIPKA_DOLJE);
    bool lijevo = ocitajPritisak(TIPKA_LIJEVO);
    bool desno = ocitajPritisak(TIPKA_DESNO);
    bool da = ocitajPritisak(TIPKA_DA);
    bool ne = ocitajPritisak(TIPKA_NE);

    if (!postavkeMode) {
        if (gore || dolje) {
            aktivirajPostavke();
        }
        return;
    }

    if (!uEditModu) {
        if (gore) {
            promijeniEkran(-1);
        } else if (dolje) {
            promijeniEkran(1);
        }
    }

    switch (aktivniEkran) {
        case EKRAN_KAZALJKE:
            azurirajKazaljkeNaTipkama(lijevo, desno);
            break;
        case EKRAN_VRIJEME:
            azurirajVrijemeNaTipkama(lijevo, desno, gore, dolje);
            break;
        case EKRAN_CEKICI:
            azurirajCekicNaTipkama(lijevo, desno);
            break;
        case EKRAN_OTKUCAVANJE:
            azurirajOtkucavanjeNaTipkama(lijevo, desno, gore, dolje);
            break;
        case EKRAN_ZVONO_RADNI:
            azurirajTrajanjeMs(privRadniZvono, lijevo, desno, 10000UL, 10000UL);
            break;
        case EKRAN_ZVONO_NEDJELJA:
            azurirajTrajanjeMs(privNedjeljaZvono, lijevo, desno, 10000UL, 10000UL);
            break;
        case EKRAN_SLAVLJENJE:
            azurirajTrajanjeMs(privSlavljenje, lijevo, desno, 5000UL, 10000UL);
            break;
        case EKRAN_REZERVA:
            break;
    }

    if (da) {
        if (!uEditModu) {
            zapocniUredjivanjeTrenutnogEkrana();
        } else {
            spremiPromjene();
        }
    }

    if (ne) {
        if (uEditModu) {
            otkaziUredjivanje();
        } else {
            postavkeMode = false;
            resetirajUredjivanje();
        }
    }

    if (postavkeMode) {
        prikazAzuriran = false;
    }
}

bool uPostavkama() {
    return postavkeMode;
}

const char* dohvatiPostavkeRedak1() {
    pripremiPrikaz();
    prikazAzuriran = true;
    return prikazRedak1;
}

const char* dohvatiPostavkeRedak2() {
    if (!prikazAzuriran) {
        pripremiPrikaz();
    }
    prikazAzuriran = false;
    return prikazRedak2;
}
