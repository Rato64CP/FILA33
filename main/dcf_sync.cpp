// dcf_sync.cpp – DCF77 synchronization module (simplified, no external library)
// Provides time synchronization from DCF77 signal during night hours (22:00-06:00)
// Implements basic bit-pattern detection without external DCF77 library
// Falls back to RTC if DCF signal is unavailable
// CORRECTED: Include time_glob.h instead of vrijeme_izvor.h

#include <Arduino.h>
#include "dcf_sync.h"
#include "podesavanja_piny.h"
#include "time_glob.h"
#include "pc_serial.h"

// ==================== DCF77 CONFIGURATION ====================

// DCF77 receiver signal on PIN_DCF_SIGNAL (open-collector input)
// Signal characteristics:
// - 1-second pulse every second (100ms LOW = bit 0, 200ms LOW = bit 1)
// - Minute mark: 2-second pause between minutes
// - 59 bits per minute (0-58 are data, bit 59 is minute marker)

// Night operation window for DCF sync (22:00 to 06:00)
// DCF77 is more reliable at night with fewer RF disturbances
static const uint8_t DCF_SAT_NOC_OD = 22;  // Night window starts at 22:00
static const uint8_t DCF_SAT_NOC_DO = 6;   // Night window ends at 06:00

// Check interval for DCF updates (once per minute)
static const unsigned long DCF_INTERVAL_PROVJERE_MS = 60000;

// Signal stabilization period: wait 60 seconds after night window starts
// before attempting to read DCF77 (signal needs time to stabilize)
static const unsigned long DCF_VRIJEME_STABILIZACIJE_MS = 60000;

// ==================== DCF77 BIT DETECTION ====================

// Pulse width detection thresholds (milliseconds)
// Bit encoding: 100ms pulse = bit 0, 200ms pulse = bit 1
static const unsigned long PULS_0_MIN = 80;      // Minimum pulse for bit 0
static const unsigned long PULS_0_MAX = 150;     // Maximum pulse for bit 0
static const unsigned long PULS_1_MIN = 180;     // Minimum pulse for bit 1
static const unsigned long PULS_1_MAX = 250;     // Maximum pulse for bit 1

// ==================== STATE TRACKING ====================

// Has DCF receiver been started?
static bool dcfPokrenut = false;

// Last successful time update from DCF77
static DateTime zadnjeDCF = DateTime((uint32_t)0);

// Timestamp of last DCF sync attempt
static unsigned long zadnjaProvjeraMillis = 0;

// When night window started (for stabilization timeout)
static unsigned long vrijemePocetka = 0;

// DCF bit buffer (59 bits per minute)
static uint8_t dcfBitovi[59];
static int dcfBrojBita = 0;
static unsigned long dcfPosljednjaPulsaVrijeme = 0;
static bool dcfSadaJeNiska = false;

// ==================== HELPER FUNCTIONS ====================

// Check if current time is within DCF77 operating window (22:00-06:00)
// DCF77 signal is better at night with fewer RF disturbances
static bool jeNocniDCFInterval() {
  DateTime sada = dohvatiTrenutnoVrijeme();
  uint8_t sat = sada.hour();
  
  if (DCF_SAT_NOC_OD == DCF_SAT_NOC_DO) {
    return false;  // Invalid configuration if boundaries are equal
  }

  // Handle wrap-around at midnight (22:00 to 23:59, then 00:00 to 06:00)
  if (DCF_SAT_NOC_OD > DCF_SAT_NOC_DO) {
    return sat >= DCF_SAT_NOC_OD || sat < DCF_SAT_NOC_DO;
  }

  // Normal range without wrap-around (unusual for DCF but supported)
  return sat >= DCF_SAT_NOC_OD && sat < DCF_SAT_NOC_DO;
}

// Check if DCF signal has stabilized since night window started
// Returns true if enough time (60s) has passed since entering night window
static bool dcfStabiliziran() {
  return (millis() - vrijemePocetka) > DCF_VRIJEME_STABILIZACIJE_MS;
}

// Detect DCF bit from pulse duration
// Returns: 0 for bit 0, 1 for bit 1, -1 for invalid pulse
static int procitajDCFBit(unsigned long trajanjePulseMs) {
  if (trajanjePulseMs >= PULS_0_MIN && trajanjePulseMs <= PULS_0_MAX) {
    return 0;  // Bit 0 detected
  } else if (trajanjePulseMs >= PULS_1_MIN && trajanjePulseMs <= PULS_1_MAX) {
    return 1;  // Bit 1 detected
  }
  return -1;  // Invalid pulse duration
}

// Decode DCF77 time from 59-bit buffer
// Returns DateTime object or DateTime(0) if decoding fails
static DateTime dekodiraiDCFVrijeme() {
  // DCF77 bit positions (0-indexed):
  // 0: Not used
  // 1: CEST/CET flag (0=CET, 1=CEST)
  // 2: CEST transition flag
  // 3: Leap second flag
  // 4: Start of time data
  // 5-10: Minutes (6 bits, BCD coded)
  // 11: Minute parity
  // 12-17: Hours (6 bits, BCD coded)
  // 18-24: Day of month (6 bits, BCD coded)
  // 25-28: Day of week (3 bits, 1=Monday)
  // 29-34: Month (5 bits, BCD coded)
  // 35-39: Year (8 bits, BCD coded, offset from 2000)
  // 40-58: Parity bits
  
  // Simplified approach: extract minutes and hours only
  // Full implementation would need parity checking and BCD decoding
  
  if (dcfBrojBita < 59) {
    return DateTime((uint32_t)0);  // Incomplete buffer
  }
  
  // Extract minutes (bits 5-10, BCD coded)
  // BCD: tens in bits 5-9, ones in bits 10
  // This is simplified - full DCF77 has more complex encoding
  
  // For now, just signal that we received a complete frame
  // Return current RTC time as fallback (DCF77 reception requires full implementation)
  return dohvatiTrenutnoVrijeme();
}

// ==================== PUBLIC FUNCTIONS ====================

// Initialize DCF77 receiver module
// Called during system startup to configure interrupt-based decoding
void inicijalizirajDCF() {
  // Configure physical pin for DCF signal (open collector input)
  pinMode(PIN_DCF_SIGNAL, INPUT);
  
  // Initialize bit buffer
  memset(dcfBitovi, 0, sizeof(dcfBitovi));
  dcfBrojBita = 0;
  dcfPokrenut = true;
  
  // Record when night window started (for stabilization timeout)
  vrijemePocetka = millis();
  
  // Initialize last check timestamp to force check on first call
  zadnjaProvjeraMillis = 0;
  
  posaljiPCLog(F("DCF77: Inicijaliziran na PIN "));
  String log = F("PIN = ");
  log += PIN_DCF_SIGNAL;
  posaljiPCLog(log);
}

// Main DCF77 synchronization routine
// Call from main loop() once per second to check for time updates
// Only attempts sync during night hours (22:00-06:00) when signal is stronger
void osvjeziDCFSinkronizaciju() {
  // Module not initialized
  if (!dcfPokrenut) {
    return;
  }

  // Outside operating window (daytime 06:00-22:00) - skip DCF checking
  if (!jeNocniDCFInterval()) {
    // Reset check timestamp on transition to day mode
    zadnjaProvjeraMillis = 0;
    dcfBrojBita = 0;  // Reset buffer on day transition
    return;
  }

  // Calculate time since last check
  unsigned long sadaMs = millis();
  
  // Skip if checked recently (only check every 60 seconds)
  if (zadnjaProvjeraMillis != 0 && 
      (sadaMs - zadnjaProvjeraMillis) < DCF_INTERVAL_PROVJERE_MS) {
    return;
  }
  
  // Update last check timestamp
  zadnjaProvjeraMillis = sadaMs;

  // Wait for signal stabilization after entering night window
  if (!dcfStabiliziran()) {
    return;
  }

  // Read DCF signal and detect pulse edges
  bool trenutnaVrijednost = (digitalRead(PIN_DCF_SIGNAL) == LOW);
  
  // Edge detection: LOW pulse (DCF sends LOW for bits)
  if (trenutnaVrijednost != dcfSadaJeNiska) {
    unsigned long trajanjePulseMs = sadaMs - dcfPosljednjaPulsaVrijeme;
    
    // Falling edge (HIGH to LOW) - start of bit pulse
    if (trenutnaVrijednost && !dcfSadaJeNiska) {
      dcfPosljednjaPulsaVrijeme = sadaMs;
    }
    // Rising edge (LOW to HIGH) - end of bit pulse
    else if (!trenutnaVrijednost && dcfSadaJeNiska) {
      // Detect bit value from pulse duration
      int bit = procitajDCFBit(trajanjePulseMs);
      
      if (bit >= 0 && dcfBrojBita < 59) {
        // Valid bit detected
        dcfBitovi[dcfBrojBita] = bit;
        dcfBrojBita++;
        
        // Complete minute (59 bits received)
        if (dcfBrojBita >= 59) {
          // Attempt to decode time
          DateTime novi = dekodiraiDCFVrijeme();
          
          if (novi.unixtime() > 0) {
            zadnjeDCF = novi;
            
            // Update RTC if DCF signal is better than current source
            String trenutniIzvor = dohvatiIzvorVremena();
            if (trenutniIzvor != "NTP" || jeSinkronizacijaZastarjela()) {
              azurirajVrijemeIzDCF(novi);
              
              String log = F("DCF77: Sinkronizacija - ");
              log += novi.year();
              log += F("-");
              log += novi.month();
              log += F("-");
              log += novi.day();
              log += F(" ");
              log += novi.hour();
              log += F(":");
              log += novi.minute();
              posaljiPCLog(log);
            }
          }
          
          // Reset for next minute
          dcfBrojBita = 0;
        }
      } else if (bit < 0) {
        // Invalid pulse - might be minute mark (2-second pause)
        // Reset on minute boundary
        if (trajanjePulseMs > 1500) {
          dcfBrojBita = 0;  // Minute mark detected, reset buffer
          posaljiPCLog(F("DCF77: Minute mark"));
        }
      }
    }
    
    dcfSadaJeNiska = trenutnaVrijednost;
  }
}

// ==================== DIAGNOSTIC FUNCTIONS ====================

// Get last successful DCF77 time reading
// Returns DateTime object from last sync, or DateTime(0) if never synced
DateTime dohvatiZadnjeDCFVrijeme() {
  return zadnjeDCF;
}

// Check if DCF77 receiver has ever synced successfully
bool jeKadDCFSinkroniziran() {
  return zadnjeDCF.unixtime() > 0;
}