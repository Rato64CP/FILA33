// otkucavanje.cpp – REFACTORED: Hammer Striking System WITH Celebration/Funeral
// Mechanical hammer striking (via relay impulses) with celebration and funeral modes
// CELEBRATION and FUNERAL modes are ONLY in this file (NOT in zvonjenje.cpp)
//


#include <Arduino.h>
#include <RTClib.h>
#include "otkucavanje.h"
#include "zvonjenje.h"
#include "podesavanja_piny.h"
#include "time_glob.h"
#include "postavke.h"
#include "lcd_display.h"
#include "pc_serial.h"

// ==================== HAMMER TIMING CONSTANTS ====================

// Hammer pulse timing (relay impulse control)
const unsigned long TRAJANJE_IMPULSA_CEKICA_DEFAULT = 150UL;  // 150ms hammer strike
const unsigned long PAUZA_MEZI_UDARACA_DEFAULT = 400UL;       // 400ms between strikes

// Slavljenje: točan uzorak 1-2-2 s definiranim pauzama
const unsigned long SLAVLJENJE_TRAJANJE_UDARCA_MS = 150UL;
const unsigned long SLAVLJENJE_PAUZA_NAKON_CEKIC1_MS = 300UL;
const unsigned long SLAVLJENJE_PAUZA_NAKON_CEKIC2_MS = 150UL;

// Mrtvačko: oba čekića 150ms, zatim 10s pauza
const unsigned long MRTVACKO_TRAJANJE_UDARCA_MS = 150UL;
const unsigned long MRTVACKO_PAUZA_MS = 10000UL;

// Button debouncing: 30ms window for noise immunity
const unsigned long DEBOUNCE_BUTTON_MS = 30UL;

// ==================== STATE MACHINE CONSTANTS ====================

enum VrstaOtkucavanja {
  OTKUCAVANJE_NONE = 0,          // No striking
  OTKUCAVANJE_SATI = 1,          // Hour striking sequence
  OTKUCAVANJE_POLA = 2,          // Half-hour single strike
  OTKUCAVANJE_SLAVLJENJE = 3,    // Celebration mode
  OTKUCAVANJE_MRTVACKO = 4       // Funeral mode
};

// ==================== STATE VARIABLES ====================

// Current striking operation state
static struct {
  VrstaOtkucavanja vrsta;             // Type of current operation
  int preostali_udarci;               // Remaining strikes to perform
  unsigned long vrijeme_pocetka_ms;   // When operation started
  bool cekic_aktivan;                 // Is hammer currently energized
  int aktivni_pin;                    // Which hammer pin is being used
  unsigned long vrijeme_zadnje_aktivacije; // When hammer was last toggled
  
  // Blocking flags
  bool blokirano;                     // Is striking blocked (e.g., by bell inertia)
} otkucavanje = {
  OTKUCAVANJE_NONE,
  0,
  0,
  false,
  -1,
  0,
  false
};

// Stanje slavljenja: beskonačni slijed 1,2,2 dok je način rada aktivan
static struct {
  bool slavljenje_aktivno;            // Is celebration mode running
  unsigned long vrijeme_pocetka_ms;   // When celebration started
  int trenutni_korak;                 // 0=Č1, 1=Č2, 2=Č2
  unsigned long vrijeme_koraka_ms;    // Vrijeme početka trenutne faze
  bool cekic_aktivan;                 // Je li čekić trenutno uključen
  int aktivni_pin;                    // Trenutno aktivni čekić (ako je uključen)
} slavljenje = {
  false,
  0,
  0,
  0,
  false,
  -1
};

// Stanje mrtvačkog: oba čekića zajedno pa duga pauza
static struct {
  bool mrtvacko_aktivno;              // Is funeral mode running
  unsigned long vrijeme_pocetka_ms;   // When funeral mode started
  bool cekici_aktivni;                // Jesu li oba čekića trenutno uključena
  unsigned long vrijeme_faze_ms;      // Vrijeme početka trenutačne faze
} mrtvacko = {
  false,
  0,
  false,
  0
};

// User settings and global state
static bool blokada_otkucavanja = false;        // Can be set by user to disable all striking
static DateTime zadnje_izmjereno_vrijeme;       // Track minute boundaries

// Button state tracking with debouncing
static struct {
  // Celebration button (PIN 43)
  bool prethodno_stanje_slavljenja;           // Previous state of celebration button
  unsigned long vrijeme_promjene_slavljenja;  // When state change started
  bool debounce_u_tijeku_slavljenja;          // Is debouncing in progress
  
  // Funeral button (PIN 42)
  bool prethodno_stanje_mrtvackog;            // Previous state of funeral button
  unsigned long vrijeme_promjene_mrtvackog;   // When state change started
  bool debounce_u_tijeku_mrtvackog;           // Is debouncing in progress
} dugmad = {
  true, 0, false,  // Celebration button: init HIGH (unpressed)
  true, 0, false   // Funeral button: init HIGH (unpressed)
};

// ==================== HELPER FUNCTIONS ====================

// Activate hammer relay (energize for strike impulse)
static void aktivirajCekic_Internal(int pin) {
  if (pin == PIN_CEKIC_MUSKI || pin == PIN_CEKIC_ZENSKI) {
    digitalWrite(pin, HIGH);
  }
}

// Deactivate hammer relay
static void deaktivirajCekic_Internal(int pin) {
  if (pin == PIN_CEKIC_MUSKI || pin == PIN_CEKIC_ZENSKI) {
    digitalWrite(pin, LOW);
  }
}

// Sigurnosno gašenje oba čekića
static void deaktivirajObaCekica_Internal() {
  deaktivirajCekic_Internal(PIN_CEKIC_MUSKI);
  deaktivirajCekic_Internal(PIN_CEKIC_ZENSKI);
}

// Check if operation can proceed (not blocked by bell inertia)
static bool jeOperacijaDozvoljena() {
  if (blokada_otkucavanja) {
    return false;  // User-set block
  }
  
  // Block if bell inertia is active (bell is ringing or recently rang)
  if (jeLiInerciaAktivna()) {
    return false;
  }
  
  return true;
}

// Clear all striking state
static void ponistiAktivnoOtkucavanje() {
  if (otkucavanje.aktivni_pin >= 0) {
    deaktivirajCekic_Internal(otkucavanje.aktivni_pin);
  }
  otkucavanje.vrsta = OTKUCAVANJE_NONE;
  otkucavanje.preostali_udarci = 0;
  otkucavanje.cekic_aktivan = false;
  otkucavanje.aktivni_pin = -1;
  otkucavanje.vrijeme_pocetka_ms = 0;
  otkucavanje.vrijeme_zadnje_aktivacije = 0;
  
  String log = F("Otkucavanje: operacija otkazana");
  posaljiPCLog(log);
}

// Start next strike in sequence
static void pokreniSljedeciUdarac() {
  if (otkucavanje.preostali_udarci <= 0) {
    ponistiAktivnoOtkucavanje();
    return;
  }
  
  // Activate hammer relay
  aktivirajCekic_Internal(otkucavanje.aktivni_pin);
  otkucavanje.cekic_aktivan = true;
  otkucavanje.vrijeme_zadnje_aktivacije = millis();
  otkucavanje.preostali_udarci--;
  
  String log = F("Udarac: preostalo=");
  log += otkucavanje.preostali_udarci;
  posaljiPCLog(log);
}

// ==================== NORMAL STRIKING SEQUENCE ====================

// Strike hour: multiple rapid strikes (count of hours 1-12)
// Used for automatic hourly striking and manual control
void otkucajSate(int broj) {
  if (broj < 1 || broj > 12) {
    return;  // Invalid hour count
  }
  
  if (otkucavanje.vrsta != OTKUCAVANJE_NONE) {
    return;  // Already striking
  }
  
  if (!jeOperacijaDozvoljena()) {
    posaljiPCLog(F("Otkucavanje: blokirano (inercija ili user blok)"));
    return;
  }
  
  // Start hour striking sequence
  otkucavanje.vrsta = OTKUCAVANJE_SATI;
  otkucavanje.preostali_udarci = broj;
  otkucavanje.aktivni_pin = PIN_CEKIC_MUSKI;  // Male hammer for hourly
  otkucavanje.vrijeme_pocetka_ms = 0;  // Will be set on first strike
  otkucavanje.cekic_aktivan = false;
  
  String log = F("Otkucavanje: početi sat sa ");
  log += broj;
  log += F(" udaraca");
  posaljiPCLog(log);
  signalizirajHammer1_Active();
  
  pokreniSljedeciUdarac();
}

// Strike half-hour: single strike
// Used for automatic half-hourly striking
void otkucajPolasata() {
  if (otkucavanje.vrsta != OTKUCAVANJE_NONE) {
    return;  // Already striking
  }
  
  if (!jeOperacijaDozvoljena()) {
    posaljiPCLog(F("Otkucavanje: blokirano (inercija ili user blok)"));
    return;
  }
  
  // Start half-hour single strike
  otkucavanje.vrsta = OTKUCAVANJE_POLA;
  otkucavanje.preostali_udarci = 1;
  otkucavanje.aktivni_pin = PIN_CEKIC_ZENSKI;  // Female hammer for half-hourly
  otkucavanje.vrijeme_pocetka_ms = 0;
  otkucavanje.cekic_aktivan = false;
  
  posaljiPCLog(F("Otkucavanje: jedan udarac za pola sata"));
  signalizirajHammer2_Active();
  
  pokreniSljedeciUdarac();
}

// ==================== CELEBRATION MODE ====================

// Pokretanje slavljenja: slijed Č1, Č2, Č2 s točno definiranim pauzama
void zapocniSlavljenje() {
  unsigned long sadaMs = millis();
  
  // Don't start if bell inertia active or user blocked
  if (!jeOperacijaDozvoljena()) {
    posaljiPCLog(F("Slavljenje: ne može se pokrenuti (inercija ili blok)"));
    return;
  }
  
  // Check if mutual exclusion: funeral is active
  if (mrtvacko.mrtvacko_aktivno) {
    posaljiPCLog(F("Slavljenje: odbijeno - mrtvačko je aktivno (mutual exclusion)"));
    return;
  }
  
  slavljenje.slavljenje_aktivno = true;
  slavljenje.vrijeme_pocetka_ms = sadaMs;
  slavljenje.trenutni_korak = 0; // Korak A: ČEKIĆ 1
  slavljenje.vrijeme_koraka_ms = sadaMs;
  slavljenje.cekic_aktivan = true;
  slavljenje.aktivni_pin = PIN_CEKIC_MUSKI;
  aktivirajCekic_Internal(PIN_CEKIC_MUSKI);

  posaljiPCLog(F("Slavljenje: pokrenuto (uzorak Č1-Č2-Č2)"));
  signalizirajCelebration_Mode();
}

// Stop celebration mode immediately
void zaustaviSlavljenje() {
  if (slavljenje.slavljenje_aktivno) {
    slavljenje.slavljenje_aktivno = false;
    deaktivirajObaCekica_Internal();
    slavljenje.cekic_aktivan = false;
    slavljenje.aktivni_pin = -1;
    posaljiPCLog(F("Slavljenje: zaustavljeno"));
  }
}

// Check if celebration mode is currently active
bool jeSlavljenjeUTijeku() {
  return slavljenje.slavljenje_aktivno;
}

// Ažuriranje slavljenja (neblokirajući automat stanja)
static void azurirajSlavljenje(unsigned long sadaMs) {
  if (!slavljenje.slavljenje_aktivno) {
    return;
  }

  const unsigned long proteklo = sadaMs - slavljenje.vrijeme_koraka_ms;
  const unsigned long trazena_pauza = (slavljenje.trenutni_korak == 0)
      ? SLAVLJENJE_PAUZA_NAKON_CEKIC1_MS
      : SLAVLJENJE_PAUZA_NAKON_CEKIC2_MS;

  if (slavljenje.cekic_aktivan) {
    if (proteklo >= SLAVLJENJE_TRAJANJE_UDARCA_MS) {
      deaktivirajCekic_Internal(slavljenje.aktivni_pin);
      slavljenje.cekic_aktivan = false;
      slavljenje.aktivni_pin = -1;
      slavljenje.vrijeme_koraka_ms = sadaMs;
    }
    return;
  }

  if (proteklo >= trazena_pauza) {
    slavljenje.trenutni_korak = (slavljenje.trenutni_korak + 1) % 3;
    const int sljedeci_pin = (slavljenje.trenutni_korak == 0) ? PIN_CEKIC_MUSKI : PIN_CEKIC_ZENSKI;
    aktivirajCekic_Internal(sljedeci_pin);
    slavljenje.aktivni_pin = sljedeci_pin;
    slavljenje.cekic_aktivan = true;
    slavljenje.vrijeme_koraka_ms = sadaMs;
  }
}

// ==================== FUNERAL MODE ====================

// Pokretanje mrtvačkog: oba čekića 150ms, zatim 10s pauza
void zapocniMrtvacko() {
  unsigned long sadaMs = millis();
  
  // Don't start if bell inertia active or user blocked
  if (!jeOperacijaDozvoljena()) {
    posaljiPCLog(F("Mrtvačko: ne može se pokrenuti (inercija ili blok)"));
    return;
  }
  
  // Check if mutual exclusion: celebration is active
  if (slavljenje.slavljenje_aktivno) {
    posaljiPCLog(F("Mrtvačko: odbijeno - slavljenje je aktivno (mutual exclusion)"));
    return;
  }
  
  mrtvacko.mrtvacko_aktivno = true;
  mrtvacko.vrijeme_pocetka_ms = sadaMs;
  mrtvacko.cekici_aktivni = true;
  mrtvacko.vrijeme_faze_ms = sadaMs;
  aktivirajCekic_Internal(PIN_CEKIC_MUSKI);
  aktivirajCekic_Internal(PIN_CEKIC_ZENSKI);

  posaljiPCLog(F("Mrtvačko: pokrenuto (oba čekića 150ms / pauza 10s)"));
  signalizirajFuneral_Mode();
}

// Stop funeral mode immediately
void zaustaviMrtvacko() {
  if (mrtvacko.mrtvacko_aktivno) {
    mrtvacko.mrtvacko_aktivno = false;
    deaktivirajObaCekica_Internal();
    mrtvacko.cekici_aktivni = false;
    posaljiPCLog(F("Mrtvačko: zaustavljeno"));
  }
}

// Check if funeral mode is currently active
bool jeMrtvackoUTijeku() {
  return mrtvacko.mrtvacko_aktivno;
}

// Ažuriranje mrtvačkog (neblokirajući automat stanja)
static void azurirajMrtvacko(unsigned long sadaMs) {
  if (!mrtvacko.mrtvacko_aktivno) {
    return;
  }

  const unsigned long proteklo = sadaMs - mrtvacko.vrijeme_faze_ms;

  if (mrtvacko.cekici_aktivni) {
    if (proteklo >= MRTVACKO_TRAJANJE_UDARCA_MS) {
      deaktivirajObaCekica_Internal();
      mrtvacko.cekici_aktivni = false;
      mrtvacko.vrijeme_faze_ms = sadaMs;
    }
    return;
  }

  if (proteklo >= MRTVACKO_PAUZA_MS) {
    aktivirajCekic_Internal(PIN_CEKIC_MUSKI);
    aktivirajCekic_Internal(PIN_CEKIC_ZENSKI);
    mrtvacko.cekici_aktivni = true;
    mrtvacko.vrijeme_faze_ms = sadaMs;
  }
}

// ==================== BUTTON DEBOUNCING (PIN 43 & 42) ====================

// Debounce celebration button (PIN 43) with 30ms window
static void provjeriDugmeSlavljenja(unsigned long sadaMs) {
  bool trenutnoStanje = (digitalRead(PIN_KEY_CELEBRATION) == LOW);  // LOW = pressed
  
  // If state matches previous, no change
  if (trenutnoStanje == dugmad.prethodno_stanje_slavljenja) {
    if (dugmad.debounce_u_tijeku_slavljenja) {
      dugmad.debounce_u_tijeku_slavljenja = false;
    }
    return;
  }
  
  // State changed - start debounce timer if not already debouncing
  if (!dugmad.debounce_u_tijeku_slavljenja) {
    dugmad.vrijeme_promjene_slavljenja = sadaMs;
    dugmad.debounce_u_tijeku_slavljenja = true;
    return;
  }
  
  // Check if debounce time has elapsed
  unsigned long vremeProslo = sadaMs - dugmad.vrijeme_promjene_slavljenja;
  if (vremeProslo >= DEBOUNCE_BUTTON_MS) {
    // Debounce complete - accept the state change
    dugmad.prethodno_stanje_slavljenja = trenutnoStanje;
    dugmad.debounce_u_tijeku_slavljenja = false;
    
    // Execute action on button press (transition from HIGH to LOW)
    if (trenutnoStanje) {
      if (slavljenje.slavljenje_aktivno) {
        zaustaviSlavljenje();
        posaljiPCLog(F("Dugme: slavljenje zaustavljeno"));
      } else {
        zapocniSlavljenje();
        posaljiPCLog(F("Dugme: slavljenje pokrenuto"));
      }
    }
  }
}

// Debounce funeral button (PIN 42) with 30ms window
static void provjeriDugmeMrtvackog(unsigned long sadaMs) {
  bool trenutnoStanje = (digitalRead(PIN_KEY_FUNERAL) == LOW);  // LOW = pressed
  
  // If state matches previous, no change
  if (trenutnoStanje == dugmad.prethodno_stanje_mrtvackog) {
    if (dugmad.debounce_u_tijeku_mrtvackog) {
      dugmad.debounce_u_tijeku_mrtvackog = false;
    }
    return;
  }
  
  // State changed - start debounce timer if not already debouncing
  if (!dugmad.debounce_u_tijeku_mrtvackog) {
    dugmad.vrijeme_promjene_mrtvackog = sadaMs;
    dugmad.debounce_u_tijeku_mrtvackog = true;
    return;
  }
  
  // Check if debounce time has elapsed
  unsigned long vremeProslo = sadaMs - dugmad.vrijeme_promjene_mrtvackog;
  if (vremeProslo >= DEBOUNCE_BUTTON_MS) {
    // Debounce complete - accept the state change
    dugmad.prethodno_stanje_mrtvackog = trenutnoStanje;
    dugmad.debounce_u_tijeku_mrtvackog = false;
    
    // Execute action on button press (transition from HIGH to LOW)
    if (trenutnoStanje) {
      if (mrtvacko.mrtvacko_aktivno) {
        zaustaviMrtvacko();
        posaljiPCLog(F("Dugme: mrtvačko zaustavljeno"));
      } else {
        zapocniMrtvacko();
        posaljiPCLog(F("Dugme: mrtvačko pokrenuto"));
      }
    }
  }
}

// ==================== NORMAL HOUR STRIKING MANAGEMENT ====================

void inicijalizirajOtkucavanje() {
  // Izlazi za čekiće
  pinMode(PIN_CEKIC_MUSKI, OUTPUT);
  pinMode(PIN_CEKIC_ZENSKI, OUTPUT);
  digitalWrite(PIN_CEKIC_MUSKI, LOW);
  digitalWrite(PIN_CEKIC_ZENSKI, LOW);

  // Ulazi za tipke (slavljenje / mrtvačko)
  pinMode(PIN_KEY_CELEBRATION, INPUT_PULLUP);
  pinMode(PIN_KEY_FUNERAL, INPUT_PULLUP);

  // Početno stanje modula
  otkucavanje.vrsta = OTKUCAVANJE_NONE;
  otkucavanje.preostali_udarci = 0;
  otkucavanje.vrijeme_pocetka_ms = 0;
  otkucavanje.cekic_aktivan = false;
  otkucavanje.aktivni_pin = -1;
  otkucavanje.vrijeme_zadnje_aktivacije = 0;
  otkucavanje.blokirano = false;

  slavljenje.slavljenje_aktivno = false;
  slavljenje.vrijeme_pocetka_ms = 0;
  slavljenje.trenutni_korak = 0;
  slavljenje.vrijeme_koraka_ms = 0;
  slavljenje.cekic_aktivan = false;
  slavljenje.aktivni_pin = -1;

  mrtvacko.mrtvacko_aktivno = false;
  mrtvacko.vrijeme_pocetka_ms = 0;
  mrtvacko.cekici_aktivni = false;
  mrtvacko.vrijeme_faze_ms = 0;

  dugmad.prethodno_stanje_slavljenja = true;
  dugmad.vrijeme_promjene_slavljenja = 0;
  dugmad.debounce_u_tijeku_slavljenja = false;
  dugmad.prethodno_stanje_mrtvackog = true;
  dugmad.vrijeme_promjene_mrtvackog = 0;
  dugmad.debounce_u_tijeku_mrtvackog = false;

  zadnje_izmjereno_vrijeme = dohvatiTrenutnoVrijeme();
  blokada_otkucavanja = false;

  posaljiPCLog(F("Otkucavanje: inicijalizirano"));
}

// Called from main loop to manage hour/half-hour striking sequences
void upravljajOtkucavanjem() {
  unsigned long sadaMs = millis();
  DateTime sada = dohvatiTrenutnoVrijeme();
  
  // Check celebration/funeral buttons (PIN 43/42) with debouncing
  provjeriDugmeSlavljenja(sadaMs);
  provjeriDugmeMrtvackog(sadaMs);
  
  // Update celebration mode (if active)
  azurirajSlavljenje(sadaMs);
  
  // Update funeral mode (if active)
  azurirajMrtvacko(sadaMs);
  
  // Ako se pojavi inercija, odmah sigurno zaustavi sve aktivne načine rada čekića
  if (jeLiInerciaAktivna()) {
    if (slavljenje.slavljenje_aktivno) {
      zaustaviSlavljenje();
    }
    if (mrtvacko.mrtvacko_aktivno) {
      zaustaviMrtvacko();
    }
    if (otkucavanje.vrsta != OTKUCAVANJE_NONE) {
      ponistiAktivnoOtkucavanje();
    }
  }
  
  // Manage normal striking sequences (hour/half-hour)
  if (otkucavanje.vrsta == OTKUCAVANJE_NONE) {
    // Not currently striking - check for automatic hour/half-hour striking
    
    // Track minute change
    static int zadnja_minuta = -1;
    if (sada.minute() != zadnja_minuta) {
      zadnja_minuta = sada.minute();
      
      // Check if it's hour (minute==0) or half-hour (minute==30)
      if (sada.minute() == 0 && !jeSlavljenjeUTijeku() && !jeMrtvackoUTijeku()) {
        // Hour - ring hour count
        int broj = sada.hour() % 12;
        if (broj == 0) broj = 12;  // 12h format
        
        if (jeDozvoljenoOtkucavanjeUSatu(sada.hour())) {
          otkucajSate(broj);
        }
      } else if (sada.minute() == 30 && !jeSlavljenjeUTijeku() && !jeMrtvackoUTijeku()) {
        // Half-hour - single strike
        if (jeDozvoljenoOtkucavanjeUSatu(sada.hour())) {
          otkucajPolasata();
        }
      }
    }
    
    return;  // No active striking
  }
  
  // Manage ongoing striking operation
  if (!jeOperacijaDozvoljena() && otkucavanje.vrsta != OTKUCAVANJE_NONE) {
    ponistiAktivnoOtkucavanje();
    return;
  }
  
  // Get timing configuration from settings
  unsigned long trajanje_impulsa = dohvatiTrajanjeImpulsaCekica();
  unsigned long pauza_mezi = dohvatiPauzuIzmeduUdaraca();
  
  if (trajanje_impulsa == 0) trajanje_impulsa = TRAJANJE_IMPULSA_CEKICA_DEFAULT;
  if (pauza_mezi == 0) pauza_mezi = PAUZA_MEZI_UDARACA_DEFAULT;
  
  // Manage hammer pulse timing
  if (otkucavanje.cekic_aktivan) {
    // Hammer is currently energized
    unsigned long proteklo = sadaMs - otkucavanje.vrijeme_zadnje_aktivacije;
    
    // Deactivate after impulse duration
    if (proteklo >= trajanje_impulsa) {
      deaktivirajCekic_Internal(otkucavanje.aktivni_pin);
      otkucavanje.cekic_aktivan = false;
      otkucavanje.vrijeme_zadnje_aktivacije = sadaMs;
    }
  } else {
    // Hammer is inactive - wait for pause then start next strike
    if (otkucavanje.vrijeme_zadnje_aktivacije == 0) {
      // First strike - go immediately
      pokreniSljedeciUdarac();
    } else {
      unsigned long proteklo = sadaMs - otkucavanje.vrijeme_zadnje_aktivacije;
      
      // Wait for pause between strikes
      if (proteklo >= pauza_mezi) {
        if (otkucavanje.preostali_udarci > 0) {
          pokreniSljedeciUdarac();
        } else {
          // Sequence complete
          ponistiAktivnoOtkucavanje();
        }
      }
    }
  }
}

// ==================== USER BLOCKING CONTROL ====================

// Allow user to block all hammer striking (e.g., during bell movement or night hours)
void postaviBlokaduOtkucavanja(bool blokiraj) {
  if (blokada_otkucavanja == blokiraj) {
    return;  // No change
  }
  
  blokada_otkucavanja = blokiraj;
  
  if (blokada_otkucavanja) {
    posaljiPCLog(F("Blokada otkucavanja: UKLJUČENA"));
    
    // Stop any active striking
    if (otkucavanje.vrsta != OTKUCAVANJE_NONE) {
      ponistiAktivnoOtkucavanje();
    }
    if (slavljenje.slavljenje_aktivno) {
      zaustaviSlavljenje();
    }
    if (mrtvacko.mrtvacko_aktivno) {
      zaustaviMrtvacko();
    }
  } else {
    posaljiPCLog(F("Blokada otkucavanja: ISKLJUČENA"));
  }
}
