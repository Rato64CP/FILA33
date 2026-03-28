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
#include "debouncing.h"
#include "podesavanja_piny.h"

// ==================== STATE MACHINE ====================

static MenuState trenutnoStanje = MENU_STATE_DISPLAY_TIME;
static unsigned long zadnjaAktivnost = 0;
static const unsigned long TIMEOUT_MENIJA_MS = 30000; // 30 seconds auto-return to clock

// ==================== MENU NAVIGATION ====================

static int odabraniIndex = 0;
static const int BROJ_STAVKI_GLAVNI_MENU = 5;
static const char TEKST_GLAVNI_KOREKCIJA_RUKU[] PROGMEM = "Korekcija ruku";
static const char TEKST_GLAVNI_POSTAVKE[] PROGMEM = "Postavke";
static const char TEKST_GLAVNI_INFORMACIJE[] PROGMEM = "Informacije";
static const char TEKST_GLAVNI_WIFI[] PROGMEM = "WiFi";
static const char TEKST_GLAVNI_POVRATAK[] PROGMEM = "Povratak";
static const char* const stavkeGlavnogMenuja[] PROGMEM = {
  TEKST_GLAVNI_KOREKCIJA_RUKU,
  TEKST_GLAVNI_POSTAVKE,
  TEKST_GLAVNI_INFORMACIJE,
  TEKST_GLAVNI_WIFI,
  TEKST_GLAVNI_POVRATAK
};

static const int BROJ_STAVKI_POSTAVKI = 9;
static const char TEKST_POSTAVKE_VRIJEME[] PROGMEM = "Vrijeme";
static const char TEKST_POSTAVKE_MOD_ZVONA[] PROGMEM = "Mod zvona";
static const char TEKST_POSTAVKE_TIHI_SATI[] PROGMEM = "Tihi sati";
static const char TEKST_POSTAVKE_CAVLI[] PROGMEM = "Cavli";
static const char TEKST_POSTAVKE_SLAVLJENJE[] PROGMEM = "Slavljenje";
static const char TEKST_POSTAVKE_SINKRONIZ[] PROGMEM = "Sinkroniz.";
static const char TEKST_POSTAVKE_LCD[] PROGMEM = "LCD svjetlo";
static const char TEKST_POSTAVKE_MQTT[] PROGMEM = "MQTT";
static const char TEKST_POSTAVKE_POVRATAK[] PROGMEM = "Povratak";
static const char* const stavkePostavki[] PROGMEM = {
  TEKST_POSTAVKE_VRIJEME,
  TEKST_POSTAVKE_MOD_ZVONA,
  TEKST_POSTAVKE_TIHI_SATI,
  TEKST_POSTAVKE_CAVLI,
  TEKST_POSTAVKE_SLAVLJENJE,
  TEKST_POSTAVKE_SINKRONIZ,
  TEKST_POSTAVKE_LCD,
  TEKST_POSTAVKE_MQTT,
  TEKST_POSTAVKE_POVRATAK
};

static const int BROJ_STAVKI_MODA = 3;
static const char TEKST_MOD_NORMALNO[] PROGMEM = "Normalno";
static const char TEKST_MOD_SLAVLJENJE[] PROGMEM = "Slavljenje";
static const char TEKST_MOD_POVRATAK[] PROGMEM = "Povratak";
static const char* const stavkeModa[] PROGMEM = {
  TEKST_MOD_NORMALNO,
  TEKST_MOD_SLAVLJENJE,
  TEKST_MOD_POVRATAK
};

// ==================== CONFIRMATION DIALOG ====================

static bool zadnja_izboru_je_da = true;
static char porukaZaKonfirmaciju[17] = "";
static void (*funkcijaNaDA)() = NULL;
static MenuState stanjePovratkaKonfirmacije = MENU_STATE_DISPLAY_TIME;

// ==================== PASSWORD ENTRY ====================

static char unesenaLozinka[9] = "";
static int pozicija_lozinke = 0;
static bool u_modu_lozinke = false;

// ==================== HAND CORRECTION ====================

static int korektnaMinuta = 0;
static int korektniSat = 0;
static bool u_korekciji_ruku = false;
static int faza_korekcije = 0; // 0 = hours, 1 = minutes, 2 = confirm

// ==================== TIME ADJUSTMENT ====================

static int privremeniSat = 0;
static int privremenaMinuta = 0;
static int privremenaSekuned = 0;
static int faza_vremena = 0; // 0 = hours, 1 = minutes, 2 = seconds, 3 = confirm
static bool potvrdiVrijeme = true;

// ==================== QUIET HOURS ADJUSTMENT ====================

static int tihiSatOd = 22;
static int tihiSatDo = 6;
static int faza_tihih_sati = 0; // 0 = OD, 1 = DO

static int trajanjeZvonaRdMin = 2;
static int trajanjeZvonaNedMin = 3;
static int trajanjeSlavljenjaMin = 2;
static bool slavljenjePrijeZvonjenja = false;
static int brojMjestaZaCavleUredjivanje = 5;
static int brojZvonaUredjivanje = 2;
static uint8_t cavliRadniUredjivanje[4] = {1, 2, 0, 0};
static uint8_t cavliNedjeljaUredjivanje[4] = {3, 4, 0, 0};
static uint8_t cavaoSlavljenjaUredjivanje = 5;
static uint8_t cavaoMrtvackogUredjivanje = 0;
static int faza_postavki_cavala = 0;

static int infoStrana = 0;
static const int BROJ_INFO_STRANA = 3;
static int wifiStrana = 0;
static const int BROJ_WIFI_STRANA = 3;
static bool wifiUredjivanjeAktivno = false;
static int wifiPolje = 0; // 0 = SSID, 1 = lozinka
static int wifiKursor = 0;
static int wifiOdabirZnaka = 0;
union MrezniUredjivanje {
  struct {
    char ssid[33];
    char lozinka[33];
  } wifi;
  struct {
    char broker[40];
    char korisnik[33];
    char lozinka[33];
  } mqtt;
  struct {
    char ntpServer[40];
  } sink;
};
static MrezniUredjivanje mrezniUredjivanje = {};
#define wifiSsidUredjivanje mrezniUredjivanje.wifi.ssid
#define wifiLozinkaUredjivanje mrezniUredjivanje.wifi.lozinka
static const char WIFI_SKUP_ZNAKOVA[] PROGMEM =
  " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-.,!@#";
static int mqttStrana = 0;
static const int BROJ_MQTT_STRANA = 6;
static bool mqttUredjivanjeAktivno = false;
static int mqttPolje = 0; // 0 = broker, 1 = korisnik, 2 = lozinka
static int mqttKursor = 0;
static bool mqttOmogucenUredjivanje = false;
static uint16_t mqttPortUredjivanje = 1883;
#define mqttBrokerUredjivanje mrezniUredjivanje.mqtt.broker
#define mqttKorisnikUredjivanje mrezniUredjivanje.mqtt.korisnik
#define mqttLozinkaUredjivanje mrezniUredjivanje.mqtt.lozinka
static int sinkStrana = 0;
static const int BROJ_SINK_STRANA = 3;
static bool sinkUredjivanjeAktivno = false;
static int sinkKursor = 0;
static bool dcfOmogucenUredjivanje = true;
#define ntpServerUredjivanje mrezniUredjivanje.sink.ntpServer

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

static char* dohvatiWiFiBufferZaUredjivanje() {
  return (wifiPolje == 0) ? wifiSsidUredjivanje : wifiLozinkaUredjivanje;
}

static int dohvatiWiFiMaxDuljinu() {
  return (wifiPolje == 0) ? 32 : 32;
}

static char* dohvatiMQTTBufferZaUredjivanje() {
  if (mqttPolje == 0) return mqttBrokerUredjivanje;
  return (mqttPolje == 1) ? mqttKorisnikUredjivanje : mqttLozinkaUredjivanje;
}

static int dohvatiMQTTMaxDuljinu() {
  if (mqttPolje == 0) return 39;
  return 32;
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

static int dohvatiBrojZnakovaZaUredjivanje() {
  return static_cast<int>(sizeof(WIFI_SKUP_ZNAKOVA) - 1);
}

static char dohvatiZnakZaUredjivanje(int indeks) {
  return static_cast<char>(pgm_read_byte(&WIFI_SKUP_ZNAKOVA[indeks]));
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

static void ucitajWiFiUredjivanjeIzPostavki() {
  strncpy(wifiSsidUredjivanje, dohvatiWifiSsid(), sizeof(wifiSsidUredjivanje) - 1);
  wifiSsidUredjivanje[sizeof(wifiSsidUredjivanje) - 1] = '\0';
  strncpy(wifiLozinkaUredjivanje, dohvatiWifiLozinku(), sizeof(wifiLozinkaUredjivanje) - 1);
  wifiLozinkaUredjivanje[sizeof(wifiLozinkaUredjivanje) - 1] = '\0';
}

static void pokreniWiFiUredjivanje(int polje) {
  wifiPolje = polje;
  wifiKursor = 0;
  wifiOdabirZnaka = pronadiIndeksZnaka(dohvatiWiFiBufferZaUredjivanje()[wifiKursor]);
  wifiUredjivanjeAktivno = true;
}

static void obrisiJedanWiFiZnak() {
  char* buffer = dohvatiWiFiBufferZaUredjivanje();
  const int duljina = static_cast<int>(strlen(buffer));
  if (duljina == 0) {
    return;
  }

  if (wifiKursor >= duljina) {
    wifiKursor = duljina - 1;
  }

  for (int i = wifiKursor; i < duljina; ++i) {
    buffer[i] = buffer[i + 1];
  }

  if (wifiKursor > 0 && wifiKursor >= static_cast<int>(strlen(buffer))) {
    wifiKursor--;
  }
  wifiOdabirZnaka = pronadiIndeksZnaka(buffer[wifiKursor]);
}

static void ucitajMQTTUredjivanjeIzPostavki() {
  mqttOmogucenUredjivanje = jeMQTTOmogucen();
  mqttPortUredjivanje = dohvatiMQTTPort();
  strncpy(mqttBrokerUredjivanje, dohvatiMQTTBroker(), sizeof(mqttBrokerUredjivanje) - 1);
  mqttBrokerUredjivanje[sizeof(mqttBrokerUredjivanje) - 1] = '\0';
  strncpy(mqttKorisnikUredjivanje, dohvatiMQTTKorisnika(), sizeof(mqttKorisnikUredjivanje) - 1);
  mqttKorisnikUredjivanje[sizeof(mqttKorisnikUredjivanje) - 1] = '\0';
  strncpy(mqttLozinkaUredjivanje, dohvatiMQTTLozinku(), sizeof(mqttLozinkaUredjivanje) - 1);
  mqttLozinkaUredjivanje[sizeof(mqttLozinkaUredjivanje) - 1] = '\0';
}

static void pokreniMQTTUredjivanje(int polje) {
  mqttPolje = polje;
  mqttKursor = 0;
  mqttUredjivanjeAktivno = true;
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
  if (brojMjestaZaCavleUredjivanje < 5) brojMjestaZaCavleUredjivanje = 5;
  if (brojMjestaZaCavleUredjivanje > 10) brojMjestaZaCavleUredjivanje = 10;
  if (brojZvonaUredjivanje < 1) brojZvonaUredjivanje = 1;
  if (brojZvonaUredjivanje > 4) brojZvonaUredjivanje = 4;

  for (uint8_t i = 0; i < 4; ++i) {
    cavliRadniUredjivanje[i] = ograniceniCavaoZaMeni(cavliRadniUredjivanje[i]);
    cavliNedjeljaUredjivanje[i] = ograniceniCavaoZaMeni(cavliNedjeljaUredjivanje[i]);
  }
  cavaoSlavljenjaUredjivanje = ograniceniCavaoZaMeni(cavaoSlavljenjaUredjivanje);
  cavaoMrtvackogUredjivanje = ograniceniCavaoZaMeni(cavaoMrtvackogUredjivanje);
}

static void ucitajPostavkeCavalaZaUredjivanje() {
  brojMjestaZaCavleUredjivanje = dohvatiBrojMjestaZaCavle();
  brojZvonaUredjivanje = dohvatiBrojZvona();
  for (uint8_t i = 0; i < 4; ++i) {
    cavliRadniUredjivanje[i] = dohvatiCavaoRadniZaZvono(i + 1);
    cavliNedjeljaUredjivanje[i] = dohvatiCavaoNedjeljaZaZvono(i + 1);
  }
  cavaoSlavljenjaUredjivanje = dohvatiCavaoSlavljenja();
  cavaoMrtvackogUredjivanje = dohvatiCavaoMrtvackog();
  trajanjeZvonaRdMin = dohvatiTrajanjeZvonjenjaRadniMin();
  trajanjeZvonaNedMin = dohvatiTrajanjeZvonjenjaNedjeljaMin();
  trajanjeSlavljenjaMin = dohvatiTrajanjeSlavljenjaMin();
  slavljenjePrijeZvonjenja = jeSlavljenjePrijeZvonjenja();
  sanitizirajRasporedCavalaUredjivanje();
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
  odabraniIndex = 2;
  zatvoriKonfirmaciju(MENU_STATE_SETTINGS);
  posaljiPCLog(F("Tihi sati spremljeni"));
}

static void potvrdiSpremanjePostavkiCavala() {
  sanitizirajRasporedCavalaUredjivanje();
  postaviRasporedCavala(static_cast<uint8_t>(brojMjestaZaCavleUredjivanje),
                       static_cast<uint8_t>(brojZvonaUredjivanje),
                       cavliRadniUredjivanje,
                       cavliNedjeljaUredjivanje,
                       cavaoSlavljenjaUredjivanje,
                       cavaoMrtvackogUredjivanje);
  postaviPostavkeCavala(trajanjeZvonaRdMin,
                        trajanjeZvonaNedMin,
                        trajanjeSlavljenjaMin,
                        slavljenjePrijeZvonjenja);
  faza_postavki_cavala = 0;
  odabraniIndex = 3;
  zatvoriKonfirmaciju(MENU_STATE_SETTINGS);
  posaljiPCLog(F("Postavke cavala spremljene"));
}

static void potvrdiSpremanjeWiFiPostavki() {
  postaviWiFiPodatke(wifiSsidUredjivanje, wifiLozinkaUredjivanje);
  posaljiWifiPostavkeESP();
  posaljiPCLog(F("WiFi: spremljene postavke i poslano ESP-u"));
  povratakNaGlavniPrikaz();
}

static void potvrdiSpremanjeMQTTPostavki() {
  postaviMQTTOmogucen(mqttOmogucenUredjivanje);
  postaviMQTTPodatke(
    mqttBrokerUredjivanje,
    mqttPortUredjivanje,
    mqttKorisnikUredjivanje,
    mqttLozinkaUredjivanje);
  if (mqttOmogucenUredjivanje) {
    char komanda[160];
    snprintf(
        komanda,
        sizeof(komanda),
        "MQTT:CONNECT|%s|%u|%s|%s",
        dohvatiMQTTBroker(),
        dohvatiMQTTPort(),
        dohvatiMQTTKorisnika(),
        dohvatiMQTTLozinku());
    posaljiESPKomandu(komanda);
  } else {
    posaljiESPKomandu("MQTT:DISCONNECT");
  }
  posaljiPCLog(F("MQTT: spremljene postavke i poslano ESP-u"));
  povratakNaGlavniPrikaz();
}

static void potvrdiSpremanjeSinkPostavki() {
  postaviSinkronizacijskePostavke(ntpServerUredjivanje, dcfOmogucenUredjivanje);
  posaljiNTPPostavkeESP();
  posaljiPCLog(F("Sinkronizacija: spremljene NTP i DCF postavke"));
  povratakNaGlavniPrikaz();
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
  if (odabraniIndex == 3) {
    snprintf(redak2, sizeof(redak2), "> Cavli %d/%dz",
             dohvatiBrojMjestaZaCavle(), dohvatiBrojZvona());
  } else if (odabraniIndex == 4) {
    snprintf(redak2, sizeof(redak2), "> Slavljenje:%d", dohvatiModSlavljenja());
  } else if (odabraniIndex == 5) {
    snprintf(redak2, sizeof(redak2), "> DCF: %s", jeDCFOmogucen() ? "ON" : "OFF");
  } else if (odabraniIndex == 6) {
    snprintf(redak2, sizeof(redak2), "> LCD: %s", jeLCDPozadinskoOsvjetljenjeUkljuceno() ? "ON" : "OFF");
  } else if (odabraniIndex == 7) {
    snprintf(redak2, sizeof(redak2), "> MQTT: %s", jeMQTTOmogucen() ? "ON" : "OFF");
  } else {
    char stavka[15];
    ucitajTekstIzProgmem(stavkePostavki, odabraniIndex, stavka, sizeof(stavka));
    snprintf(redak2, sizeof(redak2), "> %s", stavka);
  }
  prikaziPoruku(redak1, redak2);
}

static void prikaziIzbiraModaZvona() {
  char redak1[17];
  char redak2[17];
  kopirajLiteralIzFlash(redak1, sizeof(redak1), PSTR("MOD ZVONA"));
  char stavka[15];
  ucitajTekstIzProgmem(stavkeModa, odabraniIndex, stavka, sizeof(stavka));
  snprintf(redak2, sizeof(redak2), "> %s", stavka);
  prikaziPoruku(redak1, redak2);
}

static void prikaziKorekcijuRuku() {
  char redak1[17];
  char redak2[17];

  if (faza_korekcije == 0) {
    snprintf(redak1, sizeof(redak1), "Sat: %dh", korektniSat);
  } else if (faza_korekcije == 1) {
    snprintf(redak1, sizeof(redak1), "Minuta: %dm", korektnaMinuta);
  } else {
    snprintf(redak1, sizeof(redak1), "Potvrdi?");
  }

  kopirajLiteralIzFlash(redak2, sizeof(redak2), PSTR("[UP/DOWN podesi]"));
  prikaziPoruku(redak1, redak2);
}

static void prikaziPrilagodbanjeVremena() {
  char redak1[17];
  char redak2[17];

  if (faza_vremena == 0) {
    snprintf(redak1, sizeof(redak1), "Sat: %d", privremeniSat);
    kopirajLiteralIzFlash(redak2, sizeof(redak2), PSTR("[UP/DOWN][SEL]"));
  } else if (faza_vremena == 1) {
    snprintf(redak1, sizeof(redak1), "Minuta: %d", privremenaMinuta);
    kopirajLiteralIzFlash(redak2, sizeof(redak2), PSTR("[UP/DOWN][SEL]"));
  } else if (faza_vremena == 2) {
    snprintf(redak1, sizeof(redak1), "Sekunda: %d", privremenaSekuned);
    kopirajLiteralIzFlash(redak2, sizeof(redak2), PSTR("[UP/DOWN][SEL]"));
  } else {
    kopirajLiteralIzFlash(redak1, sizeof(redak1), PSTR("Spremi vrijeme?"));
    if (potvrdiVrijeme) {
      kopirajLiteralIzFlash(redak2, sizeof(redak2), PSTR(">DA        NE"));
    } else {
      kopirajLiteralIzFlash(redak2, sizeof(redak2), PSTR(" DA       >NE"));
    }
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
    snprintf(redak2, sizeof(redak2), "%d mjesta [SEL]", brojMjestaZaCavleUredjivanje);
  } else if (faza_postavki_cavala == 1) {
    kopirajLiteralIzFlash(redak1, sizeof(redak1), PSTR("Broj zvona"));
    snprintf(redak2, sizeof(redak2), "%d zvona [SEL]", brojZvonaUredjivanje);
  } else if (faza_postavki_cavala >= 2 && faza_postavki_cavala <= 5) {
    const int indeks = faza_postavki_cavala - 2;
    snprintf(redak1, sizeof(redak1), "RD zvono %d", indeks + 1);
    snprintf(redak2, sizeof(redak2), "cavao:%02d [SEL]", cavliRadniUredjivanje[indeks]);
  } else if (faza_postavki_cavala >= 6 && faza_postavki_cavala <= 9) {
    const int indeks = faza_postavki_cavala - 6;
    snprintf(redak1, sizeof(redak1), "NED zvono %d", indeks + 1);
    snprintf(redak2, sizeof(redak2), "cavao:%02d [SEL]", cavliNedjeljaUredjivanje[indeks]);
  } else if (faza_postavki_cavala == 10) {
    kopirajLiteralIzFlash(redak1, sizeof(redak1), PSTR("Slavljenje cav"));
    snprintf(redak2, sizeof(redak2), "SL:%02d [SEL]", cavaoSlavljenjaUredjivanje);
  } else if (faza_postavki_cavala == 11) {
    kopirajLiteralIzFlash(redak1, sizeof(redak1), PSTR("Mrtvacko cav"));
    snprintf(redak2, sizeof(redak2), "MRT:%02d [SEL]", cavaoMrtvackogUredjivanje);
  } else if (faza_postavki_cavala == 12) {
    kopirajLiteralIzFlash(redak1, sizeof(redak1), PSTR("Zvono RD 1-4m"));
    snprintf(redak2, sizeof(redak2), "RD:%d min [SEL]", trajanjeZvonaRdMin);
  } else if (faza_postavki_cavala == 13) {
    kopirajLiteralIzFlash(redak1, sizeof(redak1), PSTR("Zvono NED 1-4"));
    snprintf(redak2, sizeof(redak2), "NED:%d min [SEL]", trajanjeZvonaNedMin);
  } else if (faza_postavki_cavala == 14) {
    kopirajLiteralIzFlash(redak1, sizeof(redak1), PSTR("Slavljenje 1-4"));
    snprintf(redak2, sizeof(redak2), "SL:%d min [SEL]", trajanjeSlavljenjaMin);
  } else {
    kopirajLiteralIzFlash(redak1, sizeof(redak1), PSTR("Slavi prije?"));
    snprintf(redak2, sizeof(redak2), "%s [SEL]", slavljenjePrijeZvonjenja ? "PRIJE" : "POSLIJE");
  }
  prikaziPoruku(redak1, redak2);
}

static void prikaziWiFiConfig() {
  char redak1[17];
  char redak2[17];

  if (wifiUredjivanjeAktivno) {
    const char* oznakaPolja = (wifiPolje == 0) ? "SSID" : "PASS";
    const char* izvor = dohvatiWiFiBufferZaUredjivanje();
    const int maxDuljina = dohvatiWiFiMaxDuljinu();
    if (wifiKursor < 0) wifiKursor = 0;
    if (wifiKursor >= maxDuljina) wifiKursor = maxDuljina - 1;

    int pocetak = wifiKursor - 5;
    if (pocetak < 0) pocetak = 0;
    if (pocetak > maxDuljina - 10) pocetak = maxDuljina - 10;
    if (pocetak < 0) pocetak = 0;

    char pregled[11];
    for (int i = 0; i < 10; ++i) {
      const int indeks = pocetak + i;
      char znak = izvor[indeks];
      if (znak == '\0') {
        znak = '_';
      } else if (wifiPolje == 1) {
        znak = '*';
      }
      pregled[i] = znak;
    }
    pregled[10] = '\0';

    snprintf(redak1, sizeof(redak1), "%s:%-10s", oznakaPolja, pregled);
    const int brojZnakova = dohvatiBrojZnakovaZaUredjivanje();
    if (wifiOdabirZnaka < brojZnakova) {
      const char aktivniZnak = dohvatiZnakZaUredjivanje(wifiOdabirZnaka);
      snprintf(redak2, sizeof(redak2), "P%02d %c DEL OK", wifiKursor + 1, aktivniZnak);
    } else if (wifiOdabirZnaka == brojZnakova) {
      snprintf(redak2, sizeof(redak2), "P%02d DEL SEL", wifiKursor + 1);
    } else {
      snprintf(redak2, sizeof(redak2), "P%02d OK  SEL", wifiKursor + 1);
    }
  } else if (wifiStrana == 0) {
    kopirajLiteralIzFlash(redak1, sizeof(redak1), PSTR("WiFi SSID"));
    snprintf(redak2, sizeof(redak2), "%.16s", wifiSsidUredjivanje);
  } else if (wifiStrana == 1) {
    kopirajLiteralIzFlash(redak1, sizeof(redak1), PSTR("WiFi Lozinka"));
    const size_t duljina = strlen(wifiLozinkaUredjivanje);
    int maska = static_cast<int>(duljina);
    if (maska > 16) maska = 16;
    for (int i = 0; i < maska; ++i) {
      redak2[i] = '*';
    }
    for (int i = maska; i < 16; ++i) {
      redak2[i] = ' ';
    }
    redak2[16] = '\0';
  } else {
    kopirajLiteralIzFlash(redak1, sizeof(redak1), PSTR("WiFi spremanje"));
    kopirajLiteralIzFlash(redak2, sizeof(redak2), PSTR("SEL=Spremi ESP"));
  }

  redak1[sizeof(redak1) - 1] = '\0';
  redak2[sizeof(redak2) - 1] = '\0';
  prikaziPoruku(redak1, redak2);
}

static void prikaziMQTTConfig() {
  char redak1[17];
  char redak2[17];

  if (mqttUredjivanjeAktivno) {
    const char* oznakaPolja = (mqttPolje == 0) ? "BROKER" : ((mqttPolje == 1) ? "USER" : "PASS");
    const char* izvor = dohvatiMQTTBufferZaUredjivanje();
    const int maxDuljina = dohvatiMQTTMaxDuljinu();
    if (mqttKursor < 0) mqttKursor = 0;
    if (mqttKursor >= maxDuljina) mqttKursor = maxDuljina - 1;

    int pocetak = mqttKursor - 5;
    if (pocetak < 0) pocetak = 0;
    if (pocetak > maxDuljina - 10) pocetak = maxDuljina - 10;
    if (pocetak < 0) pocetak = 0;

    char pregled[11];
    for (int i = 0; i < 10; ++i) {
      const int indeks = pocetak + i;
      char znak = izvor[indeks];
      if (znak == '\0') {
        znak = '_';
      } else if (mqttPolje == 2) {
        znak = '*';
      }
      pregled[i] = znak;
    }
    pregled[10] = '\0';

    snprintf(redak1, sizeof(redak1), "%s:%-10s", oznakaPolja, pregled);
    const char aktivniZnak = izvor[mqttKursor] == '\0' ? '_' : izvor[mqttKursor];
    snprintf(redak2, sizeof(redak2), "P%02d %c L/R U/D", mqttKursor + 1, aktivniZnak);
  } else if (mqttStrana == 0) {
    kopirajLiteralIzFlash(redak1, sizeof(redak1), PSTR("MQTT aktivan"));
    snprintf(redak2, sizeof(redak2), "%s [SEL]", mqttOmogucenUredjivanje ? "ON" : "OFF");
  } else if (mqttStrana == 1) {
    kopirajLiteralIzFlash(redak1, sizeof(redak1), PSTR("MQTT Broker"));
    snprintf(redak2, sizeof(redak2), "%.16s", mqttBrokerUredjivanje);
  } else if (mqttStrana == 2) {
    kopirajLiteralIzFlash(redak1, sizeof(redak1), PSTR("MQTT Port"));
    snprintf(redak2, sizeof(redak2), "%u L/R10 U/D1", mqttPortUredjivanje);
  } else if (mqttStrana == 3) {
    kopirajLiteralIzFlash(redak1, sizeof(redak1), PSTR("MQTT User"));
    snprintf(redak2, sizeof(redak2), "%.16s", mqttKorisnikUredjivanje);
  } else if (mqttStrana == 4) {
    kopirajLiteralIzFlash(redak1, sizeof(redak1), PSTR("MQTT Lozinka"));
    const size_t duljina = strlen(mqttLozinkaUredjivanje);
    int maska = static_cast<int>(duljina);
    if (maska > 16) maska = 16;
    for (int i = 0; i < maska; ++i) {
      redak2[i] = '*';
    }
    for (int i = maska; i < 16; ++i) {
      redak2[i] = ' ';
    }
    redak2[16] = '\0';
  } else {
    kopirajLiteralIzFlash(redak1, sizeof(redak1), PSTR("MQTT spremanje"));
    kopirajLiteralIzFlash(redak2, sizeof(redak2), PSTR("SEL=Spremi ESP"));
  }

  redak1[sizeof(redak1) - 1] = '\0';
  redak2[sizeof(redak2) - 1] = '\0';
  prikaziPoruku(redak1, redak2);
}

static void prikaziSinkConfig() {
  char redak1[17];
  char redak2[17];

  if (sinkUredjivanjeAktivno) {
    const int maxDuljina = dohvatiSinkMaxDuljinu();
    if (sinkKursor < 0) sinkKursor = 0;
    if (sinkKursor >= maxDuljina) sinkKursor = maxDuljina - 1;

    int pocetak = sinkKursor - 5;
    if (pocetak < 0) pocetak = 0;
    if (pocetak > maxDuljina - 10) pocetak = maxDuljina - 10;
    if (pocetak < 0) pocetak = 0;

    char pregled[11];
    for (int i = 0; i < 10; ++i) {
      const int indeks = pocetak + i;
      char znak = ntpServerUredjivanje[indeks];
      pregled[i] = (znak == '\0') ? '_' : znak;
    }
    pregled[10] = '\0';

    snprintf(redak1, sizeof(redak1), "NTP:%-10s", pregled);
    const char aktivniZnak = ntpServerUredjivanje[sinkKursor] == '\0' ? '_' : ntpServerUredjivanje[sinkKursor];
    snprintf(redak2, sizeof(redak2), "P%02d %c L/R U/D", sinkKursor + 1, aktivniZnak);
  } else if (sinkStrana == 0) {
    kopirajLiteralIzFlash(redak1, sizeof(redak1), PSTR("DCF antena"));
    snprintf(redak2, sizeof(redak2), "%s [SEL]", dcfOmogucenUredjivanje ? "ON" : "OFF");
  } else if (sinkStrana == 1) {
    kopirajLiteralIzFlash(redak1, sizeof(redak1), PSTR("NTP server"));
    snprintf(redak2, sizeof(redak2), "%.16s", ntpServerUredjivanje);
  } else {
    kopirajLiteralIzFlash(redak1, sizeof(redak1), PSTR("Sink spremanje"));
    kopirajLiteralIzFlash(redak2, sizeof(redak2), PSTR("SEL=Spremi ESP"));
  }

  redak1[sizeof(redak1) - 1] = '\0';
  redak2[sizeof(redak2) - 1] = '\0';
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
    int memorKazMin = dohvatiMemoriraneKazaljkeMinuta();
    int satKaz = memorKazMin / 60;
    int minKaz = memorKazMin % 60;

    snprintf(redak1, sizeof(redak1), "Kaz:%d:%02d", satKaz, minKaz);
    kopirajLiteralIzFlash(redak2, sizeof(redak2), PSTR("[LEFT/RIGHT]"));
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

static void prikaziUnos_Lozinke() {
  char redak1[17];
  char redak2[17];
  kopirajLiteralIzFlash(redak1, sizeof(redak1), PSTR("Lozinka:"));

  for (int i = 0; i < 8; i++) {
    if (unesenaLozinka[i] == '\0') {
      redak2[i] = '_';
    } else {
      redak2[i] = '*';
    }
  }
  redak2[8] = '\0';

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
        u_korekciji_ruku = true;
        faza_korekcije = 0;
        DateTime sada = dohvatiTrenutnoVrijeme();
        korektniSat = sada.hour();
        korektnaMinuta = sada.minute();
        trenutnoStanje = MENU_STATE_HAND_CORRECTION;
        posaljiPCLog(F("Ulazak u korekciju ruku"));
      } else if (odabraniIndex == 1) {
        odabraniIndex = 0;
        trenutnoStanje = MENU_STATE_SETTINGS;
        posaljiPCLog(F("Ulazak u postavke"));
      } else if (odabraniIndex == 2) {
        trenutnoStanje = MENU_STATE_INFO_DISPLAY;
        infoStrana = 0;
        posaljiPCLog(F("Ulazak u informacije"));
      } else if (odabraniIndex == 3) {
        trenutnoStanje = MENU_STATE_WIFI_CONFIG;
        wifiStrana = 0;
        wifiUredjivanjeAktivno = false;
        ucitajWiFiUredjivanjeIzPostavki();
        posaljiPCLog(F("Ulazak u WiFi"));
      } else if (odabraniIndex == 4) {
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
      if (odabraniIndex == 0) {
        DateTime sada = dohvatiTrenutnoVrijeme();
        privremeniSat = sada.hour();
        privremenaMinuta = sada.minute();
        privremenaSekuned = sada.second();
        faza_vremena = 0;
        potvrdiVrijeme = true;
        trenutnoStanje = MENU_STATE_TIME_ADJUST;
        posaljiPCLog(F("Ulazak u prilagodbu vremena"));
      } else if (odabraniIndex == 1) {
        odabraniIndex = 0;
        trenutnoStanje = MENU_STATE_MODE_SELECT;
        posaljiPCLog(F("Ulazak u izbor moda"));
      } else if (odabraniIndex == 2) {
        tihiSatOd = dohvatiTihiPeriodOdSata();
        tihiSatDo = dohvatiTihiPeriodDoSata();
        faza_tihih_sati = 0;
        trenutnoStanje = MENU_STATE_QUIET_HOURS;
        posaljiPCLog(F("Ulazak u podesavanje tihih sati"));
      } else if (odabraniIndex == 3) {
        ucitajPostavkeCavalaZaUredjivanje();
        faza_postavki_cavala = 0;
        trenutnoStanje = MENU_STATE_NAIL_SETTINGS;
        posaljiPCLog(F("Ulazak u postavke cavala"));
      } else if (odabraniIndex == 4) {
        const uint8_t noviMod = (dohvatiModSlavljenja() == 1) ? 2 : 1;
        postaviModSlavljenja(noviMod);
        char log[48];
        snprintf(log, sizeof(log), "Izbornik: mod slavljenja postavljen na %u", noviMod);
        posaljiPCLog(log);
      } else if (odabraniIndex == 5) {
        trenutnoStanje = MENU_STATE_SYNC_CONFIG;
        sinkStrana = 0;
        sinkUredjivanjeAktivno = false;
        ucitajSinkUredjivanjeIzPostavki();
        posaljiPCLog(F("Ulazak u sinkronizacijske postavke"));
      } else if (odabraniIndex == 6) {
        bool novoStanje = !jeLCDPozadinskoOsvjetljenjeUkljuceno();
        postaviLCDPozadinskoOsvjetljenje(novoStanje);
        primijeniLCDPozadinskoOsvjetljenje(novoStanje);
        posaljiPCLog(novoStanje ? F("Izbornik: LCD osvjetljenje postavljeno na ON")
                                : F("Izbornik: LCD osvjetljenje postavljeno na OFF"));
      } else if (odabraniIndex == 7) {
        trenutnoStanje = MENU_STATE_MQTT_CONFIG;
        mqttStrana = 0;
        mqttUredjivanjeAktivno = false;
        ucitajMQTTUredjivanjeIzPostavki();
        posaljiPCLog(F("Ulazak u MQTT postavke"));
      } else if (odabraniIndex == 8) {
        odabraniIndex = 1;
        trenutnoStanje = MENU_STATE_MAIN_MENU;
      }
      break;
    case KEY_BACK:
      odabraniIndex = 1;
      trenutnoStanje = MENU_STATE_MAIN_MENU;
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
      if (faza_postavki_cavala == 0) {
        brojMjestaZaCavleUredjivanje = (brojMjestaZaCavleUredjivanje >= 10) ? 5 : (brojMjestaZaCavleUredjivanje + 1);
      } else if (faza_postavki_cavala == 1) {
        brojZvonaUredjivanje = (brojZvonaUredjivanje >= 4) ? 1 : (brojZvonaUredjivanje + 1);
      } else if (faza_postavki_cavala >= 2 && faza_postavki_cavala <= 5) {
        prilagodiVrijednostCavla(&cavliRadniUredjivanje[faza_postavki_cavala - 2], 1);
      } else if (faza_postavki_cavala >= 6 && faza_postavki_cavala <= 9) {
        prilagodiVrijednostCavla(&cavliNedjeljaUredjivanje[faza_postavki_cavala - 6], 1);
      } else if (faza_postavki_cavala == 10) {
        prilagodiVrijednostCavla(&cavaoSlavljenjaUredjivanje, 1);
      } else if (faza_postavki_cavala == 11) {
        prilagodiVrijednostCavla(&cavaoMrtvackogUredjivanje, 1);
      } else if (faza_postavki_cavala == 12) {
        trajanjeZvonaRdMin = (trajanjeZvonaRdMin % 4) + 1;
      } else if (faza_postavki_cavala == 13) {
        trajanjeZvonaNedMin = (trajanjeZvonaNedMin % 4) + 1;
      } else if (faza_postavki_cavala == 14) {
        trajanjeSlavljenjaMin = (trajanjeSlavljenjaMin % 4) + 1;
      } else {
        slavljenjePrijeZvonjenja = !slavljenjePrijeZvonjenja;
      }
      sanitizirajRasporedCavalaUredjivanje();
      break;
    case KEY_DOWN:
      if (faza_postavki_cavala == 0) {
        brojMjestaZaCavleUredjivanje = (brojMjestaZaCavleUredjivanje <= 5) ? 10 : (brojMjestaZaCavleUredjivanje - 1);
      } else if (faza_postavki_cavala == 1) {
        brojZvonaUredjivanje = (brojZvonaUredjivanje <= 1) ? 4 : (brojZvonaUredjivanje - 1);
      } else if (faza_postavki_cavala >= 2 && faza_postavki_cavala <= 5) {
        prilagodiVrijednostCavla(&cavliRadniUredjivanje[faza_postavki_cavala - 2], -1);
      } else if (faza_postavki_cavala >= 6 && faza_postavki_cavala <= 9) {
        prilagodiVrijednostCavla(&cavliNedjeljaUredjivanje[faza_postavki_cavala - 6], -1);
      } else if (faza_postavki_cavala == 10) {
        prilagodiVrijednostCavla(&cavaoSlavljenjaUredjivanje, -1);
      } else if (faza_postavki_cavala == 11) {
        prilagodiVrijednostCavla(&cavaoMrtvackogUredjivanje, -1);
      } else if (faza_postavki_cavala == 12) {
        trajanjeZvonaRdMin = (trajanjeZvonaRdMin - 2 + 4) % 4 + 1;
      } else if (faza_postavki_cavala == 13) {
        trajanjeZvonaNedMin = (trajanjeZvonaNedMin - 2 + 4) % 4 + 1;
      } else if (faza_postavki_cavala == 14) {
        trajanjeSlavljenjaMin = (trajanjeSlavljenjaMin - 2 + 4) % 4 + 1;
      } else {
        slavljenjePrijeZvonjenja = !slavljenjePrijeZvonjenja;
      }
      sanitizirajRasporedCavalaUredjivanje();
      break;
    case KEY_SELECT:
      if (faza_postavki_cavala < 15) {
        faza_postavki_cavala++;
      } else {
        otvoriKonfirmaciju(PSTR("Spremi cavle?"), potvrdiSpremanjePostavkiCavala, MENU_STATE_NAIL_SETTINGS);
      }
      break;
    case KEY_BACK:
      faza_postavki_cavala = 0;
      trenutnoStanje = MENU_STATE_SETTINGS;
      posaljiPCLog(F("Postavke cavala: odustajanje bez spremanja"));
      break;
    default:
      break;
  }
}

static void obradiKlucKorekcija(KeyEvent event) {
  switch (event) {
    case KEY_UP:
      if (faza_korekcije == 0) {
        korektniSat = (korektniSat + 1) % 12;
      } else if (faza_korekcije == 1) {
        korektnaMinuta = (korektnaMinuta + 1) % 60;
      }
      break;
    case KEY_DOWN:
      if (faza_korekcije == 0) {
        korektniSat = (korektniSat - 1 + 12) % 12;
      } else if (faza_korekcije == 1) {
        korektnaMinuta = (korektnaMinuta - 1 + 60) % 60;
      }
      break;
    case KEY_SELECT:
      if (faza_korekcije < 1) {
        faza_korekcije++;
      } else {
        // Primijeni korekciju kroz istu dinamicku putanju kao NTP/manual sinkronizacija.
        postaviRucnuPozicijuKazaljki(korektniSat, korektnaMinuta);

        char log[40];
        snprintf(log, sizeof(log), "Korekcija ruku: %d:%02d", korektniSat, korektnaMinuta);
        posaljiPCLog(log);

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

static void obradiKlucPrilagodbanjeVremena(KeyEvent event) {
  switch (event) {
    case KEY_UP:
      if (faza_vremena == 0) {
        privremeniSat = (privremeniSat + 1) % 24;
      } else if (faza_vremena == 1) {
        privremenaMinuta = (privremenaMinuta + 1) % 60;
      } else if (faza_vremena == 2) {
        privremenaSekuned = (privremenaSekuned + 1) % 60;
      } else {
        potvrdiVrijeme = true;
      }
      break;
    case KEY_DOWN:
      if (faza_vremena == 0) {
        privremeniSat = (privremeniSat - 1 + 24) % 24;
      } else if (faza_vremena == 1) {
        privremenaMinuta = (privremenaMinuta - 1 + 60) % 60;
      } else if (faza_vremena == 2) {
        privremenaSekuned = (privremenaSekuned - 1 + 60) % 60;
      } else {
        potvrdiVrijeme = false;
      }
      break;
    case KEY_LEFT:
      if (faza_vremena == 3) {
        potvrdiVrijeme = true;
      }
      break;
    case KEY_RIGHT:
      if (faza_vremena == 3) {
        potvrdiVrijeme = false;
      }
      break;
    case KEY_SELECT:
      if (faza_vremena < 2) {
        faza_vremena++;
      } else if (faza_vremena == 2) {
        faza_vremena = 3;
        potvrdiVrijeme = true;
      } else if (potvrdiVrijeme) {
        const DateTime sada = dohvatiTrenutnoVrijeme();
        azurirajVrijemeRucno(DateTime(
          sada.year(), sada.month(), sada.day(),
          privremeniSat, privremenaMinuta, privremenaSekuned));
        povratakNaGlavniPrikaz();
      } else {
        povratakNaGlavniPrikaz();
      }
      break;
    case KEY_BACK:
      if (faza_vremena == 3) {
        faza_vremena = 2;
        potvrdiVrijeme = true;
      } else {
        povratakNaGlavniPrikaz();
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
        posaljiPCLog(F("Mod: Normalno"));
      } else if (odabraniIndex == 1) {
        zapocniSlavljenje();
        posaljiPCLog(F("Mod: Slavljenje"));
      } else {
        odabraniIndex = 1;
        trenutnoStanje = MENU_STATE_SETTINGS;
        break;
      }
      povratakNaGlavniPrikaz();
      break;
    case KEY_BACK:
      odabraniIndex = 1;
      trenutnoStanje = MENU_STATE_SETTINGS;
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
      odabraniIndex = 2;
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

static void obradiKlucLozinka(KeyEvent event) {
  switch (event) {
    case KEY_UP:
      if (pozicija_lozinke < 8 && unesenaLozinka[pozicija_lozinke] == '\0') {
        unesenaLozinka[pozicija_lozinke] = '0';
      } else if (unesenaLozinka[pozicija_lozinke] != '\0') {
        unesenaLozinka[pozicija_lozinke]++;
        if (unesenaLozinka[pozicija_lozinke] > '9') {
          unesenaLozinka[pozicija_lozinke] = '0';
        }
      }
      break;
    case KEY_DOWN:
      if (pozicija_lozinke < 8 && unesenaLozinka[pozicija_lozinke] != '\0') {
        unesenaLozinka[pozicija_lozinke]--;
        if (unesenaLozinka[pozicija_lozinke] < '0') {
          unesenaLozinka[pozicija_lozinke] = '9';
        }
      }
      break;
    case KEY_RIGHT:
      if (pozicija_lozinke < 7) {
        pozicija_lozinke++;
      }
      break;
    case KEY_LEFT:
      if (pozicija_lozinke > 0) {
        pozicija_lozinke--;
      }
      break;
    case KEY_SELECT:
      if (strcmp(unesenaLozinka, dohvatiWifiLozinku()) == 0) {
        posaljiPCLog(F("Lozinka OK - admin pristup"));
        u_modu_lozinke = false;
        memset(unesenaLozinka, 0, sizeof(unesenaLozinka));
        povratakNaGlavniPrikaz();
      } else {
        posaljiPCLog(F("Neispravna lozinka"));
        memset(unesenaLozinka, 0, sizeof(unesenaLozinka));
        pozicija_lozinke = 0;
      }
      break;
    case KEY_BACK:
      memset(unesenaLozinka, 0, sizeof(unesenaLozinka));
      pozicija_lozinke = 0;
      u_modu_lozinke = false;
      povratakNaGlavniPrikaz();
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
  memset(unesenaLozinka, 0, sizeof(unesenaLozinka));
  pozicija_lozinke = 0;
  u_korekciji_ruku = false;
  u_modu_lozinke = false;
  infoStrana = 0;
  wifiStrana = 0;
  wifiUredjivanjeAktivno = false;
  wifiPolje = 0;
  wifiKursor = 0;
  wifiOdabirZnaka = 0;
  mqttStrana = 0;
  mqttUredjivanjeAktivno = false;
  mqttPolje = 0;
  mqttKursor = 0;
  mqttOmogucenUredjivanje = jeMQTTOmogucen();
  mqttPortUredjivanje = dohvatiMQTTPort();
  sinkStrana = 0;
  sinkUredjivanjeAktivno = false;
  sinkKursor = 0;
  dcfOmogucenUredjivanje = jeDCFOmogucen();
  memset(&mrezniUredjivanje, 0, sizeof(mrezniUredjivanje));
  ucitajPostavkeCavalaZaUredjivanje();
  faza_postavki_cavala = 0;
  potvrdiVrijeme = true;

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

    case MENU_STATE_MODE_SELECT:
      obradiKlucModeSelect(event);
      break;

    case MENU_STATE_INFO_DISPLAY:
      obradiKlucInfo(event);
      break;

    case MENU_STATE_CONFIRMATION:
      obradiKlucKonfirmacija(event);
      break;

    case MENU_STATE_PASSWORD_ENTRY:
      obradiKlucLozinka(event);
      break;

    case MENU_STATE_WIFI_CONFIG:
      if (wifiUredjivanjeAktivno) {
        char* buffer = dohvatiWiFiBufferZaUredjivanje();
        const int brojZnakova = dohvatiBrojZnakovaZaUredjivanje();
        const int maxDuljina = dohvatiWiFiMaxDuljinu();
        const int brojOpcija = brojZnakova + 2; // DEL i OK

        if (event == KEY_LEFT || event == KEY_RIGHT) {
          if (event == KEY_LEFT) {
            wifiOdabirZnaka = (wifiOdabirZnaka - 1 + brojOpcija) % brojOpcija;
          } else {
            wifiOdabirZnaka = (wifiOdabirZnaka + 1) % brojOpcija;
          }
        } else if (event == KEY_UP) {
          if (wifiKursor > 0) {
            wifiKursor--;
          }
          wifiOdabirZnaka = pronadiIndeksZnaka(buffer[wifiKursor]);
        } else if (event == KEY_DOWN) {
          if (wifiKursor < (maxDuljina - 1)) {
            wifiKursor++;
            if (buffer[wifiKursor] == '\0') {
              buffer[wifiKursor] = ' ';
              buffer[wifiKursor + 1] = '\0';
            }
          }
          wifiOdabirZnaka = pronadiIndeksZnaka(buffer[wifiKursor]);
        } else if (event == KEY_SELECT) {
          if (wifiOdabirZnaka < brojZnakova) {
            buffer[wifiKursor] = dohvatiZnakZaUredjivanje(wifiOdabirZnaka);
            if (wifiKursor + 1 < maxDuljina && buffer[wifiKursor + 1] == '\0') {
              buffer[wifiKursor + 1] = '\0';
            }
            if (wifiKursor < (maxDuljina - 1)) {
              wifiKursor++;
            }
            wifiOdabirZnaka = pronadiIndeksZnaka(buffer[wifiKursor]);
          } else if (wifiOdabirZnaka == brojZnakova) {
            obrisiJedanWiFiZnak();
          } else {
            while (strlen(buffer) > 0 && buffer[strlen(buffer) - 1] == ' ') {
              buffer[strlen(buffer) - 1] = '\0';
            }
            wifiUredjivanjeAktivno = false;
          }
        } else if (event == KEY_BACK) {
          obrisiJedanWiFiZnak();
        }
      } else if (event == KEY_LEFT) {
        wifiStrana = (wifiStrana - 1 + BROJ_WIFI_STRANA) % BROJ_WIFI_STRANA;
      } else if (event == KEY_RIGHT) {
        wifiStrana = (wifiStrana + 1) % BROJ_WIFI_STRANA;
      } else if (event == KEY_SELECT) {
        if (wifiStrana == 0) {
          pokreniWiFiUredjivanje(0);
        } else if (wifiStrana == 1) {
          pokreniWiFiUredjivanje(1);
        } else {
          otvoriKonfirmaciju(PSTR("Spremi WiFi?"), potvrdiSpremanjeWiFiPostavki, MENU_STATE_WIFI_CONFIG);
        }
      } else if (event == KEY_BACK) {
        wifiStrana = 0;
        wifiUredjivanjeAktivno = false;
        odabraniIndex = 3;
        trenutnoStanje = MENU_STATE_MAIN_MENU;
      }
      break;

    case MENU_STATE_MQTT_CONFIG:
      if (mqttUredjivanjeAktivno) {
        char* buffer = dohvatiMQTTBufferZaUredjivanje();
        const int brojZnakova = dohvatiBrojZnakovaZaUredjivanje();
        const int maxDuljina = dohvatiMQTTMaxDuljinu();

        if (event == KEY_LEFT || event == KEY_RIGHT) {
          const int trenutniIndeks = pronadiIndeksZnaka(buffer[mqttKursor]);
          int noviIndeks = trenutniIndeks;
          if (event == KEY_LEFT) {
            noviIndeks = (trenutniIndeks - 1 + brojZnakova) % brojZnakova;
          } else {
            noviIndeks = (trenutniIndeks + 1) % brojZnakova;
          }
          buffer[mqttKursor] = dohvatiZnakZaUredjivanje(noviIndeks);
          if (mqttKursor + 1 < maxDuljina && buffer[mqttKursor + 1] == '\0') {
            buffer[mqttKursor + 1] = '\0';
          }
        } else if (event == KEY_UP) {
          if (mqttKursor > 0) {
            mqttKursor--;
          }
        } else if (event == KEY_DOWN) {
          if (mqttKursor < (maxDuljina - 1)) {
            mqttKursor++;
            if (buffer[mqttKursor] == '\0') {
              buffer[mqttKursor] = ' ';
              buffer[mqttKursor + 1] = '\0';
            }
          }
        } else if (event == KEY_SELECT) {
          while (strlen(buffer) > 0 && buffer[strlen(buffer) - 1] == ' ') {
            buffer[strlen(buffer) - 1] = '\0';
          }
          mqttUredjivanjeAktivno = false;
        } else if (event == KEY_BACK) {
          buffer[mqttKursor] = '\0';
          while (mqttKursor > 0 && buffer[mqttKursor - 1] == '\0') {
            mqttKursor--;
          }
          mqttUredjivanjeAktivno = false;
        }
      } else if (event == KEY_LEFT) {
        if (mqttStrana == 2) {
          mqttPortUredjivanje = (mqttPortUredjivanje > 10) ? static_cast<uint16_t>(mqttPortUredjivanje - 10) : 1;
        } else {
          mqttStrana = (mqttStrana - 1 + BROJ_MQTT_STRANA) % BROJ_MQTT_STRANA;
        }
      } else if (event == KEY_RIGHT) {
        if (mqttStrana == 2) {
          mqttPortUredjivanje = (mqttPortUredjivanje < 65525) ? static_cast<uint16_t>(mqttPortUredjivanje + 10) : 65535;
        } else {
          mqttStrana = (mqttStrana + 1) % BROJ_MQTT_STRANA;
        }
      } else if (event == KEY_UP) {
        if (mqttStrana == 0) {
          mqttOmogucenUredjivanje = !mqttOmogucenUredjivanje;
        } else if (mqttStrana == 2 && mqttPortUredjivanje < 65535) {
          mqttPortUredjivanje++;
        }
      } else if (event == KEY_DOWN) {
        if (mqttStrana == 0) {
          mqttOmogucenUredjivanje = !mqttOmogucenUredjivanje;
        } else if (mqttStrana == 2 && mqttPortUredjivanje > 1) {
          mqttPortUredjivanje--;
        }
      } else if (event == KEY_SELECT) {
        if (mqttStrana == 0) {
          mqttOmogucenUredjivanje = !mqttOmogucenUredjivanje;
        } else if (mqttStrana == 1) {
          pokreniMQTTUredjivanje(0);
        } else if (mqttStrana == 2) {
          mqttPortUredjivanje = (mqttPortUredjivanje == 0) ? 1883 : mqttPortUredjivanje;
        } else if (mqttStrana == 3) {
          pokreniMQTTUredjivanje(1);
        } else if (mqttStrana == 4) {
          pokreniMQTTUredjivanje(2);
        } else {
          otvoriKonfirmaciju(PSTR("Spremi MQTT?"), potvrdiSpremanjeMQTTPostavki, MENU_STATE_MQTT_CONFIG);
        }
      } else if (event == KEY_BACK) {
        mqttStrana = 0;
        mqttUredjivanjeAktivno = false;
        odabraniIndex = 7;
        trenutnoStanje = MENU_STATE_SETTINGS;
      }
      break;

    case MENU_STATE_SYNC_CONFIG:
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
          sinkUredjivanjeAktivno = false;
        } else if (event == KEY_BACK) {
          ntpServerUredjivanje[sinkKursor] = '\0';
          while (sinkKursor > 0 && ntpServerUredjivanje[sinkKursor - 1] == '\0') {
            sinkKursor--;
          }
          sinkUredjivanjeAktivno = false;
        }
      } else if (event == KEY_LEFT) {
        sinkStrana = (sinkStrana - 1 + BROJ_SINK_STRANA) % BROJ_SINK_STRANA;
      } else if (event == KEY_RIGHT) {
        sinkStrana = (sinkStrana + 1) % BROJ_SINK_STRANA;
      } else if (event == KEY_UP || event == KEY_DOWN) {
        if (sinkStrana == 0) {
          dcfOmogucenUredjivanje = !dcfOmogucenUredjivanje;
        }
      } else if (event == KEY_SELECT) {
        if (sinkStrana == 0) {
          dcfOmogucenUredjivanje = !dcfOmogucenUredjivanje;
        } else if (sinkStrana == 1) {
          pokreniSinkUredjivanje();
        } else {
          otvoriKonfirmaciju(PSTR("Spremi sink?"), potvrdiSpremanjeSinkPostavki, MENU_STATE_SYNC_CONFIG);
        }
      } else if (event == KEY_BACK) {
        sinkStrana = 0;
        sinkUredjivanjeAktivno = false;
        odabraniIndex = 5;
        trenutnoStanje = MENU_STATE_SETTINGS;
      }
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
  u_korekciji_ruku = false;
  u_modu_lozinke = false;
  faza_vremena = 0;
  potvrdiVrijeme = true;
  faza_tihih_sati = 0;
  faza_postavki_cavala = 0;
  infoStrana = 0;
  wifiStrana = 0;
  wifiUredjivanjeAktivno = false;
  wifiPolje = 0;
  wifiKursor = 0;
  wifiOdabirZnaka = 0;
  mqttStrana = 0;
  mqttUredjivanjeAktivno = false;
  mqttPolje = 0;
  mqttKursor = 0;
  sinkStrana = 0;
  sinkUredjivanjeAktivno = false;
  sinkKursor = 0;
  memset(unesenaLozinka, 0, sizeof(unesenaLozinka));
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
    case MENU_STATE_MODE_SELECT:
      prikaziIzbiraModaZvona();
      break;
    case MENU_STATE_INFO_DISPLAY:
      prikaziInfoDisplay();
      break;
    case MENU_STATE_CONFIRMATION:
      prikaziKonfirmaciju();
      break;
    case MENU_STATE_PASSWORD_ENTRY:
      prikaziUnos_Lozinke();
      break;
    case MENU_STATE_WIFI_CONFIG:
      prikaziWiFiConfig();
      break;
    case MENU_STATE_MQTT_CONFIG:
      prikaziMQTTConfig();
      break;
    case MENU_STATE_SYNC_CONFIG:
      prikaziSinkConfig();
      break;
    default:
      break;
  }
}
