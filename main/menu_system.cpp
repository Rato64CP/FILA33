// menu_system.cpp - Comprehensive 6-key LCD menu system with state management
#include <Arduino.h>
#include <RTClib.h>
#include <Wire.h>
#include <avr/pgmspace.h>
#include <string.h>
#include <stdio.h>
#include "menu_system.h"
#include "lcd_display.h"
#include "time_glob.h"
#include "kazaljke_sata.h"
#include "okretna_ploca.h"
#include "zvonjenje.h"
#include "otkucavanje.h"
#include "postavke.h"
#include "pc_serial.h"
#include "esp_serial.h"
#include "dcf_sync.h"
#include "debouncing.h"
#include "podesavanja_piny.h"
#include "sunceva_automatika.h"

// ==================== STATE MACHINE ====================

static MenuState trenutnoStanje = MENU_STATE_DISPLAY_TIME;
static unsigned long zadnjaAktivnost = 0;
static const unsigned long TIMEOUT_MENIJA_MS = 30000; // Auto-povratak na sat nakon 30 s

// ==================== MENU NAVIGATION ====================

static int odabraniIndex = 0;
static const int BROJ_STAVKI_GLAVNI_MENU = 3;
static const char TEKST_GLAVNI_POSTAVKE[] PROGMEM = "Postavke";
static const char TEKST_GLAVNI_INFORMACIJE[] PROGMEM = "Informacije";
static const char TEKST_GLAVNI_POVRATAK[] PROGMEM = "Povratak";
static const char* const stavkeGlavnogMenuja[] PROGMEM = {
  TEKST_GLAVNI_POSTAVKE,
  TEKST_GLAVNI_INFORMACIJE,
  TEKST_GLAVNI_POVRATAK
};

static const int BROJ_STAVKI_POSTAVKI = 9;
static const char TEKST_POSTAVKE_MATICNI_SAT[] PROGMEM = "Maticni sat";
static const char TEKST_POSTAVKE_KAZALJKE[] PROGMEM = "Kazaljke";
static const char TEKST_POSTAVKE_PLOCA[] PROGMEM = "Okretna ploca";
static const char TEKST_POSTAVKE_MREZA[] PROGMEM = "Mreza";
static const char TEKST_POSTAVKE_TIHI_SATI[] PROGMEM = "Tihi sati";
static const char TEKST_POSTAVKE_CAVLI[] PROGMEM = "Cavli";
static const char TEKST_POSTAVKE_SUNCE[] PROGMEM = "Sunce";
static const char TEKST_POSTAVKE_SUSTAV[] PROGMEM = "Sustav";
static const char TEKST_POSTAVKE_POVRATAK[] PROGMEM = "Povratak";
static const char* const stavkePostavki[] PROGMEM = {
  TEKST_POSTAVKE_MATICNI_SAT,
  TEKST_POSTAVKE_KAZALJKE,
  TEKST_POSTAVKE_PLOCA,
  TEKST_POSTAVKE_MREZA,
  TEKST_POSTAVKE_TIHI_SATI,
  TEKST_POSTAVKE_CAVLI,
  TEKST_POSTAVKE_SUNCE,
  TEKST_POSTAVKE_SUSTAV,
  TEKST_POSTAVKE_POVRATAK
};
static const int INDEX_POSTAVKE_MATICNI_SAT = 0;
static const int INDEX_POSTAVKE_KAZALJKE = 1;
static const int INDEX_POSTAVKE_PLOCA = 2;
static const int INDEX_POSTAVKE_MREZA = 3;
static const int INDEX_POSTAVKE_TIHI_SATI = 4;
static const int INDEX_POSTAVKE_CAVLI = 5;
static const int INDEX_POSTAVKE_SUNCE = 6;
static const int INDEX_POSTAVKE_SUSTAV = 7;
static const int INDEX_POSTAVKE_POVRATAK = 8;

static const int BROJ_STAVKI_MATICNOG_SATA = 5;
static const char TEKST_MATICNI_RUCNO[] PROGMEM = "Rucno namjesti";
static const char TEKST_MATICNI_DCF[] PROGMEM = "DCF postavke";
static const char TEKST_MATICNI_NTP[] PROGMEM = "NTP postavke";
static const char TEKST_MATICNI_OTKUCAJ[] PROGMEM = "Mod otkuc.";
static const char TEKST_MATICNI_POVRATAK[] PROGMEM = "Povratak";
static const char* const stavkeMaticnogSata[] PROGMEM = {
  TEKST_MATICNI_RUCNO,
  TEKST_MATICNI_DCF,
  TEKST_MATICNI_NTP,
  TEKST_MATICNI_OTKUCAJ,
  TEKST_MATICNI_POVRATAK
};

static const int BROJ_STAVKI_KAZALJKI = 3;
static const char TEKST_KAZALJKE_NAMJESTANJE[] PROGMEM = "Namjestanje";
static const char TEKST_KAZALJKE_UGRADENE[] PROGMEM = "Ugradjene";
static const char TEKST_KAZALJKE_POVRATAK[] PROGMEM = "Povratak";
static const char* const stavkeKazaljki[] PROGMEM = {
  TEKST_KAZALJKE_NAMJESTANJE,
  TEKST_KAZALJKE_UGRADENE,
  TEKST_KAZALJKE_POVRATAK
};

static const int BROJ_STAVKI_PLOCE = 3;
static const char TEKST_PLOCA_AKTIVNA[] PROGMEM = "ON/OFF";
static const char TEKST_PLOCA_NAMJESTANJE[] PROGMEM = "Namjestanje";
static const char TEKST_PLOCA_POVRATAK[] PROGMEM = "Povratak";
static const char* const stavkePloce[] PROGMEM = {
  TEKST_PLOCA_AKTIVNA,
  TEKST_PLOCA_NAMJESTANJE,
  TEKST_PLOCA_POVRATAK
};

static const int BROJ_STAVKI_MREZE = 3;
static const char TEKST_MREZA_WIFI[] PROGMEM = "WiFi";
static const char TEKST_MREZA_WIFI_IP[] PROGMEM = "WiFi IP";
static const char TEKST_MREZA_POVRATAK[] PROGMEM = "Povratak";
static const char* const stavkeMreze[] PROGMEM = {
  TEKST_MREZA_WIFI,
  TEKST_MREZA_WIFI_IP,
  TEKST_MREZA_POVRATAK
};

static const int BROJ_STAVKI_SUSTAVA = 2;
static const char TEKST_SUSTAV_LCD[] PROGMEM = "LCD svjetlo";
static const char TEKST_SUSTAV_POVRATAK[] PROGMEM = "Povratak";
static const char* const stavkeSustava[] PROGMEM = {
  TEKST_SUSTAV_LCD,
  TEKST_SUSTAV_POVRATAK
};

static const int BROJ_STAVKI_MODA = 3;
static const char TEKST_MOD_OPCIJA1[] PROGMEM = "1 Klasika";
static const char TEKST_MOD_OPCIJA2[] PROGMEM = "2 Kvartalno";
static const char TEKST_MOD_POVRATAK[] PROGMEM = "Povratak";
static const char* const stavkeModa[] PROGMEM = {
  TEKST_MOD_OPCIJA1,
  TEKST_MOD_OPCIJA2,
  TEKST_MOD_POVRATAK
};

// ==================== CONFIRMATION DIALOG ====================

static bool zadnja_izboru_je_da = true;
static char porukaZaKonfirmaciju[17] = "";
static void (*funkcijaNaDA)() = NULL;
static MenuState stanjePovratkaKonfirmacije = MENU_STATE_DISPLAY_TIME;

// ==================== HAND CORRECTION ====================

static int korektnaMinuta = 0;
static int korektniSat = 12;
static int faza_korekcije = 0; // 0 = hours, 1 = minutes
static int odabraniIndexKazaljke = 0;
static MenuState stanjePovratkaKorekcijeRuku = MENU_STATE_MAIN_MENU;

// ==================== TIME ADJUSTMENT ====================

static int privremeniSat = 0;
static int privremenaMinuta = 0;
static int faza_vremena = 0; // 0 = sat, 1 = minuta, 2 = spremi

// ==================== MATICNI SAT / DCF / NTP ====================

static int dcfStrana = 0;
static int ntpStrana = 0;
static bool ntpOmogucenUredjivanje = true;

// ==================== OKRETNA PLOCA ====================

static int plocaSatUredjivanje = 5;
static int plocaMinutaUredjivanje = 0;

static void ucitajVrijemePloceZaUredjivanjeIzPozicije(int pozicija) {
  int sat24 = 0;
  int minuta = 0;
  if (pretvoriPozicijuPloceUVrijeme(constrain(pozicija, 0, 63), sat24, minuta)) {
    plocaSatUredjivanje = sat24;
    plocaMinutaUredjivanje = minuta;
    return;
  }

  plocaSatUredjivanje = 5;
  plocaMinutaUredjivanje = 14;
}

static int dohvatiPozicijuPloceIzUredjivanja() {
  int pozicija = 0;
  if (pretvoriVrijemeUPozicijuPloce(plocaSatUredjivanje, plocaMinutaUredjivanje, pozicija)) {
    return constrain(pozicija, 0, 63);
  }
  return constrain(dohvatiPozicijuPloce(), 0, 63);
}

static void pomakniUredjivanjePloceZaPozicije(int deltaPozicija) {
  int pozicija = dohvatiPozicijuPloceIzUredjivanja();
  pozicija = (pozicija + deltaPozicija) % 64;
  if (pozicija < 0) {
    pozicija += 64;
  }
  ucitajVrijemePloceZaUredjivanjeIzPozicije(pozicija);
}
static int faza_ploce = 0; // 0 = sat, 1 = minuta, 2 = spremi

// ==================== QUIET HOURS ADJUSTMENT ====================

static int tihiSatOd = 22;
static int tihiSatDo = 6;
static int faza_tihih_sati = 0; // 0 = OD, 1 = DO

static int trajanjeZvonaRdMin = 2;
static int trajanjeZvonaNedMin = 3;
static int trajanjeSlavljenjaMin = 2;
static int odgodaSlavljenjaMin = 0;
static int brojMjestaZaCavleUredjivanje = 5;
static int brojZvonaUredjivanje = 2;
static uint8_t cavliRadniUredjivanje[4] = {1, 2, 0, 0};
static uint8_t cavliNedjeljaUredjivanje[4] = {3, 4, 0, 0};
static int faza_postavki_cavala = 0;

static const int BROJ_STRANA_SUNCE = 9;
static const int SUNCE_STRANA_JUTRO_ON = 0;
static const int SUNCE_STRANA_JUTRO_ZVONO = 1;
static const int SUNCE_STRANA_JUTRO_ODGODA = 2;
static const int SUNCE_STRANA_PODNE_ON = 3;
static const int SUNCE_STRANA_PODNE_ZVONO = 4;
static const int SUNCE_STRANA_VECER_ON = 5;
static const int SUNCE_STRANA_VECER_ZVONO = 6;
static const int SUNCE_STRANA_VECER_ODGODA = 7;
static const int SUNCE_STRANA_SPREMI = 8;
static int sunceStrana = 0;
static bool sunceOmogucenoUredjivanje[SUNCEVI_DOGADAJ_BROJ] = {false, false, false};
static uint8_t sunceZvonoUredjivanje[SUNCEVI_DOGADAJ_BROJ] = {1, 1, 1};
static int sunceOdgodaUredjivanje[SUNCEVI_DOGADAJ_BROJ] = {0, 0, 0};

static int infoStrana = 0;
static const int BROJ_INFO_STRANA = 3;
static int wifiInfoStrana = 0;
static const char SKUP_ZNAKOVA_UREDJIVANJA[] PROGMEM =
  " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-.,!@#";
static bool sinkUredjivanjeAktivno = false;
static int sinkKursor = 0;
static bool dcfOmogucenUredjivanje = true;
static char ntpServerUredjivanje[40] = "";

// ==================== I2C ADDRESS DETECTION ====================

static void otkrijI2CAdrese() {
  Wire.begin();
  posaljiPCLog(F("Scanning I2C addresses..."));

  int dostupnihAdresi = 0;
  for (int adresa = 1; adresa < 127; adresa++) {
    Wire.beginTransmission(adresa);
    int greska = Wire.endTransmission();

    if (greska == 0) {
      char log[48];
      const char* opis = "";

      // Identify common devices
      if (adresa == 0x27 || adresa == 0x3F) {
        opis = " (LCD I2C)";
      } else if (adresa == 0x68) {
        opis = " (DS3231 RTC)";
      } else if (adresa == 0x57) {
        opis = " (24C32 EEPROM)";
      }

      snprintf(log, sizeof(log), "I2C uredjaj na adresi: 0x%X%s", adresa, opis);
      posaljiPCLog(log);
      dostupnihAdresi++;
    }
  }

  char logSummary[32];
  snprintf(logSummary, sizeof(logSummary), "Pronadjeno I2C uredjaja: %d", dostupnihAdresi);
  posaljiPCLog(logSummary);
}

static int dohvatiSinkMaxDuljinu() {
  return 39;
}

static void ucitajTekstIzProgmem(const char* const* niz, int indeks, char* odrediste, size_t velicina) {
  strncpy_P(odrediste, reinterpret_cast<PGM_P>(pgm_read_ptr(&niz[indeks])), velicina - 1);
  odrediste[velicina - 1] = '\0';
}

static void kopirajLiteralIzFlash(char* odrediste, size_t velicina, PGM_P literal) {
  strncpy_P(odrediste, literal, velicina - 1);
  odrediste[velicina - 1] = '\0';
}

static int pretvoriMinuteUKazaljkeSat12h(int minutaKazaljki) {
  const int sat24 = ((minutaKazaljki / 60) % 12 + 12) % 12;
  return (sat24 == 0) ? 12 : sat24;
}

static void pokreniNamjestanjeKazaljki(MenuState povratnoStanje) {
  if (!imaKazaljkeSata()) {
    posaljiPCLog(F("Kazaljke nisu ugradjene - namjestanje nije dostupno"));
    return;
  }

  const int memoriraneMinute = dohvatiMemoriraneKazaljkeMinuta();
  korektniSat = pretvoriMinuteUKazaljkeSat12h(memoriraneMinute);
  korektnaMinuta = ((memoriraneMinute % 60) + 60) % 60;
  faza_korekcije = 0;
  stanjePovratkaKorekcijeRuku = povratnoStanje;
  postaviRucnuBlokaduKazaljki(true);
  trenutnoStanje = MENU_STATE_HAND_CORRECTION;
  posaljiPCLog(F("Ulazak u namjestanje kazaljki"));
}

static void zavrsiNamjestanjeKazaljki(bool potvrdiPromjenu) {
  if (potvrdiPromjenu) {
    const int satKazaljke = (korektniSat % 12);
    postaviRucnuPozicijuKazaljki(satKazaljke, korektnaMinuta);

    char log[56];
    snprintf(log, sizeof(log), "Namjestanje kazaljki: %02d:%02d", korektniSat, korektnaMinuta);
    posaljiPCLog(log);
  } else {
    posaljiPCLog(F("Namjestanje kazaljki: odustajanje"));
  }

  postaviRucnuBlokaduKazaljki(false);
  faza_korekcije = 0;
  trenutnoStanje = potvrdiPromjenu ? MENU_STATE_DISPLAY_TIME : stanjePovratkaKorekcijeRuku;
}

static int dohvatiBrojZnakovaZaUredjivanje() {
  return static_cast<int>(sizeof(SKUP_ZNAKOVA_UREDJIVANJA) - 1);
}

static char dohvatiZnakZaUredjivanje(int indeks) {
  return static_cast<char>(pgm_read_byte(&SKUP_ZNAKOVA_UREDJIVANJA[indeks]));
}

static int pronadiIndeksZnaka(char znak) {
  const int brojZnakova = dohvatiBrojZnakovaZaUredjivanje();
  for (int i = 0; i < brojZnakova; ++i) {
    if (dohvatiZnakZaUredjivanje(i) == znak) {
      return i;
    }
  }
  return 0;
}

static void ucitajSinkUredjivanjeIzPostavki() {
  dcfOmogucenUredjivanje = jeDCFOmogucen();
  strncpy(ntpServerUredjivanje, dohvatiNTPServer(), sizeof(ntpServerUredjivanje) - 1);
  ntpServerUredjivanje[sizeof(ntpServerUredjivanje) - 1] = '\0';
}

static void pokreniSinkUredjivanje() {
  sinkKursor = 0;
  sinkUredjivanjeAktivno = true;
}

static void otvoriKonfirmaciju(PGM_P poruka, void (*funkcijaPotvrde)(), MenuState povratnoStanje) {
  kopirajLiteralIzFlash(porukaZaKonfirmaciju, sizeof(porukaZaKonfirmaciju), poruka);
  funkcijaNaDA = funkcijaPotvrde;
  stanjePovratkaKonfirmacije = povratnoStanje;
  zadnja_izboru_je_da = true;
  trenutnoStanje = MENU_STATE_CONFIRMATION;
}

static void zatvoriKonfirmaciju(MenuState sljedeceStanje) {
  funkcijaNaDA = NULL;
  stanjePovratkaKonfirmacije = MENU_STATE_DISPLAY_TIME;
  zadnja_izboru_je_da = true;
  porukaZaKonfirmaciju[0] = '\0';
  trenutnoStanje = sljedeceStanje;
}

static uint8_t ograniceniCavaoZaMeni(int vrijednost) {
  if (vrijednost < 0) return 0;
  if (vrijednost > brojMjestaZaCavleUredjivanje) return static_cast<uint8_t>(brojMjestaZaCavleUredjivanje);
  return static_cast<uint8_t>(vrijednost);
}

static void sanitizirajRasporedCavalaUredjivanje() {
  brojMjestaZaCavleUredjivanje = 5;
  brojZvonaUredjivanje = 2;

  for (uint8_t i = 0; i < 4; ++i) {
    cavliRadniUredjivanje[i] = ograniceniCavaoZaMeni(cavliRadniUredjivanje[i]);
    if (cavliRadniUredjivanje[i] > 4) {
      cavliRadniUredjivanje[i] = 0;
    }
    cavliNedjeljaUredjivanje[i] = ograniceniCavaoZaMeni(cavliNedjeljaUredjivanje[i]);
    if (cavliNedjeljaUredjivanje[i] > 4) {
      cavliNedjeljaUredjivanje[i] = 0;
    }
  }
  for (uint8_t i = 2; i < 4; ++i) {
    cavliRadniUredjivanje[i] = 0;
    cavliNedjeljaUredjivanje[i] = 0;
  }
  odgodaSlavljenjaMin = constrain(odgodaSlavljenjaMin, 0, 10);
}

static void ucitajPostavkeCavalaZaUredjivanje() {
  brojMjestaZaCavleUredjivanje = dohvatiBrojMjestaZaCavle();
  brojZvonaUredjivanje = dohvatiBrojZvona();
  for (uint8_t i = 0; i < 4; ++i) {
    cavliRadniUredjivanje[i] = dohvatiCavaoRadniZaZvono(i + 1);
    cavliNedjeljaUredjivanje[i] = dohvatiCavaoNedjeljaZaZvono(i + 1);
  }
  trajanjeZvonaRdMin = dohvatiTrajanjeZvonjenjaRadniMin();
  trajanjeZvonaNedMin = dohvatiTrajanjeZvonjenjaNedjeljaMin();
  trajanjeSlavljenjaMin = dohvatiTrajanjeSlavljenjaMin();
  odgodaSlavljenjaMin = dohvatiOdgoduSlavljenjaMin();
  sanitizirajRasporedCavalaUredjivanje();
}

static void ucitajTihiPeriodZaUredjivanje() {
  tihiSatOd = dohvatiTihiPeriodOdSata();
  tihiSatDo = dohvatiTihiPeriodDoSata();
  faza_tihih_sati = 0;
}

static void ucitajSunceveDogadajeZaUredjivanje() {
  for (uint8_t i = 0; i < SUNCEVI_DOGADAJ_BROJ; ++i) {
    sunceOmogucenoUredjivanje[i] = jeSuncevDogadajOmogucen(i);
    sunceZvonoUredjivanje[i] = dohvatiZvonoZaSuncevDogadaj(i);
    sunceOdgodaUredjivanje[i] = dohvatiOdgoduSuncevogDogadajaMin(i);
  }
  sunceOdgodaUredjivanje[SUNCEVI_DOGADAJ_PODNE] = 0;
  sunceStrana = SUNCE_STRANA_JUTRO_ON;
}

static uint8_t dohvatiSuncevDogadajZaStranu(int strana) {
  if (strana <= SUNCE_STRANA_JUTRO_ODGODA) {
    return SUNCEVI_DOGADAJ_JUTRO;
  }
  if (strana <= SUNCE_STRANA_PODNE_ZVONO) {
    return SUNCEVI_DOGADAJ_PODNE;
  }
  return SUNCEVI_DOGADAJ_VECER;
}

static void dohvatiNazivSuncevogDogadaja(uint8_t dogadaj, char* odrediste, size_t velicina) {
  if (dogadaj == SUNCEVI_DOGADAJ_JUTRO) {
    snprintf(odrediste, velicina, "Jutro");
  } else if (dogadaj == SUNCEVI_DOGADAJ_PODNE) {
    snprintf(odrediste, velicina, "Podne");
  } else {
    snprintf(odrediste, velicina, "Vecer");
  }
}

static void prilagodiVrijednostCavla(uint8_t* vrijednost, int delta) {
  int novaVrijednost = static_cast<int>(*vrijednost) + delta;
  if (novaVrijednost < 0) novaVrijednost = brojMjestaZaCavleUredjivanje;
  if (novaVrijednost > brojMjestaZaCavleUredjivanje) novaVrijednost = 0;
  *vrijednost = static_cast<uint8_t>(novaVrijednost);
}

static void potvrdiSpremanjeTihihSati() {
  postaviTihiPeriodSatnihOtkucaja(tihiSatOd, tihiSatDo);
  faza_tihih_sati = 0;
  odabraniIndex = INDEX_POSTAVKE_TIHI_SATI;
  zatvoriKonfirmaciju(MENU_STATE_SETTINGS);
  posaljiPCLog(F("Tihi sati spremljeni"));
}

static void potvrdiSpremanjePostavkiCavala() {
  sanitizirajRasporedCavalaUredjivanje();
  postaviRasporedCavala(static_cast<uint8_t>(brojMjestaZaCavleUredjivanje),
                       static_cast<uint8_t>(brojZvonaUredjivanje),
                       cavliRadniUredjivanje,
                       cavliNedjeljaUredjivanje,
                       5,
                       0);
  postaviPostavkeCavala(trajanjeZvonaRdMin,
                        trajanjeZvonaNedMin,
                        trajanjeSlavljenjaMin,
                        static_cast<uint8_t>(odgodaSlavljenjaMin));
  faza_postavki_cavala = 0;
  odabraniIndex = INDEX_POSTAVKE_CAVLI;
  zatvoriKonfirmaciju(MENU_STATE_SETTINGS);
  posaljiPCLog(F("Postavke cavala spremljene"));
}

static void potvrdiSpremanjeSuncevihDogadaja() {
  for (uint8_t i = 0; i < SUNCEVI_DOGADAJ_BROJ; ++i) {
    postaviSuncevDogadaj(i,
                         sunceOmogucenoUredjivanje[i],
                         sunceZvonoUredjivanje[i],
                         sunceOdgodaUredjivanje[i]);
  }
  odabraniIndex = INDEX_POSTAVKE_SUNCE;
  zatvoriKonfirmaciju(MENU_STATE_SETTINGS);
  posaljiPCLog(F("Suncevi dogadaji spremljeni"));
}

// ==================== MENU DISPLAY FUNCTIONS ====================

static void prikaziGlavniMeni() {
  char redak1[17];
  char redak2[17];
  kopirajLiteralIzFlash(redak1, sizeof(redak1), PSTR("GLAVNI MENI"));
  char stavka[15];
  ucitajTekstIzProgmem(stavkeGlavnogMenuja, odabraniIndex, stavka, sizeof(stavka));
  snprintf(redak2, sizeof(redak2), "> %s", stavka);
  prikaziPoruku(redak1, redak2);
}

static void prikaziPostavkeMenu() {
  char redak1[17];
  char redak2[17];
  kopirajLiteralIzFlash(redak1, sizeof(redak1), PSTR("POSTAVKE"));
  char stavka[15];
  ucitajTekstIzProgmem(stavkePostavki, odabraniIndex, stavka, sizeof(stavka));
  snprintf(redak2, sizeof(redak2), "> %s", stavka);
  prikaziPoruku(redak1, redak2);
}

static void prikaziMaticniSatMenu() {
  char redak1[17];
  char redak2[17];
  kopirajLiteralIzFlash(redak1, sizeof(redak1), PSTR("MATICNI SAT"));
  char stavka[15];
  ucitajTekstIzProgmem(stavkeMaticnogSata, odabraniIndex, stavka, sizeof(stavka));
  snprintf(redak2, sizeof(redak2), "> %s", stavka);
  prikaziPoruku(redak1, redak2);
}

static void prikaziMrezuMenu() {
  char redak1[17];
  char redak2[17];
  kopirajLiteralIzFlash(redak1, sizeof(redak1), PSTR("MREZA"));
  if (odabraniIndex == 0) {
    snprintf(redak2, sizeof(redak2), "> WiFi: %s",
             jeWiFiOmogucen() ? "ON" : "OFF");
  } else {
    char stavka[15];
    ucitajTekstIzProgmem(stavkeMreze, odabraniIndex, stavka, sizeof(stavka));
    snprintf(redak2, sizeof(redak2), "> %s", stavka);
  }
  prikaziPoruku(redak1, redak2);
}

static void prikaziSustavMenu() {
  char redak1[17];
  char redak2[17];
  kopirajLiteralIzFlash(redak1, sizeof(redak1), PSTR("SUSTAV"));
  if (odabraniIndex == 0) {
    snprintf(redak2, sizeof(redak2), "> LCD: %s",
             jeLCDPozadinskoOsvjetljenjeUkljuceno() ? "ON" : "OFF");
  } else {
    kopirajLiteralIzFlash(redak2, sizeof(redak2), PSTR("> Povratak"));
  }
  prikaziPoruku(redak1, redak2);
}

static void prikaziPlocaMenu() {
  char redak1[17];
  char redak2[17];
  kopirajLiteralIzFlash(redak1, sizeof(redak1), PSTR("OKRETNA PLOCA"));
  if (odabraniIndex == 0) {
    snprintf(redak2, sizeof(redak2), "> Aktivna:%s",
             jePlocaKonfigurirana() ? "ON" : "OFF");
  } else {
    char stavka[15];
    ucitajTekstIzProgmem(stavkePloce, odabraniIndex, stavka, sizeof(stavka));
    snprintf(redak2, sizeof(redak2), "> %s", stavka);
  }
  prikaziPoruku(redak1, redak2);
}

static void prikaziDcfConfig() {
  char redak1[17];
  char redak2[17];
  if (dcfStrana == 0) {
    kopirajLiteralIzFlash(redak1, sizeof(redak1), PSTR("DCF aktivan"));
    if (dcfOmogucenUredjivanje) {
      kopirajLiteralIzFlash(redak2, sizeof(redak2), PSTR("ON RIGHT=Start"));
    } else {
      kopirajLiteralIzFlash(redak2, sizeof(redak2), PSTR("OFF [UP/DOWN]"));
    }
  } else {
    kopirajLiteralIzFlash(redak1, sizeof(redak1), PSTR("Pokreni DCF"));
    snprintf(redak2, sizeof(redak2), "%s [SEL]", jeDCFSinkronizacijaUTijeku() ? "U TIJEKU" : "START");
  }
  prikaziPoruku(redak1, redak2);
}

static void prikaziNtpConfig() {
  char redak1[17];
  char redak2[17];
  if (ntpStrana == 0) {
    kopirajLiteralIzFlash(redak1, sizeof(redak1), PSTR("NTP aktivan"));
    if (ntpOmogucenUredjivanje) {
      kopirajLiteralIzFlash(redak2, sizeof(redak2), PSTR("ON RIGHT=Dalje"));
    } else {
      kopirajLiteralIzFlash(redak2, sizeof(redak2), PSTR("OFF [UP/DOWN]"));
    }
  } else if (ntpStrana == 1) {
    kopirajLiteralIzFlash(redak1, sizeof(redak1), PSTR("Pokreni NTP"));
    kopirajLiteralIzFlash(redak2, sizeof(redak2), PSTR("SEL=Zahtjev"));
  } else {
    kopirajLiteralIzFlash(redak1, sizeof(redak1), PSTR("NTP server"));
    snprintf(redak2, sizeof(redak2), "%.16s", ntpServerUredjivanje);
  }
  prikaziPoruku(redak1, redak2);
}

static void prikaziKazaljkeMenu() {
  char redak1[17];
  char redak2[17];
  kopirajLiteralIzFlash(redak1, sizeof(redak1), PSTR("KAZALJKE"));

  if (odabraniIndexKazaljke == 1) {
    snprintf(redak2, sizeof(redak2), "> Ugradj: %s", imaKazaljkeSata() ? "DA" : "NE");
  } else {
    char stavka[15];
    ucitajTekstIzProgmem(stavkeKazaljki, odabraniIndexKazaljke, stavka, sizeof(stavka));
    snprintf(redak2, sizeof(redak2), "> %s", stavka);
  }
  prikaziPoruku(redak1, redak2);
}

static void prikaziIzbiraModaZvona() {
  char redak1[17];
  char redak2[17];
  kopirajLiteralIzFlash(redak1, sizeof(redak1), PSTR("MOD OTKUC."));
  char stavka[15];
  ucitajTekstIzProgmem(stavkeModa, odabraniIndex, stavka, sizeof(stavka));
  snprintf(redak2, sizeof(redak2), "> %s", stavka);
  prikaziPoruku(redak1, redak2);
}

static void prikaziKorekcijuRuku() {
  char redak1[17];
  char redak2[17];

  kopirajLiteralIzFlash(redak1, sizeof(redak1), PSTR("Trenutna poz."));
  if (faza_korekcije == 0) {
    snprintf(redak2, sizeof(redak2), ">%02d<:%02d", korektniSat, korektnaMinuta);
  } else {
    snprintf(redak2, sizeof(redak2), "%02d:>%02d<", korektniSat, korektnaMinuta);
  }
  prikaziPoruku(redak1, redak2);
}

static void prikaziPrilagodbanjeVremena() {
  char redak1[17];
  char redak2[17];

  snprintf(redak1, sizeof(redak1), "Vrijeme %02d:%02d", privremeniSat, privremenaMinuta);
  if (faza_vremena == 0) {
    kopirajLiteralIzFlash(redak2, sizeof(redak2), PSTR(">SAT< MIN SPREM"));
  } else if (faza_vremena == 1) {
    kopirajLiteralIzFlash(redak2, sizeof(redak2), PSTR(" SAT >MIN<SPREM"));
  } else {
    kopirajLiteralIzFlash(redak2, sizeof(redak2), PSTR(" SAT  MIN >OK<"));
  }
  prikaziPoruku(redak1, redak2);
}

static void prikaziPodesavanjePloceNovo() {
  char redak1[17];
  char redak2[17];

  snprintf(redak1, sizeof(redak1), "Ploca %02d:%02d", plocaSatUredjivanje, plocaMinutaUredjivanje);
  if (faza_ploce == 0) {
    kopirajLiteralIzFlash(redak2, sizeof(redak2), PSTR(">SAT< MIN SPREM"));
  } else if (faza_ploce == 1) {
    kopirajLiteralIzFlash(redak2, sizeof(redak2), PSTR(" SAT >MIN<SPREM"));
  } else {
    kopirajLiteralIzFlash(redak2, sizeof(redak2), PSTR(" SAT  MIN >OK<"));
  }
  prikaziPoruku(redak1, redak2);
}

static void prikaziPodesavanjeTihihSati() {
  char redak1[17];
  char redak2[17];

  if (faza_tihih_sati == 0) {
    kopirajLiteralIzFlash(redak1, sizeof(redak1), PSTR("Tihi sati: OD"));
  } else {
    kopirajLiteralIzFlash(redak1, sizeof(redak1), PSTR("Tihi sati: DO"));
  }

  snprintf(redak2, sizeof(redak2), "OD:%02d DO:%02d", tihiSatOd, tihiSatDo);
  prikaziPoruku(redak1, redak2);
}

static void prikaziPodesavanjeCavala() {
  char redak1[17];
  char redak2[17];

  if (faza_postavki_cavala == 0) {
    kopirajLiteralIzFlash(redak1, sizeof(redak1), PSTR("Mjesta cavala"));
    snprintf(redak2, sizeof(redak2), "%d mjesta fiksno", brojMjestaZaCavleUredjivanje);
  } else if (faza_postavki_cavala == 1) {
    kopirajLiteralIzFlash(redak1, sizeof(redak1), PSTR("Broj zvona"));
    snprintf(redak2, sizeof(redak2), "%d zvona fiksno", brojZvonaUredjivanje);
  } else if (faza_postavki_cavala >= 2 && faza_postavki_cavala <= 3) {
    const int indeks = faza_postavki_cavala - 2;
    snprintf(redak1, sizeof(redak1), "RD zvono %d", indeks + 1);
    snprintf(redak2, sizeof(redak2), "cavao:%02d [SEL]", cavliRadniUredjivanje[indeks]);
  } else if (faza_postavki_cavala >= 4 && faza_postavki_cavala <= 5) {
    const int indeks = faza_postavki_cavala - 4;
    snprintf(redak1, sizeof(redak1), "NED zvono %d", indeks + 1);
    snprintf(redak2, sizeof(redak2), "cavao:%02d [SEL]", cavliNedjeljaUredjivanje[indeks]);
  } else if (faza_postavki_cavala == 6) {
    kopirajLiteralIzFlash(redak1, sizeof(redak1), PSTR("Slavljenje cav"));
    snprintf(redak2, sizeof(redak2), "SL:05 fiksno");
  } else if (faza_postavki_cavala == 7) {
    kopirajLiteralIzFlash(redak1, sizeof(redak1), PSTR("Odgoda slavl"));
    snprintf(redak2, sizeof(redak2), "%d min [SEL]", odgodaSlavljenjaMin);
  } else if (faza_postavki_cavala == 8) {
    kopirajLiteralIzFlash(redak1, sizeof(redak1), PSTR("Zvono RD 1-4m"));
    snprintf(redak2, sizeof(redak2), "RD:%d min [SEL]", trajanjeZvonaRdMin);
  } else if (faza_postavki_cavala == 9) {
    kopirajLiteralIzFlash(redak1, sizeof(redak1), PSTR("Zvono NED 1-4"));
    snprintf(redak2, sizeof(redak2), "NED:%d min [SEL]", trajanjeZvonaNedMin);
  } else if (faza_postavki_cavala == 10) {
    kopirajLiteralIzFlash(redak1, sizeof(redak1), PSTR("Slavljenje 1-4"));
    snprintf(redak2, sizeof(redak2), "SL:%d min [SEL]", trajanjeSlavljenjaMin);
  } else {
    kopirajLiteralIzFlash(redak1, sizeof(redak1), PSTR("Spremi cavle"));
    kopirajLiteralIzFlash(redak2, sizeof(redak2), PSTR("[SEL]"));
  }
  prikaziPoruku(redak1, redak2);
}

static void prikaziSunceveDogadaje() {
  char redak1[17];
  char redak2[17];

  if (sunceStrana == SUNCE_STRANA_SPREMI) {
    kopirajLiteralIzFlash(redak1, sizeof(redak1), PSTR("Spremi sunce"));
    kopirajLiteralIzFlash(redak2, sizeof(redak2), PSTR("[SEL] potvrda"));
    prikaziPoruku(redak1, redak2);
    return;
  }

  const uint8_t dogadaj = dohvatiSuncevDogadajZaStranu(sunceStrana);
  char naziv[7];
  dohvatiNazivSuncevogDogadaja(dogadaj, naziv, sizeof(naziv));

  int minuteDogadaja = 0;
  if (dohvatiDanasnjeVrijemeSuncevogDogadajaMin(dogadaj, minuteDogadaja)) {
    const int sat = minuteDogadaja / 60;
    const int minuta = minuteDogadaja % 60;
    snprintf(redak1, sizeof(redak1), "%s %02d:%02d", naziv, sat, minuta);
  } else {
    snprintf(redak1, sizeof(redak1), "%s --:--", naziv);
  }

  if (sunceStrana == SUNCE_STRANA_JUTRO_ON ||
      sunceStrana == SUNCE_STRANA_PODNE_ON ||
      sunceStrana == SUNCE_STRANA_VECER_ON) {
    snprintf(redak2, sizeof(redak2), "Aktivno:%s",
             sunceOmogucenoUredjivanje[dogadaj] ? "ON" : "OFF");
  } else if (sunceStrana == SUNCE_STRANA_JUTRO_ZVONO ||
             sunceStrana == SUNCE_STRANA_PODNE_ZVONO ||
             sunceStrana == SUNCE_STRANA_VECER_ZVONO) {
    snprintf(redak2, sizeof(redak2), "Zvono:BELL%u",
             static_cast<unsigned>(sunceZvonoUredjivanje[dogadaj]));
  } else {
    snprintf(redak2, sizeof(redak2), "Odg:%+04d min",
             sunceOdgodaUredjivanje[dogadaj]);
  }

  prikaziPoruku(redak1, redak2);
}

static void prikaziWiFiIP() {
  char redak1[17];
  char redak2[17];

  if (wifiInfoStrana == 0) {
    kopirajLiteralIzFlash(redak1, sizeof(redak1), PSTR("WiFi IP [L/R]"));
    const char* ipAdresa = dohvatiESPWiFiLokalnuIP();
    if (ipAdresa != nullptr && ipAdresa[0] != '\0') {
      snprintf(redak2, sizeof(redak2), "%.16s", ipAdresa);
    } else if (jeWiFiPovezanNaESP()) {
      kopirajLiteralIzFlash(redak2, sizeof(redak2), PSTR("Cekam IP..."));
    } else {
      kopirajLiteralIzFlash(redak2, sizeof(redak2), PSTR("Nije dostupna"));
    }
  } else {
    kopirajLiteralIzFlash(redak1, sizeof(redak1), PSTR("WiFi MAC [L/R]"));
    const char* macAdresa = dohvatiESPWiFiMACAdresu();
    if (macAdresa != nullptr && macAdresa[0] != '\0') {
      char kompaktniMac[13];
      int indeks = 0;
      for (size_t i = 0; macAdresa[i] != '\0' && indeks < 12; ++i) {
        if (macAdresa[i] != ':') {
          kompaktniMac[indeks++] = macAdresa[i];
        }
      }
      kompaktniMac[indeks] = '\0';
      snprintf(redak2, sizeof(redak2), "%.16s", kompaktniMac);
    } else if (jeWiFiPovezanNaESP()) {
      kopirajLiteralIzFlash(redak2, sizeof(redak2), PSTR("Cekam MAC..."));
    } else {
      kopirajLiteralIzFlash(redak2, sizeof(redak2), PSTR("Nije dostupna"));
    }
  }
  prikaziPoruku(redak1, redak2);
}

static void prikaziInfoDisplay() {
  char redak1[17];
  char redak2[17];

  if (infoStrana == 0) {
    snprintf(redak1, sizeof(redak1), "RTC:%s SQW:%c",
             jeRTCPouzdan() ? "OK" : "FAIL",
             jeRtcSqwAktivan() ? 'O' : 'F');
    snprintf(redak2, sizeof(redak2), "Izvor:%s", dohvatiOznakuIzvoraVremena());
  } else if (infoStrana == 1) {
    if (imaKazaljkeSata()) {
      int memorKazMin = dohvatiMemoriraneKazaljkeMinuta();
      int satKaz = memorKazMin / 60;
      int minKaz = memorKazMin % 60;

      snprintf(redak1, sizeof(redak1), "Kaz:%d:%02d", satKaz, minKaz);
      kopirajLiteralIzFlash(redak2, sizeof(redak2), PSTR("[LEFT/RIGHT]"));
    } else {
      kopirajLiteralIzFlash(redak1, sizeof(redak1), PSTR("Kazaljke"));
      kopirajLiteralIzFlash(redak2, sizeof(redak2), PSTR("NEMA [L/R]"));
    }
  } else {
    int pozPloca = dohvatiPozicijuPloce();

    snprintf(redak1, sizeof(redak1), "Ploca:%d", pozPloca);
    kopirajLiteralIzFlash(redak2, sizeof(redak2), PSTR("[LEFT/RIGHT]"));
  }

  redak1[sizeof(redak1) - 1] = '\0';
  redak2[sizeof(redak2) - 1] = '\0';
  prikaziPoruku(redak1, redak2);
}

static void prikaziKonfirmaciju() {
  char redak1[17];
  char redak2[17];

  strncpy(redak1, porukaZaKonfirmaciju, sizeof(redak1) - 1);
  redak1[sizeof(redak1) - 1] = '\0';

  if (zadnja_izboru_je_da) {
    kopirajLiteralIzFlash(redak2, sizeof(redak2), PSTR("> DA       NE"));
  } else {
    kopirajLiteralIzFlash(redak2, sizeof(redak2), PSTR("  DA     > NE"));
  }
  prikaziPoruku(redak1, redak2);
}

// ==================== KEY PROCESSING ====================

static void obradiKlucGlavniMeni(KeyEvent event) {
  switch (event) {
    case KEY_UP:
      odabraniIndex = (odabraniIndex - 1 + BROJ_STAVKI_GLAVNI_MENU) % BROJ_STAVKI_GLAVNI_MENU;
      break;
    case KEY_DOWN:
      odabraniIndex = (odabraniIndex + 1) % BROJ_STAVKI_GLAVNI_MENU;
      break;
    case KEY_SELECT:
      if (odabraniIndex == 0) {
        odabraniIndex = 0;
        trenutnoStanje = MENU_STATE_SETTINGS;
        posaljiPCLog(F("Ulazak u postavke"));
      } else if (odabraniIndex == 1) {
        trenutnoStanje = MENU_STATE_INFO_DISPLAY;
        infoStrana = 0;
        posaljiPCLog(F("Ulazak u informacije"));
      } else if (odabraniIndex == 2) {
        povratakNaGlavniPrikaz();
      }
      break;
    case KEY_BACK:
      povratakNaGlavniPrikaz();
      break;
    default:
      break;
  }
}

static void obradiKlucPostavke(KeyEvent event) {
  switch (event) {
    case KEY_UP:
      odabraniIndex = (odabraniIndex - 1 + BROJ_STAVKI_POSTAVKI) % BROJ_STAVKI_POSTAVKI;
      break;
    case KEY_DOWN:
      odabraniIndex = (odabraniIndex + 1) % BROJ_STAVKI_POSTAVKI;
      break;
    case KEY_SELECT:
      if (odabraniIndex == INDEX_POSTAVKE_MATICNI_SAT) {
        odabraniIndex = 0;
        trenutnoStanje = MENU_STATE_CLOCK_SETTINGS;
        posaljiPCLog(F("Ulazak u postavke maticnog sata"));
      } else if (odabraniIndex == INDEX_POSTAVKE_KAZALJKE) {
        odabraniIndexKazaljke = 0;
        trenutnoStanje = MENU_STATE_HAND_SETTINGS;
        posaljiPCLog(F("Ulazak u postavke kazaljki"));
      } else if (odabraniIndex == INDEX_POSTAVKE_PLOCA) {
        odabraniIndex = 0;
        trenutnoStanje = MENU_STATE_PLATE_SETTINGS;
        posaljiPCLog(F("Ulazak u postavke okretne ploce"));
      } else if (odabraniIndex == INDEX_POSTAVKE_MREZA) {
        odabraniIndex = 0;
        trenutnoStanje = MENU_STATE_NETWORK_SETTINGS;
        posaljiPCLog(F("Ulazak u mrezne postavke"));
      } else if (odabraniIndex == INDEX_POSTAVKE_TIHI_SATI) {
        ucitajTihiPeriodZaUredjivanje();
        trenutnoStanje = MENU_STATE_QUIET_HOURS;
        posaljiPCLog(F("Ulazak u postavke tihih sati"));
      } else if (odabraniIndex == INDEX_POSTAVKE_CAVLI) {
        ucitajPostavkeCavalaZaUredjivanje();
        faza_postavki_cavala = 0;
        trenutnoStanje = MENU_STATE_NAIL_SETTINGS;
        posaljiPCLog(F("Ulazak u postavke cavala"));
      } else if (odabraniIndex == INDEX_POSTAVKE_SUNCE) {
        ucitajSunceveDogadajeZaUredjivanje();
        trenutnoStanje = MENU_STATE_SUNCE_SETTINGS;
        posaljiPCLog(F("Ulazak u postavke sunca"));
      } else if (odabraniIndex == INDEX_POSTAVKE_SUSTAV) {
        odabraniIndex = 0;
        trenutnoStanje = MENU_STATE_SYSTEM_SETTINGS;
        posaljiPCLog(F("Ulazak u sustavske postavke"));
      } else if (odabraniIndex == INDEX_POSTAVKE_POVRATAK) {
        odabraniIndex = 0;
        trenutnoStanje = MENU_STATE_MAIN_MENU;
      }
      break;
    case KEY_BACK:
      odabraniIndex = 0;
      trenutnoStanje = MENU_STATE_MAIN_MENU;
      break;
    default:
      break;
  }
}

static void obradiKlucMaticniSat(KeyEvent event) {
  switch (event) {
    case KEY_UP:
      odabraniIndex = (odabraniIndex - 1 + BROJ_STAVKI_MATICNOG_SATA) % BROJ_STAVKI_MATICNOG_SATA;
      break;
    case KEY_DOWN:
      odabraniIndex = (odabraniIndex + 1) % BROJ_STAVKI_MATICNOG_SATA;
      break;
    case KEY_SELECT:
      if (odabraniIndex == 0) {
        const DateTime sada = dohvatiTrenutnoVrijeme();
        privremeniSat = sada.hour();
        privremenaMinuta = sada.minute();
        faza_vremena = 0;
        trenutnoStanje = MENU_STATE_TIME_ADJUST;
      } else if (odabraniIndex == 1) {
        dcfStrana = 0;
        dcfOmogucenUredjivanje = jeDCFOmogucen();
        trenutnoStanje = MENU_STATE_DCF_CONFIG;
      } else if (odabraniIndex == 2) {
        ntpStrana = 0;
        ntpOmogucenUredjivanje = jeNTPOmogucen();
        sinkUredjivanjeAktivno = false;
        ucitajSinkUredjivanjeIzPostavki();
        trenutnoStanje = MENU_STATE_NTP_CONFIG;
      } else if (odabraniIndex == 3) {
        odabraniIndex = constrain(static_cast<int>(dohvatiModOtkucavanja()), 1, 2) - 1;
        trenutnoStanje = MENU_STATE_MODE_SELECT;
        posaljiPCLog(F("Ulazak u odabir moda otkucavanja"));
      } else {
        odabraniIndex = 0;
        trenutnoStanje = MENU_STATE_SETTINGS;
      }
      break;
    case KEY_BACK:
      odabraniIndex = 0;
      trenutnoStanje = MENU_STATE_SETTINGS;
      break;
    default:
      break;
  }
}

static void obradiKlucDcfConfig(KeyEvent event) {
  switch (event) {
    case KEY_UP:
    case KEY_DOWN:
      if (dcfStrana == 0) {
        dcfOmogucenUredjivanje = !dcfOmogucenUredjivanje;
        postaviSinkronizacijskePostavke(dohvatiNTPServer(), dcfOmogucenUredjivanje);
        inicijalizirajDCF();
      }
      break;
    case KEY_LEFT:
      dcfStrana = 0;
      break;
    case KEY_RIGHT:
      if (dcfOmogucenUredjivanje) {
        dcfStrana = 1;
      }
      break;
    case KEY_SELECT:
      if (dcfStrana == 0) {
        dcfOmogucenUredjivanje = !dcfOmogucenUredjivanje;
        postaviSinkronizacijskePostavke(dohvatiNTPServer(), dcfOmogucenUredjivanje);
        inicijalizirajDCF();
      } else {
        pokreniRucniDCFPrijem();
      }
      break;
    case KEY_BACK:
      odabraniIndex = 1;
      trenutnoStanje = MENU_STATE_CLOCK_SETTINGS;
      break;
    default:
      break;
  }
}

static void obradiKlucNtpConfig(KeyEvent event) {
  if (sinkUredjivanjeAktivno) {
    const int brojZnakova = dohvatiBrojZnakovaZaUredjivanje();
    const int maxDuljina = dohvatiSinkMaxDuljinu();

    if (event == KEY_LEFT || event == KEY_RIGHT) {
      const int trenutniIndeks = pronadiIndeksZnaka(ntpServerUredjivanje[sinkKursor]);
      int noviIndeks = trenutniIndeks;
      if (event == KEY_LEFT) {
        noviIndeks = (trenutniIndeks - 1 + brojZnakova) % brojZnakova;
      } else {
        noviIndeks = (trenutniIndeks + 1) % brojZnakova;
      }
      ntpServerUredjivanje[sinkKursor] = dohvatiZnakZaUredjivanje(noviIndeks);
      if (sinkKursor + 1 < maxDuljina && ntpServerUredjivanje[sinkKursor + 1] == '\0') {
        ntpServerUredjivanje[sinkKursor + 1] = '\0';
      }
    } else if (event == KEY_UP) {
      if (sinkKursor > 0) {
        sinkKursor--;
      }
    } else if (event == KEY_DOWN) {
      if (sinkKursor < (maxDuljina - 1)) {
        sinkKursor++;
        if (ntpServerUredjivanje[sinkKursor] == '\0') {
          ntpServerUredjivanje[sinkKursor] = ' ';
          ntpServerUredjivanje[sinkKursor + 1] = '\0';
        }
      }
    } else if (event == KEY_SELECT) {
      while (strlen(ntpServerUredjivanje) > 0 && ntpServerUredjivanje[strlen(ntpServerUredjivanje) - 1] == ' ') {
        ntpServerUredjivanje[strlen(ntpServerUredjivanje) - 1] = '\0';
      }
      postaviSinkronizacijskePostavke(ntpServerUredjivanje, jeDCFOmogucen());
      posaljiNTPPostavkeESP();
      sinkUredjivanjeAktivno = false;
    } else if (event == KEY_BACK) {
      sinkUredjivanjeAktivno = false;
    }
    return;
  }

  switch (event) {
    case KEY_UP:
    case KEY_DOWN:
      if (ntpStrana == 0) {
        ntpOmogucenUredjivanje = !ntpOmogucenUredjivanje;
        postaviNTPOmogucen(ntpOmogucenUredjivanje);
      }
      break;
    case KEY_LEFT:
      if (ntpStrana > 0) {
        ntpStrana--;
      }
      break;
    case KEY_RIGHT:
      if (ntpOmogucenUredjivanje && ntpStrana < 2) {
        ntpStrana++;
      }
      break;
    case KEY_SELECT:
      if (ntpStrana == 0) {
        ntpOmogucenUredjivanje = !ntpOmogucenUredjivanje;
        postaviNTPOmogucen(ntpOmogucenUredjivanje);
      } else if (ntpStrana == 1) {
        posaljiNTPZahtjevESP();
      } else {
        pokreniSinkUredjivanje();
      }
      break;
    case KEY_BACK:
      ntpStrana = 0;
      sinkUredjivanjeAktivno = false;
      odabraniIndex = 2;
      trenutnoStanje = MENU_STATE_CLOCK_SETTINGS;
      break;
    default:
      break;
  }
}

static void obradiKlucPloce(KeyEvent event) {
  switch (event) {
    case KEY_UP:
      odabraniIndex = (odabraniIndex - 1 + BROJ_STAVKI_PLOCE) % BROJ_STAVKI_PLOCE;
      break;
    case KEY_DOWN:
      odabraniIndex = (odabraniIndex + 1) % BROJ_STAVKI_PLOCE;
      break;
    case KEY_SELECT:
      if (odabraniIndex == 0) {
        const bool novoStanje = !jePlocaKonfigurirana();
        postaviKonfiguracijuPloce(novoStanje, dohvatiPocetakPloceMinute(), dohvatiKrajPloceMinute());
      } else if (odabraniIndex == 1) {
        ucitajVrijemePloceZaUredjivanjeIzPozicije(dohvatiPozicijuPloce());
        faza_ploce = 0;
        trenutnoStanje = MENU_STATE_PLATE_ADJUST;
      } else {
        odabraniIndex = 2;
        trenutnoStanje = MENU_STATE_SETTINGS;
      }
      break;
    case KEY_BACK:
      odabraniIndex = 2;
      trenutnoStanje = MENU_STATE_SETTINGS;
      break;
    default:
      break;
  }
}

static void obradiKlucNamjestanjaPloce(KeyEvent event) {
  switch (event) {
    case KEY_UP:
      if (faza_ploce == 0) {
        pomakniUredjivanjePloceZaPozicije(4);
      } else if (faza_ploce == 1) {
        pomakniUredjivanjePloceZaPozicije(1);
      }
      break;
    case KEY_DOWN:
      if (faza_ploce == 0) {
        pomakniUredjivanjePloceZaPozicije(-4);
      } else if (faza_ploce == 1) {
        pomakniUredjivanjePloceZaPozicije(-1);
      }
      break;
    case KEY_LEFT:
      if (faza_ploce > 0) {
        faza_ploce--;
      }
      break;
    case KEY_RIGHT:
      if (faza_ploce < 2) {
        faza_ploce++;
      }
      break;
    case KEY_SELECT:
      if (faza_ploce < 2) {
        faza_ploce++;
      } else {
        postaviTrenutniPolozajPloce(dohvatiPozicijuPloceIzUredjivanja());
        oznaciPlocuKaoSinkroniziranu();
        odabraniIndex = 1;
        trenutnoStanje = MENU_STATE_PLATE_SETTINGS;
      }
      break;
    case KEY_BACK:
      odabraniIndex = 1;
      trenutnoStanje = MENU_STATE_PLATE_SETTINGS;
      break;
    default:
      break;
  }
}

static void obradiKlucMreza(KeyEvent event) {
  switch (event) {
    case KEY_UP:
      odabraniIndex = (odabraniIndex - 1 + BROJ_STAVKI_MREZE) % BROJ_STAVKI_MREZE;
      break;
    case KEY_DOWN:
      odabraniIndex = (odabraniIndex + 1) % BROJ_STAVKI_MREZE;
      break;
    case KEY_SELECT:
      if (odabraniIndex == 0) {
        const bool novoStanje = !jeWiFiOmogucen();
        postaviWiFiOmogucen(novoStanje);
        posaljiWiFiStatusESP();
      } else if (odabraniIndex == 1) {
        wifiInfoStrana = 0;
        trenutnoStanje = MENU_STATE_WIFI_IP_DISPLAY;
      } else {
        odabraniIndex = INDEX_POSTAVKE_MREZA;
        trenutnoStanje = MENU_STATE_SETTINGS;
      }
      break;
    case KEY_BACK:
      odabraniIndex = INDEX_POSTAVKE_MREZA;
      trenutnoStanje = MENU_STATE_SETTINGS;
      break;
    default:
      break;
  }
}

static void obradiKlucWiFiIP(KeyEvent event) {
  switch (event) {
    case KEY_LEFT:
      wifiInfoStrana = (wifiInfoStrana - 1 + 2) % 2;
      break;
    case KEY_RIGHT:
      wifiInfoStrana = (wifiInfoStrana + 1) % 2;
      break;
    case KEY_SELECT:
    case KEY_BACK:
      wifiInfoStrana = 0;
      odabraniIndex = 1;
      trenutnoStanje = MENU_STATE_NETWORK_SETTINGS;
      break;
    default:
      break;
  }
}

static void obradiKlucSustav(KeyEvent event) {
  switch (event) {
    case KEY_UP:
      odabraniIndex = (odabraniIndex - 1 + BROJ_STAVKI_SUSTAVA) % BROJ_STAVKI_SUSTAVA;
      break;
    case KEY_DOWN:
      odabraniIndex = (odabraniIndex + 1) % BROJ_STAVKI_SUSTAVA;
      break;
    case KEY_SELECT:
      if (odabraniIndex == 0) {
        const bool novoStanje = !jeLCDPozadinskoOsvjetljenjeUkljuceno();
        postaviLCDPozadinskoOsvjetljenje(novoStanje);
        primijeniLCDPozadinskoOsvjetljenje(novoStanje);
      } else {
        odabraniIndex = INDEX_POSTAVKE_SUSTAV;
        trenutnoStanje = MENU_STATE_SETTINGS;
      }
      break;
    case KEY_BACK:
      odabraniIndex = INDEX_POSTAVKE_SUSTAV;
      trenutnoStanje = MENU_STATE_SETTINGS;
      break;
    default:
      break;
  }
}

static void obradiKlucTihiSati(KeyEvent event) {
  switch (event) {
    case KEY_UP:
      if (faza_tihih_sati == 0) {
        tihiSatOd = (tihiSatOd + 1) % 24;
      } else {
        tihiSatDo = (tihiSatDo + 1) % 24;
      }
      break;
    case KEY_DOWN:
      if (faza_tihih_sati == 0) {
        tihiSatOd = (tihiSatOd - 1 + 24) % 24;
      } else {
        tihiSatDo = (tihiSatDo - 1 + 24) % 24;
      }
      break;
    case KEY_SELECT:
      if (faza_tihih_sati == 0) {
        faza_tihih_sati = 1;
      } else {
        otvoriKonfirmaciju(PSTR("Spremi tihi sat"), potvrdiSpremanjeTihihSati, MENU_STATE_QUIET_HOURS);
      }
      break;
    case KEY_BACK:
      faza_tihih_sati = 0;
      odabraniIndex = INDEX_POSTAVKE_TIHI_SATI;
      trenutnoStanje = MENU_STATE_SETTINGS;
      posaljiPCLog(F("Tihi sati: odustajanje bez spremanja"));
      break;
    default:
      break;
  }
}

static void obradiKlucPostavkeCavala(KeyEvent event) {
  switch (event) {
    case KEY_UP:
      if (faza_postavki_cavala >= 2 && faza_postavki_cavala <= 3) {
        prilagodiVrijednostCavla(&cavliRadniUredjivanje[faza_postavki_cavala - 2], 1);
      } else if (faza_postavki_cavala >= 4 && faza_postavki_cavala <= 5) {
        prilagodiVrijednostCavla(&cavliNedjeljaUredjivanje[faza_postavki_cavala - 4], 1);
      } else if (faza_postavki_cavala == 7) {
        odgodaSlavljenjaMin = (odgodaSlavljenjaMin + 1) % 11;
      } else if (faza_postavki_cavala == 8) {
        trajanjeZvonaRdMin = (trajanjeZvonaRdMin % 4) + 1;
      } else if (faza_postavki_cavala == 9) {
        trajanjeZvonaNedMin = (trajanjeZvonaNedMin % 4) + 1;
      } else if (faza_postavki_cavala == 10) {
        trajanjeSlavljenjaMin = (trajanjeSlavljenjaMin % 4) + 1;
      }
      sanitizirajRasporedCavalaUredjivanje();
      break;
    case KEY_DOWN:
      if (faza_postavki_cavala >= 2 && faza_postavki_cavala <= 3) {
        prilagodiVrijednostCavla(&cavliRadniUredjivanje[faza_postavki_cavala - 2], -1);
      } else if (faza_postavki_cavala >= 4 && faza_postavki_cavala <= 5) {
        prilagodiVrijednostCavla(&cavliNedjeljaUredjivanje[faza_postavki_cavala - 4], -1);
      } else if (faza_postavki_cavala == 7) {
        odgodaSlavljenjaMin = (odgodaSlavljenjaMin + 10) % 11;
      } else if (faza_postavki_cavala == 8) {
        trajanjeZvonaRdMin = (trajanjeZvonaRdMin - 2 + 4) % 4 + 1;
      } else if (faza_postavki_cavala == 9) {
        trajanjeZvonaNedMin = (trajanjeZvonaNedMin - 2 + 4) % 4 + 1;
      } else if (faza_postavki_cavala == 10) {
        trajanjeSlavljenjaMin = (trajanjeSlavljenjaMin - 2 + 4) % 4 + 1;
      }
      sanitizirajRasporedCavalaUredjivanje();
      break;
    case KEY_SELECT:
      if (faza_postavki_cavala < 11) {
        faza_postavki_cavala++;
      } else {
        otvoriKonfirmaciju(PSTR("Spremi cavle?"), potvrdiSpremanjePostavkiCavala, MENU_STATE_NAIL_SETTINGS);
      }
      break;
    case KEY_BACK:
      faza_postavki_cavala = 0;
      odabraniIndex = INDEX_POSTAVKE_CAVLI;
      trenutnoStanje = MENU_STATE_SETTINGS;
      posaljiPCLog(F("Postavke cavala: odustajanje bez spremanja"));
      break;
    default:
      break;
  }
}

static void obradiKlucSunce(KeyEvent event) {
  if (sunceStrana == SUNCE_STRANA_SPREMI) {
    switch (event) {
      case KEY_LEFT:
        sunceStrana = (sunceStrana - 1 + BROJ_STRANA_SUNCE) % BROJ_STRANA_SUNCE;
        break;
      case KEY_RIGHT:
        sunceStrana = (sunceStrana + 1) % BROJ_STRANA_SUNCE;
        break;
      case KEY_SELECT:
        otvoriKonfirmaciju(PSTR("Spremi sunce?"), potvrdiSpremanjeSuncevihDogadaja, MENU_STATE_SUNCE_SETTINGS);
        break;
      case KEY_BACK:
        sunceStrana = SUNCE_STRANA_JUTRO_ON;
        odabraniIndex = INDEX_POSTAVKE_SUNCE;
        trenutnoStanje = MENU_STATE_SETTINGS;
        break;
      default:
        break;
    }
    return;
  }

  const uint8_t dogadaj = dohvatiSuncevDogadajZaStranu(sunceStrana);
  switch (event) {
    case KEY_LEFT:
      sunceStrana = (sunceStrana - 1 + BROJ_STRANA_SUNCE) % BROJ_STRANA_SUNCE;
      break;
    case KEY_RIGHT:
      sunceStrana = (sunceStrana + 1) % BROJ_STRANA_SUNCE;
      break;
    case KEY_UP:
      if (sunceStrana == SUNCE_STRANA_JUTRO_ON ||
          sunceStrana == SUNCE_STRANA_PODNE_ON ||
          sunceStrana == SUNCE_STRANA_VECER_ON) {
        sunceOmogucenoUredjivanje[dogadaj] = !sunceOmogucenoUredjivanje[dogadaj];
      } else if (sunceStrana == SUNCE_STRANA_JUTRO_ZVONO ||
                 sunceStrana == SUNCE_STRANA_PODNE_ZVONO ||
                 sunceStrana == SUNCE_STRANA_VECER_ZVONO) {
        sunceZvonoUredjivanje[dogadaj] =
            (sunceZvonoUredjivanje[dogadaj] >= 2) ? 1 : (sunceZvonoUredjivanje[dogadaj] + 1);
      } else {
        sunceOdgodaUredjivanje[dogadaj] = constrain(sunceOdgodaUredjivanje[dogadaj] + 1, -180, 180);
      }
      break;
    case KEY_DOWN:
      if (sunceStrana == SUNCE_STRANA_JUTRO_ON ||
          sunceStrana == SUNCE_STRANA_PODNE_ON ||
          sunceStrana == SUNCE_STRANA_VECER_ON) {
        sunceOmogucenoUredjivanje[dogadaj] = !sunceOmogucenoUredjivanje[dogadaj];
      } else if (sunceStrana == SUNCE_STRANA_JUTRO_ZVONO ||
                 sunceStrana == SUNCE_STRANA_PODNE_ZVONO ||
                 sunceStrana == SUNCE_STRANA_VECER_ZVONO) {
        sunceZvonoUredjivanje[dogadaj] =
            (sunceZvonoUredjivanje[dogadaj] <= 1) ? 2 : (sunceZvonoUredjivanje[dogadaj] - 1);
      } else {
        sunceOdgodaUredjivanje[dogadaj] = constrain(sunceOdgodaUredjivanje[dogadaj] - 1, -180, 180);
      }
      break;
    case KEY_SELECT:
      if (sunceStrana == SUNCE_STRANA_JUTRO_ON ||
          sunceStrana == SUNCE_STRANA_PODNE_ON ||
          sunceStrana == SUNCE_STRANA_VECER_ON) {
        sunceOmogucenoUredjivanje[dogadaj] = !sunceOmogucenoUredjivanje[dogadaj];
      } else if (sunceStrana == SUNCE_STRANA_JUTRO_ZVONO ||
                 sunceStrana == SUNCE_STRANA_PODNE_ZVONO ||
                 sunceStrana == SUNCE_STRANA_VECER_ZVONO) {
        sunceZvonoUredjivanje[dogadaj] =
            (sunceZvonoUredjivanje[dogadaj] >= 2) ? 1 : (sunceZvonoUredjivanje[dogadaj] + 1);
      }
      break;
    case KEY_BACK:
      sunceStrana = SUNCE_STRANA_JUTRO_ON;
      odabraniIndex = INDEX_POSTAVKE_SUNCE;
      trenutnoStanje = MENU_STATE_SETTINGS;
      break;
    default:
      break;
  }
}

static void obradiKlucPostavkeKazaljki(KeyEvent event) {
  switch (event) {
    case KEY_UP:
      odabraniIndexKazaljke =
          (odabraniIndexKazaljke - 1 + BROJ_STAVKI_KAZALJKI) % BROJ_STAVKI_KAZALJKI;
      break;
    case KEY_DOWN:
      odabraniIndexKazaljke = (odabraniIndexKazaljke + 1) % BROJ_STAVKI_KAZALJKI;
      break;
    case KEY_SELECT:
      if (odabraniIndexKazaljke == 0) {
        pokreniNamjestanjeKazaljki(MENU_STATE_HAND_SETTINGS);
      } else if (odabraniIndexKazaljke == 1) {
        bool novoStanje = !imaKazaljkeSata();
        postaviImaKazaljkeSata(novoStanje);
        posaljiPCLog(novoStanje ? F("Izbornik: kazaljke postavljene na ON")
                                : F("Izbornik: kazaljke postavljene na OFF"));
      } else {
        odabraniIndex = 1;
        trenutnoStanje = MENU_STATE_SETTINGS;
      }
      break;
    case KEY_BACK:
      odabraniIndex = 1;
      trenutnoStanje = MENU_STATE_SETTINGS;
      break;
    default:
      break;
  }
}

static void obradiKlucKorekcija(KeyEvent event) {
  switch (event) {
    case KEY_UP:
      if (faza_korekcije == 0) {
        korektniSat = (korektniSat >= 12) ? 1 : (korektniSat + 1);
      } else {
        korektnaMinuta = (korektnaMinuta + 1) % 60;
      }
      break;
    case KEY_DOWN:
      if (faza_korekcije == 0) {
        korektniSat = (korektniSat <= 1) ? 12 : (korektniSat - 1);
      } else {
        korektnaMinuta = (korektnaMinuta - 1 + 60) % 60;
      }
      break;
    case KEY_LEFT:
      faza_korekcije = 0;
      break;
    case KEY_RIGHT:
      faza_korekcije = 1;
      break;
    case KEY_SELECT:
      zavrsiNamjestanjeKazaljki(true);
      break;
    case KEY_BACK:
      zavrsiNamjestanjeKazaljki(false);
      break;
    default:
      break;
  }
}

static void obradiKlucPrilagodbanjeVremena(KeyEvent event) {
  switch (event) {
    case KEY_UP:
      if (faza_vremena == 0) {
        privremeniSat = (privremeniSat + 1) % 24;
      } else if (faza_vremena == 1) {
        privremenaMinuta = (privremenaMinuta + 1) % 60;
      }
      break;
    case KEY_DOWN:
      if (faza_vremena == 0) {
        privremeniSat = (privremeniSat - 1 + 24) % 24;
      } else if (faza_vremena == 1) {
        privremenaMinuta = (privremenaMinuta - 1 + 60) % 60;
      }
      break;
    case KEY_LEFT:
      if (faza_vremena > 0) {
        faza_vremena--;
      }
      break;
    case KEY_RIGHT:
      if (faza_vremena < 2) {
        faza_vremena++;
      }
      break;
    case KEY_SELECT:
      if (faza_vremena < 2) {
        faza_vremena++;
      } else {
        const DateTime sada = dohvatiTrenutnoVrijeme();
        azurirajVrijemeRucno(DateTime(
          sada.year(), sada.month(), sada.day(),
          privremeniSat, privremenaMinuta, 0));
        odabraniIndex = 0;
        trenutnoStanje = MENU_STATE_CLOCK_SETTINGS;
      }
      break;
    case KEY_BACK:
      if (faza_vremena > 0) {
        faza_vremena--;
      } else {
        odabraniIndex = 0;
        trenutnoStanje = MENU_STATE_CLOCK_SETTINGS;
      }
      break;
    default:
      break;
  }
}

static void obradiKlucModeSelect(KeyEvent event) {
  switch (event) {
    case KEY_UP:
      odabraniIndex = (odabraniIndex - 1 + BROJ_STAVKI_MODA) % BROJ_STAVKI_MODA;
      break;
    case KEY_DOWN:
      odabraniIndex = (odabraniIndex + 1) % BROJ_STAVKI_MODA;
      break;
    case KEY_SELECT:
      if (odabraniIndex == 0) {
        postaviModOtkucavanja(1);
        posaljiPCLog(F("Mod otkucavanja: odabrana opcija 1"));
      } else if (odabraniIndex == 1) {
        postaviModOtkucavanja(2);
        posaljiPCLog(F("Mod otkucavanja: odabrana opcija 2"));
      } else {
        odabraniIndex = 3;
        trenutnoStanje = MENU_STATE_CLOCK_SETTINGS;
        break;
      }
      odabraniIndex = 3;
      trenutnoStanje = MENU_STATE_CLOCK_SETTINGS;
      break;
    case KEY_BACK:
      odabraniIndex = 3;
      trenutnoStanje = MENU_STATE_CLOCK_SETTINGS;
      break;
    default:
      break;
  }
}

static void obradiKlucInfo(KeyEvent event) {
  switch (event) {
    case KEY_LEFT:
      infoStrana = (infoStrana - 1 + BROJ_INFO_STRANA) % BROJ_INFO_STRANA;
      break;
    case KEY_RIGHT:
      infoStrana = (infoStrana + 1) % BROJ_INFO_STRANA;
      break;
    case KEY_BACK:
      infoStrana = 0;
      odabraniIndex = 1;
      trenutnoStanje = MENU_STATE_MAIN_MENU;
      break;
    default:
      break;
  }
}

static void obradiKlucKonfirmacija(KeyEvent event) {
  switch (event) {
    case KEY_LEFT:
      zadnja_izboru_je_da = true;
      break;
    case KEY_RIGHT:
      zadnja_izboru_je_da = false;
      break;
    case KEY_SELECT:
      if (zadnja_izboru_je_da && funkcijaNaDA != NULL) {
        funkcijaNaDA();
      } else {
        zatvoriKonfirmaciju(stanjePovratkaKonfirmacije);
      }
      break;
    case KEY_BACK:
      zatvoriKonfirmaciju(stanjePovratkaKonfirmacije);
      break;
    default:
      break;
  }
}

// ==================== PUBLIC API ====================

void inicijalizirajMenuSistem() {
  trenutnoStanje = MENU_STATE_DISPLAY_TIME;
  odabraniIndex = 0;
  zadnjaAktivnost = millis();
  zadnja_izboru_je_da = true;
  funkcijaNaDA = NULL;
  stanjePovratkaKonfirmacije = MENU_STATE_DISPLAY_TIME;
  porukaZaKonfirmaciju[0] = '\0';
  infoStrana = 0;
  wifiInfoStrana = 0;
  sinkUredjivanjeAktivno = false;
  sinkKursor = 0;
  ntpServerUredjivanje[0] = '\0';
  dcfOmogucenUredjivanje = jeDCFOmogucen();
  ntpOmogucenUredjivanje = jeNTPOmogucen();
  dcfStrana = 0;
  ntpStrana = 0;
  ucitajVrijemePloceZaUredjivanjeIzPozicije(dohvatiPozicijuPloce());
  faza_ploce = 0;
  ucitajPostavkeCavalaZaUredjivanje();
  ucitajSunceveDogadajeZaUredjivanje();
  faza_postavki_cavala = 0;
  sunceStrana = SUNCE_STRANA_JUTRO_ON;
  odabraniIndexKazaljke = 0;
  stanjePovratkaKorekcijeRuku = MENU_STATE_MAIN_MENU;

  otkrijI2CAdrese();

  posaljiPCLog(F("Menu sistem inicijaliziran"));
}

void upravljajMenuSistemom() {
  if (trenutnoStanje != MENU_STATE_DISPLAY_TIME) {
    unsigned long sadaMs = millis();
    if (sadaMs - zadnjaAktivnost > TIMEOUT_MENIJA_MS) {
      povratakNaGlavniPrikaz();
      return;
    }
  }

  osvjeziLCDZaMeni();
}

void obradiKluc(KeyEvent event) {
  if (event == KEY_NONE) return;

  zadnjaAktivnost = millis();

  switch (trenutnoStanje) {
    case MENU_STATE_DISPLAY_TIME:
      if (event == KEY_SELECT) {
        odabraniIndex = 0;
        trenutnoStanje = MENU_STATE_MAIN_MENU;
        posaljiPCLog(F("Ulazak u glavni meni"));
      }
      break;

    case MENU_STATE_MAIN_MENU:
      obradiKlucGlavniMeni(event);
      break;

    case MENU_STATE_SETTINGS:
      obradiKlucPostavke(event);
      break;

    case MENU_STATE_CLOCK_SETTINGS:
      obradiKlucMaticniSat(event);
      break;

    case MENU_STATE_DCF_CONFIG:
      obradiKlucDcfConfig(event);
      break;

    case MENU_STATE_NTP_CONFIG:
      obradiKlucNtpConfig(event);
      break;

    case MENU_STATE_HAND_SETTINGS:
      obradiKlucPostavkeKazaljki(event);
      break;

    case MENU_STATE_PLATE_SETTINGS:
      obradiKlucPloce(event);
      break;

    case MENU_STATE_PLATE_ADJUST:
      obradiKlucNamjestanjaPloce(event);
      break;

    case MENU_STATE_NETWORK_SETTINGS:
      obradiKlucMreza(event);
      break;

    case MENU_STATE_SYSTEM_SETTINGS:
      obradiKlucSustav(event);
      break;

    case MENU_STATE_HAND_CORRECTION:
      obradiKlucKorekcija(event);
      break;

    case MENU_STATE_TIME_ADJUST:
      obradiKlucPrilagodbanjeVremena(event);
      break;

    case MENU_STATE_QUIET_HOURS:
      obradiKlucTihiSati(event);
      break;

    case MENU_STATE_NAIL_SETTINGS:
      obradiKlucPostavkeCavala(event);
      break;

    case MENU_STATE_SUNCE_SETTINGS:
      obradiKlucSunce(event);
      break;

    case MENU_STATE_MODE_SELECT:
      obradiKlucModeSelect(event);
      break;

    case MENU_STATE_INFO_DISPLAY:
      obradiKlucInfo(event);
      break;

    case MENU_STATE_CONFIRMATION:
      obradiKlucKonfirmacija(event);
      break;

    case MENU_STATE_WIFI_IP_DISPLAY:
      obradiKlucWiFiIP(event);
      break;

    default:
      break;
  }
}

MenuState dohvatiMenuState() {
  return trenutnoStanje;
}

void povratakNaGlavniPrikaz() {
  funkcijaNaDA = NULL;
  stanjePovratkaKonfirmacije = MENU_STATE_DISPLAY_TIME;
  zadnja_izboru_je_da = true;
  porukaZaKonfirmaciju[0] = '\0';
  trenutnoStanje = MENU_STATE_DISPLAY_TIME;
  odabraniIndex = 0;
  odabraniIndexKazaljke = 0;
  stanjePovratkaKorekcijeRuku = MENU_STATE_MAIN_MENU;
  postaviRucnuBlokaduKazaljki(false);
  faza_vremena = 0;
  dcfStrana = 0;
  ntpStrana = 0;
  faza_tihih_sati = 0;
  faza_postavki_cavala = 0;
  sunceStrana = SUNCE_STRANA_JUTRO_ON;
  faza_ploce = 0;
  infoStrana = 0;
  sinkUredjivanjeAktivno = false;
  sinkKursor = 0;
  ntpServerUredjivanje[0] = '\0';
  dcfOmogucenUredjivanje = jeDCFOmogucen();
  ntpOmogucenUredjivanje = jeNTPOmogucen();
  ucitajSunceveDogadajeZaUredjivanje();
  ucitajVrijemePloceZaUredjivanjeIzPozicije(dohvatiPozicijuPloce());
  zadnjaAktivnost = millis();
  posaljiPCLog(F("Povratak na prikaz sata"));
}

void osvjeziLCDZaMeni() {
  switch (trenutnoStanje) {
    case MENU_STATE_DISPLAY_TIME:
      prikaziSat();
      break;
    case MENU_STATE_MAIN_MENU:
      prikaziGlavniMeni();
      break;
    case MENU_STATE_SETTINGS:
      prikaziPostavkeMenu();
      break;
    case MENU_STATE_CLOCK_SETTINGS:
      prikaziMaticniSatMenu();
      break;
    case MENU_STATE_DCF_CONFIG:
      prikaziDcfConfig();
      break;
    case MENU_STATE_NTP_CONFIG:
      prikaziNtpConfig();
      break;
    case MENU_STATE_HAND_SETTINGS:
      prikaziKazaljkeMenu();
      break;
    case MENU_STATE_PLATE_SETTINGS:
      prikaziPlocaMenu();
      break;
    case MENU_STATE_PLATE_ADJUST:
      prikaziPodesavanjePloceNovo();
      break;
    case MENU_STATE_NETWORK_SETTINGS:
      prikaziMrezuMenu();
      break;
    case MENU_STATE_SYSTEM_SETTINGS:
      prikaziSustavMenu();
      break;
    case MENU_STATE_HAND_CORRECTION:
      prikaziKorekcijuRuku();
      break;
    case MENU_STATE_TIME_ADJUST:
      prikaziPrilagodbanjeVremena();
      break;
    case MENU_STATE_QUIET_HOURS:
      prikaziPodesavanjeTihihSati();
      break;
    case MENU_STATE_NAIL_SETTINGS:
      prikaziPodesavanjeCavala();
      break;
    case MENU_STATE_SUNCE_SETTINGS:
      prikaziSunceveDogadaje();
      break;
    case MENU_STATE_MODE_SELECT:
      prikaziIzbiraModaZvona();
      break;
    case MENU_STATE_INFO_DISPLAY:
      prikaziInfoDisplay();
      break;
    case MENU_STATE_CONFIRMATION:
      prikaziKonfirmaciju();
      break;
    case MENU_STATE_WIFI_IP_DISPLAY:
      prikaziWiFiIP();
      break;
    default:
      break;
  }
}
