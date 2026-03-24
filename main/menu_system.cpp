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

static const int BROJ_STAVKI_POSTAVKI = 4;
static const char* stavkePostavki[] = {
  "Vrijeme",
  "Mod zvona",
  "Sat zvona",
  "Povratak"
};

static const int BROJ_STAVKI_MODA = 3;
static const char* stavkeModa[] = {
  "Normalno",
  "Slavljenje",
  "Povratak"
};

// ==================== CONFIRMATION DIALOG ====================

static bool cekamo_da_ne = false;
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
  snprintf(redak2, sizeof(redak2), "> %s", stavkePostavki[odabraniIndex]);
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
    strncpy(redak1, "Potvrdi vrijeme", sizeof(redak1) - 1);
    strncpy(redak2, "DA/NE", sizeof(redak2) - 1);
  }
  redak1[sizeof(redak1) - 1] = '\0';
  redak2[sizeof(redak2) - 1] = '\0';
  prikaziPoruku(redak1, redak2);
}

static void prikaziWiFiConfig() {
  // Fixed: Use prikaziPoruku instead of directly accessing lcd
  char redak1[17] = "WiFi Setup";
  char redak2[17];
  strncpy(redak2, dohvatiWifiSsid(), sizeof(redak2) - 1);
  redak2[sizeof(redak2) - 1] = '\0';
  prikaziPoruku(redak1, redak2);
}

static void prikaziInfoDisplay() {
  // Fixed: Use prikaziPoruku instead of directly accessing lcd
  char redak1[17];
  char redak2[17];
  
  static int infoStrana = 0;
  
  if (infoStrana == 0) {
    snprintf(redak1, sizeof(redak1), "RTC:%s", jeRTCPouzdan() ? "OK" : "FAIL");
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
        odabraniIndex = 0;
        posaljiPCLog(F("Ulazak u informacije"));
      } else if (odabraniIndex == 3) {
        trenutnoStanje = MENU_STATE_WIFI_CONFIG;
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
        trenutnoStanje = MENU_STATE_TIME_ADJUST;
        posaljiPCLog(F("Ulazak u prilagodbu vremena"));
      } else if (odabraniIndex == 1) {
        // Mode selection
        odabraniIndex = 0;
        trenutnoStanje = MENU_STATE_MODE_SELECT;
        posaljiPCLog(F("Ulazak u izbor moda"));
      } else if (odabraniIndex == 2) {
        // Time enable/disable
        postaviBlokaduOtkucavanja(jeZvonoUTijeku());
        posaljiPCLog(F("Toggle otkucavanja"));
      } else if (odabraniIndex == 3) {
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
        // Apply correction
        int ciljnaMinuta = korektniSat * 60 + korektnaMinuta;
        kompenzirajKazaljke(false);
        
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
      }
      break;
    case KEY_DOWN:
      if (faza_vremena == 0) {
        privremeniSat = (privremeniSat - 1 + 24) % 24;
      } else if (faza_vremena == 1) {
        privremenaMinuta = (privremenaMinuta - 1 + 60) % 60;
      } else if (faza_vremena == 2) {
        privremenaSekuned = (privremenaSekuned - 1 + 60) % 60;
      }
      break;
    case KEY_SELECT:
      if (faza_vremena < 2) {
        faza_vremena++;
      } else {
        // Confirmation
        faza_vremena = 3;
      }
      break;
    case KEY_BACK:
      povratakNaGlavniPrikaz();
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
      // Cycle back through info screens (not implemented here but available)
      break;
    case KEY_RIGHT:
      // Cycle forward through info screens (not implemented here but available)
      break;
    case KEY_BACK:
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
      if (event == KEY_BACK) {
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

int dohvatiOdabraniIndex() {
  return odabraniIndex;
}

void potvrdiAkciju(bool da) {
  if (da && funkcijaNaDA != NULL) {
    funkcijaNaDA();
  }
  povratakNaGlavniPrikaz();
}

void ulaziUManjuLozinkom() {
  u_modu_lozinke = true;
  memset(unesenaLozinka, 0, sizeof(unesenaLozinka));
  pozicija_lozinke = 0;
  trenutnoStanje = MENU_STATE_PASSWORD_ENTRY;
  posaljiPCLog(F("Ulazak u modu unosa lozinke"));
}