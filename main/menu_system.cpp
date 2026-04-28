// menu_system.cpp - LCD izbornik toranjskog sata s upravljanjem stanjima
#include <Arduino.h>
#include <RTClib.h>
#include <Wire.h>
#include <avr/pgmspace.h>
#include <string.h>
#include <stdio.h>
#include "menu_system.h"
#include "lcd_display.h"
#include "flash_text_utils.h"
#include "time_glob.h"
#include "kazaljke_sata.h"
#include "okretna_ploca.h"
#include "zvonjenje.h"
#include "otkucavanje.h"
#include "postavke.h"
#include "pc_serial.h"
#include "esp_serial.h"
#include "debouncing.h"
#include "podesavanja_piny.h"
#include "sunceva_automatika.h"

// ==================== STATE MACHINE ====================

static MenuState trenutnoStanje = MENU_STATE_DISPLAY_TIME;
static unsigned long zadnjaAktivnost = 0;
static const unsigned long TIMEOUT_MENIJA_MS = 30000; // Auto-povratak na sat nakon 30 s

// ==================== MENU NAVIGATION ====================

static int odabraniIndex = 0;
static const int BROJ_STAVKI_GLAVNI_MENU = 10;
static const char TEKST_GLAVNI_MATICNI_SAT[] PROGMEM = "Maticni sat";
static const char TEKST_GLAVNI_KAZALJKE[] PROGMEM = "Kazaljke";
static const char TEKST_GLAVNI_PLOCA[] PROGMEM = "Okretna ploca";
static const char TEKST_GLAVNI_MREZA[] PROGMEM = "Mreza";
static const char TEKST_GLAVNI_TIHI_SATI[] PROGMEM = "Otkuc./BAT";
static const char TEKST_GLAVNI_STAPICI[] PROGMEM = "Stapici";
static const char TEKST_GLAVNI_SUNCE[] PROGMEM = "Sunce";
static const char TEKST_GLAVNI_BLAGDANI[] PROGMEM = "Blagdani";
static const char TEKST_GLAVNI_SUSTAV[] PROGMEM = "Sustav";
static const char TEKST_GLAVNI_POVRATAK[] PROGMEM = "Povratak";
static const char* const stavkeGlavnogMenuja[] PROGMEM = {
  TEKST_GLAVNI_MATICNI_SAT,
  TEKST_GLAVNI_KAZALJKE,
  TEKST_GLAVNI_PLOCA,
  TEKST_GLAVNI_MREZA,
  TEKST_GLAVNI_TIHI_SATI,
  TEKST_GLAVNI_STAPICI,
  TEKST_GLAVNI_SUNCE,
  TEKST_GLAVNI_BLAGDANI,
  TEKST_GLAVNI_SUSTAV,
  TEKST_GLAVNI_POVRATAK
};

static const int INDEX_POSTAVKE_MATICNI_SAT = 0;
static const int INDEX_POSTAVKE_KAZALJKE = 1;
static const int INDEX_POSTAVKE_PLOCA = 2;
static const int INDEX_POSTAVKE_MREZA = 3;
static const int INDEX_POSTAVKE_TIHI_SATI = 4;
static const int INDEX_POSTAVKE_STAPICI = 5;
static const int INDEX_POSTAVKE_SUNCE = 6;
static const int INDEX_POSTAVKE_BLAGDANI = 7;
static const int INDEX_POSTAVKE_SUSTAV = 8;
static const int INDEX_POSTAVKE_POVRATAK = 9;

static const int BROJ_STAVKI_MREZE = 3;
static const char TEKST_MREZA_WIFI[] PROGMEM = "WiFi";
static const char TEKST_MREZA_WIFI_IP[] PROGMEM = "WiFi IP";
static const char TEKST_MREZA_POVRATAK[] PROGMEM = "Povratak";
static const char* const stavkeMreze[] PROGMEM = {
  TEKST_MREZA_WIFI,
  TEKST_MREZA_WIFI_IP,
  TEKST_MREZA_POVRATAK
};

static const int BROJ_STAVKI_SUSTAVA = 7;
static const char TEKST_SUSTAV_LCD[] PROGMEM = "LCD svjetlo";
static const char TEKST_SUSTAV_LOGIRANJE[] PROGMEM = "Logiranje";
static const char TEKST_SUSTAV_RS485[] PROGMEM = "RS485";
static const char TEKST_SUSTAV_CEKIC[] PROGMEM = "Impuls cekica";
static const char TEKST_SUSTAV_INERCIJA_Z1[] PROGMEM = "Inercija Z1";
static const char TEKST_SUSTAV_INERCIJA_Z2[] PROGMEM = "Inercija Z2";
static const char TEKST_SUSTAV_POVRATAK[] PROGMEM = "Povratak";
static const char* const stavkeSustava[] PROGMEM = {
  TEKST_SUSTAV_LCD,
  TEKST_SUSTAV_LOGIRANJE,
  TEKST_SUSTAV_RS485,
  TEKST_SUSTAV_CEKIC,
  TEKST_SUSTAV_INERCIJA_Z1,
  TEKST_SUSTAV_INERCIJA_Z2,
  TEKST_SUSTAV_POVRATAK
};

// ==================== HAND CORRECTION ====================

static int korektnaMinuta = 0;
static int korektniSat = 12;
static int faza_korekcije = 0; // 0 = hours, 1 = minutes
static int pretvoriMinuteUKazaljkeSat12h(int minutaKazaljki);
static void resetirajUnosVremena();

static void ucitajKazaljkeZaUredjivanje() {
  const int memoriraneMinute = dohvatiMemoriraneKazaljkeMinuta();
  korektniSat = pretvoriMinuteUKazaljkeSat12h(memoriraneMinute);
  korektnaMinuta = ((memoriraneMinute % 60) + 60) % 60;
  faza_korekcije = 0;
  resetirajUnosVremena();
}

// ==================== TIME ADJUSTMENT ====================

static int privremeniDan = 1;
static int privremeniMjesec = 1;
static int privremenaGodina = 2026;
static int privremeniSat = 0;
static int privremenaMinuta = 0;

// ==================== MATICNI SAT / NTP ====================

static bool ntpOmogucenUredjivanje = true;
static uint8_t faza_postavki_maticnog_sata = 0; // 0 = dan, 1 = mjesec, 2 = godina, 3 = sat, 4 = minuta, 5 = NTP

// ==================== SUSTAV ====================

static bool lcdPozadinskoOsvjetljenjeUredjivanje = true;
static bool logiranjeUredjivanje = true;
static bool rs485Uredjivanje = false;
static unsigned int trajanjeImpulsaCekicaUredjivanje = 150;
static uint8_t inercijaZvona1Uredjivanje = 90;
static uint8_t inercijaZvona2Uredjivanje = 90;

// ==================== OKRETNA PLOCA ====================

static int plocaSatUredjivanje = 5;
static int plocaMinutaUredjivanje = 0;
static bool plocaAktivnaUredjivanje = true;
static int plocaPocetakUredjivanjeMinuta = 4 * 60 + 59;
static int plocaKrajUredjivanjeMinuta = 20 * 60 + 44;
static uint8_t faza_postavki_ploce = 3; // 0 = aktivna, 1 = pocetak, 2 = kraj, 3 = pozicija

static int pomakniMinuteUDanuZaKorakPloce(int minuteUDanu, int koraci) {
  const int MINUTA_U_DANU = 24 * 60;
  int rezultat = minuteUDanu + (koraci * 15);
  rezultat %= MINUTA_U_DANU;
  if (rezultat < 0) {
    rezultat += MINUTA_U_DANU;
  }
  return rezultat;
}

static void formatirajMinuteUDanuHHMM(int minuteUDanu, char* odrediste, size_t velicina) {
  const int normalizirano = ((minuteUDanu % (24 * 60)) + (24 * 60)) % (24 * 60);
  const int sat = normalizirano / 60;
  const int minuta = normalizirano % 60;
  snprintf(odrediste, velicina, "%02d:%02d", sat, minuta);
}

static bool pretvoriPozicijuPloceUVrijemeZaPocetak(int pozicija,
                                                   int pocetakMinuta,
                                                   int* sat24,
                                                   int* minuta) {
  if (pozicija < 0 || pozicija > 63 || sat24 == nullptr || minuta == nullptr) {
    return false;
  }

  const int ukupnoMinuta = pocetakMinuta + (pozicija * 15);
  if (ukupnoMinuta < 0 || ukupnoMinuta > (23 * 60 + 59)) {
    return false;
  }

  *sat24 = ukupnoMinuta / 60;
  *minuta = ukupnoMinuta % 60;
  return true;
}

static bool pretvoriVrijemeUPozicijuPloceZaPocetak(int sat24,
                                                   int minuta,
                                                   int pocetakMinuta,
                                                   int* pozicija) {
  if (sat24 < 0 || sat24 > 23 || minuta < 0 || minuta > 59 || pozicija == nullptr) {
    return false;
  }

  const int ukupnoMinuta = sat24 * 60 + minuta;
  const int diff = ukupnoMinuta - pocetakMinuta;
  if (diff < 0 || diff >= (64 * 15) || (diff % 15) != 0) {
    return false;
  }

  *pozicija = diff / 15;
  return *pozicija >= 0 && *pozicija < 64;
}

static void ucitajVrijemePloceZaUredjivanjeIzPozicije(int pozicija) {
  int sat24 = 0;
  int minuta = 0;
  if (pretvoriPozicijuPloceUVrijemeZaPocetak(constrain(pozicija, 0, 63),
                                             plocaPocetakUredjivanjeMinuta,
                                             &sat24,
                                             &minuta)) {
    plocaSatUredjivanje = sat24;
    plocaMinutaUredjivanje = minuta;
    return;
  }

  plocaSatUredjivanje = 4;
  plocaMinutaUredjivanje = 59;
}

static int dohvatiPozicijuPloceIzUredjivanja() {
  int pozicija = 0;
  if (pretvoriVrijemeUPozicijuPloceZaPocetak(plocaSatUredjivanje,
                                             plocaMinutaUredjivanje,
                                             plocaPocetakUredjivanjeMinuta,
                                             &pozicija)) {
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

static void ucitajPostavkePloceZaUredjivanje() {
  plocaAktivnaUredjivanje = jePlocaKonfigurirana();
  plocaPocetakUredjivanjeMinuta = dohvatiPocetakPloceMinute();
  plocaKrajUredjivanjeMinuta = dohvatiKrajPloceMinute();
  ucitajVrijemePloceZaUredjivanjeIzPozicije(dohvatiPozicijuPloce());
  faza_postavki_ploce = 3;
  resetirajUnosVremena();
}

// ==================== BROJCANI UNOS VREMENA ====================

static bool unosVremenaAktivan = false;
static uint8_t uneseneZnamenkeVremena[4] = {0, 0, 0, 0};
static uint8_t brojUnesenihZnamenkiVremena = 0;

// ==================== QUIET HOURS ADJUSTMENT ====================

static int tihiSatOd = 22;
static int tihiSatDo = 6;
static uint8_t faza_tihih_sati = 0; // 0 = OD, 1 = DO, 2 = OTK, 3 = SL, 4 = MRT
static uint8_t modOtkucavanjaUredjivanje = 2;
static uint8_t modSlavljenjaUredjivanje = 1;
static uint8_t modMrtvackogUredjivanje = 1;

static int trajanjeZvonaRdMin = 2;
static int trajanjeZvonaNedMin = 3;
static int trajanjeSlavljenjaMin = 2;
static int odgodaSlavljenjaSekundeUredjivanje = 15;
static uint8_t faza_postavki_cavala = 0;

static int prebaciTrajanjeStapicaMinute(int trenutno, int smjer) {
  static const int DOPUSTENA_TRAJANJA[] = {2, 3, 4};
  const int brojTrajanja = sizeof(DOPUSTENA_TRAJANJA) / sizeof(DOPUSTENA_TRAJANJA[0]);
  int indeks = 0;

  for (int i = 0; i < brojTrajanja; ++i) {
    if (DOPUSTENA_TRAJANJA[i] == trenutno) {
      indeks = i;
      break;
    }
  }

  if (smjer > 0) {
    indeks = (indeks + 1) % brojTrajanja;
  } else {
    indeks = (indeks - 1 + brojTrajanja) % brojTrajanja;
  }

  return DOPUSTENA_TRAJANJA[indeks];
}

static int prebaciOdgoduSlavljenjaSekunde(int trenutno, int smjer) {
  static const int DOPUSTENE_ODGODE[] = {15, 30, 45, 60};
  const int brojOdgoda = sizeof(DOPUSTENE_ODGODE) / sizeof(DOPUSTENE_ODGODE[0]);
  int indeks = 0;

  for (int i = 0; i < brojOdgoda; ++i) {
    if (DOPUSTENE_ODGODE[i] == trenutno) {
      indeks = i;
      break;
    }
  }

  if (smjer > 0) {
    indeks = (indeks + 1) % brojOdgoda;
  } else {
    indeks = (indeks - 1 + brojOdgoda) % brojOdgoda;
  }

  return DOPUSTENE_ODGODE[indeks];
}

static int sanitizirajOdgoduSlavljenjaSekundeZaMeni(int trenutno) {
  static const int DOPUSTENE_ODGODE[] = {15, 30, 45, 60};
  int najbliza = DOPUSTENE_ODGODE[0];
  int najmanjaRazlika = abs(trenutno - najbliza);

  for (size_t i = 1; i < (sizeof(DOPUSTENE_ODGODE) / sizeof(DOPUSTENE_ODGODE[0])); ++i) {
    const int kandidat = DOPUSTENE_ODGODE[i];
    const int razlika = abs(trenutno - kandidat);
    if (razlika < najmanjaRazlika) {
      najbliza = kandidat;
      najmanjaRazlika = razlika;
    }
  }

  return najbliza;
}

static const int DOPUSTENE_SUNCEVE_ODGODE[] = {-30, -20, -10, 0, 10, 20, 30};
static const int BROJ_DOPUSTENIH_SUNCEVIH_ODGODA =
    sizeof(DOPUSTENE_SUNCEVE_ODGODE) / sizeof(DOPUSTENE_SUNCEVE_ODGODE[0]);
static const uint8_t SUNCE_STRANICA_NOCNA_RASVJETA = SUNCEVI_DOGADAJ_BROJ;
static const uint8_t BROJ_STRANICA_SUNCA = SUNCEVI_DOGADAJ_BROJ + 1;
static uint8_t sunceDogadajUredjivanje = 0;
static uint8_t faza_postavki_sunca = 0; // 0 = stranica, 1 = aktivno, 2 = zvono, 3 = odgoda
static bool sunceOmogucenoUredjivanje[SUNCEVI_DOGADAJ_BROJ] = {false, false, false};
static uint8_t sunceZvonoUredjivanje[SUNCEVI_DOGADAJ_BROJ] = {1, 1, 1};
static int sunceOdgodaUredjivanje[SUNCEVI_DOGADAJ_BROJ] = {0, 0, 0};
static bool nocnaRasvjetaUredjivanje = false;

// ==================== BLAGDANI ====================

static uint8_t blagdaniSlavljenjeMaskaUredjivanje = 0;
static uint8_t blagdaniRazdobljaMaskaUredjivanje = 0;
static bool sviSvetiOmogucenoUredjivanje = false;
static uint8_t sviSvetiPocetakSatUredjivanje = 15;
static uint8_t sviSvetiZavrsetakSatUredjivanje = 8;
static uint8_t stranica_postavki_blagdana = 0; // 0=slavljenje, 1=Svi sveti
static uint8_t faza_postavki_blagdana = 0; // 0=J, 1=P, 2=V, 3=A, 4=P, 5=VG

static uint8_t wifiInfoStrana = 0;

static bool jePrijestupnaGodinaUredjivanja(int godina) {
  return ((godina % 4) == 0 && (godina % 100) != 0) || ((godina % 400) == 0);
}

static int dohvatiBrojDanaUMjesecuUredjivanja(int mjesec, int godina) {
  switch (mjesec) {
    case 1:
    case 3:
    case 5:
    case 7:
    case 8:
    case 10:
    case 12:
      return 31;
    case 4:
    case 6:
    case 9:
    case 11:
      return 30;
    case 2:
      return jePrijestupnaGodinaUredjivanja(godina) ? 29 : 28;
    default:
      return 31;
  }
}

static void sanitizirajDatumVrijemeMaticnogSata() {
  privremenaGodina = constrain(privremenaGodina, 2024, 2099);
  privremeniMjesec = constrain(privremeniMjesec, 1, 12);
  privremeniSat = constrain(privremeniSat, 0, 23);
  privremenaMinuta = constrain(privremenaMinuta, 0, 59);
  privremeniDan = constrain(
      privremeniDan,
      1,
      dohvatiBrojDanaUMjesecuUredjivanja(privremeniMjesec, privremenaGodina));
}

static void ucitajDatumVrijemeMaticnogSataZaUredjivanje() {
  const DateTime sada = dohvatiTrenutnoVrijeme();
  privremeniDan = sada.day();
  privremeniMjesec = sada.month();
  privremenaGodina = sada.year();
  privremeniSat = sada.hour();
  privremenaMinuta = sada.minute();
  sanitizirajDatumVrijemeMaticnogSata();
}

static void ucitajMaticniSatZaUredjivanje() {
  ucitajDatumVrijemeMaticnogSataZaUredjivanje();
  ntpOmogucenUredjivanje = jeNTPOmogucen();
  faza_postavki_maticnog_sata = 0;
}

static unsigned int prilagodiTrajanjeImpulsaCekicaZaMeni(unsigned int trenutnoMs, int deltaKorak) {
  int novoMs = static_cast<int>(trenutnoMs) + (deltaKorak * 10);
  if (novoMs > 150) {
    novoMs = 50;
  } else if (novoMs < 50) {
    novoMs = 150;
  }
  return static_cast<unsigned int>(novoMs);
}

static uint8_t prilagodiInercijuZvonaZaMeni(uint8_t trenutnoSekunde, int deltaKorak) {
  int noveSekunde = static_cast<int>(trenutnoSekunde) + (deltaKorak * 10);
  if (noveSekunde > 180) {
    noveSekunde = 10;
  } else if (noveSekunde < 10) {
    noveSekunde = 180;
  }
  return static_cast<uint8_t>(noveSekunde);
}

static void ucitajSustavZaUredjivanje() {
  lcdPozadinskoOsvjetljenjeUredjivanje = jeLCDPozadinskoOsvjetljenjeUkljuceno();
  logiranjeUredjivanje = jePCLogiranjeOmoguceno();
  rs485Uredjivanje = jeRS485Omogucen();
  trajanjeImpulsaCekicaUredjivanje = dohvatiTrajanjeImpulsaCekica();
  inercijaZvona1Uredjivanje = dohvatiInercijuZvona1Sekunde();
  inercijaZvona2Uredjivanje = dohvatiInercijuZvona2Sekunde();
}

static bool jeTipkaZaPrethodnuStavku(KeyEvent event) {
  return event == KEY_UP || event == KEY_LEFT;
}

static bool jeTipkaZaSljedecuStavku(KeyEvent event) {
  return event == KEY_DOWN || event == KEY_RIGHT;
}

static bool jeTipkaZaOtvaranjeGlavnogMenija(KeyEvent event) {
  return event == KEY_UP || event == KEY_DOWN;
}

static void resetirajUnosVremena() {
  unosVremenaAktivan = false;
  brojUnesenihZnamenkiVremena = 0;
  for (uint8_t i = 0; i < 4; ++i) {
    uneseneZnamenkeVremena[i] = 0;
  }
}

static bool dohvatiZnamenkuIzKljuce(KeyEvent event, uint8_t* znamenka) {
  if (znamenka == NULL) {
    return false;
  }

  switch (event) {
    case KEY_DIGIT_0: *znamenka = 0; return true;
    case KEY_DIGIT_1: *znamenka = 1; return true;
    case KEY_DIGIT_2: *znamenka = 2; return true;
    case KEY_DIGIT_3: *znamenka = 3; return true;
    case KEY_DIGIT_4: *znamenka = 4; return true;
    case KEY_DIGIT_5: *znamenka = 5; return true;
    case KEY_DIGIT_6: *znamenka = 6; return true;
    case KEY_DIGIT_7: *znamenka = 7; return true;
    case KEY_DIGIT_8: *znamenka = 8; return true;
    case KEY_DIGIT_9: *znamenka = 9; return true;
    default:
      return false;
  }
}

static void dodajZnamenkuUnosaVremena(uint8_t znamenka) {
  if (brojUnesenihZnamenkiVremena >= 4) {
    brojUnesenihZnamenkiVremena = 0;
  }

  uneseneZnamenkeVremena[brojUnesenihZnamenkiVremena++] = znamenka;
  unosVremenaAktivan = true;
}

static bool jeUnosVremenaPotpun() {
  return unosVremenaAktivan && brojUnesenihZnamenkiVremena == 4;
}

static void formatirajMaskuUnosaVremena(char* odrediste, size_t velicina) {
  char prikaz[6] = {'_', '_', ':', '_', '_', '\0'};
  for (uint8_t i = 0; i < brojUnesenihZnamenkiVremena && i < 4; ++i) {
    prikaz[i + (i >= 2 ? 1 : 0)] = static_cast<char>('0' + uneseneZnamenkeVremena[i]);
  }
  snprintf(odrediste, velicina, "%s", prikaz);
}

static bool dohvatiUnesenoVrijeme(int* sat, int* minuta) {
  if (!jeUnosVremenaPotpun() || sat == NULL || minuta == NULL) {
    return false;
  }

  *sat = (uneseneZnamenkeVremena[0] * 10) + uneseneZnamenkeVremena[1];
  *minuta = (uneseneZnamenkeVremena[2] * 10) + uneseneZnamenkeVremena[3];
  return true;
}

static bool postaviVrijemeIzUnosaZaKazaljke() {
  int sat = 0;
  int minuta = 0;
  if (!dohvatiUnesenoVrijeme(&sat, &minuta)) {
    posaljiPCLog(F("Kazaljke: nepotpun unos vremena"));
    return false;
  }

  if (sat < 1 || sat > 12 || minuta < 0 || minuta > 59) {
    posaljiPCLog(F("Kazaljke: neispravan unos vremena HH:MM"));
    return false;
  }

  korektniSat = sat;
  korektnaMinuta = minuta;
  return true;
}

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

static int pretvoriMinuteUKazaljkeSat12h(int minutaKazaljki) {
  const int sat24 = ((minutaKazaljki / 60) % 12 + 12) % 12;
  return (sat24 == 0) ? 12 : sat24;
}

static void vratiNaGlavniMeniNaStavku(int indeks) {
  odabraniIndex = constrain(indeks, 0, BROJ_STAVKI_GLAVNI_MENU - 1);
  trenutnoStanje = MENU_STATE_MAIN_MENU;
}

static void ucitajPostavkeCavalaZaUredjivanje() {
  trajanjeZvonaRdMin = dohvatiTrajanjeZvonjenjaRadniMin();
  trajanjeZvonaNedMin = dohvatiTrajanjeZvonjenjaNedjeljaMin();
  trajanjeSlavljenjaMin = dohvatiTrajanjeSlavljenjaMin();
  odgodaSlavljenjaSekundeUredjivanje = dohvatiOdgoduSlavljenjaSekunde();
  faza_postavki_cavala = 0;
  trajanjeZvonaRdMin = constrain(trajanjeZvonaRdMin, 2, 4);
  trajanjeZvonaNedMin = constrain(trajanjeZvonaNedMin, 2, 4);
  trajanjeSlavljenjaMin = constrain(trajanjeSlavljenjaMin, 2, 4);
  odgodaSlavljenjaSekundeUredjivanje =
      sanitizirajOdgoduSlavljenjaSekundeZaMeni(odgodaSlavljenjaSekundeUredjivanje);
}

static void ucitajTihiPeriodZaUredjivanje() {
  tihiSatOd = dohvatiBATPeriodOdSata();
  tihiSatDo = dohvatiBATPeriodDoSata();
  modOtkucavanjaUredjivanje = dohvatiModOtkucavanja();
  modSlavljenjaUredjivanje = dohvatiModSlavljenja();
  modMrtvackogUredjivanje = dohvatiModMrtvackog();
  faza_tihih_sati = 0;
}

static void ucitajSunceveDogadajeZaUredjivanje() {
  for (uint8_t i = 0; i < SUNCEVI_DOGADAJ_BROJ; ++i) {
    sunceOmogucenoUredjivanje[i] = jeSuncevDogadajOmogucen(i);
    sunceZvonoUredjivanje[i] = dohvatiZvonoZaSuncevDogadaj(i);
    sunceOdgodaUredjivanje[i] = dohvatiOdgoduSuncevogDogadajaMin(i);
  }
  nocnaRasvjetaUredjivanje = jeNocnaRasvjetaOmogucena();
  sunceOdgodaUredjivanje[SUNCEVI_DOGADAJ_PODNE] = 0;
  sunceDogadajUredjivanje = SUNCEVI_DOGADAJ_JUTRO;
  faza_postavki_sunca = 0;
}

static void ucitajBlagdaneZaUredjivanje() {
  blagdaniSlavljenjeMaskaUredjivanje = dohvatiMaskuBlagdanskogSlavljenja();
  blagdaniRazdobljaMaskaUredjivanje = dohvatiMaskuBlagdanskihRazdoblja();
  sviSvetiOmogucenoUredjivanje = jeSviSvetiMrtvackoOmoguceno();
  sviSvetiPocetakSatUredjivanje = dohvatiSviSvetiPocetakSat();
  sviSvetiZavrsetakSatUredjivanje = dohvatiSviSvetiZavrsetakSat();
  stranica_postavki_blagdana = 0;
  faza_postavki_blagdana = 0;
}

static bool jeBitUkljucen(uint8_t maska, uint8_t bit) {
  return (maska & (1U << bit)) != 0;
}

static void promijeniBitMaske(uint8_t& maska, uint8_t bit) {
  maska ^= static_cast<uint8_t>(1U << bit);
}

static uint8_t pomakniSatSviSvetiPocetak(uint8_t trenutniSat, int smjer) {
  int noviSat = static_cast<int>(trenutniSat) + smjer;
  if (noviSat < 0) {
    noviSat = 20;
  } else if (noviSat > 20) {
    noviSat = 0;
  }
  return static_cast<uint8_t>(noviSat);
}

static uint8_t pomakniSatSviSvetiZavrsetak(uint8_t trenutniSat, int smjer) {
  int noviSat = static_cast<int>(trenutniSat) + smjer;
  if (noviSat < 6) {
    noviSat = 23;
  } else if (noviSat > 23) {
    noviSat = 6;
  }
  return static_cast<uint8_t>(noviSat);
}

static void dohvatiNazivSuncevogDogadaja(uint8_t dogadaj, char* odrediste, size_t velicina) {
  if (dogadaj == SUNCEVI_DOGADAJ_JUTRO) {
    snprintf(odrediste, velicina, "JUTRO");
  } else if (dogadaj == SUNCEVI_DOGADAJ_PODNE) {
    snprintf(odrediste, velicina, "PODNE");
  } else {
    snprintf(odrediste, velicina, "VECER");
  }
}

static bool jeStranicaNocneRasvjeteUSuncu() {
  return sunceDogadajUredjivanje == SUNCE_STRANICA_NOCNA_RASVJETA;
}

static void pomakniStranicuSunca(int smjer) {
  int novaStranica = static_cast<int>(sunceDogadajUredjivanje) + smjer;
  if (novaStranica < 0) {
    novaStranica = BROJ_STRANICA_SUNCA - 1;
  } else if (novaStranica >= BROJ_STRANICA_SUNCA) {
    novaStranica = 0;
  }

  sunceDogadajUredjivanje = static_cast<uint8_t>(novaStranica);
  if (jeStranicaNocneRasvjeteUSuncu()) {
    faza_postavki_sunca = 0;
  }
}

static int prilagodiSuncevuOdgoduZaKorak(int trenutnaOdgoda, int smjer) {
  int indeks = 0;
  int najmanjaRazlika = abs(trenutnaOdgoda - DOPUSTENE_SUNCEVE_ODGODE[0]);

  for (int i = 1; i < BROJ_DOPUSTENIH_SUNCEVIH_ODGODA; ++i) {
    const int razlika = abs(trenutnaOdgoda - DOPUSTENE_SUNCEVE_ODGODE[i]);
    if (razlika < najmanjaRazlika) {
      indeks = i;
      najmanjaRazlika = razlika;
    }
  }

  if (smjer > 0 && indeks < (BROJ_DOPUSTENIH_SUNCEVIH_ODGODA - 1)) {
    ++indeks;
  } else if (smjer < 0 && indeks > 0) {
    --indeks;
  }

  return DOPUSTENE_SUNCEVE_ODGODE[indeks];
}

static void potvrdiSpremanjeTihihSati() {
  postaviKompaktnePostavkeOtkucavanja(
      tihiSatDo,
      tihiSatOd,
      modOtkucavanjaUredjivanje,
      modSlavljenjaUredjivanje,
      modMrtvackogUredjivanje);
  faza_tihih_sati = 0;
  vratiNaGlavniMeniNaStavku(INDEX_POSTAVKE_TIHI_SATI);
  posaljiPCLog(F("Kompaktne postavke otkucavanja spremljene"));
}

static void potvrdiSpremanjePostavkiCavala() {
  postaviPostavkeCavala(static_cast<uint8_t>(trajanjeZvonaRdMin),
                        static_cast<uint8_t>(trajanjeZvonaNedMin),
                        static_cast<uint8_t>(trajanjeSlavljenjaMin),
                        static_cast<uint8_t>(odgodaSlavljenjaSekundeUredjivanje));
  faza_postavki_cavala = 0;
  vratiNaGlavniMeniNaStavku(INDEX_POSTAVKE_STAPICI);
  posaljiPCLog(F("Postavke stapica spremljene"));
}

static void potvrdiSpremanjeSuncevihDogadaja() {
  for (uint8_t i = 0; i < SUNCEVI_DOGADAJ_BROJ; ++i) {
    postaviSuncevDogadaj(i,
                         sunceOmogucenoUredjivanje[i],
                         sunceZvonoUredjivanje[i],
                         sunceOdgodaUredjivanje[i]);
  }
  postaviNocnuRasvjetuOmoguceno(nocnaRasvjetaUredjivanje);
  sunceDogadajUredjivanje = SUNCEVI_DOGADAJ_JUTRO;
  faza_postavki_sunca = 0;
  vratiNaGlavniMeniNaStavku(INDEX_POSTAVKE_SUNCE);
  posaljiPCLog(F("Suncevi dogadaji spremljeni"));
}

static void potvrdiSpremanjeBlagdana() {
  postaviBlagdanskePostavke(blagdaniSlavljenjeMaskaUredjivanje,
                            blagdaniRazdobljaMaskaUredjivanje);
  postaviSviSvetiPostavke(sviSvetiOmogucenoUredjivanje,
                          sviSvetiPocetakSatUredjivanje,
                          sviSvetiZavrsetakSatUredjivanje);
  stranica_postavki_blagdana = 0;
  faza_postavki_blagdana = 0;
  vratiNaGlavniMeniNaStavku(INDEX_POSTAVKE_BLAGDANI);
  posaljiPCLog(F("Blagdani: postavke spremljene"));
}

// ==================== MENU DISPLAY FUNCTIONS ====================

static void prikaziGlavniMeni() {
  char redak1[17];
  char redak2[17];
  FlashTekst::kopirajLiteral(redak1, sizeof(redak1), PSTR("GLAVNI MENI"));
  char stavka[15];
  FlashTekst::ucitajIzNiza(stavkeGlavnogMenuja, odabraniIndex, stavka, sizeof(stavka));
  snprintf(redak2, sizeof(redak2), "> %s", stavka);
  prikaziPoruku(redak1, redak2);
}

static void prikaziMaticniSatMenu() {
  char redak1[17];
  char redak2[17];
  snprintf(redak1,
           sizeof(redak1),
           "SAT %02d:%02d NTP%u",
           privremeniSat,
           privremenaMinuta,
           ntpOmogucenUredjivanje ? 1U : 0U);
  snprintf(redak2,
           sizeof(redak2),
           "DAT %02d.%02d.%04d",
           privremeniDan,
           privremeniMjesec,
           privremenaGodina);
  prikaziPoruku(redak1, redak2);

  lcd.cursor();
  lcd.blink();
  switch (faza_postavki_maticnog_sata) {
    case 0:
      lcd.setCursor(4, 0);
      break;
    case 1:
      lcd.setCursor(7, 0);
      break;
    case 2:
      lcd.setCursor(10, 0);
      break;
    case 3:
      lcd.setCursor(4, 1);
      break;
    case 4:
      lcd.setCursor(7, 1);
      break;
    default:
      lcd.setCursor(13, 1);
      break;
  }
}

static void prikaziMrezuMenu() {
  char redak1[17];
  char redak2[17];
  FlashTekst::kopirajLiteral(redak1, sizeof(redak1), PSTR("MREZA"));
  if (odabraniIndex == 0) {
    snprintf(redak2, sizeof(redak2), "> WiFi: %s",
             jeWiFiOmogucen() ? "ON" : "OFF");
  } else {
    char stavka[15];
    FlashTekst::ucitajIzNiza(stavkeMreze, odabraniIndex, stavka, sizeof(stavka));
    snprintf(redak2, sizeof(redak2), "> %s", stavka);
  }
  prikaziPoruku(redak1, redak2);
}

static void prikaziSustavMenu() {
  char redak1[17];
  char redak2[17];
  FlashTekst::kopirajLiteral(redak1, sizeof(redak1), PSTR("SUSTAV"));
  if (odabraniIndex == 0) {
    snprintf(redak2, sizeof(redak2), "> LCD: %s",
             lcdPozadinskoOsvjetljenjeUredjivanje ? "ON" : "OFF");
  } else if (odabraniIndex == 1) {
    snprintf(redak2, sizeof(redak2), "> Log: %s",
             logiranjeUredjivanje ? "ON" : "OFF");
  } else if (odabraniIndex == 2) {
    snprintf(redak2, sizeof(redak2), "> RS485: %u",
             rs485Uredjivanje ? 1U : 0U);
  } else if (odabraniIndex == 3) {
    snprintf(redak2,
             sizeof(redak2),
             "> Cekic:%3ums",
             static_cast<unsigned>(trajanjeImpulsaCekicaUredjivanje));
  } else if (odabraniIndex == 4) {
    snprintf(redak2,
             sizeof(redak2),
             "> Z1 iner:%3us",
             static_cast<unsigned>(inercijaZvona1Uredjivanje));
  } else if (odabraniIndex == 5) {
    snprintf(redak2,
             sizeof(redak2),
             "> Z2 iner:%3us",
             static_cast<unsigned>(inercijaZvona2Uredjivanje));
  } else {
    FlashTekst::kopirajLiteral(redak2, sizeof(redak2), PSTR("> Povratak"));
  }
  prikaziPoruku(redak1, redak2);
}

static void prikaziPlocaMenu() {
  char redak1[17];
  char redak2[17];
  char pocetak[6];
  char kraj[6];
  formatirajMinuteUDanuHHMM(plocaPocetakUredjivanjeMinuta, pocetak, sizeof(pocetak));
  formatirajMinuteUDanuHHMM(plocaKrajUredjivanjeMinuta, kraj, sizeof(kraj));
  snprintf(redak1,
           sizeof(redak1),
           "PL%d %s-%s",
           plocaAktivnaUredjivanje ? 1 : 0,
           pocetak,
           kraj);

  snprintf(redak2, sizeof(redak2), "POZ: %02d:%02d", plocaSatUredjivanje, plocaMinutaUredjivanje);
  prikaziPoruku(redak1, redak2);

  lcd.cursor();
  lcd.blink();
  switch (faza_postavki_ploce) {
    case 0:
      lcd.setCursor(2, 0);
      break;
    case 1:
      lcd.setCursor(4, 0);
      break;
    case 2:
      lcd.setCursor(10, 0);
      break;
    default:
      lcd.setCursor(5, 1);
      break;
  }
}

static void prikaziKazaljkeMenu() {
  char redak1[17];
  char redak2[17];
  const int memoriraneMinute = dohvatiMemoriraneKazaljkeMinuta();
  const int satPozicije = pretvoriMinuteUKazaljkeSat12h(memoriraneMinute);
  const int minutaPozicije = ((memoriraneMinute % 60) + 60) % 60;
  snprintf(redak1, sizeof(redak1), "POZ: %02d:%02d", satPozicije, minutaPozicije);

  if (unosVremenaAktivan) {
    char maska[6];
    formatirajMaskuUnosaVremena(maska, sizeof(maska));
    snprintf(redak2, sizeof(redak2), "STANJE: %s", maska);
  } else {
    snprintf(redak2, sizeof(redak2), "STANJE: %02d:%02d", korektniSat, korektnaMinuta);
  }
  prikaziPoruku(redak1, redak2);

  lcd.cursor();
  lcd.blink();
  if (faza_korekcije == 0) {
    lcd.setCursor(8, 1);
  } else {
    lcd.setCursor(11, 1);
  }
}

static void prikaziPodesavanjeTihihSati() {
  char redak1[17];
  char redak2[17];

  snprintf(redak1, sizeof(redak1), "BAT %02d:00-%02d:00", tihiSatOd, tihiSatDo);
  snprintf(redak2,
           sizeof(redak2),
           "OTK:%u S:%u M:%u",
           static_cast<unsigned>(modOtkucavanjaUredjivanje),
           static_cast<unsigned>(modSlavljenjaUredjivanje),
           static_cast<unsigned>(modMrtvackogUredjivanje));
  prikaziPoruku(redak1, redak2);

  lcd.cursor();
  lcd.blink();
  switch (faza_tihih_sati) {
    case 0:
      lcd.setCursor(4, 0);
      break;
    case 1:
      lcd.setCursor(10, 0);
      break;
    case 2:
      lcd.setCursor(4, 1);
      break;
    case 3:
      lcd.setCursor(8, 1);
      break;
    default:
      lcd.setCursor(12, 1);
      break;
  }
}

static void prikaziPodesavanjeCavala() {
  char redak1[17];
  char redak2[17];

  FlashTekst::kopirajLiteral(redak1, sizeof(redak1), PSTR("STAPICI"));
  snprintf(redak2,
           sizeof(redak2),
           "TR%d TN%d S+%02d TS%d",
           trajanjeZvonaRdMin,
           trajanjeZvonaNedMin,
           odgodaSlavljenjaSekundeUredjivanje,
           trajanjeSlavljenjaMin);
  prikaziPoruku(redak1, redak2);

  lcd.cursor();
  lcd.blink();
  switch (faza_postavki_cavala) {
    case 0:
      lcd.setCursor(2, 1);
      break;
    case 1:
      lcd.setCursor(6, 1);
      break;
    case 2:
      lcd.setCursor(10, 1);
      break;
    default:
      lcd.setCursor(15, 1);
      break;
  }
}

static void prikaziSunceveDogadaje() {
  char redak1[17];
  char redak2[17];
  if (jeStranicaNocneRasvjeteUSuncu()) {
    FlashTekst::kopirajLiteral(redak1, sizeof(redak1), PSTR("NOCNA RASVJETA"));
    snprintf_P(redak2, sizeof(redak2), PSTR("AUTO:%u UD/LR"),
               nocnaRasvjetaUredjivanje ? 1U : 0U);
    prikaziPoruku(redak1, redak2);
    lcd.cursor();
    lcd.blink();
    lcd.setCursor(5, 1);
    return;
  }

  const uint8_t dogadaj =
      static_cast<uint8_t>(constrain(sunceDogadajUredjivanje, 0, SUNCEVI_DOGADAJ_BROJ - 1));
  char naziv[7];
  dohvatiNazivSuncevogDogadaja(dogadaj, naziv, sizeof(naziv));

  int minuteDogadaja = 0;
  if (dohvatiDanasnjeVrijemeSuncevogDogadajaMin(dogadaj, minuteDogadaja)) {
    const int sat = minuteDogadaja / 60;
    const int minuta = minuteDogadaja % 60;
    snprintf_P(redak1, sizeof(redak1), PSTR("Danas %02d:%02d LR"), sat, minuta);
  } else {
      FlashTekst::kopirajLiteral(redak1, sizeof(redak1), PSTR("Danas --:-- LR"));
  }

  snprintf_P(redak2,
             sizeof(redak2),
             PSTR("%s %u Z%u %+03d"),
             naziv,
             sunceOmogucenoUredjivanje[dogadaj] ? 1U : 0U,
             static_cast<unsigned>(sunceZvonoUredjivanje[dogadaj]),
             sunceOdgodaUredjivanje[dogadaj]);
  prikaziPoruku(redak1, redak2);

  lcd.cursor();
  lcd.blink();
  switch (faza_postavki_sunca) {
    case 0:
      lcd.setCursor(0, 1);
      break;
    case 1:
      lcd.setCursor(6, 1);
      break;
    case 2:
      lcd.setCursor(9, 1);
      break;
    default:
      lcd.setCursor(11, 1);
      break;
  }
}

static void prikaziBlagdane() {
  char redak1[17];
  char redak2[17];
  if (stranica_postavki_blagdana == 1) {
    snprintf_P(redak1,
               sizeof(redak1),
               PSTR("SVI SVETI %u"),
               sviSvetiOmogucenoUredjivanje ? 1U : 0U);
    snprintf_P(redak2,
               sizeof(redak2),
               PSTR("P:%02u  Z:%02u"),
               static_cast<unsigned>(sviSvetiPocetakSatUredjivanje),
               static_cast<unsigned>(sviSvetiZavrsetakSatUredjivanje));
    prikaziPoruku(redak1, redak2);

    lcd.cursor();
    lcd.blink();
    switch (faza_postavki_blagdana) {
      case 0:
        lcd.setCursor(11, 0);
        break;
      case 1:
        lcd.setCursor(3, 1);
        break;
      default:
        lcd.setCursor(10, 1);
        break;
    }
    return;
  }

  snprintf_P(redak1,
             sizeof(redak1),
             PSTR("SLAVI J%u P%u V%u"),
             jeBitUkljucen(blagdaniSlavljenjeMaskaUredjivanje, SUNCEVI_DOGADAJ_JUTRO) ? 1U : 0U,
             jeBitUkljucen(blagdaniSlavljenjeMaskaUredjivanje, SUNCEVI_DOGADAJ_PODNE) ? 1U : 0U,
             jeBitUkljucen(blagdaniSlavljenjeMaskaUredjivanje, SUNCEVI_DOGADAJ_VECER) ? 1U : 0U);
  snprintf_P(redak2,
             sizeof(redak2),
             PSTR("A:%u P:%u VG:%u"),
             jeBitUkljucen(blagdaniRazdobljaMaskaUredjivanje, BLAGDAN_ANTE) ? 1U : 0U,
             jeBitUkljucen(blagdaniRazdobljaMaskaUredjivanje, BLAGDAN_PETAR) ? 1U : 0U,
             jeBitUkljucen(blagdaniRazdobljaMaskaUredjivanje, BLAGDAN_VELIKA_GOSPA) ? 1U : 0U);
  prikaziPoruku(redak1, redak2);

  lcd.cursor();
  lcd.blink();
  switch (faza_postavki_blagdana) {
    case 0:
      lcd.setCursor(7, 0);
      break;
    case 1:
      lcd.setCursor(10, 0);
      break;
    case 2:
      lcd.setCursor(13, 0);
      break;
    case 3:
      lcd.setCursor(2, 1);
      break;
    case 4:
      lcd.setCursor(6, 1);
      break;
    default:
      lcd.setCursor(11, 1);
      break;
  }
}

static void prikaziWiFiIP() {
  char redak1[17];
  char redak2[17];

  if (wifiInfoStrana == 0) {
    FlashTekst::kopirajLiteral(redak1, sizeof(redak1), PSTR("WiFi IP [LR]"));
    const char* ipAdresa = dohvatiESPWiFiLokalnuIP();
    if (ipAdresa != nullptr && ipAdresa[0] != '\0') {
      snprintf(redak2, sizeof(redak2), "%.16s", ipAdresa);
    } else if (jeWiFiPovezanNaESP()) {
      FlashTekst::kopirajLiteral(redak2, sizeof(redak2), PSTR("Cekam IP..."));
    } else {
      FlashTekst::kopirajLiteral(redak2, sizeof(redak2), PSTR("Nije dostupna"));
    }
  } else {
    FlashTekst::kopirajLiteral(redak1, sizeof(redak1), PSTR("WiFiMAC [LR]"));
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
      FlashTekst::kopirajLiteral(redak2, sizeof(redak2), PSTR("Cekam MAC..."));
    } else {
      FlashTekst::kopirajLiteral(redak2, sizeof(redak2), PSTR("Nije dostupna"));
    }
  }
  prikaziPoruku(redak1, redak2);
}

// ==================== KEY PROCESSING ====================

static void obradiKlucGlavniMeni(KeyEvent event) {
  if (jeTipkaZaPrethodnuStavku(event)) {
    odabraniIndex = (odabraniIndex - 1 + BROJ_STAVKI_GLAVNI_MENU) % BROJ_STAVKI_GLAVNI_MENU;
    return;
  }
  if (jeTipkaZaSljedecuStavku(event)) {
    odabraniIndex = (odabraniIndex + 1) % BROJ_STAVKI_GLAVNI_MENU;
    return;
  }

  switch (event) {
    case KEY_SELECT:
      if (odabraniIndex == INDEX_POSTAVKE_MATICNI_SAT) {
        ucitajMaticniSatZaUredjivanje();
        trenutnoStanje = MENU_STATE_CLOCK_SETTINGS;
        posaljiPCLog(F("Ulazak u postavke maticnog sata"));
      } else if (odabraniIndex == INDEX_POSTAVKE_KAZALJKE) {
        ucitajKazaljkeZaUredjivanje();
        postaviRucnuBlokaduKazaljki(true);
        trenutnoStanje = MENU_STATE_HAND_SETTINGS;
        posaljiPCLog(F("Ulazak u postavke kazaljki"));
      } else if (odabraniIndex == INDEX_POSTAVKE_PLOCA) {
        ucitajPostavkePloceZaUredjivanje();
        postaviRucnuBlokaduPloce(true);
        trenutnoStanje = MENU_STATE_PLATE_SETTINGS;
        posaljiPCLog(F("Ulazak u postavke okretne ploce"));
      } else if (odabraniIndex == INDEX_POSTAVKE_MREZA) {
        odabraniIndex = 0;
        trenutnoStanje = MENU_STATE_NETWORK_SETTINGS;
        posaljiPCLog(F("Ulazak u mrezne postavke"));
      } else if (odabraniIndex == INDEX_POSTAVKE_TIHI_SATI) {
        ucitajTihiPeriodZaUredjivanje();
        trenutnoStanje = MENU_STATE_QUIET_HOURS;
        posaljiPCLog(F("Ulazak u kompaktne postavke otkucavanja"));
      } else if (odabraniIndex == INDEX_POSTAVKE_STAPICI) {
        ucitajPostavkeCavalaZaUredjivanje();
        trenutnoStanje = MENU_STATE_NAIL_SETTINGS;
        posaljiPCLog(F("Ulazak u postavke stapica"));
      } else if (odabraniIndex == INDEX_POSTAVKE_SUNCE) {
        ucitajSunceveDogadajeZaUredjivanje();
        trenutnoStanje = MENU_STATE_SUNCE_SETTINGS;
        posaljiPCLog(F("Ulazak u postavke sunca"));
      } else if (odabraniIndex == INDEX_POSTAVKE_BLAGDANI) {
        ucitajBlagdaneZaUredjivanje();
        trenutnoStanje = MENU_STATE_HOLIDAY_SETTINGS;
        posaljiPCLog(F("Ulazak u postavke blagdana"));
      } else if (odabraniIndex == INDEX_POSTAVKE_SUSTAV) {
        ucitajSustavZaUredjivanje();
        odabraniIndex = 0;
        trenutnoStanje = MENU_STATE_SYSTEM_SETTINGS;
        posaljiPCLog(F("Ulazak u sustavske postavke"));
      } else if (odabraniIndex == INDEX_POSTAVKE_POVRATAK) {
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

static void obradiKlucMaticniSat(KeyEvent event) {
  switch (event) {
    case KEY_LEFT:
      faza_postavki_maticnog_sata = (faza_postavki_maticnog_sata - 1 + 6) % 6;
      break;
    case KEY_RIGHT:
      faza_postavki_maticnog_sata = (faza_postavki_maticnog_sata + 1) % 6;
      break;
    case KEY_UP:
      if (faza_postavki_maticnog_sata == 0) {
        const int brojDana = dohvatiBrojDanaUMjesecuUredjivanja(privremeniMjesec, privremenaGodina);
        privremeniDan = (privremeniDan >= brojDana) ? 1 : (privremeniDan + 1);
      } else if (faza_postavki_maticnog_sata == 1) {
        privremeniMjesec = (privremeniMjesec >= 12) ? 1 : (privremeniMjesec + 1);
        sanitizirajDatumVrijemeMaticnogSata();
      } else if (faza_postavki_maticnog_sata == 2) {
        privremenaGodina = (privremenaGodina >= 2099) ? 2024 : (privremenaGodina + 1);
        sanitizirajDatumVrijemeMaticnogSata();
      } else if (faza_postavki_maticnog_sata == 3) {
        privremeniSat = (privremeniSat + 1) % 24;
      } else if (faza_postavki_maticnog_sata == 4) {
        privremenaMinuta = (privremenaMinuta + 1) % 60;
      } else {
        ntpOmogucenUredjivanje = !ntpOmogucenUredjivanje;
      }
      break;
    case KEY_DOWN:
      if (faza_postavki_maticnog_sata == 0) {
        const int brojDana = dohvatiBrojDanaUMjesecuUredjivanja(privremeniMjesec, privremenaGodina);
        privremeniDan = (privremeniDan <= 1) ? brojDana : (privremeniDan - 1);
      } else if (faza_postavki_maticnog_sata == 1) {
        privremeniMjesec = (privremeniMjesec <= 1) ? 12 : (privremeniMjesec - 1);
        sanitizirajDatumVrijemeMaticnogSata();
      } else if (faza_postavki_maticnog_sata == 2) {
        privremenaGodina = (privremenaGodina <= 2024) ? 2099 : (privremenaGodina - 1);
        sanitizirajDatumVrijemeMaticnogSata();
      } else if (faza_postavki_maticnog_sata == 3) {
        privremeniSat = (privremeniSat - 1 + 24) % 24;
      } else if (faza_postavki_maticnog_sata == 4) {
        privremenaMinuta = (privremenaMinuta - 1 + 60) % 60;
      } else {
        ntpOmogucenUredjivanje = !ntpOmogucenUredjivanje;
      }
      break;
    case KEY_SELECT:
      sanitizirajDatumVrijemeMaticnogSata();
      azurirajVrijemeRucno(DateTime(
          privremenaGodina,
          privremeniMjesec,
          privremeniDan,
          privremeniSat,
          privremenaMinuta,
          0));
      postaviSinkronizacijskePostavke(dohvatiNTPServer());
      postaviNTPOmogucen(ntpOmogucenUredjivanje);
      ntpOmogucenUredjivanje = jeNTPOmogucen();
      posaljiNTPPostavkeESP();
      posaljiPCLog(F("Maticni sat: rucno vrijeme i NTP postavke spremljeni"));
      vratiNaGlavniMeniNaStavku(INDEX_POSTAVKE_MATICNI_SAT);
      break;
    case KEY_BACK:
      ucitajMaticniSatZaUredjivanje();
      vratiNaGlavniMeniNaStavku(INDEX_POSTAVKE_MATICNI_SAT);
      break;
    default:
      break;
  }
}

static void obradiKlucPloce(KeyEvent event) {
  switch (event) {
    case KEY_LEFT:
      faza_postavki_ploce = (faza_postavki_ploce - 1 + 4) % 4;
      break;
    case KEY_RIGHT:
      faza_postavki_ploce = (faza_postavki_ploce + 1) % 4;
      break;
    case KEY_UP:
      if (faza_postavki_ploce == 0) {
        plocaAktivnaUredjivanje = !plocaAktivnaUredjivanje;
      } else if (faza_postavki_ploce == 1) {
        plocaPocetakUredjivanjeMinuta =
            pomakniMinuteUDanuZaKorakPloce(plocaPocetakUredjivanjeMinuta, 1);
      } else if (faza_postavki_ploce == 2) {
        plocaKrajUredjivanjeMinuta =
            pomakniMinuteUDanuZaKorakPloce(plocaKrajUredjivanjeMinuta, 1);
      } else {
        pomakniUredjivanjePloceZaPozicije(1);
      }
      break;
    case KEY_DOWN:
      if (faza_postavki_ploce == 0) {
        plocaAktivnaUredjivanje = !plocaAktivnaUredjivanje;
      } else if (faza_postavki_ploce == 1) {
        plocaPocetakUredjivanjeMinuta =
            pomakniMinuteUDanuZaKorakPloce(plocaPocetakUredjivanjeMinuta, -1);
      } else if (faza_postavki_ploce == 2) {
        plocaKrajUredjivanjeMinuta =
            pomakniMinuteUDanuZaKorakPloce(plocaKrajUredjivanjeMinuta, -1);
      } else {
        pomakniUredjivanjePloceZaPozicije(-1);
      }
      break;
    case KEY_SELECT:
      postaviKonfiguracijuPloce(plocaAktivnaUredjivanje,
                                plocaPocetakUredjivanjeMinuta,
                                plocaKrajUredjivanjeMinuta);
      postaviTrenutniPolozajPloce(dohvatiPozicijuPloceIzUredjivanja());
      postaviRucnuBlokaduPloce(false);
      zatraziPoravnanjeTaktaPloce();

      {
        char log[112];
        snprintf(log,
                 sizeof(log),
                 "Ploca: spremljeno PL%d %02d:%02d-%02d:%02d, pozicija %02d:%02d",
                 plocaAktivnaUredjivanje ? 1 : 0,
                 plocaPocetakUredjivanjeMinuta / 60,
                 plocaPocetakUredjivanjeMinuta % 60,
                 plocaKrajUredjivanjeMinuta / 60,
                 plocaKrajUredjivanjeMinuta % 60,
                 plocaSatUredjivanje,
                 plocaMinutaUredjivanje);
        posaljiPCLog(log);
      }

      vratiNaGlavniMeniNaStavku(INDEX_POSTAVKE_PLOCA);
      break;
    case KEY_BACK:
      postaviRucnuBlokaduPloce(false);
      vratiNaGlavniMeniNaStavku(INDEX_POSTAVKE_PLOCA);
      break;
    default:
      break;
  }
}

static void obradiKlucMreza(KeyEvent event) {
  if (event == KEY_UP) {
    odabraniIndex = (odabraniIndex - 1 + BROJ_STAVKI_MREZE) % BROJ_STAVKI_MREZE;
    return;
  }
  if (event == KEY_DOWN) {
    odabraniIndex = (odabraniIndex + 1) % BROJ_STAVKI_MREZE;
    return;
  }

  switch (event) {
    case KEY_LEFT:
    case KEY_RIGHT:
      if (odabraniIndex == 0) {
        const bool novoStanje = !jeWiFiOmogucen();
        postaviWiFiOmogucen(novoStanje);
        posaljiWiFiStatusESP();
      }
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
        vratiNaGlavniMeniNaStavku(INDEX_POSTAVKE_MREZA);
      }
      break;
    case KEY_BACK:
      vratiNaGlavniMeniNaStavku(INDEX_POSTAVKE_MREZA);
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
      postaviLCDPozadinskoOsvjetljenje(lcdPozadinskoOsvjetljenjeUredjivanje);
      primijeniLCDPozadinskoOsvjetljenje(lcdPozadinskoOsvjetljenjeUredjivanje);
      postaviPCLogiranjeOmoguceno(logiranjeUredjivanje);
      postaviRS485Omogucen(rs485Uredjivanje);
      postaviTrajanjeImpulsaCekica(trajanjeImpulsaCekicaUredjivanje);
      postaviInercijeZvona(inercijaZvona1Uredjivanje, inercijaZvona2Uredjivanje);
      vratiNaGlavniMeniNaStavku(INDEX_POSTAVKE_SUSTAV);
      posaljiPCLog(F("Sustavske postavke spremljene"));
      break;
    case KEY_LEFT:
      if (odabraniIndex == 0) {
        lcdPozadinskoOsvjetljenjeUredjivanje = !lcdPozadinskoOsvjetljenjeUredjivanje;
      } else if (odabraniIndex == 1) {
        logiranjeUredjivanje = !logiranjeUredjivanje;
      } else if (odabraniIndex == 2) {
        rs485Uredjivanje = !rs485Uredjivanje;
      } else if (odabraniIndex == 3) {
        trajanjeImpulsaCekicaUredjivanje =
            prilagodiTrajanjeImpulsaCekicaZaMeni(trajanjeImpulsaCekicaUredjivanje, -1);
      } else if (odabraniIndex == 4) {
        inercijaZvona1Uredjivanje =
            prilagodiInercijuZvonaZaMeni(inercijaZvona1Uredjivanje, -1);
      } else if (odabraniIndex == 5) {
        inercijaZvona2Uredjivanje =
            prilagodiInercijuZvonaZaMeni(inercijaZvona2Uredjivanje, -1);
      }
      break;
    case KEY_RIGHT:
      if (odabraniIndex == 0) {
        lcdPozadinskoOsvjetljenjeUredjivanje = !lcdPozadinskoOsvjetljenjeUredjivanje;
      } else if (odabraniIndex == 1) {
        logiranjeUredjivanje = !logiranjeUredjivanje;
      } else if (odabraniIndex == 2) {
        rs485Uredjivanje = !rs485Uredjivanje;
      } else if (odabraniIndex == 3) {
        trajanjeImpulsaCekicaUredjivanje =
            prilagodiTrajanjeImpulsaCekicaZaMeni(trajanjeImpulsaCekicaUredjivanje, 1);
      } else if (odabraniIndex == 4) {
        inercijaZvona1Uredjivanje =
            prilagodiInercijuZvonaZaMeni(inercijaZvona1Uredjivanje, 1);
      } else if (odabraniIndex == 5) {
        inercijaZvona2Uredjivanje =
            prilagodiInercijuZvonaZaMeni(inercijaZvona2Uredjivanje, 1);
      }
      break;
    case KEY_BACK:
      ucitajSustavZaUredjivanje();
      vratiNaGlavniMeniNaStavku(INDEX_POSTAVKE_SUSTAV);
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
      } else if (faza_tihih_sati == 1) {
        tihiSatDo = (tihiSatDo + 1) % 24;
      } else if (faza_tihih_sati == 2) {
        modOtkucavanjaUredjivanje = (modOtkucavanjaUredjivanje + 1) % 3;
      } else if (faza_tihih_sati == 3) {
        modSlavljenjaUredjivanje = (modSlavljenjaUredjivanje >= 2) ? 1 : 2;
      } else {
        modMrtvackogUredjivanje = (modMrtvackogUredjivanje >= 2) ? 1 : 2;
      }
      break;
    case KEY_DOWN:
      if (faza_tihih_sati == 0) {
        tihiSatOd = (tihiSatOd - 1 + 24) % 24;
      } else if (faza_tihih_sati == 1) {
        tihiSatDo = (tihiSatDo - 1 + 24) % 24;
      } else if (faza_tihih_sati == 2) {
        modOtkucavanjaUredjivanje = (modOtkucavanjaUredjivanje + 2) % 3;
      } else if (faza_tihih_sati == 3) {
        modSlavljenjaUredjivanje = (modSlavljenjaUredjivanje <= 1) ? 2 : 1;
      } else {
        modMrtvackogUredjivanje = (modMrtvackogUredjivanje <= 1) ? 2 : 1;
      }
      break;
    case KEY_LEFT:
      faza_tihih_sati = (faza_tihih_sati - 1 + 5) % 5;
      break;
    case KEY_RIGHT:
      faza_tihih_sati = (faza_tihih_sati + 1) % 5;
      break;
    case KEY_SELECT:
      potvrdiSpremanjeTihihSati();
      break;
    case KEY_BACK:
      faza_tihih_sati = 0;
      vratiNaGlavniMeniNaStavku(INDEX_POSTAVKE_TIHI_SATI);
      posaljiPCLog(F("Kompaktne postavke otkucavanja: odustajanje bez spremanja"));
      break;
    default:
      break;
  }
}

static void obradiKlucPostavkeCavala(KeyEvent event) {
  switch (event) {
    case KEY_LEFT:
      faza_postavki_cavala = (faza_postavki_cavala - 1 + 4) % 4;
      break;
    case KEY_RIGHT:
      faza_postavki_cavala = (faza_postavki_cavala + 1) % 4;
      break;
    case KEY_UP:
      if (faza_postavki_cavala == 0) {
        trajanjeZvonaRdMin = prebaciTrajanjeStapicaMinute(trajanjeZvonaRdMin, 1);
      } else if (faza_postavki_cavala == 1) {
        trajanjeZvonaNedMin = prebaciTrajanjeStapicaMinute(trajanjeZvonaNedMin, 1);
      } else if (faza_postavki_cavala == 2) {
        odgodaSlavljenjaSekundeUredjivanje =
            prebaciOdgoduSlavljenjaSekunde(odgodaSlavljenjaSekundeUredjivanje, 1);
      } else {
        trajanjeSlavljenjaMin = prebaciTrajanjeStapicaMinute(trajanjeSlavljenjaMin, 1);
      }
      break;
    case KEY_DOWN:
      if (faza_postavki_cavala == 0) {
        trajanjeZvonaRdMin = prebaciTrajanjeStapicaMinute(trajanjeZvonaRdMin, -1);
      } else if (faza_postavki_cavala == 1) {
        trajanjeZvonaNedMin = prebaciTrajanjeStapicaMinute(trajanjeZvonaNedMin, -1);
      } else if (faza_postavki_cavala == 2) {
        odgodaSlavljenjaSekundeUredjivanje =
            prebaciOdgoduSlavljenjaSekunde(odgodaSlavljenjaSekundeUredjivanje, -1);
      } else {
        trajanjeSlavljenjaMin = prebaciTrajanjeStapicaMinute(trajanjeSlavljenjaMin, -1);
      }
      break;
    case KEY_SELECT:
      potvrdiSpremanjePostavkiCavala();
      break;
    case KEY_BACK:
      faza_postavki_cavala = 0;
      vratiNaGlavniMeniNaStavku(INDEX_POSTAVKE_STAPICI);
      posaljiPCLog(F("Postavke stapica: odustajanje bez spremanja"));
      break;
    default:
      break;
  }
}

static void obradiKlucSunce(KeyEvent event) {
  const bool stranicaNocneRasvjete = jeStranicaNocneRasvjeteUSuncu();
  const uint8_t dogadaj =
      static_cast<uint8_t>(constrain(sunceDogadajUredjivanje, 0, SUNCEVI_DOGADAJ_BROJ - 1));
  switch (event) {
    case KEY_LEFT:
      if (stranicaNocneRasvjete) {
        pomakniStranicuSunca(-1);
      } else if (faza_postavki_sunca == 0) {
        pomakniStranicuSunca(-1);
      } else {
        faza_postavki_sunca = (faza_postavki_sunca - 1 + 4) % 4;
      }
      break;
    case KEY_RIGHT:
      if (stranicaNocneRasvjete) {
        pomakniStranicuSunca(1);
      } else if (faza_postavki_sunca >= 3) {
        pomakniStranicuSunca(1);
      } else {
        faza_postavki_sunca = (faza_postavki_sunca + 1) % 4;
      }
      break;
    case KEY_UP:
      if (stranicaNocneRasvjete) {
        nocnaRasvjetaUredjivanje = !nocnaRasvjetaUredjivanje;
      } else if (faza_postavki_sunca == 0) {
        // Stranica se u izborniku Sunce mijenja lijevo/desno kako bi
        // gore/dolje ostalo rezervirano samo za uredjivanje vrijednosti.
      } else if (faza_postavki_sunca == 1) {
        sunceOmogucenoUredjivanje[dogadaj] = !sunceOmogucenoUredjivanje[dogadaj];
      } else if (faza_postavki_sunca == 2) {
        sunceZvonoUredjivanje[dogadaj] =
            (sunceZvonoUredjivanje[dogadaj] >= 2) ? 1 : (sunceZvonoUredjivanje[dogadaj] + 1);
      } else {
        if (dogadaj != SUNCEVI_DOGADAJ_PODNE) {
          sunceOdgodaUredjivanje[dogadaj] =
              prilagodiSuncevuOdgoduZaKorak(sunceOdgodaUredjivanje[dogadaj], 1);
        }
      }
      break;
    case KEY_DOWN:
      if (stranicaNocneRasvjete) {
        nocnaRasvjetaUredjivanje = !nocnaRasvjetaUredjivanje;
      } else if (faza_postavki_sunca == 0) {
        // Stranica se u izborniku Sunce mijenja lijevo/desno kako bi
        // gore/dolje ostalo rezervirano samo za uredjivanje vrijednosti.
      } else if (faza_postavki_sunca == 1) {
        sunceOmogucenoUredjivanje[dogadaj] = !sunceOmogucenoUredjivanje[dogadaj];
      } else if (faza_postavki_sunca == 2) {
        sunceZvonoUredjivanje[dogadaj] =
            (sunceZvonoUredjivanje[dogadaj] <= 1) ? 2 : (sunceZvonoUredjivanje[dogadaj] - 1);
      } else {
        if (dogadaj != SUNCEVI_DOGADAJ_PODNE) {
          sunceOdgodaUredjivanje[dogadaj] =
              prilagodiSuncevuOdgoduZaKorak(sunceOdgodaUredjivanje[dogadaj], -1);
        }
      }
      break;
    case KEY_SELECT:
      potvrdiSpremanjeSuncevihDogadaja();
      break;
    case KEY_BACK:
      sunceDogadajUredjivanje = SUNCEVI_DOGADAJ_JUTRO;
      faza_postavki_sunca = 0;
      vratiNaGlavniMeniNaStavku(INDEX_POSTAVKE_SUNCE);
      break;
    default:
      break;
  }
}

static void obradiKlucBlagdani(KeyEvent event) {
  switch (event) {
    case KEY_LEFT:
      if (stranica_postavki_blagdana == 0 && faza_postavki_blagdana == 0) {
        stranica_postavki_blagdana = 1;
        faza_postavki_blagdana = 2;
      } else if (stranica_postavki_blagdana == 1 && faza_postavki_blagdana == 0) {
        stranica_postavki_blagdana = 0;
        faza_postavki_blagdana = 5;
      } else {
        --faza_postavki_blagdana;
      }
      break;
    case KEY_RIGHT:
      if (stranica_postavki_blagdana == 0 && faza_postavki_blagdana == 5) {
        stranica_postavki_blagdana = 1;
        faza_postavki_blagdana = 0;
      } else if (stranica_postavki_blagdana == 1 && faza_postavki_blagdana == 2) {
        stranica_postavki_blagdana = 0;
        faza_postavki_blagdana = 0;
      } else {
        ++faza_postavki_blagdana;
      }
      break;
    case KEY_UP:
    case KEY_DOWN:
      if (stranica_postavki_blagdana == 1) {
        const int smjer = (event == KEY_UP) ? 1 : -1;
        if (faza_postavki_blagdana == 0) {
          sviSvetiOmogucenoUredjivanje = !sviSvetiOmogucenoUredjivanje;
        } else if (faza_postavki_blagdana == 1) {
          sviSvetiPocetakSatUredjivanje =
              pomakniSatSviSvetiPocetak(sviSvetiPocetakSatUredjivanje, smjer);
        } else {
          sviSvetiZavrsetakSatUredjivanje =
              pomakniSatSviSvetiZavrsetak(sviSvetiZavrsetakSatUredjivanje, smjer);
        }
      } else if (faza_postavki_blagdana < 3) {
        promijeniBitMaske(blagdaniSlavljenjeMaskaUredjivanje, faza_postavki_blagdana);
      } else {
        promijeniBitMaske(blagdaniRazdobljaMaskaUredjivanje,
                          static_cast<uint8_t>(faza_postavki_blagdana - 3));
      }
      break;
    case KEY_SELECT:
      potvrdiSpremanjeBlagdana();
      break;
    case KEY_BACK:
      stranica_postavki_blagdana = 0;
      faza_postavki_blagdana = 0;
      vratiNaGlavniMeniNaStavku(INDEX_POSTAVKE_BLAGDANI);
      posaljiPCLog(F("Blagdani: odustajanje bez spremanja"));
      break;
    default:
      break;
  }
}

static void obradiKlucPostavkeKazaljki(KeyEvent event) {
  uint8_t znamenka = 0;
  if (dohvatiZnamenkuIzKljuce(event, &znamenka)) {
    dodajZnamenkuUnosaVremena(znamenka);
    return;
  }

  switch (event) {
    case KEY_LEFT:
      if (unosVremenaAktivan) {
        resetirajUnosVremena();
      }
      faza_korekcije = 0;
      break;
    case KEY_RIGHT:
      if (unosVremenaAktivan) {
        resetirajUnosVremena();
      }
      faza_korekcije = 1;
      break;
    case KEY_UP:
      if (unosVremenaAktivan) {
        resetirajUnosVremena();
      }
      if (faza_korekcije == 0) {
        korektniSat = (korektniSat >= 12) ? 1 : (korektniSat + 1);
      } else {
        korektnaMinuta = (korektnaMinuta + 1) % 60;
      }
      break;
    case KEY_DOWN:
      if (unosVremenaAktivan) {
        resetirajUnosVremena();
      }
      if (faza_korekcije == 0) {
        korektniSat = (korektniSat <= 1) ? 12 : (korektniSat - 1);
      } else {
        korektnaMinuta = (korektnaMinuta - 1 + 60) % 60;
      }
      break;
    case KEY_SELECT:
      if (unosVremenaAktivan) {
        if (!postaviVrijemeIzUnosaZaKazaljke()) {
          break;
        }
        resetirajUnosVremena();
      }

      {
        const int satKazaljke = (korektniSat % 12);
        postaviRucnuPozicijuKazaljki(satKazaljke, korektnaMinuta);
        postaviRucnuBlokaduKazaljki(false);
        zatraziPoravnanjeTaktaKazaljki();

        char log[112];
        snprintf(log,
                 sizeof(log),
                 "Kazaljke: postavljeno stanje %02d:%02d, krecem u automatsko poravnanje",
                 korektniSat,
                 korektnaMinuta);
        posaljiPCLog(log);
      }

      vratiNaGlavniMeniNaStavku(INDEX_POSTAVKE_KAZALJKE);
      break;
    case KEY_BACK:
      if (unosVremenaAktivan) {
        resetirajUnosVremena();
        break;
      }
      postaviRucnuBlokaduKazaljki(false);
      vratiNaGlavniMeniNaStavku(INDEX_POSTAVKE_KAZALJKE);
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
  wifiInfoStrana = 0;
  ntpOmogucenUredjivanje = jeNTPOmogucen();
  ucitajSustavZaUredjivanje();
  ucitajVrijemePloceZaUredjivanjeIzPozicije(dohvatiPozicijuPloce());
  resetirajUnosVremena();
  ucitajPostavkeCavalaZaUredjivanje();
  ucitajSunceveDogadajeZaUredjivanje();
  ucitajBlagdaneZaUredjivanje();
  faza_postavki_cavala = 0;
  sunceDogadajUredjivanje = SUNCEVI_DOGADAJ_JUTRO;
  faza_postavki_sunca = 0;
  stranica_postavki_blagdana = 0;
  faza_postavki_blagdana = 0;
  resetirajUnosVremena();

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
      if (jeTipkaZaOtvaranjeGlavnogMenija(event)) {
        odabraniIndex = 0;
        trenutnoStanje = MENU_STATE_MAIN_MENU;
        posaljiPCLog(F("Ulazak u glavni meni"));
      }
      break;

    case MENU_STATE_MAIN_MENU:
      obradiKlucGlavniMeni(event);
      break;

    case MENU_STATE_CLOCK_SETTINGS:
      obradiKlucMaticniSat(event);
      break;

    case MENU_STATE_HAND_SETTINGS:
      obradiKlucPostavkeKazaljki(event);
      break;

    case MENU_STATE_PLATE_SETTINGS:
      obradiKlucPloce(event);
      break;

    case MENU_STATE_NETWORK_SETTINGS:
      obradiKlucMreza(event);
      break;

    case MENU_STATE_SYSTEM_SETTINGS:
      obradiKlucSustav(event);
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

    case MENU_STATE_HOLIDAY_SETTINGS:
      obradiKlucBlagdani(event);
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
  trenutnoStanje = MENU_STATE_DISPLAY_TIME;
  odabraniIndex = 0;
  postaviRucnuBlokaduKazaljki(false);
  postaviRucnuBlokaduPloce(false);
  faza_tihih_sati = 0;
  faza_postavki_cavala = 0;
  sunceDogadajUredjivanje = SUNCEVI_DOGADAJ_JUTRO;
  faza_postavki_sunca = 0;
  stranica_postavki_blagdana = 0;
  faza_postavki_blagdana = 0;
  resetirajUnosVremena();
  ntpOmogucenUredjivanje = jeNTPOmogucen();
  ucitajSustavZaUredjivanje();
  ucitajSunceveDogadajeZaUredjivanje();
  ucitajBlagdaneZaUredjivanje();
  ucitajVrijemePloceZaUredjivanjeIzPozicije(dohvatiPozicijuPloce());
  prisiliOsvjezavanjeGlavnogPrikazaLCD();
  zadnjaAktivnost = millis();
  posaljiPCLog(F("Povratak na prikaz sata"));
}

void osvjeziLCDZaMeni() {
  lcd.noBlink();
  lcd.noCursor();

  switch (trenutnoStanje) {
    case MENU_STATE_DISPLAY_TIME:
      prikaziSat();
      break;
    case MENU_STATE_MAIN_MENU:
      prikaziGlavniMeni();
      break;
    case MENU_STATE_CLOCK_SETTINGS:
      prikaziMaticniSatMenu();
      break;
    case MENU_STATE_HAND_SETTINGS:
      prikaziKazaljkeMenu();
      break;
    case MENU_STATE_PLATE_SETTINGS:
      prikaziPlocaMenu();
      break;
    case MENU_STATE_NETWORK_SETTINGS:
      prikaziMrezuMenu();
      break;
    case MENU_STATE_SYSTEM_SETTINGS:
      prikaziSustavMenu();
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
    case MENU_STATE_HOLIDAY_SETTINGS:
      prikaziBlagdane();
      break;
    case MENU_STATE_WIFI_IP_DISPLAY:
      prikaziWiFiIP();
      break;
    default:
      break;
  }
}
