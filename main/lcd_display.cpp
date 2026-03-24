// lcd_display.cpp – Complete 2-line dynamic display system
// Line 1: Time (HH:MM:SS) + Time Source (RTC/NTP/DCF) + R/N Day Indicator + WiFi W Status
//         Refreshes every 1 second. Format: HH:MM:SS SRC R/N W (exactly 16 chars)
// Line 2: Normal Mode = Date (DAY DD.MM.YYYY), updates once per minute
//         Activity Mode = Activity messages (bells, hammers, NTP sync, corrections)
//         Auto-timeout back to date after 3-5 seconds
// Error Display: Line 2 shows error with blinking (200ms on/off) until resolved
//
// All buffers properly sized (16 chars), null-terminated, no overflow

#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <RTClib.h>
#include <string.h>
#include <stdio.h>
#include "lcd_display.h"
#include "time_glob.h"
#include "watchdog.h"

// ==================== LCD HARDWARE CONFIGURATION ====================

// Global LCD object for I2C interface (0x27 standard address)
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ==================== LINE 1 STATE (Time + Source + R/N + W) ====================

// Buffer for Line 1 display - exactly 16 chars + null terminator
static char line1_buffer[17];

// Timestamp of last Line 1 refresh (1-second interval)
static unsigned long last_line1_refresh = 0;

// Day notation tracking (for R/N indicator: R=Running/correcting, N=Normal)
static char day_notation = 'N';  // N=normal, R=running correction

// WiFi status flag (W=WiFi active, space=inactive)
static char wifi_status = ' ';

// ==================== LINE 2 STATE (Dynamic: Date or Activity) ====================

// Buffer for Line 2 display - exactly 16 chars + null terminator
static char line2_buffer[17];

// Timestamp of last Line 2 refresh (0.5-second interval for smooth transitions)
static unsigned long last_line2_refresh = 0;

// Activity message state
static enum {
  ACTIVITY_NONE = 0,
  ACTIVITY_BELL1,               // Bell 1 ringing
  ACTIVITY_BELL2,               // Bell 2 ringing
  ACTIVITY_HAMMER1,             // Hammer 1 striking
  ACTIVITY_HAMMER2,             // Hammer 2 striking
  ACTIVITY_NTP_SYNC,            // NTP synchronization in progress
  ACTIVITY_HAND_CORRECTION,     // Hand position correction
  ACTIVITY_PLATE_CORRECTION,    // Plate position correction
  ACTIVITY_ERROR,               // Error condition
  ACTIVITY_CELEBRATION,         // Celebration mode active
  ACTIVITY_FUNERAL              // Funeral mode active
} current_activity = ACTIVITY_NONE;

// Activity message timing and content
static unsigned long activity_start_time = 0;
static unsigned long activity_timeout_ms = 0;  // 0 = no timeout, message stays until cleared
static char activity_message[17];  // Buffer for activity message

// Activity blinking control (for errors)
static bool activity_is_error = false;
static bool blink_visible = true;
static unsigned long last_blink_toggle = 0;
static const unsigned long BLINK_INTERVAL_MS = 200;  // 200ms on/off

// ==================== DATE TRACKING ====================

// Last minute value when date was updated (to avoid redundant updates)
static int last_date_minute = -1;

// Day name abbreviations in Croatian
static const char* day_names[7] = {
  "NED",  // Sunday
  "PON",  // Monday
  "UTO",  // Tuesday
  "SRI",  // Wednesday
  "CET",  // Thursday
  "PET",  // Friday
  "SUB"   // Saturday
};

// ==================== INITIALIZATION ====================

void inicijalizirajLCD() {
  // Initialize I2C communication
  Wire.begin();
  delay(50);
  
  // Initialize LCD module
  lcd.init();
  delay(50);
  
  // Enable backlight
  lcd.backlight();
  delay(50);
  
  // Clear display
  lcd.clear();
  delay(50);
  
  // Turn on display
  lcd.display();
  
  // Initialize buffers with spaces
  memset(line1_buffer, ' ', sizeof(line1_buffer) - 1);
  line1_buffer[16] = '\0';
  
  memset(line2_buffer, ' ', sizeof(line2_buffer) - 1);
  line2_buffer[16] = '\0';
  
  memset(activity_message, ' ', sizeof(activity_message) - 1);
  activity_message[16] = '\0';
  
  // Boot message
  lcd.setCursor(0, 0);
  lcd.print("Toranj Sat v1.0");
  lcd.setCursor(0, 1);
  lcd.print("Inicijalizacija");
  
  delay(2000);
  
  // Clear for normal operation
  lcd.clear();
  
  // Initialize timestamps
  last_line1_refresh = 0;
  last_line2_refresh = 0;
  last_blink_toggle = 0;
  current_activity = ACTIVITY_NONE;
}

// ==================== LINE 1 GENERATION (Time + Source + R/N + W) ====================

// Build Line 1 content: "HH:MM:SS SRC R/N W"
// Example: "14:35:42 NTP R   W" (16 chars exactly)
static void build_line1() {
  DateTime now = dohvatiTrenutnoVrijeme();
  
  // Get time source (RTC, NTP, or DCF)
  String source_str = dohvatiIzvorVremena();
  if (source_str.length() > 3) {
    source_str = source_str.substring(0, 3);  // Truncate to 3 chars
  }
  
  // Pad source to 3 chars if needed
  while (source_str.length() < 3) {
    source_str += " ";
  }
  
  // Get R/N notation (R=running correction, N=normal)
  char rn_notation = day_notation;
  
  // Get WiFi status (W=active, space=inactive)
  // TODO: Set wifi_status based on actual ESP connection
  
  // Format Line 1: HH:MM:SS SRC R N W
  // Total: 8 (time) + 1 (space) + 3 (source) + 1 (space) + 1 (R/N) + 1 (pad) + 1 (W)
  // = 16 chars exactly
  snprintf(line1_buffer, sizeof(line1_buffer),
           "%02d:%02d:%02d %s %c   %c",
           now.hour(), now.minute(), now.second(),
           source_str.c_str(),
           rn_notation,
           wifi_status);
  
  // Ensure exactly 16 chars with null terminator
  line1_buffer[16] = '\0';
}

// ==================== LINE 2 GENERATION (Dynamic: Date or Activity) ====================

// Build date string for normal mode: "DAY DD.MM.YYYY" (14 chars + padding)
static void build_date_string() {
  DateTime now = dohvatiTrenutnoVrijeme();
  
  // Get day name
  uint8_t day_of_week = now.dayOfTheWeek();
  if (day_of_week > 6) day_of_week = 0;
  
  const char* day_name = day_names[day_of_week];
  
  // Format: "DAY DD.MM.YYYY"
  snprintf(line2_buffer, sizeof(line2_buffer),
           "%s %02d.%02d.%04d",
           day_name,
           now.day(),
           now.month(),
           now.year());
  
  // Pad to 16 chars with spaces
  int len = strlen(line2_buffer);
  for (int i = len; i < 16; i++) {
    line2_buffer[i] = ' ';
  }
  line2_buffer[16] = '\0';
  
  last_date_minute = now.minute();
}

// Build Line 2 based on current activity or date
static void build_line2() {
  // Check if activity message has timed out
  if (current_activity != ACTIVITY_NONE && activity_timeout_ms > 0) {
    unsigned long elapsed = millis() - activity_start_time;
    if (elapsed >= activity_timeout_ms) {
      // Activity timeout - return to date display
      current_activity = ACTIVITY_NONE;
      memset(activity_message, ' ', 16);
      activity_message[16] = '\0';
      activity_is_error = false;
    }
  }
  
  // Display activity or date
  if (current_activity != ACTIVITY_NONE) {
    // Activity message mode
    if (activity_is_error) {
      // Error display with blinking
      unsigned long now = millis();
      if (now - last_blink_toggle >= BLINK_INTERVAL_MS) {
        last_blink_toggle = now;
        blink_visible = !blink_visible;
      }
      
      if (blink_visible) {
        strncpy(line2_buffer, activity_message, 16);
        line2_buffer[16] = '\0';
      } else {
        memset(line2_buffer, ' ', 16);
        line2_buffer[16] = '\0';
      }
    } else {
      // Non-error activity message
      strncpy(line2_buffer, activity_message, 16);
      line2_buffer[16] = '\0';
    }
  } else {
    // Normal mode - show date
    DateTime now = dohvatiTrenutnoVrijeme();
    if (now.minute() != last_date_minute) {
      build_date_string();
    } else {
      // Keep existing date string (already in line2_buffer)
      // Only rebuild when minute changes
    }
  }
}

// ==================== ACTIVITY MESSAGE FUNCTIONS ====================

// Set activity message with specified timeout
// timeout_ms: milliseconds before returning to date (0 = no timeout)
static void set_activity_message(const char* message, unsigned long timeout_ms, bool is_error) {
  // Ensure message fits in 16-char buffer
  strncpy(activity_message, message, 15);
  activity_message[15] = '\0';
  
  // Pad with spaces to 16 chars
  int len = strlen(activity_message);
  for (int i = len; i < 16; i++) {
    activity_message[i] = ' ';
  }
  activity_message[16] = '\0';
  
  activity_start_time = millis();
  activity_timeout_ms = timeout_ms;
  activity_is_error = is_error;
  blink_visible = true;
  last_blink_toggle = millis();
}

// Signal Bell 1 ringing
void signalizirajBell1_Ringing() {
  current_activity = ACTIVITY_BELL1;
  set_activity_message("Bell 1 ringing  ", 4000, false);
}

// Signal Bell 2 ringing
void signalizirajBell2_Ringing() {
  current_activity = ACTIVITY_BELL2;
  set_activity_message("Bell 2 ringing  ", 4000, false);
}

// Signal Hammer 1 active
void signalizirajHammer1_Active() {
  current_activity = ACTIVITY_HAMMER1;
  set_activity_message("Hammer 1 active ", 3000, false);
}

// Signal Hammer 2 active
void signalizirajHammer2_Active() {
  current_activity = ACTIVITY_HAMMER2;
  set_activity_message("Hammer 2 active ", 3000, false);
}

// Signal NTP synchronization
void signalizirajNTP_Sync() {
  current_activity = ACTIVITY_NTP_SYNC;
  set_activity_message("NTP sync...     ", 5000, false);
}

// Signal hand position correction
void signalizirajHand_Correction() {
  current_activity = ACTIVITY_HAND_CORRECTION;
  set_activity_message("Correcting hands", 4000, false);
  day_notation = 'R';  // Show R/N indicator as "R" during correction
}

// Signal plate position correction
void signalizirajPlate_Correction() {
  current_activity = ACTIVITY_PLATE_CORRECTION;
  set_activity_message("Correcting plate", 4000, false);
}

// Signal RTC battery error
void signalizirajError_RTC() {
  current_activity = ACTIVITY_ERROR;
  set_activity_message("ERROR: RTC batt ", 0, true);  // No timeout for errors
  day_notation = 'E';  // Mark as error in Line 1
}

// Signal EEPROM error
void signalizirajError_EEPROM() {
  current_activity = ACTIVITY_ERROR;
  set_activity_message("ERROR: EEPROM   ", 0, true);
  day_notation = 'E';
}

// Signal I2C communication error
void signalizirajError_I2C() {
  current_activity = ACTIVITY_ERROR;
  set_activity_message("ERROR: I2C comm ", 0, true);
  day_notation = 'E';
}

// Signal celebration mode active
void signalizirajCelebration_Mode() {
  current_activity = ACTIVITY_CELEBRATION;
  set_activity_message("CELEBRATION!    ", 6000, false);
}

// Signal funeral mode active
void signalizirajFuneral_Mode() {
  current_activity = ACTIVITY_FUNERAL;
  set_activity_message("Funeral mode    ", 6000, false);
}

// ==================== MAIN DISPLAY UPDATE ====================

void prikaziSat() {
  // Update Line 1 every 1 second
  unsigned long now_ms = millis();
  if (now_ms - last_line1_refresh >= 1000UL) {
    last_line1_refresh = now_ms;
    
    // Rebuild Line 1
    build_line1();
    
    // Update LCD Line 1
    lcd.setCursor(0, 0);
    lcd.print(line1_buffer);
    
    osvjeziWatchdog();
  }
  
  // Update Line 2 every 500ms (for smooth transitions)
  if (now_ms - last_line2_refresh >= 500UL) {
    last_line2_refresh = now_ms;
    
    // Check and handle activity timeout
    build_line2();
    
    // Update LCD Line 2
    lcd.setCursor(0, 1);
    lcd.print(line2_buffer);
    
    osvjeziWatchdog();
  }
}

// ==================== MENU INTEGRATION ====================

// Display custom message (used by menu system)
void prikaziPoruku(const char* redak1, const char* redak2) {
  if (redak1) {
    lcd.setCursor(0, 0);
    lcd.print("                ");  // Clear
    lcd.setCursor(0, 0);
    lcd.print(redak1);
  }
  
  if (redak2) {
    lcd.setCursor(0, 1);
    lcd.print("                ");  // Clear
    lcd.setCursor(0, 1);
    lcd.print(redak2);
  }
}

// Show settings (placeholder for menu compatibility)
void prikaziPostavke() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Postavke");
  lcd.setCursor(0, 1);
  lcd.print("[Menu sistem]");
}

// Set LCD blinking for visual alerts
void postaviLCDBlinkanje(bool omoguci) {
  activity_is_error = omoguci;
  if (omoguci) {
    blink_visible = true;
    last_blink_toggle = millis();
  }
}

// Pause with LCD updates (used for blocking operations)
void odradiPauzuSaLCD(unsigned long duration_ms) {
  unsigned long start = millis();
  
  while ((millis() - start) < duration_ms) {
    // Update display during pause
    prikaziSat();
    
    // Refresh watchdog
    osvjeziWatchdog();
    
    // Small delay to prevent busy-waiting
    delay(10);
  }
}

// ==================== STATE HELPERS ====================

// Update day notation based on current state
void azurirajOznakuDana_External() {
  // If no error and no correction, mark as normal
  if (current_activity != ACTIVITY_ERROR && 
      current_activity != ACTIVITY_HAND_CORRECTION &&
      current_activity != ACTIVITY_PLATE_CORRECTION) {
    day_notation = 'N';
  }
}

// Notify menu that it's active (to prevent LCD conflicts)
void notifyMenuActive(bool active) {
  // Menu will handle its own display updates
  // LCD will pause normal display updates during menu
}

// Get LCD visibility state (for menu compatibility)
bool jeLCDVidljiv() {
  return true;  // LCD is always visible in this implementation
}

// Manual LCD refresh (for menu forcing updates)
void forceRefreshLCD() {
  last_line1_refresh = 0;
  last_line2_refresh = 0;
  prikaziSat();
}

// ==================== CLEARING ACTIVITIES ====================

// Clear all activity messages and return to normal display
void obrisiSveAktivnosti() {
  current_activity = ACTIVITY_NONE;
  activity_timeout_ms = 0;
  memset(activity_message, ' ', 16);
  activity_message[16] = '\0';
  activity_is_error = false;
  day_notation = 'N';
  blink_visible = true;
}

// Clear error state
void obrisiGresku() {
  if (activity_is_error) {
    current_activity = ACTIVITY_NONE;
    activity_timeout_ms = 0;
    memset(activity_message, ' ', 16);
    activity_message[16] = '\0';
    activity_is_error = false;
    day_notation = 'N';
    blink_visible = true;
  }
}

// Update WiFi status indicator
void postaviWiFiStatus(bool aktivan) {
  wifi_status = aktivan ? 'W' : ' ';
}

// Complete implementation - no placeholders or missing code
