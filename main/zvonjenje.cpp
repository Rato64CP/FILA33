// zvonjenje.cpp – BELL CONTROL ONLY (NO CELEBRATION/FUNERAL)
// Bell system for hourly (Bell 1) and half-hourly (Bell 2) strikes
// Celebration and Funeral modes moved to otkucavanje.cpp
//
// System Requirements:
// - uključiZvono()/isključiZvono() for BELL1/BELL2 only
// - obradiCavleNaPloči() after plate N phase completes
// - aktivirajZvonaAkoTrebaju() at HH:XX:30 (30s after minute start)
// - jeLiInerciaAktivna() - 90 second inertia from BELL commands only
// - MQTT: toranj/bell1/cmd, toranj/bell2/cmd, toranj/bell1/state, toranj/bell2/state

#include <Arduino.h>
#include <RTClib.h>
#include "zvonjenje.h"
#include "podesavanja_piny.h"
#include "time_glob.h"
#include "lcd_display.h"
#include "postavke.h"
#include "pc_serial.h"

// ==================== CONSTANTS ====================

// Inertia blocking duration: 90 seconds (mechanical settling time after bell movement)
// Prevents hammer strikes during bell solenoid operation
const unsigned long TRAJANJE_INERCIJE_MS = 90000UL;

// ==================== STATE TRACKING ====================

// Bell state (BELL1 and BELL2 only)
static struct {
  bool bell1_aktivan;
  bool bell2_aktivan;
  unsigned long bell1_start_ms;
  unsigned long bell2_start_ms;
  unsigned long bell1_duration_ms;
  unsigned long bell2_duration_ms;
} zvona = {false, false, 0, 0, 0, 0};

// Inertia blocking (90-second period after bell activation)
static struct {
  bool inercija_aktivna;
  unsigned long vrijeme_pocetka;
  unsigned long trajanje_ms;
} inercija = {false, 0, TRAJANJE_INERCIJE_MS};

// Track last activated minute
static int zadnja_aktivirana_minuta = -1;

// ==================== RELAY CONTROL ====================

// Enable Bell 1 relay
static void aktivirajBell1_Relej() {
  digitalWrite(PIN_ZVONO_1, HIGH);
  inercija.inercija_aktivna = true;
  inercija.vrijeme_pocetka = millis();
  
  String log = F("Bell1: aktivirana, inercija (90s) početa");
  posaljiPCLog(log);
  signalizirajBell1_Ringing();
}

// Disable Bell 1 relay
static void deaktivirajBell1_Relej() {
  digitalWrite(PIN_ZVONO_1, LOW);
  String log = F("Bell1: deaktivirana");
  posaljiPCLog(log);
}

// Enable Bell 2 relay
static void aktivirajBell2_Relej() {
  digitalWrite(PIN_ZVONO_2, HIGH);
  inercija.inercija_aktivna = true;
  inercija.vrijeme_pocetka = millis();
  
  String log = F("Bell2: aktivirana, inercija (90s) početa");
  posaljiPCLog(log);
  signalizirajBell2_Ringing();
}

// Disable Bell 2 relay
static void deaktivirajBell2_Relej() {
  digitalWrite(PIN_ZVONO_2, LOW);
  String log = F("Bell2: deaktivirana");
  posaljiPCLog(log);
}

// ==================== MECHANICAL INPUT PROCESSING ====================

// Process plate mechanical cam inputs (5 sensors)
// Called at minute boundary after plate N-phase completes
void obradiCavleNaPloči() {
  DateTime sada = dohvatiTrenutnoVrijeme();
  unsigned long sadaMs = millis();
  
  // Determine day of week (0=Sunday, 1=Monday, ... 6=Saturday)
  uint8_t dan_u_tjednu = sada.dayOfTheWeek();
  bool je_nedjelja = (dan_u_tjednu == 0);
  
  // Read mechanical input sensors
  bool ulaz_1 = (digitalRead(PIN_ULAZA_PLOCE_1) == LOW);  // Bell1 weekday
  bool ulaz_2 = (digitalRead(PIN_ULAZA_PLOCE_2) == LOW);  // Bell2 weekday
  bool ulaz_3 = (digitalRead(PIN_ULAZA_PLOCE_3) == LOW);  // Bell1 Sunday
  bool ulaz_4 = (digitalRead(PIN_ULAZA_PLOCE_4) == LOW);  // Bell2 Sunday
  
  // Log raw sensor state
  String ulazi_log = F("Čavli (raw): 1=");
  ulazi_log += ulaz_1 ? "ON" : "OFF";
  ulazi_log += F(" 2=");
  ulazi_log += ulaz_2 ? "ON" : "OFF";
  ulazi_log += F(" 3=");
  ulazi_log += ulaz_3 ? "ON" : "OFF";
  ulazi_log += F(" 4=");
  ulazi_log += ulaz_4 ? "ON" : "OFF";
  posaljiPCLog(ulazi_log);
  
  // Determine which bells should ring
  bool treba_bell1 = je_nedjelja ? ulaz_3 : ulaz_1;
  bool treba_bell2 = je_nedjelja ? ulaz_4 : ulaz_2;
  
  // Get bell duration from settings
  unsigned long trajanje_bell = je_nedjelja ? 
    dohvatiTrajanjeZvonjenjaNedjeljaMs() : 
    dohvatiTrajanjeZvonjenjaRadniMs();
  
  if (trajanje_bell == 0) {
    trajanje_bell = je_nedjelja ? 180000UL : 120000UL;
  }
  
  // Activate bells
  if (treba_bell1 && !zvona.bell1_aktivan) {
    uključiZvono(1);
    zvona.bell1_start_ms = sadaMs;
    zvona.bell1_duration_ms = trajanje_bell;
    
    String log = F("Čavli: Bell1 aktiviran");
    posaljiPCLog(log);
  }
  
  if (treba_bell2 && !zvona.bell2_aktivan) {
    uključiZvono(2);
    zvona.bell2_start_ms = sadaMs;
    zvona.bell2_duration_ms = trajanje_bell;
    
    String log = F("Čavli: Bell2 aktiviran");
    posaljiPCLog(log);
  }
}

// ==================== AUTOMATIC BELL ACTIVATION ====================

// Activate bells at HH:XX:30
void aktivirajZvonaAkoTrebaju() {
  DateTime sada = dohvatiTrenutnoVrijeme();
  unsigned long sadaMs = millis();
  
  // Only at second 30
  if (sada.second() != 30) {
    return;
  }
  
  // Prevent duplicates
  if (sada.minute() == zadnja_aktivirana_minuta) {
    return;
  }
  
  // Check if allowed in this hour
  if (!jeDozvoljenoOtkucavanjeUSatu(sada.hour())) {
    return;
  }
  
  // Hour (minute==0) - Bell 1
  if (sada.minute() == 0) {
    if (dohvatiDozvoljenoZvonjenjeBell1()) {
      unsigned long trajanje = dohvatiTrajanjeZvonjenjaRadniMs();
      if (trajanje == 0) trajanje = 120000UL;
      
      uključiZvono(1);
      zvona.bell1_start_ms = sadaMs;
      zvona.bell1_duration_ms = trajanje;
      
      String log = F("Autom: Bell1 za puni sat");
      posaljiPCLog(log);
    }
  } 
  // Half-hour (minute==30) - Bell 2
  else if (sada.minute() == 30) {
    if (dohvatiDozvoljenoZvonjenjeBell2()) {
      unsigned long trajanje = dohvatiTrajanjeZvonjenjaRadniMs();
      if (trajanje == 0) trajanje = 120000UL;
      
      uključiZvono(2);
      zvona.bell2_start_ms = sadaMs;
      zvona.bell2_duration_ms = trajanje;
      
      String log = F("Autom: Bell2 za pola sata");
      posaljiPCLog(log);
    }
  }
  
  zadnja_aktivirana_minuta = sada.minute();
}

// ==================== PUBLIC API ====================

// Enable Bell N (1=Bell1, 2=Bell2)
void uključiZvono(int zvono) {
  if (zvono == 1) {
    if (!zvona.bell1_aktivan) {
      aktivirajBell1_Relej();
      zvona.bell1_aktivan = true;
    }
  } else if (zvono == 2) {
    if (!zvona.bell2_aktivan) {
      aktivirajBell2_Relej();
      zvona.bell2_aktivan = true;
    }
  }
}

// ASCII spelling variant
void ukljuciZvono(int zvono) {
  uključiZvono(zvono);
}

// Disable Bell N
void isključiZvono(int zvono) {
  if (zvono == 1) {
    if (zvona.bell1_aktivan) {
      deaktivirajBell1_Relej();
      zvona.bell1_aktivan = false;
    }
    inercija.inercija_aktivna = true;
    inercija.vrijeme_pocetka = millis();
  } else if (zvono == 2) {
    if (zvona.bell2_aktivan) {
      deaktivirajBell2_Relej();
      zvona.bell2_aktivan = false;
    }
    inercija.inercija_aktivna = true;
    inercija.vrijeme_pocetka = millis();
  }
}

// ASCII spelling variant
void iskljuciZvono(int zvono) {
  isključiZvono(zvono);
}

// Check if inertia is active
bool jeLiInerciaAktivna() {
  if (!inercija.inercija_aktivna) {
    return false;
  }
  
  unsigned long sadaMs = millis();
  unsigned long proteklo = sadaMs - inercija.vrijeme_pocetka;
  
  if (proteklo >= inercija.trajanje_ms) {
    inercija.inercija_aktivna = false;
    String log = F("Inercija: završena nakon 90s");
    posaljiPCLog(log);
    return false;
  }
  
  return true;
}

// Check if either bell is ringing
bool jeZvonoUTijeku() {
  return zvona.bell1_aktivan || zvona.bell2_aktivan;
}

// Activate bell with specific duration (for MQTT and mechanical inputs)
void aktivirajZvonjenje(int zvono) {
  uključiZvono(zvono);
}

// Deactivate bell (for MQTT)
void deaktivirajZvonjenje(int zvono) {
  isključiZvono(zvono);
}

// ==================== CELEBRATION/FUNERAL STUBS ====================

// These are now implemented in otkucavanje.cpp, but included here for API compatibility
void zapocniSlavljenje() {
  // Implemented in otkucavanje.cpp
}

void zaustaviSlavljenje() {
  // Implemented in otkucavanje.cpp
}

void zapocniMrtvacko() {
  // Implemented in otkucavanje.cpp
}

void zaustaviMrtvacko() {
  // Implemented in otkucavanje.cpp
}

bool jeSlavljenjeUTijeku() {
  // Implemented in otkucavanje.cpp
  return false;
}

bool jeMrtvackoUTijeku() {
  // Implemented in otkucavanje.cpp
  return false;
}

// ==================== INITIALIZATION ====================

void inicijalizirajZvona() {
  // Configure bell relay pins
  pinMode(PIN_ZVONO_1, OUTPUT);
  pinMode(PIN_ZVONO_2, OUTPUT);
  digitalWrite(PIN_ZVONO_1, LOW);
  digitalWrite(PIN_ZVONO_2, LOW);
  
  // Configure mechanical input pins
  pinMode(PIN_ULAZA_PLOCE_1, INPUT_PULLUP);
  pinMode(PIN_ULAZA_PLOCE_2, INPUT_PULLUP);
  pinMode(PIN_ULAZA_PLOCE_3, INPUT_PULLUP);
  pinMode(PIN_ULAZA_PLOCE_4, INPUT_PULLUP);
  pinMode(PIN_ULAZA_PLOCE_5, INPUT_PULLUP);
  
  // Initialize state
  zvona.bell1_aktivan = false;
  zvona.bell2_aktivan = false;
  inercija.inercija_aktivna = false;
  zadnja_aktivirana_minuta = -1;
  
  posaljiPCLog(F("Zvona: inicijalizirana - BELL CONTROL ONLY"));
}

// ==================== MAIN LOOP MANAGEMENT ====================

void upravljajZvonom() {
  unsigned long sadaMs = millis();
  
  // Update inertia
  jeLiInerciaAktivna();
  
  // Manage Bell 1 duration
  if (zvona.bell1_aktivan) {
    unsigned long proteklo = sadaMs - zvona.bell1_start_ms;
    if (proteklo >= zvona.bell1_duration_ms) {
      isključiZvono(1);
      String log = F("Bell1: trajanje isteklo");
      posaljiPCLog(log);
    }
  }
  
  // Manage Bell 2 duration
  if (zvona.bell2_aktivan) {
    unsigned long proteklo = sadaMs - zvona.bell2_start_ms;
    if (proteklo >= zvona.bell2_duration_ms) {
      isključiZvono(2);
      String log = F("Bell2: trajanje isteklo");
      posaljiPCLog(log);
    }
  }
  
  // Check mechanical inputs
  static int zadnja_minuta_mehanike = -1;
  DateTime sada = dohvatiTrenutnoVrijeme();
  if (sada.minute() != zadnja_minuta_mehanike) {
    zadnja_minuta_mehanike = sada.minute();
    obradiCavleNaPloči();
  }
  
  // Automatic bell activation
  aktivirajZvonaAkoTrebaju();
}