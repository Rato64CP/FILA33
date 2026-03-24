// kazaljke_sata.cpp – Complete rewrite with new K-minuta logic
// K-minuta (0-719) stored in external EEPROM for 12-hour cycle (720 minutes = 12 hours)
// Normal mode (<10 min difference): continuous impulses until synchronized
// Aggressive mode (>=10 min difference): impulses every 6 seconds without waiting for minute boundary
// Dynamic calculation: re-read RTC and calculate new difference after EVERY impulse
// Even/odd relay selection: if K-minuta is even, trigger next ODD relay; if odd, trigger EVEN relay
// Parity enforcement: strict alternation - even K → odd relay, odd K → even relay
// DST waiting state threshold: 70 minutes to handle ±60 minute DST transitions plus 10-minute safety margin

#include <Arduino.h>
#include <RTClib.h>
#include "kazaljke_sata.h"
#include "podesavanja_piny.h"
#include "time_glob.h"
#include "lcd_display.h"
#include "eeprom_konstante.h"
#include "wear_leveling.h"
#include "pc_serial.h"
#include "watchdog.h"

// ==================== CONSTANTS ====================

const unsigned long TRAJANJE_IMPULSA = 6000UL;  // 6-second impulse duration (3s + 3s phases)
const int BROJ_MINUTA_CIKLUS = 720;             // 12-hour cycle: 720 minutes
const int NORMA_RAZLIKA_THRESHOLD = 10;         // Threshold for normal vs. aggressive mode
const unsigned long IMPULS_INTERVAL_MS = 6000UL; // 6-second interval between impulses
const int MAKS_PAMETNI_POMAK_MINUTA = 15;       // Smart mode threshold
const unsigned long FAZA_TRAJANJE_MS = 3000UL;  // Each phase (first and second) is 3 seconds
// DST waiting state threshold: 70 minutes covers ±60 minute DST jumps plus 10-minute safety margin
const int DST_WAITING_THRESHOLD = 70;            // Updated from 30 to 70 minutes for DST handling

// ==================== STATE VARIABLES ====================

// K-minuta: software position 0-719 stored in external EEPROM
// Represents the 12-hour hand position in minutes
static int K_minuta = 0;

// Impulse state machine variables
static unsigned long vrijemePocetkaImpulsa = 0;
static bool impulsUTijeku = false;
static bool drugaFaza = false;
static int zadnjaAktiviranaMinuta = -1;

// Synchronization tracking
static bool kazaljkeUSinkronu = true;

// Correction mode variables
static bool korekcija_u_tijeku = false;
static int trenutnaBrojImpulsa = 0;
static unsigned long vremePocetkaKorekcije = 0;
static unsigned long vremePosljednjegImpulsa = 0;

// ==================== HELPER FUNCTIONS ====================

// Calculate 12-hour minute position from RTC time
static int izracunajDvanaestSatneMinute(const DateTime& vrijeme)
{
  int sati = vrijeme.hour() % 12;
  return sati * 60 + vrijeme.minute();
}

// Calculate difference between RTC position and stored K-minuta
// Returns positive if RTC is ahead (need to move forward)
// Returns negative if RTC is behind (need to move backward)
static int izracunajRazliku(const DateTime& rtcVrijeme)
{
  int rtcMinuta = izracunajDvanaestSatneMinute(rtcVrijeme);
  int razlika = rtcMinuta - K_minuta;
  
  // Normalize to -360..+360 range (within 12-hour cycle)
  if (razlika < -360) razlika += 720;
  if (razlika > 360) razlika -= 720;
  
  return razlika;
}

// Check if hands are synchronized (difference within ±1 minute)
static bool jeSinkronizirana(int razlika)
{
  return (razlika >= -1 && razlika <= 1);
}

// Determine which relay to activate based on K-minuta parity
// Strict parity enforcement: If K-minuta is even, next relay should be ODD (return 1)
// If K-minuta is odd, next relay should be EVEN (return 0)
// This ensures proper hand movement with alternating relay activation
static int odaberiRelej()
{
  if (K_minuta % 2 == 0) {
    // K-minuta is EVEN → next relay is ODD (neparni)
    return 1;
  } else {
    // K-minuta is ODD → next relay is EVEN (parni)
    return 0;
  }
}

// Aktivira relejsku fazu prema paritetu K_minuta.
// drugaFaza=false -> osnovni relej za trenutni korak
// drugaFaza=true  -> suprotni relej za završetak istog mehaničkog impulsa
static void aktivirajRelejskuFazu(bool drugaFazaImpulsa)
{
  int relej = odaberiRelej();
  if (drugaFazaImpulsa) {
    relej = (relej == 0) ? 1 : 0;
  }

  if (relej == 0) {
    digitalWrite(PIN_RELEJ_PARNE_KAZALJKE, HIGH);
    digitalWrite(PIN_RELEJ_NEPARNE_KAZALJKE, LOW);
  } else {
    digitalWrite(PIN_RELEJ_PARNE_KAZALJKE, LOW);
    digitalWrite(PIN_RELEJ_NEPARNE_KAZALJKE, HIGH);
  }
}

// Activate the first phase of an impulse (3 seconds)
// Energize the selected relay based on parity
static void pokreniPrvuFazu()
{
  int relej = odaberiRelej();
  aktivirajRelejskuFazu(false);
  
  vrijemePocetkaImpulsa = millis();
  impulsUTijeku = true;
  drugaFaza = false;
  
  String log = F("Kazaljke: prva faza, K_minuta=");
  log += K_minuta;
  log += F(" relej=");
  log += (relej == 0 ? F("PARNI") : F("NEPARNI"));
  posaljiPCLog(log);
}

// Finish one complete impulse cycle and increment K-minuta
// Both phases (6 seconds total) are complete
// Immediately save new position to external EEPROM
static void zavrsiImpuls()
{
  // Deactivate both relays
  digitalWrite(PIN_RELEJ_PARNE_KAZALJKE, LOW);
  digitalWrite(PIN_RELEJ_NEPARNE_KAZALJKE, LOW);
  impulsUTijeku = false;
  drugaFaza = false;
  
  // Increment K-minuta and save to EEPROM immediately (critical for power-loss recovery)
  K_minuta = (K_minuta + 1) % BROJ_MINUTA_CIKLUS;
  WearLeveling::spremi(EepromLayout::BAZA_KAZALJKE, EepromLayout::SLOTOVI_KAZALJKE, K_minuta);
  
  String log = F("Kazaljke: impuls završen, nova K_minuta=");
  log += K_minuta;
  posaljiPCLog(log);
}

// Load K-minuta from external EEPROM on startup
static void ucitajKminutu()
{
  if (!WearLeveling::ucitaj(EepromLayout::BAZA_KAZALJKE, EepromLayout::SLOTOVI_KAZALJKE, K_minuta)) {
    K_minuta = 0;
    posaljiPCLog(F("Kazaljke: K_minuta inicijalizirana na 0"));
  } else {
    // Validate loaded value
    if (K_minuta < 0) K_minuta = 0;
    K_minuta %= BROJ_MINUTA_CIKLUS;
    
    String log = F("Kazaljke: K_minuta učitana iz EEPROM: ");
    log += K_minuta;
    posaljiPCLog(log);
  }
}

// ==================== INITIALIZATION ====================

void inicijalizirajKazaljke()
{
  // Configure relay control pins as outputs
  pinMode(PIN_RELEJ_PARNE_KAZALJKE, OUTPUT);
  pinMode(PIN_RELEJ_NEPARNE_KAZALJKE, OUTPUT);
  digitalWrite(PIN_RELEJ_PARNE_KAZALJKE, LOW);
  digitalWrite(PIN_RELEJ_NEPARNE_KAZALJKE, LOW);
  
  // Load K-minuta from external EEPROM
  ucitajKminutu();
  
  // Initialize state variables
  impulsUTijeku = false;
  drugaFaza = false;
  zadnjaAktiviranaMinuta = dohvatiTrenutnoVrijeme().minute();
  korekcija_u_tijeku = false;
  trenutnaBrojImpulsa = 0;
  vremePosljednjegImpulsa = 0;
  kazaljkeUSinkronu = false;
  
  String log = F("Kazaljke: inicijalizirane, K_minuta=");
  log += K_minuta;
  posaljiPCLog(log);
}

// ==================== NORMAL OPERATION MODE ====================

void upravljajKazaljkama()
{
  // If correction is in progress, don't run normal operation
  if (korekcija_u_tijeku) {
    return;
  }
  
  DateTime now = dohvatiTrenutnoVrijeme();
  int trenutnaMinuta = now.minute();
  
  // Normal operation: send one impulse per minute (at minute boundary)
  if (!impulsUTijeku && trenutnaMinuta != zadnjaAktiviranaMinuta) {
    pokreniPrvuFazu();
    zadnjaAktiviranaMinuta = trenutnaMinuta;
  }
  
  // If impulse is in progress, manage phase transitions
  if (!impulsUTijeku) return;
  
  unsigned long sadaMs = millis();
  unsigned long proteklo = sadaMs - vrijemePocetkaImpulsa;
  
  // Transition from first phase to second phase after 3 seconds
  if (!drugaFaza && proteklo >= FAZA_TRAJANJE_MS) {
    aktivirajRelejskuFazu(true);
    vrijemePocetkaImpulsa = millis();
    drugaFaza = true;
  }
  // Complete the impulse cycle after second phase duration
  else if (drugaFaza && proteklo >= FAZA_TRAJANJE_MS) {
    zavrsiImpuls();
  }
}

// ==================== DYNAMIC CORRECTION LOGIC ====================

// Normal mode: continuous impulses until synchronized (difference < 10 minutes)
// Used when time difference is small; intelligently waits if advantageous
// DST waiting state: if difference is negative (RTC behind) and within ±70 minutes,
// consider waiting instead of aggressively moving backward
static void normalnaKorekcija()
{
  DateTime sadaRTC = dohvatiTrenutnoVrijeme();
  int razlika = izracunajRazliku(sadaRTC);
  
  // Check if synchronized - if so, complete correction
  if (jeSinkronizirana(razlika)) {
    String log = F("Normalna korekcija: sinkronizirana, razlika=");
    log += razlika;
    posaljiPCLog(log);
    korekcija_u_tijeku = false;
    kazaljkeUSinkronu = true;
    zadnjaAktiviranaMinuta = sadaRTC.minute();
    return;
  }
  
  // DST waiting state handling: 70-minute threshold
  // If RTC is behind (negative difference) and within ±70 minutes,
  // wait for natural catch-up instead of aggressive backward movement
  // This handles DST transitions gracefully (±60 min jump + 10 min safety margin)
  if (razlika < 0 && abs(razlika) <= DST_WAITING_THRESHOLD) {
    String log = F("Normalna korekcija: čekam prirodni dovodenje (razlika=");
    log += razlika;
    log += F("), DST threshold=");
    log += DST_WAITING_THRESHOLD;
    posaljiPCLog(log);
    korekcija_u_tijeku = false;
    kazaljkeUSinkronu = false;
    return;
  }
  
  unsigned long sadaMs = millis();
  
  // Send impulse immediately (don't wait for minute boundary in correction mode)
  if (vremePosljednjegImpulsa == 0 || (sadaMs - vremePosljednjegImpulsa) >= IMPULS_INTERVAL_MS) {
    pokreniPrvuFazu();
    vremePosljednjegImpulsa = sadaMs;
    trenutnaBrojImpulsa++;
    
    String log = F("Normalna korekcija: impuls #");
    log += trenutnaBrojImpulsa;
    log += F(" razlika=");
    log += razlika;
    log += F(" K_minuta=");
    log += K_minuta;
    posaljiPCLog(log);
  }
  
  // Advance impulse timing: manage phase transitions
  if (impulsUTijeku) {
    unsigned long proteklo = sadaMs - vrijemePocetkaImpulsa;
    
    // First phase to second phase transition
    if (!drugaFaza && proteklo >= FAZA_TRAJANJE_MS) {
      aktivirajRelejskuFazu(true);
      vrijemePocetkaImpulsa = sadaMs;
      drugaFaza = true;
    }
    // Complete impulse cycle
    else if (drugaFaza && proteklo >= FAZA_TRAJANJE_MS) {
      zavrsiImpuls();
      
      // CRITICAL: Recalculate difference after EVERY impulse (dynamic recalculation)
      sadaRTC = dohvatiTrenutnoVrijeme();
      razlika = izracunajRazliku(sadaRTC);
      
      // Check if now synchronized
      if (jeSinkronizirana(razlika)) {
        String log = F("Normalna korekcija: sinkronizirana nakon impulsiranja, razlika=");
        log += razlika;
        posaljiPCLog(log);
        korekcija_u_tijeku = false;
        kazaljkeUSinkronu = true;
        zadnjaAktiviranaMinuta = sadaRTC.minute();
      }
      // Check DST waiting state again
      else if (razlika < 0 && abs(razlika) <= DST_WAITING_THRESHOLD) {
        String log = F("Normalna korekcija: čekam natural dovodenje (razlika=");
        log += razlika;
        log += F("), DST threshold=");
        log += DST_WAITING_THRESHOLD;
        posaljiPCLog(log);
        korekcija_u_tijeku = false;
        kazaljkeUSinkronu = false;
      }
    }
  }
}

// Aggressive mode: impulses every 6 seconds without waiting for minute boundary
// Used when time difference is >= 10 minutes
// Rapid correction mode with dynamic recalculation ensuring stop when synchronized
// DST waiting state: even in aggressive mode, apply 70-minute threshold for backward jumps
static void agresivnaKorekcija()
{
  unsigned long sadaMs = millis();
  
  // CRITICAL: Re-read RTC and recalculate difference AFTER each impulse (dynamic)
  // Do this first to check for DST waiting state
  DateTime sadaRTC = dohvatiTrenutnoVrijeme();
  int razlika = izracunajRazliku(sadaRTC);
  
  // DST waiting state handling in aggressive mode: 70-minute threshold
  // If RTC is behind (negative difference) and within ±70 minutes,
  // switch to waiting mode instead of continuing aggressive backward movement
  // This handles DST transitions gracefully
  if (razlika < 0 && abs(razlika) <= DST_WAITING_THRESHOLD) {
    String log = F("Agresivna korekcija: switching to waiting (razlika=");
    log += razlika;
    log += F("), DST threshold=");
    log += DST_WAITING_THRESHOLD;
    posaljiPCLog(log);
    korekcija_u_tijeku = false;
    kazaljkeUSinkronu = false;
    // Stop both relays immediately
    digitalWrite(PIN_RELEJ_PARNE_KAZALJKE, LOW);
    digitalWrite(PIN_RELEJ_NEPARNE_KAZALJKE, LOW);
    impulsUTijeku = false;
    drugaFaza = false;
    return;
  }
  
  // Check if synchronized - if so, stop aggressive correction
  if (jeSinkronizirana(razlika)) {
    String log = F("Agresivna korekcija: sinkronizirana, razlika=");
    log += razlika;
    posaljiPCLog(log);
    korekcija_u_tijeku = false;
    kazaljkeUSinkronu = true;
    zadnjaAktiviranaMinuta = sadaRTC.minute();
    // Stop both relays immediately
    digitalWrite(PIN_RELEJ_PARNE_KAZALJKE, LOW);
    digitalWrite(PIN_RELEJ_NEPARNE_KAZALJKE, LOW);
    impulsUTijeku = false;
    drugaFaza = false;
    return;
  }
  
  // Send impulses at 6-second intervals without waiting for minute boundary
  if (vremePosljednjegImpulsa == 0 || (sadaMs - vremePosljednjegImpulsa) >= IMPULS_INTERVAL_MS) {
    
    // Send next impulse to continue correction
    pokreniPrvuFazu();
    vremePosljednjegImpulsa = sadaMs;
    trenutnaBrojImpulsa++;
    
    String log = F("Agresivna korekcija: impuls #");
    log += trenutnaBrojImpulsa;
    log += F(" razlika=");
    log += razlika;
    log += F(" K_minuta=");
    log += K_minuta;
    posaljiPCLog(log);
  }
  
  // Advance impulse phases (still need to execute proper 3s+3s timing)
  if (impulsUTijeku) {
    unsigned long proteklo = sadaMs - vrijemePocetkaImpulsa;
    
    // First phase to second phase
    if (!drugaFaza && proteklo >= FAZA_TRAJANJE_MS) {
      aktivirajRelejskuFazu(true);
      vrijemePocetkaImpulsa = sadaMs;
      drugaFaza = true;
    }
    // Complete impulse cycle
    else if (drugaFaza && proteklo >= FAZA_TRAJANJE_MS) {
      zavrsiImpuls();
    }
  }
}

// ==================== PUBLIC API ====================

// Start dynamic correction based on current difference
// Determines whether to use normal or aggressive mode
void pokreniBudnoKorekciju()
{
  DateTime sadaRTC = dohvatiTrenutnoVrijeme();
  int razlika = izracunajRazliku(sadaRTC);
  
  // If already synchronized, no correction needed
  if (razlika == 0) {
    kazaljkeUSinkronu = true;
    return;
  }
  
  korekcija_u_tijeku = true;
  kazaljkeUSinkronu = false;
  trenutnaBrojImpulsa = 0;
  vremePosljednjegImpulsa = 0;
  vremePocetkaKorekcije = millis();
  
  String log = F("Budna korekcija pokrenuta: razlika=");
  log += razlika;
  log += F(" minuta, DST threshold=");
  log += DST_WAITING_THRESHOLD;
  log += F(", režim=");
  
  // Check DST waiting state: if negative and within ±70 minutes, start in waiting mode
  if (razlika < 0 && abs(razlika) <= DST_WAITING_THRESHOLD) {
    log += F("ČEKANJE (DST ili manjih korekcija)");
    posaljiPCLog(log);
    korekcija_u_tijeku = false;
    kazaljkeUSinkronu = false;
    return;
  }
  
  if (abs(razlika) < NORMA_RAZLIKA_THRESHOLD) {
    log += F("NORMALAN (<10min)");
    posaljiPCLog(log);
  } else {
    log += F("AGRESIVAN (>=10min)");
    posaljiPCLog(log);
  }
}

// Main correction management function (call from main loop)
// Routes between normal and aggressive modes based on difference magnitude
void upravljajKorekcijomKazaljki()
{
  if (!korekcija_u_tijeku) {
    // Not in correction mode, execute normal operation
    upravljajKazaljkama();
    return;
  }
  
  // In correction mode: determine which mode to use based on current difference
  DateTime sadaRTC = dohvatiTrenutnoVrijeme();
  int razlika = izracunajRazliku(sadaRTC);
  
  // DST waiting state check (70-minute threshold)
  if (razlika < 0 && abs(razlika) <= DST_WAITING_THRESHOLD) {
    String log = F("Korekcija: čekam prirodni dovodenje (razlika=");
    log += razlika;
    log += F("), DST threshold=");
    log += DST_WAITING_THRESHOLD;
    posaljiPCLog(log);
    korekcija_u_tijeku = false;
    kazaljkeUSinkronu = false;
    return;
  }
  
  if (abs(razlika) < NORMA_RAZLIKA_THRESHOLD) {
    // Normal mode for small differences (< 10 minutes)
    normalnaKorekcija();
  } else {
    // Aggressive mode for large differences (>= 10 minutes)
    agresivnaKorekcija();
  }
}

// Set manual hand position and trigger correction
// Called from menu when user manually sets clock hands
void postaviRucnuPozicijuKazaljki(int satKazaljke, int minutaKazaljke)
{
  // Validate and constrain inputs
  satKazaljke = constrain(satKazaljke, 0, 11);
  minutaKazaljke = constrain(minutaKazaljke, 0, 59);
  
  int ciljnaMinuta = satKazaljke * 60 + minutaKazaljke;
  K_minuta = ciljnaMinuta % BROJ_MINUTA_CIKLUS;
  WearLeveling::spremi(EepromLayout::BAZA_KAZALJKE, EepromLayout::SLOTOVI_KAZALJKE, K_minuta);
  
  String log = F("Ručna postavka: ");
  if (satKazaljke < 10) log += F("0");
  log += satKazaljke;
  log += F(":");
  if (minutaKazaljke < 10) log += F("0");
  log += minutaKazaljke;
  posaljiPCLog(log);
  
  // Trigger correction to verify new position matches RTC
  pokreniBudnoKorekciju();
}

// Move hands by specified number of minutes (used for compensation)
// Directly increments K-minuta without impulses
void pomakniKazaljkeZa(int brojMinuta)
{
  K_minuta = (K_minuta + brojMinuta) % BROJ_MINUTA_CIKLUS;
  if (K_minuta < 0) K_minuta += BROJ_MINUTA_CIKLUS;
  
  WearLeveling::spremi(EepromLayout::BAZA_KAZALJKE, EepromLayout::SLOTOVI_KAZALJKE, K_minuta);
  
  String log = F("Pomak kazaljki: +");
  log += brojMinuta;
  log += F(" minuta, nova K_minuta=");
  log += K_minuta;
  posaljiPCLog(log);
}

// Blocking impulse movement to target position (used during setup/correction)
// Executes one full 6-second impulse immediately
static void odradiJedanPomakBlokirajuci()
{
  // First phase: aktiviraj relej prema trenutnom paritetu K_minuta.
  aktivirajRelejskuFazu(false);
  
  // Wait for first phase
  odradiPauzuSaLCD(FAZA_TRAJANJE_MS);
  
  // Gasimo oba releja prije druge faze.
  digitalWrite(PIN_RELEJ_PARNE_KAZALJKE, LOW);
  digitalWrite(PIN_RELEJ_NEPARNE_KAZALJKE, LOW);
  
  // Brief pause between phases
  odradiPauzuSaLCD(200);
  
  // Activate second phase relay (suprotni od prve faze).
  aktivirajRelejskuFazu(true);
  
  // Wait for second phase
  odradiPauzuSaLCD(FAZA_TRAJANJE_MS);
  
  // Deactivate all relays
  digitalWrite(PIN_RELEJ_PARNE_KAZALJKE, LOW);
  digitalWrite(PIN_RELEJ_NEPARNE_KAZALJKE, LOW);
  
  // Brief settling time
  odradiPauzuSaLCD(400);
  
  // Increment position and save to EEPROM
  K_minuta = (K_minuta + 1) % BROJ_MINUTA_CIKLUS;
  WearLeveling::spremi(EepromLayout::BAZA_KAZALJKE, EepromLayout::SLOTOVI_KAZALJKE, K_minuta);
}

// Compensation routine: move hands to match RTC time
// Used during boot to synchronize hands if they drifted
// pametanMod: if true, avoid movements smaller than threshold (wait for natural catch-up)
// DST handling: uses 70-minute threshold for waiting state
void kompenzirajKazaljke(bool pametanMod)
{
  DateTime pocetnoVrijeme = dohvatiTrenutnoVrijeme();
  int ciljnaMinuta = izracunajDvanaestSatneMinute(pocetnoVrijeme);
  
  int razlika = ciljnaMinuta - K_minuta;
  if (razlika < 0) razlika += BROJ_MINUTA_CIKLUS;
  
  // Smart mode: avoid unnecessary movements for small differences
  if (pametanMod && razlika <= MAKS_PAMETNI_POMAK_MINUTA) {
    String log = F("Kompenzacija (pametni mod): razlika=");
    log += razlika;
    log += F(" male - čekam prirodni dovodenje");;
    posaljiPCLog(log);
    return;
  }
  
  // DST waiting state handling in boot compensation (70-minute threshold)
  // If compensation difference is backward and within ±70 minutes, wait instead
  if (razlika < 0 && abs(razlika) <= DST_WAITING_THRESHOLD) {
    String log = F("Kompenzacija: čekam prirodni dovodenje, razlika=");
    log += razlika;
    log += F(" (DST threshold=");
    log += DST_WAITING_THRESHOLD;
    log += F(")");;
    posaljiPCLog(log);
    return;
  }
  
  String log = F("Kompenzacija kazaljki: početna K_minuta=");
  log += K_minuta;
  log += F(" ciljna=");
  log += ciljnaMinuta;
  log += F(" razlika=");
  log += razlika;
  log += F(" DST_threshold=");
  log += DST_WAITING_THRESHOLD;
  posaljiPCLog(log);
  
  // Move in blocking mode (used during boot or correction)
  // Execute one impulse for each minute of difference
  for (int i = 0; i < razlika; i++) {
    osvjeziWatchdog();
    odradiJedanPomakBlokirajuci();
  }
  
  // Final synchronization
  K_minuta = ciljnaMinuta % BROJ_MINUTA_CIKLUS;
  WearLeveling::spremi(EepromLayout::BAZA_KAZALJKE, EepromLayout::SLOTOVI_KAZALJKE, K_minuta);
  
  log = F("Kompenzacija završena: K_minuta=");
  log += K_minuta;
  log += F(" (DST threshold=");
  log += DST_WAITING_THRESHOLD;
  log += F(" min)");;
  posaljiPCLog(log);
  
  // Synchronize operation after compensation
  zadnjaAktiviranaMinuta = pocetnoVrijeme.minute();
  impulsUTijeku = false;
  drugaFaza = false;
  kazaljkeUSinkronu = true;
}

// Check if hands are synchronized with RTC
// Returns true if K-minuta matches RTC's 12-hour minute position
bool suKazaljkeUSinkronu()
{
  DateTime now = dohvatiTrenutnoVrijeme();
  int trenutnaMinuta = izracunajDvanaestSatneMinute(now);
  return K_minuta == trenutnaMinuta;
}

// Get current K-minuta value (for display and logging)
// Represents software position 0-719 for 12-hour cycle
int dohvatiMemoriraneKazaljkeMinuta()
{
  return K_minuta;
}

// Mark hands as synchronized (called after correction completes)
// Used to prevent repeated correction immediately after successful sync
void oznaciKazaljkeKaoSinkronizirane()
{
  DateTime now = dohvatiTrenutnoVrijeme();
  zadnjaAktiviranaMinuta = now.minute();
  kazaljkeUSinkronu = true;
  
  String log = F("Kazaljke označene kao sinkronizirane, K_minuta=");
  log += K_minuta;
  log += F(" (DST threshold=");
  log += DST_WAITING_THRESHOLD;
  log += F(" min)");;
  posaljiPCLog(log);
}

// Notify about DST change and adjust if needed
// Called when daylight saving time transitions occur
// pomakMinuta: positive for spring forward (CET→CEST), negative for fall back (CEST→CET)
// DST handling: updated to use 70-minute threshold for waiting state
void obavijestiKazaljkeDSTPromjena(int pomakMinuta)
{
  if (pomakMinuta <= 0) {
    // Moving back (CEST → CET): don't adjust hands - let them run naturally
    // Within 70-minute threshold, natural catching up is preferred
    String log = F("DST: pomak unatrag - kazaljke NE korigiramo, čekimo natural dovodenje (threshold=");
    log += DST_WAITING_THRESHOLD;
    log += F(" min)");;
    posaljiPCLog(log);
    return;
  }
  
  // Moving forward (CET → CEST): adjust hands forward
  int brojKoraka = pomakMinuta;
  
  String log = F("DST: kazaljke pomičem za +");
  log += brojKoraka;
  log += F(" minuta (DST threshold=");
  log += DST_WAITING_THRESHOLD;
  log += F(" min)");;
  posaljiPCLog(log);
  
  // Use normal movement (not blocking) to allow user to see the adjustment
  pomakniKazaljkeZa(brojKoraka);
}

// Placeholder function required by header file
// Sets the current hand position to a specific minute value
void postaviTrenutniPolozajKazaljki(int trenutnaMinuta)
{
  // Validate and constrain input
  trenutnaMinuta = constrain(trenutnaMinuta, 0, BROJ_MINUTA_CIKLUS - 1);
  
  K_minuta = trenutnaMinuta;
  WearLeveling::spremi(EepromLayout::BAZA_KAZALJKE, EepromLayout::SLOTOVI_KAZALJKE, K_minuta);
  zadnjaAktiviranaMinuta = dohvatiTrenutnoVrijeme().minute();
  
  String log = F("Postavljena pozicija: K_minuta=");
  log += K_minuta;
  log += F(" (DST threshold=");
  log += DST_WAITING_THRESHOLD;
  log += F(" min)");;
  posaljiPCLog(log);
}

// Placeholder function required by header file
// Moves hands to a specific minute position (used in smart correction mode)
// DST handling: uses 70-minute threshold for backward movements
void pomakniKazaljkeNaMinutu(int ciljMinuta, bool pametanMod)
{
  ciljMinuta = constrain(ciljMinuta, 0, BROJ_MINUTA_CIKLUS - 1);
  
  int razlika = ciljMinuta - K_minuta;
  if (razlika < 0) razlika += BROJ_MINUTA_CIKLUS;
  
  // Smart mode check: if difference is small, wait for natural catch-up
  if (pametanMod && razlika <= MAKS_PAMETNI_POMAK_MINUTA) {
    String log = F("Pomak na minutu (pametni mod): razlika=");
    log += razlika;
    log += F(" - čekam");
    posaljiPCLog(log);
    return;
  }
  
  // DST waiting state check in pomak na minutu (70-minute threshold)
  if (razlika < 0 && abs(razlika) <= DST_WAITING_THRESHOLD) {
    String log = F("Pomak na minutu: čekam natural dovodenje, razlika=");
    log += razlika;
    log += F(" (DST threshold=");
    log += DST_WAITING_THRESHOLD;
    log += F(")");;
    posaljiPCLog(log);
    return;
  }
  
  // Normal movement
  String log = F("Pomak na minutu: cilj=");
  log += ciljMinuta;
  log += F(" K_minuta=");
  log += K_minuta;
  log += F(" razlika=");
  log += razlika;
  log += F(" (DST threshold=");
  log += DST_WAITING_THRESHOLD;
  log += F(")");;
  posaljiPCLog(log);
  
  // Execute movement
  pomakniKazaljkeZa(razlika);
}
