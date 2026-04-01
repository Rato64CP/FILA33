// lcd_display.cpp - Dinamicki 2-retni LCD prikaz toranjskog sata
// Redak 1: vrijeme (HH:MM:SS) + izvor vremena (RTC/NTP/DCF) + status sinkronizacije + WiFi status
// Redak 2: datum ili aktivnost podsustava toranjskog sata (zvona, cekici, recovery).

#include <Arduino.h>
#include <Wire.h>
#include <avr/pgmspace.h>
#include <LiquidCrystal_I2C.h>
#include <RTClib.h>
#include <string.h>
#include <stdio.h>
#include "lcd_display.h"
#include "time_glob.h"
#include "dcf_sync.h"
#include "otkucavanje.h"
#include "zvonjenje.h"
#include "unified_motion_state.h"
#include "watchdog.h"

LiquidCrystal_I2C lcd(0x27, 16, 2);

static char line1_buffer[17];
static unsigned long last_line1_refresh = 0;
static uint32_t last_line1_rtc_tick = 0xFFFFFFFFUL;
static uint32_t last_line1_dcf_visual_tick = 0xFFFFFFFFUL;

// N = normalan rad, R = korekcija, E = greska/recovery
static char status_oznaka = 'N';
static char wifi_status = ' ';

static char line2_buffer[17];
static unsigned long last_line2_refresh = 0;
static bool rtc_battery_warning_active = false;
static bool wifi_ip_prikaz_aktivan = false;
static unsigned long wifi_ip_prikaz_pocetak_ms = 0;
static char wifi_ip_poruka[17];
static const unsigned long WIFI_IP_PRIKAZ_TRAJANJE_MS = 5000UL;

static enum {
  ACTIVITY_NONE = 0,
  ACTIVITY_BELL1,
  ACTIVITY_BELL2,
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
static const char LCD_PORUKA_OTKUCAJ[] PROGMEM = "Otkucavanje...  ";
static const char LCD_PORUKA_ERR_RTC[] PROGMEM = "ERR:RTC baterija";
static const char LCD_PORUKA_ERR_EEPROM[] PROGMEM = "ERROR: EEPROM   ";
static const char LCD_PORUKA_ERR_I2C[] PROGMEM = "ERROR: I2C comm ";
static const char LCD_PORUKA_SLAVLJENJE[] PROGMEM = "SLAVLJENJE      ";
static const char LCD_PORUKA_MRTVACKO[] PROGMEM = "MRTVACKO ZVONO  ";
static const char LCD_PORUKA_BAT_RTC[] PROGMEM = "Baterija prazna";

static void kopirajTekstIzFlash(char* odrediste, size_t velicina, PGM_P izvor) {
  strncpy_P(odrediste, izvor, velicina - 1);
  odrediste[velicina - 1] = '\0';
}

static PGM_P dohvatiNazivDanaIzFlash(uint8_t danUTjednu) {
  return reinterpret_cast<PGM_P>(pgm_read_ptr(&LCD_NAZIVI_DANA[danUTjednu]));
}

static char izracunajOznakuStanjaLCD() {
  if (rtc_battery_warning_active || current_activity == ACTIVITY_ERROR || activity_is_error) {
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
  wifi_ip_poruka[0] = '\0';

  lcd.setCursor(0, 0);
  lcd.print(F("FILA 33 v.1.0"));
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
  if (jeDCFSinkronizacijaUTijeku()) {
    strncpy(source_str, jeDCFImpulsAktivan() ? "---" : "   ", sizeof(source_str) - 1);
    source_str[sizeof(source_str) - 1] = '\0';
  } else if (!jeVrijemePotvrdjenoZaAutomatiku()) {
    strncpy(source_str, "ERR", sizeof(source_str) - 1);
    source_str[sizeof(source_str) - 1] = '\0';
  } else {
    strncpy(source_str, dohvatiOznakuIzvoraVremena(), sizeof(source_str) - 1);
    source_str[sizeof(source_str) - 1] = '\0';
  }

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
  if (rtc_battery_warning_active) {
    kopirajTekstIzFlash(line2_buffer, sizeof(line2_buffer), LCD_PORUKA_BAT_RTC);
    return;
  }

  if ((current_activity == ACTIVITY_HAMMER1 || current_activity == ACTIVITY_HAMMER2) &&
      !jeOtkucavanjeUTijeku()) {
    current_activity = ACTIVITY_NONE;
    activity_timeout_ms = 0;
    memset(activity_message, ' ', 16);
    activity_message[16] = '\0';
    activity_is_error = false;
  }

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

  if (wifi_ip_prikaz_aktivan) {
    if ((millis() - wifi_ip_prikaz_pocetak_ms) >= WIFI_IP_PRIKAZ_TRAJANJE_MS) {
      wifi_ip_prikaz_aktivan = false;
      wifi_ip_poruka[0] = '\0';
    } else if (current_activity != ACTIVITY_ERROR && !activity_is_error) {
      strncpy(line2_buffer, wifi_ip_poruka, 16);
      line2_buffer[16] = '\0';
      return;
    }
  }

  if (current_activity == ACTIVITY_NONE && jeOtkucavanjeUTijeku()) {
    kopirajTekstIzFlash(line2_buffer, sizeof(line2_buffer), LCD_PORUKA_OTKUCAJ);
    return;
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
    default:
      break;
  }
}

void signalizirajHammer1_Active() {
  last_line2_refresh = 0;
}

void signalizirajHammer2_Active() {
  last_line2_refresh = 0;
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

void signalizirajUpozorenjeRtcBaterije() {
  rtc_battery_warning_active = true;
  last_line2_refresh = 0;
  status_oznaka = 'E';
}

void potvrdiUpozorenjeRtcBaterije() {
  rtc_battery_warning_active = false;
  last_line2_refresh = 0;
  last_date_minute = -1;
}

bool jeUpozorenjeRtcBaterijeAktivno() {
  return rtc_battery_warning_active;
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
  const unsigned long now_ms = millis();
  const uint32_t rtcTick = dohvatiRtcSekundniBrojac();
  const uint32_t dcfVisualTick = dohvatiDcfVizualniBrojac();
  const bool dcfPrijemAktivan = jeDCFSinkronizacijaUTijeku();

  bool trebaOsvjezitiRedak1 = false;
  if (dcfVisualTick != last_line1_dcf_visual_tick) {
    if (dcfVisualTick != last_line1_dcf_visual_tick) {
      last_line1_dcf_visual_tick = dcfVisualTick;
    }
    last_line1_refresh = now_ms;
    trebaOsvjezitiRedak1 = true;
  } else if (dcfPrijemAktivan) {
    trebaOsvjezitiRedak1 = false;
  } else if (jeRtcSqwAktivan()) {
    if (rtcTick != last_line1_rtc_tick) {
      last_line1_rtc_tick = rtcTick;
      last_line1_dcf_visual_tick = dcfVisualTick;
      last_line1_refresh = now_ms;
      trebaOsvjezitiRedak1 = true;
    }
  } else if (last_line1_refresh == 0 || (now_ms - last_line1_refresh) >= 1000UL) {
    last_line1_refresh = now_ms;
    last_line1_rtc_tick = rtcTick;
    last_line1_dcf_visual_tick = dcfVisualTick;
    trebaOsvjezitiRedak1 = true;
  }

  if (trebaOsvjezitiRedak1) {
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

void prikaziLokalnuWiFiIP(const char* ipAdresa) {
  if (ipAdresa == nullptr || ipAdresa[0] == '\0') {
    return;
  }

  strncpy(wifi_ip_poruka, ipAdresa, sizeof(wifi_ip_poruka) - 1);
  wifi_ip_poruka[sizeof(wifi_ip_poruka) - 1] = '\0';

  const int duljina = strlen(wifi_ip_poruka);
  for (int i = duljina; i < 16; ++i) {
    wifi_ip_poruka[i] = ' ';
  }
  wifi_ip_poruka[16] = '\0';

  wifi_ip_prikaz_aktivan = true;
  wifi_ip_prikaz_pocetak_ms = millis();
  last_line2_refresh = 0;
}

void primijeniLCDPozadinskoOsvjetljenje(bool ukljuci) {
  if (ukljuci) {
    lcd.backlight();
  } else {
    lcd.noBacklight();
  }
}
