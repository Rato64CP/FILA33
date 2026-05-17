// lcd_display.cpp - Dinamicki 2-retni LCD prikaz toranjskog sata
// Redak 1: vrijeme (HH:MM:SS) + izvor vremena (---/NTP/MAN) + temperatura RTC-a.
// Dvotocke trepere samo kad toranjski sat nesto radi ili kad WiFi nije spojen.
// Redak 2: datum ili aktivnost podsustava toranjskog sata (zvona, cekici, recovery),
// a po potrebi i kratki WiFi sazetak iz mreznog mosta.

#include <Arduino.h>
#include <Wire.h>
#include <avr/pgmspace.h>
#include <LiquidCrystal_I2C.h>
#include <RTClib.h>
#include <string.h>
#include <stdio.h>
#include "lcd_display.h"
#include "flash_text_utils.h"
#include "i2c_bus.h"
#include "pc_serial.h"
#include "time_glob.h"
#include "otkucavanje.h"
#include "postavke.h"
#include "zvonjenje.h"
#include "slavljenje_mrtvacko.h"
#include "unified_motion_state.h"
#include "watchdog.h"
#include "rs485_bridge.h"
#include "ups_nadzor.h"

LiquidCrystal_I2C lcd(0x27, 16, 2);

static const uint8_t LCD_I2C_ADRESA = 0x27;
static char line1_buffer[17];
static char zadnje_ispisani_redak1[17];
static unsigned long last_line1_refresh = 0;
static uint32_t last_line1_rtc_tick = 0xFFFFFFFFUL;
static bool lcd_temperatura_poznata = false;
static int8_t lcd_temperatura_cijeli_c = 0;
static unsigned long lcd_zadnje_ocitanje_temperature_ms = 0;
static const unsigned long LCD_TEMPERATURA_INTERVAL_MS = 30000UL;

static bool wifi_povezan = false;

static char line2_buffer[17];
static char zadnje_ispisani_redak2[17];
static unsigned long last_line2_refresh = 0;
static bool lcd_pozadinsko_stanje_poznato = false;
static bool lcd_pozadinsko_stanje_ukljuceno = true;
static int last_date_minute = -1;
static bool otkucavanje_poruka_aktivna = false;
static bool hod_sata_prikaz_aktivan = false;
static bool rtc_battery_warning_active = false;
static bool latched_eeprom_fault_active = false;
static bool wifi_ip_prikaz_aktivan = false;
static unsigned long wifi_ip_prikaz_pocetak_ms = 0;
static char wifi_ip_poruka[17];
static const unsigned long WIFI_IP_PRIKAZ_TRAJANJE_MS = 5000UL;
static bool lcd_i2c_greska_aktivna = false;
static bool lcd_i2c_greska_prijavljena = false;
static unsigned long lcd_zadnja_i2c_provjera_ms = 0;
static bool lcd_zadnja_i2c_dostupnost = true;
static const unsigned long LCD_I2C_PROVJERA_INTERVAL_MS = 250UL;
static uint8_t lcd_reinit_pokusaji_preostali = 0;
static unsigned long lcd_zadnji_reinit_ms = 0;
static const unsigned long LCD_REINIT_PONOVI_INTERVAL_MS = 1000UL;
static bool lcd_sqw_greska_aktivna_prethodno = false;
static bool lcd_sqw_greska_poruka_vidljiva = true;
static unsigned long lcd_sqw_greska_zadnje_treptanje_ms = 0;

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
static const uint8_t LCD_ZNAK_VELIKO_C_KVACICA = 1;
static const uint8_t LCD_GLYPH_VELIKO_C_KVACICA[8] = {
  B01010,
  B00100,
  B01110,
  B10000,
  B10000,
  B10000,
  B01110,
  B00000
};
static const char LCD_DAN_NED[] PROGMEM = "NEDJELJA";
static const char LCD_DAN_PON[] PROGMEM = "PON.";
static const char LCD_DAN_UTO[] PROGMEM = "UTORAK";
static const char LCD_DAN_SRI[] PROGMEM = "SRIJEDA";
// Za veliko slovo C s kvacicom moramo prekinuti string literal nakon \x01,
// inace bi C/C++ progutao i slovo E kao dio hex escape sekvence.
static const char LCD_DAN_CET[] PROGMEM = "\x01" "ETVRTAK";
static const char LCD_DAN_PET[] PROGMEM = "PETAK";
static const char LCD_DAN_SUB[] PROGMEM = "SUBOTA";
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
static const char LCD_PORUKA_SLAVLJENJE[] PROGMEM = "SLAVLJENJE      ";
static const char LCD_PORUKA_MRTVACKO[] PROGMEM = "MRTVA\x01KO ZVONO  ";
static const char LCD_PORUKA_BAT_RTC[] PROGMEM = "Baterija prazna";
static const char LCD_PORUKA_RTC_DEGRADED_1[] PROGMEM = "RTC OGRANICEN RAD";
static const char LCD_PORUKA_RTC_DEGRADED_2[] PROGMEM = "CEKAM OPORAVAK  ";
static const char LCD_PORUKA_SQW_GRESKA[] PROGMEM = "SQW GRESKA      ";
static const char LCD_PORUKA_LATCH_EEPROM_1[] PROGMEM = "EEPROM GRESKA   ";
static const char LCD_PORUKA_LATCH_EEPROM_2[] PROGMEM = "ENT=POTVRDI     ";
static const char LCD_PORUKA_INERCIJA[] PROGMEM = "Smirivanje zvona";
static const char LCD_PORUKA_RS485_1[] PROGMEM = "VEZA TORANJ     ";
static const char LCD_PORUKA_RS485_2[] PROGMEM = "PREKINUTA       ";
static const char LCD_PORUKA_SAFE_MODE_1[] PROGMEM = "SUSTAV ZAKLJUCAN";
static const char LCD_PORUKA_SAFE_MODE_2[] PROGMEM = "PREVISE RESETA  ";
static const char LCD_PORUKA_SAFE_MODE_3[] PROGMEM = "DRZI ENT 5 SEK ";
static const char LCD_PORUKA_UPS_NEMA_MREZE[] PROGMEM = "NEMA STRUJE!    ";
static const int LCD_BROJ_MINUTA_CIKLUS = 720;
static const int LCD_MAKS_CEKANJE_KAZALJKI_MIN = 60;
static const int LCD_PRAG_DUGE_KOREKCIJE_MIN = 2;
static const unsigned long LCD_SAFE_MODE_BLINK_INTERVAL_MS = 500UL;
static const unsigned long LCD_SAFE_MODE_UPUTA_INTERVAL_MS = 2000UL;
static const unsigned long LCD_LATCHED_FAULT_INTERVAL_MS = 1500UL;
static const unsigned long LCD_RTC_DEGRADED_INTERVAL_MS = 1500UL;
static const unsigned long LCD_RS485_PREKID_INTERVAL_MS = 1500UL;
static const unsigned long LCD_SQW_GRESKA_BLINK_INTERVAL_MS = 500UL;

static void resetirajCachePrikazaLCD() {
  zadnje_ispisani_redak1[0] = '\0';
  zadnje_ispisani_redak2[0] = '\0';
  last_line1_refresh = 0;
  last_line1_rtc_tick = 0xFFFFFFFFUL;
  last_line2_refresh = 0;
  last_date_minute = -1;
}

static bool jeWireTimeoutAktivanZaLCD() {
#if defined(WIRE_HAS_TIMEOUT)
  return Wire.getWireTimeoutFlag();
#else
  return false;
#endif
}

static void ocistiWireTimeoutZaLCD() {
#if defined(WIRE_HAS_TIMEOUT)
  Wire.clearWireTimeoutFlag();
#endif
}

static void oznaciLCDI2CGresku() {
  lcd_i2c_greska_aktivna = true;
  lcd_reinit_pokusaji_preostali = 3;
  if (!lcd_i2c_greska_prijavljena) {
    posaljiPCLog(F("LCD: I2C veza nije dostupna, cekam oporavak zaslona"));
    lcd_i2c_greska_prijavljena = true;
  }
}

static bool provjeriDostupnostLCDNaI2C(bool prisiliProvjeru = false) {
  const unsigned long sadaMs = millis();
  if (!prisiliProvjeru && lcd_zadnja_i2c_provjera_ms != 0 &&
      (sadaMs - lcd_zadnja_i2c_provjera_ms) < LCD_I2C_PROVJERA_INTERVAL_MS) {
    return lcd_zadnja_i2c_dostupnost;
  }

  lcd_zadnja_i2c_provjera_ms = sadaMs;
  pripremiI2CSabirnicuSigurno();
  Wire.beginTransmission(LCD_I2C_ADRESA);
  lcd_zadnja_i2c_dostupnost = (Wire.endTransmission() == 0);
  if (!lcd_zadnja_i2c_dostupnost || jeWireTimeoutAktivanZaLCD()) {
    ocistiWireTimeoutZaLCD();
    lcd_zadnja_i2c_dostupnost = false;
  }
  return lcd_zadnja_i2c_dostupnost;
}

static void primijeniPozadinskoStanjeNaLCDNakonOporavka() {
  if (!lcd_pozadinsko_stanje_poznato || lcd_pozadinsko_stanje_ukljuceno) {
    lcd.backlight();
  } else {
    lcd.noBacklight();
  }
}

static bool osigurajLCDSpremanZaRad() {
  if (!provjeriDostupnostLCDNaI2C()) {
    oznaciLCDI2CGresku();
    return false;
  }

  const unsigned long sadaMs = millis();
  const bool trebaPonovniReinit = lcd_reinit_pokusaji_preostali > 0 &&
                                  (lcd_zadnji_reinit_ms == 0 ||
                                   (sadaMs - lcd_zadnji_reinit_ms) >=
                                       LCD_REINIT_PONOVI_INTERVAL_MS);

  if (!lcd_i2c_greska_aktivna && !trebaPonovniReinit) {
    return true;
  }

  pripremiI2CSabirnicuSigurno();
  delay(10);
  lcd.init();
  delay(10);
  primijeniPozadinskoStanjeNaLCDNakonOporavka();
  delay(10);
  lcd.createChar(LCD_ZNAK_VELIKO_C_KVACICA, const_cast<uint8_t*>(LCD_GLYPH_VELIKO_C_KVACICA));
  lcd.clear();
  delay(10);
  lcd.display();

  if (jeWireTimeoutAktivanZaLCD()) {
    ocistiWireTimeoutZaLCD();
    oznaciLCDI2CGresku();
    return false;
  }

  resetirajCachePrikazaLCD();
  lcd_i2c_greska_aktivna = false;
  lcd_i2c_greska_prijavljena = false;
  lcd_zadnji_reinit_ms = sadaMs;
  if (lcd_reinit_pokusaji_preostali > 0) {
    --lcd_reinit_pokusaji_preostali;
  }
  posaljiPCLog(F("LCD: I2C veza oporavljena, zaslon ponovno inicijaliziran"));
  return true;
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

  if (!osigurajLCDSpremanZaRad()) {
    return;
  }

  if (strcmp(pripremljeniRedak, zadnjiRedak) == 0) {
    return;
  }

  lcd.setCursor(0, redak);
  lcd.print(pripremljeniRedak);
  if (jeWireTimeoutAktivanZaLCD()) {
    ocistiWireTimeoutZaLCD();
    oznaciLCDI2CGresku();
    return;
  }
  strncpy(zadnjiRedak, pripremljeniRedak, 17);
}

static void pripremiDrugiRedakZaLCD(const char* tekst) {
  pripremiRedakZaLCD(tekst, line2_buffer);
}

static void upisiDrugiRedakNaLCD(const char* tekst) {
  pripremiDrugiRedakZaLCD(tekst);
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
  snprintf_P(odrediste, velicina, PSTR("Hod sata: %02d:%02d"), sat12, minuta);
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

static bool trebajuDvotockeTreperitiNaLCD() {
  return jeSustavAktivanNaLCD() || !wifi_povezan;
}

static void osvjeziRtcTemperaturuZaLCD() {
  const unsigned long sadaMs = millis();
  if (lcd_zadnje_ocitanje_temperature_ms != 0 &&
      (sadaMs - lcd_zadnje_ocitanje_temperature_ms) < LCD_TEMPERATURA_INTERVAL_MS) {
    return;
  }

  float temperaturaC = 0.0f;
  lcd_zadnje_ocitanje_temperature_ms = sadaMs;
  if (!dohvatiRtcTemperaturu(temperaturaC)) {
    return;
  }

  const float zaokruzenaTemperatura =
      (temperaturaC >= 0.0f) ? (temperaturaC + 0.5f) : (temperaturaC - 0.5f);
  int temperaturaInt = static_cast<int>(zaokruzenaTemperatura);
  if (temperaturaInt > 99) {
    temperaturaInt = 99;
  } else if (temperaturaInt < -9) {
    temperaturaInt = -9;
  }

  lcd_temperatura_cijeli_c = static_cast<int8_t>(temperaturaInt);
  lcd_temperatura_poznata = true;
}

static void formatirajRtcTemperaturuZaLCD(char* odrediste) {
  if (!lcd_temperatura_poznata) {
    odrediste[0] = ' ';
    odrediste[1] = ' ';
    odrediste[2] = '\0';
    return;
  }

  snprintf_P(odrediste, 3, PSTR("%2d"), lcd_temperatura_cijeli_c);
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
  pripremiI2CSabirnicuSigurno();
  delay(50);

  lcd.init();
  delay(50);

  lcd.backlight();
  delay(50);

  // Za toranjski sat koristimo samo jedan korisnicki znak kako bi CETVRTAK
  // na LCD-u mogao imati pravo veliko slovo C s kvacicom.
  lcd.createChar(LCD_ZNAK_VELIKO_C_KVACICA, const_cast<uint8_t*>(LCD_GLYPH_VELIKO_C_KVACICA));

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
  lcd.clear();

  resetirajCachePrikazaLCD();
  last_blink_toggle = 0;
  current_activity = ACTIVITY_NONE;
  otkucavanje_poruka_aktivna = false;
  lcd_i2c_greska_aktivna = false;
  lcd_i2c_greska_prijavljena = false;
  lcd_zadnja_i2c_provjera_ms = 0;
  lcd_zadnja_i2c_dostupnost = true;
  lcd_reinit_pokusaji_preostali = 0;
  lcd_zadnji_reinit_ms = 0;
  lcd_temperatura_poznata = false;
  lcd_temperatura_cijeli_c = 0;
  lcd_zadnje_ocitanje_temperature_ms = 0;
  ocistiWireTimeoutZaLCD();
}

static void build_line1() {
  DateTime now = dohvatiTrenutnoVrijeme();
  char source_str[4];
  char temperatura_str[3];
  osvjeziRtcTemperaturuZaLCD();
  formatirajRtcTemperaturuZaLCD(temperatura_str);

  if (!jeVrijemePotvrdjenoZaAutomatiku()) {
    strncpy(source_str, "ERR", sizeof(source_str) - 1);
    source_str[sizeof(source_str) - 1] = '\0';
    snprintf_P(line1_buffer, sizeof(line1_buffer),
               PSTR("--:--:--  %s %s"),
               source_str,
               temperatura_str);
  } else {
    strncpy(source_str, dohvatiOznakuIzvoraVremena(), sizeof(source_str) - 1);
    source_str[sizeof(source_str) - 1] = '\0';
    if (strncmp(source_str, "RTC", sizeof(source_str)) == 0) {
      strncpy(source_str, "---", sizeof(source_str) - 1);
      source_str[sizeof(source_str) - 1] = '\0';
    }

    const bool dvotockeVidljive =
        trebajuDvotockeTreperitiNaLCD() ? jeRtcSqwPrvaPolovicaSekunde() : true;
    const char separator_sati_minute = dvotockeVidljive ? ':' : ' ';
    const char separator_minute_sekunde = dvotockeVidljive ? ':' : ' ';

    snprintf_P(line1_buffer, sizeof(line1_buffer),
               PSTR("%02d%c%02d%c%02d  %s %s"),
               now.hour(), separator_sati_minute, now.minute(), separator_minute_sekunde, now.second(),
               source_str,
               temperatura_str);
  }
  line1_buffer[16] = '\0';
}

static void build_date_string() {
  DateTime now = dohvatiTrenutnoVrijeme();
  uint8_t day_of_week = now.dayOfTheWeek();
  if (day_of_week > 6) day_of_week = 0;

  char day_name[13];
  char datum_poruka[17];
  FlashTekst::kopirajLiteral(day_name, sizeof(day_name), dohvatiNazivDanaIzFlash(day_of_week));

  if (day_of_week == 1) {
    snprintf_P(datum_poruka, sizeof(datum_poruka),
               PSTR("%s %02d.%02d.%04d"),
               day_name,
               now.day(),
               now.month(),
               now.year());
  } else {
    snprintf_P(datum_poruka, sizeof(datum_poruka),
               PSTR("%s %02d.%02d."),
               day_name,
               now.day(),
               now.month());
  }
  pripremiDrugiRedakZaLCD(datum_poruka);

  last_date_minute = now.minute();
}

static void build_line2() {
  if (latched_eeprom_fault_active) {
    static bool prikaziPotvrdu = false;
    static unsigned long zadnjaIzmjenaPotvrdeMs = 0;
    char poruka[17];
    const unsigned long sadaMs = millis();

    if (zadnjaIzmjenaPotvrdeMs == 0) {
      zadnjaIzmjenaPotvrdeMs = sadaMs;
    } else if ((sadaMs - zadnjaIzmjenaPotvrdeMs) >= LCD_LATCHED_FAULT_INTERVAL_MS) {
      zadnjaIzmjenaPotvrdeMs = sadaMs;
      prikaziPotvrdu = !prikaziPotvrdu;
    }

    FlashTekst::kopirajLiteral(
        poruka,
        sizeof(poruka),
        prikaziPotvrdu ? LCD_PORUKA_LATCH_EEPROM_2 : LCD_PORUKA_LATCH_EEPROM_1);
    pripremiDrugiRedakZaLCD(poruka);
    return;
  }

  if (jeRtcDegradiraniNacinAktivan()) {
    static bool prikaziDruguRtcPoruku = false;
    static unsigned long zadnjaRtcPorukaMs = 0;
    char poruka[17];
    const unsigned long sadaMs = millis();

    if (zadnjaRtcPorukaMs == 0) {
      zadnjaRtcPorukaMs = sadaMs;
    } else if ((sadaMs - zadnjaRtcPorukaMs) >= LCD_RTC_DEGRADED_INTERVAL_MS) {
      zadnjaRtcPorukaMs = sadaMs;
      prikaziDruguRtcPoruku = !prikaziDruguRtcPoruku;
    }

    FlashTekst::kopirajLiteral(
        poruka,
        sizeof(poruka),
        prikaziDruguRtcPoruku ? LCD_PORUKA_RTC_DEGRADED_2 : LCD_PORUKA_RTC_DEGRADED_1);
    pripremiDrugiRedakZaLCD(poruka);
    return;
  }

  if (rtc_battery_warning_active) {
    char poruka[17];
    FlashTekst::kopirajLiteral(poruka, sizeof(poruka), LCD_PORUKA_BAT_RTC);
    pripremiDrugiRedakZaLCD(poruka);
    return;
  }

  if (jeRS485VezaPrekinuta()) {
    static bool prikaziDruguRs485Poruku = false;
    static unsigned long zadnjaRs485PorukaMs = 0;
    char poruka[17];
    const unsigned long sadaMs = millis();

    if (zadnjaRs485PorukaMs == 0) {
      zadnjaRs485PorukaMs = sadaMs;
    } else if ((sadaMs - zadnjaRs485PorukaMs) >= LCD_RS485_PREKID_INTERVAL_MS) {
      zadnjaRs485PorukaMs = sadaMs;
      prikaziDruguRs485Poruku = !prikaziDruguRs485Poruku;
    }

    FlashTekst::kopirajLiteral(
        poruka,
        sizeof(poruka),
        prikaziDruguRs485Poruku ? LCD_PORUKA_RS485_2 : LCD_PORUKA_RS485_1);
    pripremiDrugiRedakZaLCD(poruka);
    return;
  }

  if (jeUPSModAktivan()) {
    char poruka[17];
    if (current_activity != ACTIVITY_ERROR) {
      ocistiAktivnostDrugogRetka();
    }
    otkucavanje_poruka_aktivna = false;
    hod_sata_prikaz_aktivan = false;
    FlashTekst::kopirajLiteral(poruka, sizeof(poruka), LCD_PORUKA_UPS_NEMA_MREZE);
      pripremiDrugiRedakZaLCD(poruka);
    return;
  }

  if (jeRtcSqwGreskaAktivna()) {
    const unsigned long sadaMs = millis();

    if (!lcd_sqw_greska_aktivna_prethodno) {
      lcd_sqw_greska_aktivna_prethodno = true;
      lcd_sqw_greska_poruka_vidljiva = true;
      lcd_sqw_greska_zadnje_treptanje_ms = sadaMs;
    } else if ((sadaMs - lcd_sqw_greska_zadnje_treptanje_ms) >=
               LCD_SQW_GRESKA_BLINK_INTERVAL_MS) {
      lcd_sqw_greska_zadnje_treptanje_ms = sadaMs;
      lcd_sqw_greska_poruka_vidljiva = !lcd_sqw_greska_poruka_vidljiva;
    }

    if (lcd_sqw_greska_poruka_vidljiva) {
      char poruka[17];
      FlashTekst::kopirajLiteral(poruka, sizeof(poruka), LCD_PORUKA_SQW_GRESKA);
      pripremiDrugiRedakZaLCD(poruka);
    } else {
      pripremiDrugiRedakZaLCD("");
    }
    return;
  }

  lcd_sqw_greska_aktivna_prethodno = false;
  lcd_sqw_greska_poruka_vidljiva = true;
  lcd_sqw_greska_zadnje_treptanje_ms = 0;

  const bool zvono1Aktivno = jeZvonoAktivno(1);
  const bool zvono2Aktivno = jeZvonoAktivno(2);

  // Prikaz zvona mora pratiti stvarno stanje releja toranjskog sata.
  if (current_activity == ACTIVITY_BELL1 || current_activity == ACTIVITY_BELL2 ||
      zvono1Aktivno || zvono2Aktivno) {
    char poruka[17];

    if (zvono1Aktivno && zvono2Aktivno) {
      FlashTekst::kopirajLiteral(poruka, sizeof(poruka), LCD_PORUKA_OBA_ZVONA);
      pripremiDrugiRedakZaLCD(poruka);
      return;
    }

    if (zvono1Aktivno) {
      FlashTekst::kopirajLiteral(poruka, sizeof(poruka), LCD_PORUKA_BELL1);
      pripremiDrugiRedakZaLCD(poruka);
      return;
    }

    if (zvono2Aktivno) {
      FlashTekst::kopirajLiteral(poruka, sizeof(poruka), LCD_PORUKA_BELL2);
      pripremiDrugiRedakZaLCD(poruka);
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
    FlashTekst::kopirajLiteral(poruka, sizeof(poruka), LCD_PORUKA_INERCIJA);
    pripremiDrugiRedakZaLCD(poruka);
    otkucavanje_poruka_aktivna = false;
    return;
  }

  if (wifi_ip_prikaz_aktivan) {
    if ((millis() - wifi_ip_prikaz_pocetak_ms) >= WIFI_IP_PRIKAZ_TRAJANJE_MS) {
      wifi_ip_prikaz_aktivan = false;
      wifi_ip_poruka[0] = '\0';
    } else if (current_activity != ACTIVITY_ERROR && !activity_is_error) {
      pripremiDrugiRedakZaLCD(wifi_ip_poruka);
      otkucavanje_poruka_aktivna = false;
      return;
    }
  }

  if (current_activity == ACTIVITY_NONE && jeOtkucavanjeUTijeku()) {
    char poruka[17];
    FlashTekst::kopirajLiteral(poruka, sizeof(poruka), LCD_PORUKA_OTKUCAJ);
    pripremiDrugiRedakZaLCD(poruka);
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
      pripremiDrugiRedakZaLCD(poruka);
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
        pripremiDrugiRedakZaLCD(activity_message);
      } else {
        pripremiDrugiRedakZaLCD("");
      }
    } else {
      pripremiDrugiRedakZaLCD(activity_message);
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
  FlashTekst::kopirajLiteral(activity_message, sizeof(activity_message), message);

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

static void oznaciDrugiRedakZaOsvjezavanje(bool prisiliDatum) {
  last_line2_refresh = 0;
  if (prisiliDatum) {
    last_date_minute = -1;
  }
}

static void postaviTrajnuAktivnostDrugogRetka(uint8_t aktivnost, PGM_P poruka) {
  current_activity = aktivnost;
  set_activity_message(poruka, 0, false);
}

void signalizirajError_RTC() {
  current_activity = ACTIVITY_ERROR;
  set_activity_message(LCD_PORUKA_ERR_RTC, 0, true);
}

void signalizirajError_EEPROM() {
  current_activity = ACTIVITY_ERROR;
  set_activity_message(LCD_PORUKA_ERR_EEPROM, 0, true);
}

void signalizirajUpozorenjeRtcBaterije() {
  rtc_battery_warning_active = true;
  oznaciDrugiRedakZaOsvjezavanje(false);
}

void potvrdiUpozorenjeRtcBaterije() {
  rtc_battery_warning_active = false;
  oznaciDrugiRedakZaOsvjezavanje(true);
}

bool jeUpozorenjeRtcBaterijeAktivno() {
  return rtc_battery_warning_active;
}

void signalizirajLatchedFaultEEPROM() {
  latched_eeprom_fault_active = true;
  oznaciDrugiRedakZaOsvjezavanje(false);
}

void potvrdiLatchedFaultEEPROM() {
  latched_eeprom_fault_active = false;
  ocistiAktivnostDrugogRetka();
  oznaciDrugiRedakZaOsvjezavanje(true);
}

void signalizirajCelebration_Mode() {
  postaviTrajnuAktivnostDrugogRetka(ACTIVITY_CELEBRATION, LCD_PORUKA_SLAVLJENJE);
}

void signalizirajFuneral_Mode() {
  postaviTrajnuAktivnostDrugogRetka(ACTIVITY_FUNERAL, LCD_PORUKA_MRTVACKO);
}

void prikaziSat() {
  osvjeziAutomatskoLCDPozadinskoOsvjetljenje();

  const unsigned long now_ms = millis();
  const uint32_t rtcTick = dohvatiRtcSekundniBrojac();
  static bool zadnjeStanjeDvotocakaVidljivo = true;
  static bool zadnjeTreptanjeDvotocakaAktivno = false;

  bool trebaOsvjezitiRedak1 = false;
  const bool vrijemePotvrdjeno = jeVrijemePotvrdjenoZaAutomatiku();
  const bool treptanjeDvotocakaAktivno =
      vrijemePotvrdjeno ? trebajuDvotockeTreperitiNaLCD() : false;
  const bool dvotockeVidljive =
      (vrijemePotvrdjeno && treptanjeDvotocakaAktivno) ? jeRtcSqwPrvaPolovicaSekunde() : true;
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

  if (vrijemePotvrdjeno &&
      treptanjeDvotocakaAktivno &&
      dvotockeVidljive != zadnjeStanjeDvotocakaVidljivo) {
    trebaOsvjezitiRedak1 = true;
  }

  if (treptanjeDvotocakaAktivno != zadnjeTreptanjeDvotocakaAktivno) {
    trebaOsvjezitiRedak1 = true;
  }

  if (trebaOsvjezitiRedak1) {
    zadnjeStanjeDvotocakaVidljivo = dvotockeVidljive;
    zadnjeTreptanjeDvotocakaAktivno = treptanjeDvotocakaAktivno;
    last_line1_refresh = now_ms;
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

void prisiliOsvjezavanjeGlavnogPrikazaLCD() {
  // Povratak iz izbornika mora odmah presloziti oba retka glavnog prikaza,
  // bez cekanja sljedece minute ili redovnog perioda osvjezavanja.
  last_line1_refresh = 0;
  last_line1_rtc_tick = 0xFFFFFFFFUL;
  last_line2_refresh = 0;
  last_date_minute = -1;
}

void prikaziPoruku(const char* redak1, const char* redak2) {
  osvjeziAutomatskoLCDPozadinskoOsvjetljenje();

  if (redak1) {
    upisiRedakNaLCD(0, redak1, zadnje_ispisani_redak1);
  }

  if (redak2) {
    upisiDrugiRedakNaLCD(redak2);
  }
}

void prikaziPoruku(const __FlashStringHelper* redak1,
                   const __FlashStringHelper* redak2) {
  char redak1Buffer[17] = "";
  char redak2Buffer[17] = "";

  if (redak1 != nullptr) {
    FlashTekst::kopirajLiteral(
        redak1Buffer,
        sizeof(redak1Buffer),
        reinterpret_cast<PGM_P>(redak1));
  }
  if (redak2 != nullptr) {
    FlashTekst::kopirajLiteral(
        redak2Buffer,
        sizeof(redak2Buffer),
        reinterpret_cast<PGM_P>(redak2));
  }

  prikaziPoruku(redak1 != nullptr ? redak1Buffer : nullptr,
                redak2 != nullptr ? redak2Buffer : "");
}

void prikaziZakljucaniSustav() {
  lcd_pozadinsko_stanje_poznato = true;
  lcd_pozadinsko_stanje_ukljuceno = true;
  primijeniLCDPozadinskoOsvjetljenje(true);

  static bool porukaVidljiva = true;
  static unsigned long zadnjeTreptanjeMs = 0;
  static unsigned long zadnjaIzmjenaUputeMs = 0;
  static bool prikaziUputuOtkljucavanja = false;

  const unsigned long sadaMs = millis();
  if (zadnjeTreptanjeMs == 0) {
    zadnjeTreptanjeMs = sadaMs;
  } else if ((sadaMs - zadnjeTreptanjeMs) >= LCD_SAFE_MODE_BLINK_INTERVAL_MS) {
    zadnjeTreptanjeMs = sadaMs;
    porukaVidljiva = !porukaVidljiva;
  }

  if (zadnjaIzmjenaUputeMs == 0) {
    zadnjaIzmjenaUputeMs = sadaMs;
  } else if ((sadaMs - zadnjaIzmjenaUputeMs) >= LCD_SAFE_MODE_UPUTA_INTERVAL_MS) {
    zadnjaIzmjenaUputeMs = sadaMs;
    prikaziUputuOtkljucavanja = !prikaziUputuOtkljucavanja;
  }

  if (porukaVidljiva) {
    char redak1[17];
    char redak2[17];
    FlashTekst::kopirajLiteral(redak1, sizeof(redak1), LCD_PORUKA_SAFE_MODE_1);
    FlashTekst::kopirajLiteral(
        redak2,
        sizeof(redak2),
        prikaziUputuOtkljucavanja ? LCD_PORUKA_SAFE_MODE_3 : LCD_PORUKA_SAFE_MODE_2);
    upisiRedakNaLCD(0, redak1, zadnje_ispisani_redak1);
    upisiRedakNaLCD(1, redak2, zadnje_ispisani_redak2);
  } else {
    upisiRedakNaLCD(0, "", zadnje_ispisani_redak1);
    upisiRedakNaLCD(1, "", zadnje_ispisani_redak2);
  }
}

void postaviWiFiStatus(bool aktivan) {
  wifi_povezan = aktivan;
  // Promjena WiFi stanja utjece na treperenje dvotocaka na prvom retku,
  // pa prikaz mora reagirati odmah bez cekanja sljedece sekunde.
  last_line1_refresh = 0;
  last_line1_rtc_tick = 0xFFFFFFFFUL;
}

void prikaziWiFiDijagnostiku(const char* tekst) {
  if (tekst == nullptr || tekst[0] == '\0') {
    return;
  }

  strncpy(wifi_ip_poruka, tekst, sizeof(wifi_ip_poruka) - 1);
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

  if (!osigurajLCDSpremanZaRad()) {
    return;
  }

  if (stvarnoUkljuci) {
    lcd.backlight();
  } else {
    lcd.noBacklight();
  }

  if (jeWireTimeoutAktivanZaLCD()) {
    ocistiWireTimeoutZaLCD();
    oznaciLCDI2CGresku();
  }
}
