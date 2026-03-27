// menu_system.cpp – Comprehensive 6-key LCD menu system with state management
#include <Arduino.h>
#include <RTClib.h>
#include <Wire.h>
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
static const char* stavkeGlavnogMenuja[] = {
  "Korekcija ruku",
  "Postavke",
  "Informacije",
  "WiFi",
  "Povratak"
};

static const int BROJ_STAVKI_POSTAVKI = 5;
static const char* stavkePostavki[] = {
  "Vrijeme",
  "Mod zvona",
  "Tihi sati",
  "MQTT",
  "Povratak"
};

static const int BROJ_STAVKI_MODA = 3;
static const char* stavkeModa[] = {
  "Normalno",
  "Slavljenje",
  "Povratak"
};

// ==================== CONFIRMATION DIALOG ====================

static bool zadnja_izboru_je_da = true;
static const char* porukaZaKonfirmaciju[2] = {"", ""};
static void (*funkcijaNaDA)() = NULL;

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

static int infoStrana = 0;
static const int BROJ_INFO_STRANA = 3;
static int wifiStrana = 0;
static const int BROJ_WIFI_STRANA = 3;
static bool wifiUredjivanjeAktivno = false;
static int wifiPolje = 0; // 0 = SSID, 1 = lozinka
static int wifiKursor = 0;
static char wifiSsidUredjivanje[33] = "";
static char wifiLozinkaUredjivanje[33] = "";
static const char WIFI_SKUP_ZNAKOVA[] =
  " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-.,!@#";

// ==================== I2C ADDRESS DETECTION ====================

static void otkrijI2CAdrese() {
  Wire.begin();
  posaljiPCLog(F("Scanning I2C addresses..."));
  
  int dostupnihAdresi = 0;
  for (int adresa = 1; adresa < 127; adresa++) {
    Wire.beginTransmission(adresa);
    int greska = Wire.endTransmission();
    
    if (greska == 0) {
      String log = F("I2C uredjaj na adresi: 0x");
      log += String(adresa, HEX);
      
      // Identify common devices
      if (adresa == 0x27 || adresa == 0x3F) {
        log += F(" (LCD I2C)");
      } else if (adresa == 0x68) {
        log += F(" (DS3231 RTC)");
      } else if (adresa == 0x57) {
        log += F(" (24C32 EEPROM)");
      }
      
      posaljiPCLog(log);
      dostupnihAdresi++;
    }
  }
  
  String logSummary = F("Pronadjeno I2C uredjaja: ");
  logSummary += dostupnihAdresi;
  posaljiPCLog(logSummary);
}

static char* dohvatiWiFiBufferZaUredjivanje() {
  return (wifiPolje == 0) ? wifiSsidUredjivanje : wifiLozinkaUredjivanje;
}

static int dohvatiWiFiMaxDuljinu() {
  return (wifiPolje == 0) ? 32 : 32;
}

static int pronadiIndeksZnaka(char znak) {
  const int brojZnakova = static_cast<int>(strlen(WIFI_SKUP_ZNAKOVA));
  for (int i = 0; i < brojZnakova; ++i) {
    if (WIFI_SKUP_ZNAKOVA[i] == znak) {
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
  wifiUredjivanjeAktivno = true;
}

// ==================== MENU DISPLAY FUNCTIONS ====================

static void prikaziGlavniMeni() {
  // Fixed: Use prikaziPoruku instead of directly accessing lcd
  char redak1[17] = "GLAVNI MENI";
  char redak2[17];
  snprintf(redak2, sizeof(redak2), "> %s", stavkeGlavnogMenuja[odabraniIndex]);
  prikaziPoruku(redak1, redak2);
}

static void prikaziPostavkeMenu() {
  // Fixed: Use prikaziPoruku instead of directly accessing lcd
  char redak1[17] = "POSTAVKE";
  char redak2[17];
  if (odabraniIndex == 3) {
    snprintf(redak2, sizeof(redak2), "> MQTT: %s", jeMQTTOmogucen() ? "ON" : "OFF");
  } else {
    snprintf(redak2, sizeof(redak2), "> %s", stavkePostavki[odabraniIndex]);
  }
  prikaziPoruku(redak1, redak2);
}

static void prikaziIzbiraModaZvona() {
  // Fixed: Use prikaziPoruku instead of directly accessing lcd
  char redak1[17] = "MOD ZVONA";
  char redak2[17];
  snprintf(redak2, sizeof(redak2), "> %s", stavkeModa[odabraniIndex]);
  prikaziPoruku(redak1, redak2);
}

static void prikaziKorekcijuRuku() {
  // Fixed: Use prikaziPoruku instead of directly accessing lcd
  char redak1[17];
  char redak2[17];
  
  if (faza_korekcije == 0) {
    snprintf(redak1, sizeof(redak1), "Sat: %dh", korektniSat);
  } else if (faza_korekcije == 1) {
    snprintf(redak1, sizeof(redak1), "Minuta: %dm", korektnaMinuta);
  } else {
    snprintf(redak1, sizeof(redak1), "Potvrdi?");
  }
  
  strncpy(redak2, "[UP/DOWN podesi]", sizeof(redak2) - 1);
  redak2[sizeof(redak2) - 1] = '\0';
  prikaziPoruku(redak1, redak2);
}

static void prikaziPrilagodbanjeVremena() {
  // Fixed: Use prikaziPoruku instead of directly accessing lcd
  char redak1[17];
  char redak2[17];
  
  if (faza_vremena == 0) {
    snprintf(redak1, sizeof(redak1), "Sat: %d", privremeniSat);
    strncpy(redak2, "[UP/DOWN][SEL]", sizeof(redak2) - 1);
  } else if (faza_vremena == 1) {
    snprintf(redak1, sizeof(redak1), "Minuta: %d", privremenaMinuta);
    strncpy(redak2, "[UP/DOWN][SEL]", sizeof(redak2) - 1);
  } else if (faza_vremena == 2) {
    snprintf(redak1, sizeof(redak1), "Sekunda: %d", privremenaSekuned);
    strncpy(redak2, "[UP/DOWN][SEL]", sizeof(redak2) - 1);
  } else {
    strncpy(redak1, "Spremi vrijeme?", sizeof(redak1) - 1);
    if (potvrdiVrijeme) {
      strncpy(redak2, ">DA        NE", sizeof(redak2) - 1);
    } else {
      strncpy(redak2, " DA       >NE", sizeof(redak2) - 1);
    }
  }
  redak1[sizeof(redak1) - 1] = '\0';
  redak2[sizeof(redak2) - 1] = '\0';
  prikaziPoruku(redak1, redak2);
}

static void prikaziPodesavanjeTihihSati() {
  char redak1[17];
  char redak2[17];

  if (faza_tihih_sati == 0) {
    strncpy(redak1, "Tihi sati: OD", sizeof(redak1) - 1);
  } else {
    strncpy(redak1, "Tihi sati: DO", sizeof(redak1) - 1);
  }
  redak1[sizeof(redak1) - 1] = '\0';

  snprintf(redak2, sizeof(redak2), "OD:%02d DO:%02d", tihiSatOd, tihiSatDo);
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
    const char aktivniZnak = izvor[wifiKursor] == '\0' ? '_' : izvor[wifiKursor];
    snprintf(redak2, sizeof(redak2), "P%02d %c L/R U/D", wifiKursor + 1, aktivniZnak);
  } else if (wifiStrana == 0) {
    strncpy(redak1, "WiFi SSID", sizeof(redak1) - 1);
    snprintf(redak2, sizeof(redak2), "%.16s", wifiSsidUredjivanje);
  } else if (wifiStrana == 1) {
    strncpy(redak1, "WiFi Lozinka", sizeof(redak1) - 1);
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
    strncpy(redak1, "WiFi spremanje", sizeof(redak1) - 1);
    strncpy(redak2, "SEL=Spremi ESP", sizeof(redak2) - 1);
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
    snprintf(redak2, sizeof(redak2), "Izvor:%s", dohvatiIzvorVremena().c_str());
  } else if (infoStrana == 1) {
    int memorKazMin = dohvatiMemoriraneKazaljkeMinuta();
    int satKaz = memorKazMin / 60;
    int minKaz = memorKazMin % 60;
    
    snprintf(redak1, sizeof(redak1), "Kaz:%d:%02d", satKaz, minKaz);
    strncpy(redak2, "[LEFT/RIGHT]", sizeof(redak2) - 1);
  } else {
    int pozPloca = dohvatiPozicijuPloce();
    
    snprintf(redak1, sizeof(redak1), "Ploca:%d", pozPloca);
    strncpy(redak2, "[LEFT/RIGHT]", sizeof(redak2) - 1);
  }
  
  redak1[sizeof(redak1) - 1] = '\0';
  redak2[sizeof(redak2) - 1] = '\0';
  prikaziPoruku(redak1, redak2);
}

static void prikaziKonfirmaciju() {
  // Fixed: Use prikaziPoruku instead of directly accessing lcd
  char redak1[17];
  char redak2[17];
  
  strncpy(redak1, porukaZaKonfirmaciju[0], sizeof(redak1) - 1);
  redak1[sizeof(redak1) - 1] = '\0';
  
  if (zadnja_izboru_je_da) {
    strncpy(redak2, "> DA       NE", sizeof(redak2) - 1);
  } else {
    strncpy(redak2, "  DA     > NE", sizeof(redak2) - 1);
  }
  redak2[sizeof(redak2) - 1] = '\0';
  prikaziPoruku(redak1, redak2);
}

static void prikaziUnos_Lozinke() {
  // Fixed: Use prikaziPoruku instead of directly accessing lcd
  char redak1[17] = "Lozinka:";
  char redak2[17];
  
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
        // Time adjustment
        DateTime sada = dohvatiTrenutnoVrijeme();
        privremeniSat = sada.hour();
        privremenaMinuta = sada.minute();
        privremenaSekuned = sada.second();
        faza_vremena = 0;
        potvrdiVrijeme = true;
        trenutnoStanje = MENU_STATE_TIME_ADJUST;
        posaljiPCLog(F("Ulazak u prilagodbu vremena"));
      } else if (odabraniIndex == 1) {
        // Mode selection
        odabraniIndex = 0;
        trenutnoStanje = MENU_STATE_MODE_SELECT;
        posaljiPCLog(F("Ulazak u izbor moda"));
      } else if (odabraniIndex == 2) {
        // Quiet hours for hourly strikes
        tihiSatOd = dohvatiTihiPeriodOdSata();
        tihiSatDo = dohvatiTihiPeriodDoSata();
        faza_tihih_sati = 0;
        trenutnoStanje = MENU_STATE_QUIET_HOURS;
        posaljiPCLog(F("Ulazak u podesavanje tihih sati"));
      } else if (odabraniIndex == 3) {
        bool novoStanje = !jeMQTTOmogucen();
        postaviMQTTOmogucen(novoStanje);
        posaljiPCLog(novoStanje ? F("Izbornik: MQTT postavljen na ON") : F("Izbornik: MQTT postavljen na OFF"));
      } else if (odabraniIndex == 4) {
        // Back
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
        postaviTihiPeriodSatnihOtkucaja(tihiSatOd, tihiSatDo);
        faza_tihih_sati = 0;
        trenutnoStanje = MENU_STATE_SETTINGS;
        posaljiPCLog(F("Tihi sati spremljeni"));
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
        // Primijeni korekciju kroz istu dinamičku putanju kao NTP/manual sinkronizacija.
        postaviRucnuPozicijuKazaljki(korektniSat, korektnaMinuta);
        
        String log = F("Korekcija ruku: ");
        log += korektniSat;
        log += F(":");
        if (korektnaMinuta < 10) log += "0";
        log += korektnaMinuta;
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
      }
      povratakNaGlavniPrikaz();
      break;
    case KEY_BACK:
      povratakNaGlavniPrikaz();
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
      // Verify password
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
  memset(unesenaLozinka, 0, sizeof(unesenaLozinka));
  pozicija_lozinke = 0;
  u_korekciji_ruku = false;
  u_modu_lozinke = false;
  infoStrana = 0;
  wifiStrana = 0;
  wifiUredjivanjeAktivno = false;
  wifiPolje = 0;
  wifiKursor = 0;
  ucitajWiFiUredjivanjeIzPostavki();
  potvrdiVrijeme = true;
  
  otkrijI2CAdrese();
  
  posaljiPCLog(F("Menu sistem inicijaliziran"));
}

void upravljajMenuSistemom() {
  // Auto-return to clock if timeout
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
        const int brojZnakova = static_cast<int>(strlen(WIFI_SKUP_ZNAKOVA));
        const int maxDuljina = dohvatiWiFiMaxDuljinu();

        if (event == KEY_LEFT || event == KEY_RIGHT) {
          const int trenutniIndeks = pronadiIndeksZnaka(buffer[wifiKursor]);
          int noviIndeks = trenutniIndeks;
          if (event == KEY_LEFT) {
            noviIndeks = (trenutniIndeks - 1 + brojZnakova) % brojZnakova;
          } else {
            noviIndeks = (trenutniIndeks + 1) % brojZnakova;
          }
          buffer[wifiKursor] = WIFI_SKUP_ZNAKOVA[noviIndeks];
          if (wifiKursor + 1 < maxDuljina && buffer[wifiKursor + 1] == '\0') {
            buffer[wifiKursor + 1] = '\0';
          }
        } else if (event == KEY_UP) {
          if (wifiKursor > 0) {
            wifiKursor--;
          }
        } else if (event == KEY_DOWN) {
          if (wifiKursor < (maxDuljina - 1)) {
            wifiKursor++;
            if (buffer[wifiKursor] == '\0') {
              buffer[wifiKursor] = ' ';
              buffer[wifiKursor + 1] = '\0';
            }
          }
        } else if (event == KEY_SELECT) {
          while (strlen(buffer) > 0 && buffer[strlen(buffer) - 1] == ' ') {
            buffer[strlen(buffer) - 1] = '\0';
          }
          wifiUredjivanjeAktivno = false;
        } else if (event == KEY_BACK) {
          buffer[wifiKursor] = '\0';
          while (wifiKursor > 0 && buffer[wifiKursor - 1] == '\0') {
            wifiKursor--;
          }
          wifiUredjivanjeAktivno = false;
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
          postaviWiFiPodatke(wifiSsidUredjivanje, wifiLozinkaUredjivanje);
          posaljiWifiPostavkeESP();
          posaljiPCLog(F("WiFi: spremljene postavke i poslano ESP-u"));
          povratakNaGlavniPrikaz();
        }
      } else if (event == KEY_BACK) {
        wifiStrana = 0;
        wifiUredjivanjeAktivno = false;
        odabraniIndex = 3;
        trenutnoStanje = MENU_STATE_MAIN_MENU;
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
  trenutnoStanje = MENU_STATE_DISPLAY_TIME;
  odabraniIndex = 0;
  u_korekciji_ruku = false;
  u_modu_lozinke = false;
  faza_vremena = 0;
  potvrdiVrijeme = true;
  infoStrana = 0;
  wifiStrana = 0;
  wifiUredjivanjeAktivno = false;
  wifiPolje = 0;
  wifiKursor = 0;
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
    default:
      break;
  }
}
