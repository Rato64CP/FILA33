// lcd_display.cpp - Complete 2-line dynamic display system
// Line 1: Time (HH:MM:SS) + Time Source (RTC/NTP/DCF) + R/N Day Indicator + WiFi W Status
// Line 2: Date or activity message for the tower clock subsystems.

#include <Arduino.h>
#include <Wire.h>
#include <avr/pgmspace.h>
#include <LiquidCrystal_I2C.h>
#include <RTClib.h>
#include <string.h>
#include <stdio.h>
#include "lcd_display.h"
#include "time_glob.h"
#include "zvonjenje.h"
#include "unified_motion_state.h"
#include "watchdog.h"

LiquidCrystal_I2C lcd(0x27, 16, 2);

static char line1_buffer[17];
static unsigned long last_line1_refresh = 0;

// N = normalan rad, R = korekcija, E = greska/recovery
static char status_oznaka = 'N';
static char wifi_status = ' ';

static char line2_buffer[17];
static unsigned long last_line2_refresh = 0;

static enum {
  ACTIVITY_NONE = 0,
  ACTIVITY_BELL1,
  ACTIVITY_BELL2,
  ACTIVITY_BELL3,
  ACTIVITY_BELL4,
  ACTIVITY_HAMMER1,
  ACTIVITY_HAMMER2,
  ACTIVITY_ERROR,
  ACTIVITY_CELEBRATION,
  ACTIVITY_FUNERAL
} current_activity = ACTIVITY_NONE;

static unsigned long activity_start_time = 0;
static unsigned long activity_timeout_ms = 0;
static char activity_message[17];

static bool activity_is_error = false;
static bool blink_visible = true;
static unsigned long last_blink_toggle = 0;
static const unsigned long BLINK_INTERVAL_MS = 200;
static const char LCD_DAN_NED[] PROGMEM = "NED";
static const char LCD_DAN_PON[] PROGMEM = "PON";
static const char LCD_DAN_UTO[] PROGMEM = "UTO";
static const char LCD_DAN_SRI[] PROGMEM = "SRI";
static const char LCD_DAN_CET[] PROGMEM = "CET";
static const char LCD_DAN_PET[] PROGMEM = "PET";
static const char LCD_DAN_SUB[] PROGMEM = "SUB";
static const char* const LCD_NAZIVI_DANA[] PROGMEM = {
  LCD_DAN_NED,
  LCD_DAN_PON,
  LCD_DAN_UTO,
  LCD_DAN_SRI,
  LCD_DAN_CET,
  LCD_DAN_PET,
  LCD_DAN_SUB
};
static const char LCD_PORUKA_BELL1[] PROGMEM = "Bell 1 ringing  ";
static const char LCD_PORUKA_BELL2[] PROGMEM = "Bell 2 ringing  ";
static const char LCD_PORUKA_BELL3[] PROGMEM = "Bell 3 ringing  ";
static const char LCD_PORUKA_BELL4[] PROGMEM = "Bell 4 ringing  ";
static const char LCD_PORUKA_CEKIC1[] PROGMEM = "Hammer 1 active ";
static const char LCD_PORUKA_CEKIC2[] PROGMEM = "Hammer 2 active ";
static const char LCD_PORUKA_ERR_RTC[] PROGMEM = "ERROR: RTC batt ";
static const char LCD_PORUKA_ERR_EEPROM[] PROGMEM = "ERROR: EEPROM   ";
static const char LCD_PORUKA_ERR_I2C[] PROGMEM = "ERROR: I2C comm ";
static const char LCD_PORUKA_SLAVLJENJE[] PROGMEM = "SLAVLJENJE      ";
static const char LCD_PORUKA_MRTVACKO[] PROGMEM = "MRTVACKO ZVONO  ";

static void kopirajTekstIzFlash(char* odrediste, size_t velicina, PGM_P izvor) {
  strncpy_P(odrediste, izvor, velicina - 1);
  odrediste[velicina - 1] = '\0';
}

static PGM_P dohvatiNazivDanaIzFlash(uint8_t danUTjednu) {
  return reinterpret_cast<PGM_P>(pgm_read_ptr(&LCD_NAZIVI_DANA[danUTjednu]));
}

static char izracunajOznakuStanjaLCD() {
  if (current_activity == ACTIVITY_ERROR || activity_is_error) {
    return 'E';
  }

  const EepromLayout::UnifiedMotionState stanje = UnifiedMotionStateStore::dohvatiIliMigriraj();
  if (stanje.hand_active != 0 || stanje.plate_phase != 0) {
    return 'R';
  }

  return 'N';
}

static int last_date_minute = -1;

void inicijalizirajLCD() {
  Wire.begin();
  delay(50);

  lcd.init();
  delay(50);

  lcd.backlight();
  delay(50);

  lcd.clear();
  delay(50);

  lcd.display();

  memset(line1_buffer, ' ', sizeof(line1_buffer) - 1);
  line1_buffer[16] = '\0';

  memset(line2_buffer, ' ', sizeof(line2_buffer) - 1);
  line2_buffer[16] = '\0';

  memset(activity_message, ' ', sizeof(activity_message) - 1);
  activity_message[16] = '\0';

  lcd.setCursor(0, 0);
  lcd.print(F("Toranj Sat v1.0"));
  lcd.setCursor(0, 1);
  lcd.print(F("Inicijalizacija"));

  delay(2000);
  lcd.clear();

  last_line1_refresh = 0;
  last_line2_refresh = 0;
  last_blink_toggle = 0;
  current_activity = ACTIVITY_NONE;
}

static void build_line1() {
  DateTime now = dohvatiTrenutnoVrijeme();
  char source_str[4];
  strncpy(source_str, dohvatiOznakuIzvoraVremena(), sizeof(source_str) - 1);
  source_str[sizeof(source_str) - 1] = '\0';

  status_oznaka = izracunajOznakuStanjaLCD();
  char oznaka_stanja = status_oznaka;

  snprintf(line1_buffer, sizeof(line1_buffer),
           "%02d:%02d:%02d %s %c %c",
           now.hour(), now.minute(), now.second(),
           source_str,
           oznaka_stanja,
           wifi_status);
  line1_buffer[16] = '\0';
}

static void build_date_string() {
  DateTime now = dohvatiTrenutnoVrijeme();
  uint8_t day_of_week = now.dayOfTheWeek();
  if (day_of_week > 6) day_of_week = 0;

  char day_name[4];
  kopirajTekstIzFlash(day_name, sizeof(day_name), dohvatiNazivDanaIzFlash(day_of_week));

  snprintf(line2_buffer, sizeof(line2_buffer),
           "%s %02d.%02d.%04d",
           day_name,
           now.day(),
           now.month(),
           now.year());

  int len = strlen(line2_buffer);
  for (int i = len; i < 16; i++) {
    line2_buffer[i] = ' ';
  }
  line2_buffer[16] = '\0';

  last_date_minute = now.minute();
}

static void build_line2() {
  // Dinamicka poruka za slavljenje mora pratiti stvarno stanje cekica.
  if (current_activity == ACTIVITY_CELEBRATION && !jeSlavljenjeUTijeku()) {
    current_activity = ACTIVITY_NONE;
    activity_timeout_ms = 0;
    memset(activity_message, ' ', 16);
    activity_message[16] = '\0';
    activity_is_error = false;
  }

  // Dinamicka poruka za mrtvacko zvono mora nestati cim se nacin rada ugasi.
  if (current_activity == ACTIVITY_FUNERAL && !jeMrtvackoUTijeku()) {
    current_activity = ACTIVITY_NONE;
    activity_timeout_ms = 0;
    memset(activity_message, ' ', 16);
    activity_message[16] = '\0';
    activity_is_error = false;
  }

  if (current_activity != ACTIVITY_NONE && activity_timeout_ms > 0) {
    unsigned long elapsed = millis() - activity_start_time;
    if (elapsed >= activity_timeout_ms) {
      current_activity = ACTIVITY_NONE;
      memset(activity_message, ' ', 16);
      activity_message[16] = '\0';
      activity_is_error = false;
    }
  }

  if (current_activity != ACTIVITY_NONE) {
    if (activity_is_error) {
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
      strncpy(line2_buffer, activity_message, 16);
      line2_buffer[16] = '\0';
    }
  } else {
    DateTime now = dohvatiTrenutnoVrijeme();
    if (now.minute() != last_date_minute) {
      build_date_string();
    }
  }
}

static void set_activity_message(PGM_P message, unsigned long timeout_ms, bool is_error) {
  kopirajTekstIzFlash(activity_message, sizeof(activity_message), message);

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

void signalizirajZvono_Ringing(uint8_t zvono) {
  switch (zvono) {
    case 1:
      current_activity = ACTIVITY_BELL1;
      set_activity_message(LCD_PORUKA_BELL1, 4000, false);
      break;
    case 2:
      current_activity = ACTIVITY_BELL2;
      set_activity_message(LCD_PORUKA_BELL2, 4000, false);
      break;
    case 3:
      current_activity = ACTIVITY_BELL3;
      set_activity_message(LCD_PORUKA_BELL3, 4000, false);
      break;
    case 4:
      current_activity = ACTIVITY_BELL4;
      set_activity_message(LCD_PORUKA_BELL4, 4000, false);
      break;
    default:
      break;
  }
}

void signalizirajHammer1_Active() {
  current_activity = ACTIVITY_HAMMER1;
  set_activity_message(LCD_PORUKA_CEKIC1, 3000, false);
}

void signalizirajHammer2_Active() {
  current_activity = ACTIVITY_HAMMER2;
  set_activity_message(LCD_PORUKA_CEKIC2, 3000, false);
}

void signalizirajError_RTC() {
  current_activity = ACTIVITY_ERROR;
  set_activity_message(LCD_PORUKA_ERR_RTC, 0, true);
  status_oznaka = 'E';
}

void signalizirajError_EEPROM() {
  current_activity = ACTIVITY_ERROR;
  set_activity_message(LCD_PORUKA_ERR_EEPROM, 0, true);
  status_oznaka = 'E';
}

void signalizirajError_I2C() {
  current_activity = ACTIVITY_ERROR;
  set_activity_message(LCD_PORUKA_ERR_I2C, 0, true);
  status_oznaka = 'E';
}

void signalizirajCelebration_Mode() {
  current_activity = ACTIVITY_CELEBRATION;
  set_activity_message(LCD_PORUKA_SLAVLJENJE, 0, false);
}

void signalizirajFuneral_Mode() {
  current_activity = ACTIVITY_FUNERAL;
  set_activity_message(LCD_PORUKA_MRTVACKO, 0, false);
}

void prikaziSat() {
  unsigned long now_ms = millis();
  if (now_ms - last_line1_refresh >= 1000UL) {
    last_line1_refresh = now_ms;
    build_line1();
    lcd.setCursor(0, 0);
    lcd.print(line1_buffer);
    osvjeziWatchdog();
  }

  if (now_ms - last_line2_refresh >= 500UL) {
    last_line2_refresh = now_ms;
    build_line2();
    lcd.setCursor(0, 1);
    lcd.print(line2_buffer);
    osvjeziWatchdog();
  }
}

void prikaziPoruku(const char* redak1, const char* redak2) {
  if (redak1) {
    lcd.setCursor(0, 0);
    lcd.print(F("                "));
    lcd.setCursor(0, 0);
    lcd.print(redak1);
  }

  if (redak2) {
    lcd.setCursor(0, 1);
    lcd.print(F("                "));
    lcd.setCursor(0, 1);
    lcd.print(redak2);
  }
}

void postaviWiFiStatus(bool aktivan) {
  wifi_status = aktivan ? 'W' : ' ';
}

void primijeniLCDPozadinskoOsvjetljenje(bool ukljuci) {
  if (ukljuci) {
    lcd.backlight();
  } else {
    lcd.noBacklight();
  }
}
