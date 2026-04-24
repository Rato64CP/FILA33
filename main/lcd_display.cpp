// lcd_display.cpp - Dinamicki 2-retni LCD prikaz toranjskog sata
// Redak 1: vrijeme (HH:MM:SS) + izvor vremena (RTC/NTP/MAN) + oznaka dana za cavle (R/N)
// + zvjezdica aktivnosti na zadnjem mjestu.
// Redak 2: datum ili aktivnost podsustava toranjskog sata (zvona, cekici, recovery) + WiFi oznaka na 16. znaku.

#include <Arduino.h>
#include <Wire.h>
#include <avr/pgmspace.h>
#include <LiquidCrystal_I2C.h>
#include <RTClib.h>
#include <string.h>
#include <stdio.h>
#include "lcd_display.h"
#include "time_glob.h"
#include "otkucavanje.h"
#include "postavke.h"
#include "zvonjenje.h"
#include "slavljenje_mrtvacko.h"
#include "unified_motion_state.h"
#include "watchdog.h"

LiquidCrystal_I2C lcd(0x27, 16, 2);

static char line1_buffer[17];
static char zadnje_ispisani_redak1[17];
static unsigned long last_line1_refresh = 0;
static uint32_t last_line1_rtc_tick = 0xFFFFFFFFUL;

static char wifi_status = ' ';

static char line2_buffer[17];
static char zadnje_ispisani_redak2[17];
static unsigned long last_line2_refresh = 0;
static bool lcd_pozadinsko_stanje_poznato = false;
static bool lcd_pozadinsko_stanje_ukljuceno = true;
static int last_date_minute = -1;
static bool otkucavanje_poruka_aktivna = false;
static bool hod_sata_prikaz_aktivan = false;
static bool rtc_battery_warning_active = false;
static bool wifi_ip_prikaz_aktivan = false;
static unsigned long wifi_ip_prikaz_pocetak_ms = 0;
static char wifi_ip_poruka[17];
static const unsigned long WIFI_IP_PRIKAZ_TRAJANJE_MS = 5000UL;

static enum {
  ACTIVITY_NONE = 0,
  ACTIVITY_BELL1,
  ACTIVITY_BELL2,
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
static const uint8_t LCD_NOCNI_REZIM_OD_SAT = 0;
static const uint8_t LCD_NOCNI_REZIM_DO_SAT = 5;
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
static const char LCD_PORUKA_BELL1[] PROGMEM = "Zvono 1 radi   ";
static const char LCD_PORUKA_BELL2[] PROGMEM = "Zvono 2 radi   ";
static const char LCD_PORUKA_OBA_ZVONA[] PROGMEM = "Zvone oba zvona";
static const char LCD_PORUKA_OTKUCAJ[] PROGMEM = "Otkucavanje...  ";
static const char LCD_PORUKA_ERR_RTC[] PROGMEM = "ERR:RTC baterija";
static const char LCD_PORUKA_ERR_EEPROM[] PROGMEM = "ERROR: EEPROM   ";
static const char LCD_PORUKA_ERR_I2C[] PROGMEM = "ERROR: I2C comm ";
static const char LCD_PORUKA_SLAVLJENJE[] PROGMEM = "SLAVLJENJE      ";
static const char LCD_PORUKA_MRTVACKO[] PROGMEM = "MRTVACKO ZVONO  ";
static const char LCD_PORUKA_BAT_RTC[] PROGMEM = "Baterija prazna";
static const char LCD_PORUKA_INERCIJA[] PROGMEM = "Smirivanje zvona";
static const int LCD_BROJ_MINUTA_CIKLUS = 720;
static const int LCD_MAKS_CEKANJE_KAZALJKI_MIN = 60;
static const int LCD_PRAG_DUGE_KOREKCIJE_MIN = 2;

static void kopirajTekstIzFlash(char* odrediste, size_t velicina, PGM_P izvor) {
  strncpy_P(odrediste, izvor, velicina - 1);
  odrediste[velicina - 1] = '\0';
}

static void pripremiRedakZaLCD(const char* tekst, char* odrediste) {
  memset(odrediste, ' ', 16);

  if (tekst != nullptr) {
    const size_t duljina = strlen(tekst);
    const size_t brojZnakova = (duljina < 16) ? duljina : 16;
    memcpy(odrediste, tekst, brojZnakova);
  }

  odrediste[16] = '\0';
}

static void upisiRedakNaLCD(uint8_t redak, const char* tekst, char* zadnjiRedak) {
  char pripremljeniRedak[17];
  pripremiRedakZaLCD(tekst, pripremljeniRedak);

  if (strcmp(pripremljeniRedak, zadnjiRedak) == 0) {
    return;
  }

  lcd.setCursor(0, redak);
  lcd.print(pripremljeniRedak);
  strncpy(zadnjiRedak, pripremljeniRedak, 17);
}

static void pripremiDrugiRedakSaWiFiOznakom(const char* tekst) {
  memset(line2_buffer, ' ', 16);

  if (tekst != nullptr) {
    strncpy(line2_buffer, tekst, 15);
  }

  line2_buffer[15] = wifi_status;
  line2_buffer[16] = '\0';
}

static void upisiDrugiRedakNaLCDSaWiFiOznakom(const char* tekst) {
  pripremiDrugiRedakSaWiFiOznakom(tekst);
  upisiRedakNaLCD(1, line2_buffer, zadnje_ispisani_redak2);
}

static void ocistiAktivnostDrugogRetka() {
  current_activity = ACTIVITY_NONE;
  activity_timeout_ms = 0;
  memset(activity_message, ' ', 16);
  activity_message[16] = '\0';
  activity_is_error = false;
  otkucavanje_poruka_aktivna = false;
  last_date_minute = -1;
}

static PGM_P dohvatiNazivDanaIzFlash(uint8_t danUTjednu) {
  return reinterpret_cast<PGM_P>(pgm_read_ptr(&LCD_NAZIVI_DANA[danUTjednu]));
}

static int izracunajDvanaestSatneMinuteZaLCD(const DateTime& vrijeme) {
  return (vrijeme.hour() % 12) * 60 + vrijeme.minute();
}

static int izracunajMinuteNaprijedZaLCD(int polozajKazaljki, int ciljVrijeme) {
  return (polozajKazaljki - ciljVrijeme + LCD_BROJ_MINUTA_CIKLUS) % LCD_BROJ_MINUTA_CIKLUS;
}

static bool trebajuKazaljkeSamoCekatiZaLCD(int polozajKazaljki, int ciljVrijeme) {
  const int minuteNaprijed = izracunajMinuteNaprijedZaLCD(polozajKazaljki, ciljVrijeme);
  return minuteNaprijed > 0 && minuteNaprijed <= LCD_MAKS_CEKANJE_KAZALJKI_MIN;
}

static int izracunajPreostaluKorekcijuKazaljkiZaLCD(int polozajKazaljki, int ciljVrijeme) {
  return (ciljVrijeme - polozajKazaljki + LCD_BROJ_MINUTA_CIKLUS) % LCD_BROJ_MINUTA_CIKLUS;
}

static bool trebaPrikazatiDugiHodSata(int& memoriraneMinute) {
  if (!imaKazaljkeSata() || !jeVrijemePotvrdjenoZaAutomatiku()) {
    return false;
  }

  const EepromLayout::UnifiedMotionState stanje = UnifiedMotionStateStore::dohvatiIliMigriraj();
  memoriraneMinute = stanje.hand_position;

  const int ciljVrijeme = izracunajDvanaestSatneMinuteZaLCD(dohvatiTrenutnoVrijeme());
  if (memoriraneMinute == ciljVrijeme) {
    return false;
  }

  // Ako su kazaljke malo naprijed, firmware ih namjerno ne vraca silom,
  // nego ceka da ih stvarno vrijeme sustigne.
  if (trebajuKazaljkeSamoCekatiZaLCD(memoriraneMinute, ciljVrijeme)) {
    return false;
  }

  return izracunajPreostaluKorekcijuKazaljkiZaLCD(memoriraneMinute, ciljVrijeme) >
         LCD_PRAG_DUGE_KOREKCIJE_MIN;
}

static void formatirajHodSata(int memoriraneMinute, char* odrediste, size_t velicina) {
  const int sat12 = ((memoriraneMinute / 60) % 12 == 0) ? 12 : ((memoriraneMinute / 60) % 12);
  const int minuta = memoriraneMinute % 60;
  snprintf(odrediste, velicina, "Hod sata: %02d:%02d", sat12, minuta);
}

static bool jeSustavAktivanNaLCD() {
  const EepromLayout::UnifiedMotionState stanje = UnifiedMotionStateStore::dohvatiIliMigriraj();
  return stanje.hand_active != 0 ||
         stanje.plate_phase != 0 ||
         jeZvonoUTijeku() ||
         jeOtkucavanjeUTijeku() ||
         jeSlavljenjeUTijeku() ||
         jeMrtvackoUTijeku();
}

static char dohvatiOznakuDanaZaCavle(const DateTime& vrijeme) {
  return (vrijeme.dayOfTheWeek() == 0) ? 'N' : 'R';
}

static bool jeLCDUNocnomRezimu(const DateTime& vrijeme) {
  const uint8_t sat = vrijeme.hour();
  return sat >= LCD_NOCNI_REZIM_OD_SAT && sat <= LCD_NOCNI_REZIM_DO_SAT;
}

static bool odrediStvarnoStanjeLCDPozadinskogOsvjetljenja(bool rucnoUkljuceno) {
  if (!rucnoUkljuceno) {
    return false;
  }

  return !jeLCDUNocnomRezimu(dohvatiTrenutnoVrijeme());
}

static void osvjeziAutomatskoLCDPozadinskoOsvjetljenje() {
  primijeniLCDPozadinskoOsvjetljenje(jeLCDPozadinskoOsvjetljenjeUkljuceno());
}

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
  zadnje_ispisani_redak1[0] = '\0';

  memset(line2_buffer, ' ', sizeof(line2_buffer) - 1);
  line2_buffer[16] = '\0';
  zadnje_ispisani_redak2[0] = '\0';

  memset(activity_message, ' ', sizeof(activity_message) - 1);
  activity_message[16] = '\0';
  wifi_ip_poruka[0] = '\0';

  lcd.setCursor(0, 0);
  lcd.print(F("ZVONKO v. 1.0"));
  lcd.setCursor(0, 1);
  lcd.print(F("Inicijalizacija"));

  delay(2000);
  lcd.clear();

  last_line1_refresh = 0;
  last_line2_refresh = 0;
  last_blink_toggle = 0;
  current_activity = ACTIVITY_NONE;
  otkucavanje_poruka_aktivna = false;
}

static void build_line1() {
  DateTime now = dohvatiTrenutnoVrijeme();
  char source_str[4];
  if (!jeVrijemePotvrdjenoZaAutomatiku()) {
    strncpy(source_str, "ERR", sizeof(source_str) - 1);
    source_str[sizeof(source_str) - 1] = '\0';
  } else {
    strncpy(source_str, dohvatiOznakuIzvoraVremena(), sizeof(source_str) - 1);
    source_str[sizeof(source_str) - 1] = '\0';
  }

  const char oznaka_dana = dohvatiOznakuDanaZaCavle(now);
  const char oznaka_aktivnosti = jeSustavAktivanNaLCD() ? '*' : ' ';

  snprintf(line1_buffer, sizeof(line1_buffer),
           "%02d:%02d:%02d %s %c%c",
           now.hour(), now.minute(), now.second(),
           source_str,
           oznaka_dana,
           oznaka_aktivnosti);
  line1_buffer[16] = '\0';
}

static void build_date_string() {
  DateTime now = dohvatiTrenutnoVrijeme();
  uint8_t day_of_week = now.dayOfTheWeek();
  if (day_of_week > 6) day_of_week = 0;

  char day_name[4];
  char datum_poruka[16];
  kopirajTekstIzFlash(day_name, sizeof(day_name), dohvatiNazivDanaIzFlash(day_of_week));

  snprintf(datum_poruka, sizeof(datum_poruka),
           "%s %02d.%02d.%04d",
           day_name,
           now.day(),
           now.month(),
           now.year());
  pripremiDrugiRedakSaWiFiOznakom(datum_poruka);

  last_date_minute = now.minute();
}

static void build_line2() {
  if (rtc_battery_warning_active) {
    char poruka[17];
    kopirajTekstIzFlash(poruka, sizeof(poruka), LCD_PORUKA_BAT_RTC);
    pripremiDrugiRedakSaWiFiOznakom(poruka);
    return;
  }

  const bool zvono1Aktivno = jeZvonoAktivno(1);
  const bool zvono2Aktivno = jeZvonoAktivno(2);

  // Prikaz zvona mora pratiti stvarno stanje releja toranjskog sata.
  if (current_activity == ACTIVITY_BELL1 || current_activity == ACTIVITY_BELL2 ||
      zvono1Aktivno || zvono2Aktivno) {
    char poruka[17];

    if (zvono1Aktivno && zvono2Aktivno) {
      kopirajTekstIzFlash(poruka, sizeof(poruka), LCD_PORUKA_OBA_ZVONA);
      pripremiDrugiRedakSaWiFiOznakom(poruka);
      return;
    }

    if (zvono1Aktivno) {
      kopirajTekstIzFlash(poruka, sizeof(poruka), LCD_PORUKA_BELL1);
      pripremiDrugiRedakSaWiFiOznakom(poruka);
      return;
    }

    if (zvono2Aktivno) {
      kopirajTekstIzFlash(poruka, sizeof(poruka), LCD_PORUKA_BELL2);
      pripremiDrugiRedakSaWiFiOznakom(poruka);
      return;
    }

    ocistiAktivnostDrugogRetka();
  }

  // Dinamicka poruka za slavljenje mora pratiti stvarno stanje cekica.
  if (current_activity == ACTIVITY_CELEBRATION && !jeSlavljenjeUTijeku()) {
    ocistiAktivnostDrugogRetka();
  }

  // Dinamicka poruka za mrtvacko zvono mora nestati cim se nacin rada ugasi.
  if (current_activity == ACTIVITY_FUNERAL && !jeMrtvackoUTijeku()) {
    ocistiAktivnostDrugogRetka();
  }

  if (current_activity == ACTIVITY_NONE && jeLiInerciaAktivna()) {
    char poruka[17];
    kopirajTekstIzFlash(poruka, sizeof(poruka), LCD_PORUKA_INERCIJA);
    pripremiDrugiRedakSaWiFiOznakom(poruka);
    otkucavanje_poruka_aktivna = false;
    return;
  }

  if (wifi_ip_prikaz_aktivan) {
    if ((millis() - wifi_ip_prikaz_pocetak_ms) >= WIFI_IP_PRIKAZ_TRAJANJE_MS) {
      wifi_ip_prikaz_aktivan = false;
      wifi_ip_poruka[0] = '\0';
    } else if (current_activity != ACTIVITY_ERROR && !activity_is_error) {
      pripremiDrugiRedakSaWiFiOznakom(wifi_ip_poruka);
      otkucavanje_poruka_aktivna = false;
      return;
    }
  }

  if (current_activity == ACTIVITY_NONE && jeOtkucavanjeUTijeku()) {
    char poruka[17];
    kopirajTekstIzFlash(poruka, sizeof(poruka), LCD_PORUKA_OTKUCAJ);
    pripremiDrugiRedakSaWiFiOznakom(poruka);
    otkucavanje_poruka_aktivna = true;
    hod_sata_prikaz_aktivan = false;
    return;
  }

  if (current_activity == ACTIVITY_NONE && otkucavanje_poruka_aktivna && !jeOtkucavanjeUTijeku()) {
    otkucavanje_poruka_aktivna = false;
    last_date_minute = -1;
  }

  if (current_activity == ACTIVITY_NONE) {
    int memoriraneMinute = 0;
    if (trebaPrikazatiDugiHodSata(memoriraneMinute)) {
      char poruka[17];
      formatirajHodSata(memoriraneMinute, poruka, sizeof(poruka));
      pripremiDrugiRedakSaWiFiOznakom(poruka);
      otkucavanje_poruka_aktivna = false;
      hod_sata_prikaz_aktivan = true;
      return;
    }
  }

  if (hod_sata_prikaz_aktivan) {
    hod_sata_prikaz_aktivan = false;
    last_date_minute = -1;
  }

  if (current_activity != ACTIVITY_NONE && activity_timeout_ms > 0) {
    unsigned long elapsed = millis() - activity_start_time;
    if (elapsed >= activity_timeout_ms) {
      ocistiAktivnostDrugogRetka();
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
        pripremiDrugiRedakSaWiFiOznakom(activity_message);
      } else {
        pripremiDrugiRedakSaWiFiOznakom("");
      }
    } else {
      pripremiDrugiRedakSaWiFiOznakom(activity_message);
    }
    otkucavanje_poruka_aktivna = false;
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
}

void signalizirajError_EEPROM() {
  current_activity = ACTIVITY_ERROR;
  set_activity_message(LCD_PORUKA_ERR_EEPROM, 0, true);
}

void signalizirajError_I2C() {
  current_activity = ACTIVITY_ERROR;
  set_activity_message(LCD_PORUKA_ERR_I2C, 0, true);
}

void signalizirajUpozorenjeRtcBaterije() {
  rtc_battery_warning_active = true;
  last_line2_refresh = 0;
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
  osvjeziAutomatskoLCDPozadinskoOsvjetljenje();

  const unsigned long now_ms = millis();
  const uint32_t rtcTick = dohvatiRtcSekundniBrojac();

  bool trebaOsvjezitiRedak1 = false;
  if (jeRtcSqwAktivan()) {
    if (rtcTick != last_line1_rtc_tick) {
      last_line1_rtc_tick = rtcTick;
      last_line1_refresh = now_ms;
      trebaOsvjezitiRedak1 = true;
    }
  } else if (last_line1_refresh == 0 || (now_ms - last_line1_refresh) >= 1000UL) {
    last_line1_refresh = now_ms;
    last_line1_rtc_tick = rtcTick;
    trebaOsvjezitiRedak1 = true;
  }

  if (trebaOsvjezitiRedak1) {
    build_line1();
    upisiRedakNaLCD(0, line1_buffer, zadnje_ispisani_redak1);
    osvjeziWatchdog();
  }

  if (now_ms - last_line2_refresh >= 500UL) {
    last_line2_refresh = now_ms;
    build_line2();
    upisiRedakNaLCD(1, line2_buffer, zadnje_ispisani_redak2);
    osvjeziWatchdog();
  }
}

void prikaziPoruku(const char* redak1, const char* redak2) {
  osvjeziAutomatskoLCDPozadinskoOsvjetljenje();

  if (redak1) {
    upisiRedakNaLCD(0, redak1, zadnje_ispisani_redak1);
  }

  if (redak2) {
    upisiDrugiRedakNaLCDSaWiFiOznakom(redak2);
  }
}

void postaviWiFiStatus(bool aktivan) {
  wifi_status = aktivan ? 'W' : ' ';
  // Prisili ponovno slaganje drugog retka kako bi se WiFi oznaka odmah
  // pojavila ili nestala uz datum toranjskog sata, bez cekanja nove minute.
  last_date_minute = -1;
  last_line2_refresh = 0;
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
  const bool stvarnoUkljuci = odrediStvarnoStanjeLCDPozadinskogOsvjetljenja(ukljuci);
  if (lcd_pozadinsko_stanje_poznato && lcd_pozadinsko_stanje_ukljuceno == stvarnoUkljuci) {
    return;
  }

  lcd_pozadinsko_stanje_ukljuceno = stvarnoUkljuci;
  lcd_pozadinsko_stanje_poznato = true;

  if (stvarnoUkljuci) {
    lcd.backlight();
  } else {
    lcd.noBacklight();
  }
}
