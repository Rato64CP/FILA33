#include "tipke.h"
#include <Arduino.h>
#include <RTClib.h>
// #include <cstring>
// #include <cstdio>
// #include <cstdlib>
#include <string.h>   // memset, size_t, itd.
#include <stdio.h>    // snprintf
#include <stdlib.h>   // abs, malloc, free (ako ikad zatreba)
#include "podesavanja_piny.h"
#include "time_glob.h"
#include "kazaljke_sata.h"
#include "postavke.h"

namespace {

constexpr unsigned long DEBOUNCE_MS = 40UL;
constexpr unsigned int DOZVOLJENA_TRAJANJA_CEKICA[] = {100U, 200U, 500U, 1000U};
constexpr size_t BROJ_TRAJANJA_CEKICA = sizeof(DOZVOLJENA_TRAJANJA_CEKICA) / sizeof(DOZVOLJENA_TRAJANJA_CEKICA[0]);
constexpr int MINUTA_DAN = 24 * 60;
constexpr size_t IPV4_DULJINA = 15;
constexpr char DOZVOLJENI_ZNAKOVI[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-_";
constexpr size_t BROJ_DOZVOLJENIH_ZNAKOVA = sizeof(DOZVOLJENI_ZNAKOVI) - 1;
constexpr char IPV4_PREDLOZAK[] = "000.000.000.000";

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
    EKRAN_BROJ_ZVONA,
    EKRAN_PLOCA_RASPON,
    EKRAN_PRISTUP_LOZINKA,
    EKRAN_WIFI_SSID,
    EKRAN_WIFI_LOZINKA,
    EKRAN_MREZA_DHCP,
    EKRAN_MREZA_IP,
    EKRAN_MREZA_MASKA,
    EKRAN_MREZA_GATEWAY,
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
uint8_t privBrojZvona = 0;
int privPlocaPocetak = 0;
int privPlocaKraj = 0;
char privPristupLozinka[9];
char privWifiSsid[33];
char privWifiLozinka[33];
bool privDhcp = true;
char privStatickaIp[16];
char privMreznaMaska[16];
char privGateway[16];
uint8_t kursorTekst = 0;
uint8_t kursorIPv4 = 0;

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

unsigned int normalizirajTrajanjeCekica(unsigned int vrijednost) {
    unsigned int najblize = DOZVOLJENA_TRAJANJA_CEKICA[0];
    unsigned int najmanjaRazlika = abs(static_cast<int>(vrijednost) - static_cast<int>(najblize));
    for (size_t i = 1; i < BROJ_TRAJANJA_CEKICA; ++i) {
        unsigned int kandidat = DOZVOLJENA_TRAJANJA_CEKICA[i];
        unsigned int razlika = abs(static_cast<int>(vrijednost) - static_cast<int>(kandidat));
        if (razlika < najmanjaRazlika) {
            najblize = kandidat;
            najmanjaRazlika = razlika;
        }
    }
    return najblize;
}

void kopirajTekstPolje(const char* izvor, char* odrediste, size_t velicina) {
    memset(odrediste, 0, velicina);
    strncpy(odrediste, izvor, velicina - 1);
}

char promijeniZnak(char trenutni, int pomak) {
    size_t indeks = 0;
    for (; indeks < BROJ_DOZVOLJENIH_ZNAKOVA; ++indeks) {
        if (DOZVOLJENI_ZNAKOVI[indeks] == trenutni) {
            break;
        }
    }
    if (indeks == BROJ_DOZVOLJENIH_ZNAKOVA) indeks = 0;
    indeks = (indeks + BROJ_DOZVOLJENIH_ZNAKOVA + pomak) % BROJ_DOZVOLJENIH_ZNAKOVA;
    return DOZVOLJENI_ZNAKOVI[indeks];
}

void azurirajTekstualnoPoljeNaTipkama(char* polje, size_t maxVelicina, bool lijevo, bool desno, bool gore, bool dolje) {
    if (!uEditModu) return;
    size_t duljina = maxVelicina - 1;
    if (lijevo) {
        kursorTekst = (kursorTekst + duljina - 1) % duljina;
    }
    if (desno) {
        kursorTekst = (kursorTekst + 1) % duljina;
    }
    if (gore) {
        polje[kursorTekst] = promijeniZnak(polje[kursorTekst], -1);
    }
    if (dolje) {
        polje[kursorTekst] = promijeniZnak(polje[kursorTekst], 1);
    }
    polje[duljina] = '\0';
}

void osigurajIPv4Predlozak(char* polje) {
    if (strlen(polje) != IPV4_DULJINA) {
        strncpy(polje, IPV4_PREDLOZAK, IPV4_DULJINA);
        polje[IPV4_DULJINA] = '\0';
    }
    for (size_t i = 0; i < IPV4_DULJINA; ++i) {
        if (IPV4_PREDLOZAK[i] == '.') {
            polje[i] = '.';
        } else if (polje[i] < '0' || polje[i] > '9') {
            polje[i] = '0';
        }
    }
    polje[IPV4_DULJINA] = '\0';
}

void pomakniIPv4Kursor(int pomak) {
    do {
        kursorIPv4 = (kursorIPv4 + IPV4_DULJINA + pomak) % IPV4_DULJINA;
    } while (IPV4_PREDLOZAK[kursorIPv4] == '.');
}

void azurirajIPv4NaTipkama(char* polje, bool lijevo, bool desno, bool gore, bool dolje) {
    if (!uEditModu) return;
    if (lijevo) {
        pomakniIPv4Kursor(-1);
    }
    if (desno) {
        pomakniIPv4Kursor(1);
    }
    if (gore || dolje) {
        if (polje[kursorIPv4] < '0' || polje[kursorIPv4] > '9') polje[kursorIPv4] = '0';
        int smjer = gore ? -1 : 1;
        int vrijednost = polje[kursorIPv4] - '0';
        vrijednost = (vrijednost + 10 + smjer) % 10;
        polje[kursorIPv4] = static_cast<char>('0' + vrijednost);
    }
    polje[IPV4_DULJINA] = '\0';
}

void azurirajDhcpNaTipkama(bool lijevo, bool desno, bool gore, bool dolje) {
    if (!uEditModu) return;
    if (lijevo || desno || gore || dolje) {
        privDhcp = !privDhcp;
    }
}

void resetirajUredjivanje() {
    uEditModu = false;
    sekundarnoPolje = false;
    kursorTekst = 0;
    kursorIPv4 = 0;
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
            privTrajanjeCekic = normalizirajTrajanjeCekica(dohvatiTrajanjeImpulsaCekica());
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
        case EKRAN_BROJ_ZVONA:
            privBrojZvona = dohvatiBrojZvona();
            break;
        case EKRAN_PLOCA_RASPON:
            privPlocaPocetak = dohvatiPocetakPloceMinute();
            privPlocaKraj = dohvatiKrajPloceMinute();
            sekundarnoPolje = false;
            break;
        case EKRAN_PRISTUP_LOZINKA:
            kopirajTekstPolje(dohvatiPristupnuLozinku(), privPristupLozinka, sizeof(privPristupLozinka));
            kursorTekst = 0;
            break;
        case EKRAN_WIFI_SSID:
            kopirajTekstPolje(dohvatiWifiSsid(), privWifiSsid, sizeof(privWifiSsid));
            kursorTekst = 0;
            break;
        case EKRAN_WIFI_LOZINKA:
            kopirajTekstPolje(dohvatiWifiLozinku(), privWifiLozinka, sizeof(privWifiLozinka));
            kursorTekst = 0;
            break;
        case EKRAN_MREZA_DHCP:
            privDhcp = koristiDhcpMreza();
            break;
        case EKRAN_MREZA_IP:
            kopirajTekstPolje(dohvatiStatickuIP(), privStatickaIp, sizeof(privStatickaIp));
            osigurajIPv4Predlozak(privStatickaIp);
            kursorIPv4 = 0;
            break;
        case EKRAN_MREZA_MASKA:
            kopirajTekstPolje(dohvatiMreznuMasku(), privMreznaMaska, sizeof(privMreznaMaska));
            osigurajIPv4Predlozak(privMreznaMaska);
            kursorIPv4 = 0;
            break;
        case EKRAN_MREZA_GATEWAY:
            kopirajTekstPolje(dohvatiZadaniGateway(), privGateway, sizeof(privGateway));
            osigurajIPv4Predlozak(privGateway);
            kursorIPv4 = 0;
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
        case EKRAN_BROJ_ZVONA:
            postaviBrojZvona(privBrojZvona);
            break;
        case EKRAN_PLOCA_RASPON:
            postaviRasponPloce(privPlocaPocetak, privPlocaKraj);
            break;
        case EKRAN_PRISTUP_LOZINKA:
            postaviPristupnuLozinku(privPristupLozinka);
            break;
        case EKRAN_WIFI_SSID:
            postaviWifiSsid(privWifiSsid);
            break;
        case EKRAN_WIFI_LOZINKA:
            postaviWifiLozinku(privWifiLozinka);
            break;
        case EKRAN_MREZA_DHCP:
            postaviDhcp(privDhcp);
            break;
        case EKRAN_MREZA_IP:
            postaviStatickuIP(privStatickaIp);
            break;
        case EKRAN_MREZA_MASKA:
            postaviMreznuMasku(privMreznaMaska);
            break;
        case EKRAN_MREZA_GATEWAY:
            postaviZadaniGateway(privGateway);
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
    privTrajanjeCekic = normalizirajTrajanjeCekica(privTrajanjeCekic);
    size_t indeks = 0;
    for (size_t i = 0; i < BROJ_TRAJANJA_CEKICA; ++i) {
        if (DOZVOLJENA_TRAJANJA_CEKICA[i] == privTrajanjeCekic) {
            indeks = i;
            break;
        }
    }
    if (lijevo) {
        indeks = (indeks + BROJ_TRAJANJA_CEKICA - 1) % BROJ_TRAJANJA_CEKICA;
    }
    if (desno) {
        indeks = (indeks + 1) % BROJ_TRAJANJA_CEKICA;
    }
    privTrajanjeCekic = DOZVOLJENA_TRAJANJA_CEKICA[indeks];
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

void pomakniMinutu(int& vrijednost, int korak) {
    vrijednost = (vrijednost + MINUTA_DAN + korak) % MINUTA_DAN;
}

void azurirajRasponPloceNaTipkama(bool lijevo, bool desno, bool gore, bool dolje) {
    if (!uEditModu) return;
    if (gore || dolje) {
        sekundarnoPolje = !sekundarnoPolje;
    }
    if (lijevo) {
        if (!sekundarnoPolje) {
            pomakniMinutu(privPlocaPocetak, -1);
        } else {
            pomakniMinutu(privPlocaKraj, -1);
        }
    }
    if (desno) {
        if (!sekundarnoPolje) {
            pomakniMinutu(privPlocaPocetak, 1);
        } else {
            pomakniMinutu(privPlocaKraj, 1);
        }
    }
}

void izdvojiSatMinutu(int minute, int& sat, int& min) {
    sat = (minute / 60) % 24;
    min = minute % 60;
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

void azurirajBrojZvonaNaTipkama(bool lijevo, bool desno) {
    if (!uEditModu) return;
    int broj = static_cast<int>(privBrojZvona);
    if (lijevo) {
        broj = broj <= 1 ? 5 : broj - 1;
    }
    if (desno) {
        broj = broj >= 5 ? 1 : broj + 1;
    }
    privBrojZvona = static_cast<uint8_t>(broj);
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
            unsigned int trajanje = uEditModu ? privTrajanjeCekic : normalizirajTrajanjeCekica(dohvatiTrajanjeImpulsaCekica());
            snprintf(prikazRedak1, sizeof(prikazRedak1), "Cekic %4ums%c", trajanje, uEditModu ? '*' : ' ');
            if (uEditModu) {
                snprintf(prikazRedak2, sizeof(prikazRedak2), "L/R izbor DA=Sp");
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
        case EKRAN_BROJ_ZVONA: {
            uint8_t aktivnih = uEditModu ? privBrojZvona : dohvatiBrojZvona();
            snprintf(prikazRedak1, sizeof(prikazRedak1), "Broj zvona %u%c", aktivnih, uEditModu ? '*' : ' ');
            if (uEditModu) {
                snprintf(prikazRedak2, sizeof(prikazRedak2), "L/R 1-5 DA=Sp ");
            } else {
                snprintf(prikazRedak2, sizeof(prikazRedak2), "DA=Uredi NE=Izl");
            }
            break;
        }
        case EKRAN_PLOCA_RASPON: {
            int odMin = uEditModu ? privPlocaPocetak : dohvatiPocetakPloceMinute();
            int doMin = uEditModu ? privPlocaKraj : dohvatiKrajPloceMinute();
            int odSat, odM, doSat, doM;
            izdvojiSatMinutu(odMin, odSat, odM);
            izdvojiSatMinutu(doMin, doSat, doM);
            snprintf(prikazRedak1, sizeof(prikazRedak1), "Pl %02d:%02d-%02d:%02d%c", odSat, odM, doSat, doM, uEditModu ? '*' : ' ');
            if (uEditModu) {
                if (!sekundarnoPolje) {
                    snprintf(prikazRedak2, sizeof(prikazRedak2), ">Od L/R DA=Sp");
                } else {
                    snprintf(prikazRedak2, sizeof(prikazRedak2), ">Do L/R DA=Sp");
                }
            } else {
                if (odMin == 0 && doMin == 0) {
                    snprintf(prikazRedak2, sizeof(prikazRedak2), "Onemogucena   ");
                } else {
                    snprintf(prikazRedak2, sizeof(prikazRedak2), "DA=Uredi NE=Izl");
                }
            }
            break;
        }
        case EKRAN_PRISTUP_LOZINKA: {
            const char* lozinka = uEditModu ? privPristupLozinka : dohvatiPristupnuLozinku();
            snprintf(prikazRedak1, sizeof(prikazRedak1), "Pristup %.8s", lozinka);
            if (uEditModu) {
                snprintf(prikazRedak2, sizeof(prikazRedak2), "L/R poz G/D zn");
            } else {
                snprintf(prikazRedak2, sizeof(prikazRedak2), "DA=Uredi NE=Izl");
            }
            break;
        }
        case EKRAN_WIFI_SSID: {
            const char* ssid = uEditModu ? privWifiSsid : dohvatiWifiSsid();
            snprintf(prikazRedak1, sizeof(prikazRedak1), "SSID %.10s", ssid);
            if (uEditModu) {
                snprintf(prikazRedak2, sizeof(prikazRedak2), "L/R poz G/D zn");
            } else {
                snprintf(prikazRedak2, sizeof(prikazRedak2), "DA=Uredi NE=Izl");
            }
            break;
        }
        case EKRAN_WIFI_LOZINKA: {
            const char* lozinka = uEditModu ? privWifiLozinka : dohvatiWifiLozinku();
            char prikazLozinke[11];
            strncpy(prikazLozinke, lozinka, sizeof(prikazLozinke) - 1);
            prikazLozinke[sizeof(prikazLozinke) - 1] = '\0';
            if (!uEditModu) {
                for (size_t i = 0; i < strlen(prikazLozinke); ++i) {
                    prikazLozinke[i] = '*';
                }
            }
            snprintf(prikazRedak1, sizeof(prikazRedak1), "WiFi loz %.7s", prikazLozinke);
            if (uEditModu) {
                snprintf(prikazRedak2, sizeof(prikazRedak2), "L/R poz G/D zn");
            } else {
                snprintf(prikazRedak2, sizeof(prikazRedak2), "DA=Uredi NE=Izl");
            }
            break;
        }
        case EKRAN_MREZA_DHCP: {
            bool dhcp = uEditModu ? privDhcp : koristiDhcpMreza();
            snprintf(prikazRedak1, sizeof(prikazRedak1), "DHCP %s%c", dhcp ? "DA" : "NE", uEditModu ? '*' : ' ');
            if (uEditModu) {
                snprintf(prikazRedak2, sizeof(prikazRedak2), "L/R promjena DA");
            } else {
                snprintf(prikazRedak2, sizeof(prikazRedak2), "DA=Uredi NE=Izl");
            }
            break;
        }
        case EKRAN_MREZA_IP: {
            const char* ip = uEditModu ? privStatickaIp : dohvatiStatickuIP();
            snprintf(prikazRedak1, sizeof(prikazRedak1), "%s", ip);
            if (uEditModu) {
                prikazRedak1[sizeof(prikazRedak1) - 2] = '*';
                snprintf(prikazRedak2, sizeof(prikazRedak2), "L/R poz G/D br");
            } else if (koristiDhcpMreza()) {
                snprintf(prikazRedak2, sizeof(prikazRedak2), "DHCP ukljucen ");
            } else {
                snprintf(prikazRedak2, sizeof(prikazRedak2), "Stat IP DA=Uredi");
            }
            break;
        }
        case EKRAN_MREZA_MASKA: {
            const char* maska = uEditModu ? privMreznaMaska : dohvatiMreznuMasku();
            snprintf(prikazRedak1, sizeof(prikazRedak1), "%s", maska);
            if (uEditModu) {
                prikazRedak1[sizeof(prikazRedak1) - 2] = '*';
                snprintf(prikazRedak2, sizeof(prikazRedak2), "L/R poz G/D br");
            } else if (koristiDhcpMreza()) {
                snprintf(prikazRedak2, sizeof(prikazRedak2), "DHCP ukljucen ");
            } else {
                snprintf(prikazRedak2, sizeof(prikazRedak2), "Maska DA=Uredi");
            }
            break;
        }
        case EKRAN_MREZA_GATEWAY: {
            const char* gateway = uEditModu ? privGateway : dohvatiZadaniGateway();
            snprintf(prikazRedak1, sizeof(prikazRedak1), "%s", gateway);
            if (uEditModu) {
                prikazRedak1[sizeof(prikazRedak1) - 2] = '*';
                snprintf(prikazRedak2, sizeof(prikazRedak2), "L/R poz G/D br");
            } else if (koristiDhcpMreza()) {
                snprintf(prikazRedak2, sizeof(prikazRedak2), "DHCP ukljucen ");
            } else {
                snprintf(prikazRedak2, sizeof(prikazRedak2), "Gateway DA=Uredi");
            }
            break;
        }
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
        case EKRAN_BROJ_ZVONA:
            azurirajBrojZvonaNaTipkama(lijevo, desno);
            break;
        case EKRAN_PLOCA_RASPON:
            azurirajRasponPloceNaTipkama(lijevo, desno, gore, dolje);
            break;
        case EKRAN_PRISTUP_LOZINKA:
            azurirajTekstualnoPoljeNaTipkama(privPristupLozinka, sizeof(privPristupLozinka), lijevo, desno, gore, dolje);
            break;
        case EKRAN_WIFI_SSID:
            azurirajTekstualnoPoljeNaTipkama(privWifiSsid, sizeof(privWifiSsid), lijevo, desno, gore, dolje);
            break;
        case EKRAN_WIFI_LOZINKA:
            azurirajTekstualnoPoljeNaTipkama(privWifiLozinka, sizeof(privWifiLozinka), lijevo, desno, gore, dolje);
            break;
        case EKRAN_MREZA_DHCP:
            azurirajDhcpNaTipkama(lijevo, desno, gore, dolje);
            break;
        case EKRAN_MREZA_IP:
            azurirajIPv4NaTipkama(privStatickaIp, lijevo, desno, gore, dolje);
            break;
        case EKRAN_MREZA_MASKA:
            azurirajIPv4NaTipkama(privMreznaMaska, lijevo, desno, gore, dolje);
            break;
        case EKRAN_MREZA_GATEWAY:
            azurirajIPv4NaTipkama(privGateway, lijevo, desno, gore, dolje);
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
