



























// Standard Arduino headers
#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <RTClib.h>
#include <avr/wdt.h>
#include <EEPROM.h>
#include <string.h>
#include <stdio.h>
#include "watchdog.h"

// ==================== PIN DEFINITIONS ====================

// Relay control pins for hand movement (K-minuta tracking)
#define PIN_RELEJ_PARNE_KAZALJKE 22    // Even relay (first phase)
#define PIN_RELEJ_NEPARNE_KAZALJKE 23  // Odd relay (second phase)

// Relay control pins for rotating plate
#define PIN_RELEJ_PARNE_PLOCE 24       // Even relay for plate (first phase)
#define PIN_RELEJ_NEPARNE_PLOCE 25     // Odd relay for plate (second phase)

// Bell and hammer control pins
#define PIN_ZVONO_1 26                  // Bell 1 - hourly strikes
#define PIN_ZVONO_2 27                  // Bell 2 - half-hourly
#define PIN_CEKIC_MUSKI 28              // Hammer 1 - male (hourly)
#define PIN_CEKIC_ZENSKI 29             // Hammer 2 - female (half-hour)

// Rotating plate mechanical inputs
#define PIN_ULAZA_PLOCE_1 30            // Mechanical cam 1
#define PIN_ULAZA_PLOCE_2 31            // Mechanical cam 2
#define PIN_ULAZA_PLOCE_3 32            // Mechanical cam 3
#define PIN_ULAZA_PLOCE_4 33            // Mechanical cam 4
#define PIN_ULAZA_PLOCE_5 34            // Mechanical cam 5

// DCF77 receiver signal
#define PIN_DCF_SIGNAL 35               // DCF77 digital signal

// I2C bus (RTC DS3231 and external EEPROM 24C32)
#define PIN_SDA 20                      // I2C SDA
#define PIN_SCL 21                      // I2C SCL

// Keypad pins (6-key menu navigation)
#define PIN_KEY_UP 36
#define PIN_KEY_DOWN 37
#define PIN_KEY_LEFT 38
#define PIN_KEY_RIGHT 39
#define PIN_KEY_SELECT 40
#define PIN_KEY_BACK 41

// ==================== LCD CONFIGURATION ====================

// LCD I2C address (adjust if different)
#define LCD_ADDRESS 0x27
#define LCD_COLS 16
#define LCD_ROWS 2

// Create global LCD object
LiquidCrystal_I2C lcd(LCD_ADDRESS, LCD_COLS, LCD_ROWS);

// ==================== RTC CONFIGURATION ====================

// DS3231 RTC (I2C address 0x68 - standard)
RTC_DS3231 rtc;

// ==================== EXTERNAL EEPROM CONFIGURATION ====================

// 24C32 external EEPROM (I2C address 0x57 - standard on RTC modules)
#define EEPROM_ADDRESS 0x57
#define EEPROM_PAGE_SIZE 32
#define EEPROM_TOTAL_SIZE 4096
#define EEPROM_PAGE_WRITE_DELAY 5      // milliseconds

// ==================== WEAR-LEVELING EEPROM LAYOUT ====================

namespace EepromLayout {
  // K-minuta storage (hand position 0-719)
  constexpr int BAZA_KAZALJKE = 0;
  constexpr int SLOTOVI_KAZALJKE = 6;
  constexpr int SLOT_SIZE_KAZALJKE = 4;  // sizeof(int)
  
  // Rotating plate position storage
  constexpr int BAZA_POZICIJA_PLOCE = BAZA_KAZALJKE + (SLOTOVI_KAZALJKE * SLOT_SIZE_KAZALJKE);
  constexpr int SLOTOVI_POZICIJA_PLOCE = 6;
  constexpr int SLOT_SIZE_POZICIJA_PLOCE = 4;  // sizeof(int)
  
  // Offset minutes for plate
  constexpr int BAZA_OFFSET_MINUTA = BAZA_POZICIJA_PLOCE + (SLOTOVI_POZICIJA_PLOCE * SLOT_SIZE_POZICIJA_PLOCE);
  constexpr int SLOTOVI_OFFSET_MINUTA = 4;
  constexpr int SLOT_SIZE_OFFSET_MINUTA = 4;  // sizeof(int)
  
  // Time source tracking
  constexpr int BAZA_IZVOR_VREMENA = BAZA_OFFSET_MINUTA + (SLOTOVI_OFFSET_MINUTA * SLOT_SIZE_OFFSET_MINUTA);
  constexpr int SLOTOVI_IZVOR_VREMENA = 6;
  constexpr int SLOT_SIZE_IZVOR_VREMENA = 4;  // sizeof(char[4])
}

// ==================== GLOBAL STATE VARIABLES ====================

// K-minuta tracking (0-719 representing 12-hour cycle in minutes)
static int K_minuta = 0;

// Impulse control state machine
static unsigned long vrijemePocetkaImpulsa = 0;
static bool impulsUTijeku = false;
static bool drugaFaza = false;
static int zadnjaAktiviranaMinuta = -1;

// Synchronization state
static bool kazaljkeUSinkronu = true;

// Correction mode state
static bool korekcija_u_tijeku = false;
static int trenutnaBrojImpulsa = 0;
static unsigned long vremePosljednjegImpulsa = 0;

// Rotating plate state
static int pozicijaPloce = 0;
static int offsetMinuta = 14;

// Time tracking
static DateTime trenutnoVrijeme;
static DateTime zadnjaSinkronizacija((uint32_t)0);
static String trenutniIzvor = "RTC";

// Bell state
static int trenutnoStanjeZvona = 0;  // 0=off, 1=bell1, 2=bell2
static unsigned long vrijemeStartaZvona = 0;

// Menu state
static bool meniJeAktivan = false;
static unsigned long zadnjaMeluDjetelnost = 0;

// Communication buffers
static String espUlazniBuffer = "";

// ==================== EXTERNAL EEPROM FUNCTIONS ====================

// Write to external 24C32 EEPROM via I2C
bool zapisiExternalEeprom(int adresa, const void* izvor, size_t duljina) {
  if (adresa < 0 || (adresa + duljina) > EEPROM_TOTAL_SIZE) {
    return false;
  }
  
  const uint8_t* bajtovi = (const uint8_t*)izvor;
  size_t preostalo = duljina;
  
  while (preostalo > 0) {
    // Calculate which page we're writing to and space left in page
    size_t offsetNaStrani = adresa % EEPROM_PAGE_SIZE;
    size_t prostorNaStrani = EEPROM_PAGE_SIZE - offsetNaStrani;
    size_t blok = (preostalo < prostorNaStrani) ? preostalo : prostorNaStrani;
    
    // Write page
    Wire.beginTransmission(EEPROM_ADDRESS);
    Wire.write((uint8_t)((adresa >> 8) & 0xFF));
    Wire.write((uint8_t)(adresa & 0xFF));
    for (size_t i = 0; i < blok; i++) {
      Wire.write(bajtovi[i]);
    }
    if (Wire.endTransmission() != 0) {
      return false;
    }
    
    delay(EEPROM_PAGE_WRITE_DELAY);
    
    bajtovi += blok;
    adresa += blok;
    preostalo -= blok;
  }
  
  return true;
}

// Read from external 24C32 EEPROM via I2C
bool procitajExternalEeprom(int adresa, void* odrediste, size_t duljina) {
  if (adresa < 0 || (adresa + duljina) > EEPROM_TOTAL_SIZE) {
    return false;
  }
  
  uint8_t* cilj = (uint8_t*)odrediste;
  size_t preostalo = duljina;
  
  while (preostalo > 0) {
    size_t blok = (preostalo < EEPROM_PAGE_SIZE) ? preostalo : EEPROM_PAGE_SIZE;
    
    Wire.beginTransmission(EEPROM_ADDRESS);
    Wire.write((uint8_t)((adresa >> 8) & 0xFF));
    Wire.write((uint8_t)(adresa & 0xFF));
    if (Wire.endTransmission(false) != 0) {
      return false;
    }
    
    size_t procitano = Wire.requestFrom(EEPROM_ADDRESS, (int)blok);
    if (procitano != blok) {
      return false;
    }
    
    for (size_t i = 0; i < blok; i++) {
      if (Wire.available()) {
        cilj[i] = Wire.read();
      } else {
        return false;
      }
    }
    
    cilj += blok;
    adresa += blok;
    preostalo -= blok;
  }
  
  return true;
}

// ==================== WEAR-LEVELING FUNCTIONS ====================

// Load value from EEPROM with wear-leveling (round-robin slot selection)
template <typename T>
bool ucitajSaWearLevelingom(int baznaAdresa, int brojSlotova, T& cilj) {
  if (brojSlotova <= 0) return false;
  
  // Try slots in reverse order (newest first)
  for (int slot = brojSlotova - 1; slot >= 0; --slot) {
    int adresa = baznaAdresa + (slot * sizeof(T));
    if (procitajExternalEeprom(adresa, &cilj, sizeof(T))) {
      return true;
    }
  }
  
  return false;
}

// Save value to EEPROM with wear-leveling (round-robin slot rotation)
static uint8_t globalSlotCounter = 0;
template <typename T>
bool spremiSaWearLevelingom(int baznaAdresa, int brojSlotova, const T& izvor) {
  if (brojSlotova <= 0) return false;
  
  int slot = (globalSlotCounter++) % brojSlotova;
  int adresa = baznaAdresa + (slot * sizeof(T));
  return zapisiExternalEeprom(adresa, &izvor, sizeof(T));
}

// ==================== CORE K-MINUTA MANAGEMENT ====================

// Calculate 12-hour minute position from RTC time
static int izracunajDvanaestSatneMinute(const DateTime& vrijeme) {
  int sati = vrijeme.hour() % 12;
  return sati * 60 + vrijeme.minute();
}

// Calculate difference between RTC position and K-minuta
// Positive = RTC ahead (need forward movement)
// Negative = RTC behind (need backward movement)
static int izracunajRazliku(const DateTime& rtcVrijeme) {
  int rtcMinuta = izracunajDvanaestSatneMinute(rtcVrijeme);
  int razlika = rtcMinuta - K_minuta;
  
  // Normalize to -360...+360 range within 12-hour cycle
  if (razlika < -360) razlika += 720;
  if (razlika > 360) razlika -= 720;
  
  return razlika;
}

// Check if hands are synchronized (within ±1 minute)
static bool jeSinkronizirana(int razlika) {
  return (razlika >= -1 && razlika <= 1);
}

// Determine relay selection based on K-minuta parity
// Even K-minuta → odd relay (1), Odd K-minuta → even relay (0)
static int odaberiRelej() {
  if (K_minuta % 2 == 0) {
    return 1;  // Odd relay for even K-minuta
  } else {
    return 0;  // Even relay for odd K-minuta
  }
}

// Load K-minuta from external EEPROM on startup
static void ucitajKminutu() {
  if (!ucitajSaWearLevelingom(EepromLayout::BAZA_KAZALJKE, EepromLayout::SLOTOVI_KAZALJKE, K_minuta)) {
    K_minuta = 0;
    Serial.println(F("K-minuta initialized to 0"));
  } else {
    if (K_minuta < 0) K_minuta = 0;
    K_minuta %= 720;
    
    Serial.print(F("K-minuta loaded: "));
    Serial.println(K_minuta);
  }
}

// Save K-minuta to external EEPROM immediately after relay activation
static void spremiKminutu() {
  spremiSaWearLevelingom(EepromLayout::BAZA_KAZALJKE, EepromLayout::SLOTOVI_KAZALJKE, K_minuta);
}

// ==================== IMPULSE CONTROL FUNCTIONS ====================

// Activate first phase of impulse (3 seconds)
static void pokreniPrvuFazu() {
  int relej = odaberiRelej();
  
  if (relej == 0) {
    // Even relay
    digitalWrite(PIN_RELEJ_PARNE_KAZALJKE, HIGH);
    digitalWrite(PIN_RELEJ_NEPARNE_KAZALJKE, LOW);
  } else {
    // Odd relay
    digitalWrite(PIN_RELEJ_PARNE_KAZALJKE, LOW);
    digitalWrite(PIN_RELEJ_NEPARNE_KAZALJKE, HIGH);
  }
  
  vrijemePocetkaImpulsa = millis();
  impulsUTijeku = true;
  drugaFaza = false;
  
  Serial.print(F("Impulse phase 1: K_minuta="));
  Serial.print(K_minuta);
  Serial.print(F(" relay="));
  Serial.println(relej == 0 ? "EVEN" : "ODD");
}

// Complete one impulse cycle (6 seconds total) and increment K-minuta
static void zavrsiImpuls() {
  // Deactivate both relays
  digitalWrite(PIN_RELEJ_PARNE_KAZALJKE, LOW);
  digitalWrite(PIN_RELEJ_NEPARNE_KAZALJKE, LOW);
  impulsUTijeku = false;
  drugaFaza = false;
  
  // Increment K-minuta and save immediately
  K_minuta = (K_minuta + 1) % 720;
  spremiKminutu();
  
  Serial.print(F("Impulse complete: new K_minuta="));
  Serial.println(K_minuta);
}

// ==================== INITIALIZATION FUNCTIONS ====================

// Initialize LCD display
void inicijalizirajLCD() {
  Wire.begin();
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.display();
  
  lcd.setCursor(0, 0);
  lcd.print("Toranj Sat v1.0");
  lcd.setCursor(0, 1);
  lcd.print("Inicijalizacija");
  
  delay(2000);
}

// Initialize RTC module
void inicijalizirajRTC() {
  if (!rtc.begin()) {
    Serial.println(F("RTC: DS3231 not available!"));
    lcd.clear();
    lcd.print("RTC Error!");
    while (1);
  }
  
  if (rtc.lostPower()) {
    Serial.println(F("RTC: Battery lost, time invalid"));
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  } else {
    Serial.println(F("RTC: Battery OK, time valid"));
  }
  
  trenutnoVrijeme = rtc.now();
}

// Initialize relay control pins
void inicijalizirajReleje() {
  // Hand movement relays
  pinMode(PIN_RELEJ_PARNE_KAZALJKE, OUTPUT);
  pinMode(PIN_RELEJ_NEPARNE_KAZALJKE, OUTPUT);
  digitalWrite(PIN_RELEJ_PARNE_KAZALJKE, LOW);
  digitalWrite(PIN_RELEJ_NEPARNE_KAZALJKE, LOW);
  
  // Plate relays
  pinMode(PIN_RELEJ_PARNE_PLOCE, OUTPUT);
  pinMode(PIN_RELEJ_NEPARNE_PLOCE, OUTPUT);
  digitalWrite(PIN_RELEJ_PARNE_PLOCE, LOW);
  digitalWrite(PIN_RELEJ_NEPARNE_PLOCE, LOW);
  
  Serial.println(F("Relays initialized"));
}

// Initialize bell and hammer pins
void inicijalizirajZvona() {
  pinMode(PIN_ZVONO_1, OUTPUT);
  pinMode(PIN_ZVONO_2, OUTPUT);
  pinMode(PIN_CEKIC_MUSKI, OUTPUT);
  pinMode(PIN_CEKIC_ZENSKI, OUTPUT);
  
  digitalWrite(PIN_ZVONO_1, LOW);
  digitalWrite(PIN_ZVONO_2, LOW);
  digitalWrite(PIN_CEKIC_MUSKI, LOW);
  digitalWrite(PIN_CEKIC_ZENSKI, LOW);
  
  Serial.println(F("Bells initialized"));
}

// Initialize plate mechanical inputs
void inicijalizirajPlocu() {
  pinMode(PIN_ULAZA_PLOCE_1, INPUT_PULLUP);
  pinMode(PIN_ULAZA_PLOCE_2, INPUT_PULLUP);
  pinMode(PIN_ULAZA_PLOCE_3, INPUT_PULLUP);
  pinMode(PIN_ULAZA_PLOCE_4, INPUT_PULLUP);
  pinMode(PIN_ULAZA_PLOCE_5, INPUT_PULLUP);
  
  // Load plate position from EEPROM
  if (!ucitajSaWearLevelingom(EepromLayout::BAZA_POZICIJA_PLOCE, EepromLayout::SLOTOVI_POZICIJA_PLOCE, pozicijaPloce)) {
    pozicijaPloce = 0;
  }
  if (pozicijaPloce < 0 || pozicijaPloce > 63) {
    pozicijaPloce = 0;
  }
  
  // Load offset minutes
  if (!ucitajSaWearLevelingom(EepromLayout::BAZA_OFFSET_MINUTA, EepromLayout::SLOTOVI_OFFSET_MINUTA, offsetMinuta)) {
    offsetMinuta = 14;
  }
  if (offsetMinuta < 0 || offsetMinuta > 14) {
    offsetMinuta = 14;
  }
  
  Serial.print(F("Plate initialized: position="));
  Serial.print(pozicijaPloce);
  Serial.print(F(" offset="));
  Serial.println(offsetMinuta);
}

// Initialize keypad
void inicijalizirajTipke() {
  pinMode(PIN_KEY_UP, INPUT_PULLUP);
  pinMode(PIN_KEY_DOWN, INPUT_PULLUP);
  pinMode(PIN_KEY_LEFT, INPUT_PULLUP);
  pinMode(PIN_KEY_RIGHT, INPUT_PULLUP);
  pinMode(PIN_KEY_SELECT, INPUT_PULLUP);
  pinMode(PIN_KEY_BACK, INPUT_PULLUP);
  
  Serial.println(F("Keypad initialized"));
}

// Initialize watchdog timer (8 second timeout)
void inicijalizirajWatchdog() {
  wdt_disable();
  MCUSR = 0;
  wdt_enable(WDTO_8S);
  
  Serial.println(F("Watchdog initialized (8s timeout)"));
}

// Initialize serial communication
void inicijalizirajSerije() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {
    delay(10);
  }
  
  Serial.println(F("============================================="));
  Serial.println(F("Tower Clock v1.0 - RTC+NTP+DCF Synchronization"));
  Serial.println(F("============================================="));
}

// ==================== BOOT SYNCHRONIZATION ====================

// Main boot synchronization routine
void pokreniBootSinkronizaciju() {
  Serial.println(F("\nBOOT SYNCHRONIZATION INITIATED"));
  
  DateTime sada = rtc.now();
  int rtcMinuta = izracunajDvanaestSatneMinute(sada);
  int razlika = rtcMinuta - K_minuta;
  
  if (razlika < -360) razlika += 720;
  if (razlika > 360) razlika -= 720;
  
  Serial.print(F("RTC: "));
  Serial.print(sada.hour());
  Serial.print(F(":"));
  Serial.print(sada.minute());
  Serial.print(F(" = "));
  Serial.print(rtcMinuta);
  Serial.print(F(" minutes"));
  
  Serial.print(F(" | K-minuta: "));
  Serial.println(K_minuta);
  
  Serial.print(F("Difference: "));
  Serial.println(razlika);
  
  if (razlika == 0) {
    Serial.println(F("SYNCHRONIZED: No correction needed"));
    kazaljkeUSinkronu = true;
    zadnjaAktiviranaMinuta = sada.minute();
    return;
  }
  
  // Determine mode based on difference magnitude
  bool agresivni = (abs(razlika) >= 10);
  
  Serial.print(F("Mode: "));
  Serial.println(agresivni ? "AGGRESSIVE (>=10min)" : "NORMAL (<10min)");
  
  // Correction loop
  korekcija_u_tijeku = true;
  trenutnaBrojImpulsa = 0;
  vremePosljednjegImpulsa = 0;
  
  while (korekcija_u_tijeku) {
    // CRITICAL: Re-read RTC and recalculate after each impulse
    DateTime sadaRTC = rtc.now();
    int razlikaUTrenutku = izracunajRazliku(sadaRTC);
    
    // Check if synchronized
    if (jeSinkronizirana(razlikaUTrenutku)) {
      Serial.print(F("SYNCHRONIZED after "));
      Serial.print(trenutnaBrojImpulsa);
      Serial.print(F(" impulses, final K_minuta="));
      Serial.println(K_minuta);
      korekcija_u_tijeku = false;
      kazaljkeUSinkronu = true;
      zadnjaAktiviranaMinuta = sadaRTC.minute();
      break;
    }
    
    // Send impulse
    pokreniPrvuFazu();
    
    // Execute full 6-second impulse cycle with LCD pause
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Boot Sync");
    lcd.setCursor(0, 1);
    lcd.print("Impulse #");
    lcd.print(++trenutnaBrojImpulsa);
    
    // First phase (3 seconds)
    delay(3000);
    osvjeziWatchdog();
    
    // Transition to second phase
    int relej = odaberiRelej();
    if (relej == 0) {
      digitalWrite(PIN_RELEJ_PARNE_KAZALJKE, LOW);
      digitalWrite(PIN_RELEJ_NEPARNE_KAZALJKE, HIGH);
    } else {
      digitalWrite(PIN_RELEJ_PARNE_KAZALJKE, HIGH);
      digitalWrite(PIN_RELEJ_NEPARNE_KAZALJKE, LOW);
    }
    
    delay(200);
    osvjeziWatchdog();
    
    // Second phase (3 seconds)
    delay(3000);
    osvjeziWatchdog();
    
    // Complete impulse
    zavrsiImpuls();
    
    // For aggressive mode, continue immediately without waiting for minute boundary
    // For normal mode, allow some processing time
    if (!agresivni) {
      delay(1000);
      osvjeziWatchdog();
    }
  }
  
  digitalWrite(PIN_RELEJ_PARNE_KAZALJKE, LOW);
  digitalWrite(PIN_RELEJ_NEPARNE_KAZALJKE, LOW);
}

// ==================== MAIN LOOP FUNCTIONS ====================

// Update current time from RTC
void updateTime() {
  static unsigned long lastUpdate = 0;
  unsigned long now = millis();
  
  if (now - lastUpdate >= 100 || lastUpdate == 0) {
    lastUpdate = now;
    trenutnoVrijeme = rtc.now();
  }
}

// Normal operation - send one impulse per minute at minute boundary
void upravljajNormalnoOtkucavanje() {
  if (korekcija_u_tijeku || !kazaljkeUSinkronu) {
    return;
  }
  
  int trenutnaMinuta = trenutnoVrijeme.minute();
  
  // Start impulse at new minute
  if (!impulsUTijeku && trenutnaMinuta != zadnjaAktiviranaMinuta) {
    pokreniPrvuFazu();
    zadnjaAktiviranaMinuta = trenutnaMinuta;
  }
  
  // Manage impulse phase transitions
  if (!impulsUTijeku) return;
  
  unsigned long sadaMs = millis();
  unsigned long proteklo = sadaMs - vrijemePocetkaImpulsa;
  
  // Transition to second phase after 3 seconds
  if (!drugaFaza && proteklo >= 3000UL) {
    int relej = odaberiRelej();
    if (relej == 0) {
      digitalWrite(PIN_RELEJ_PARNE_KAZALJKE, LOW);
      digitalWrite(PIN_RELEJ_NEPARNE_KAZALJKE, HIGH);
    } else {
      digitalWrite(PIN_RELEJ_PARNE_KAZALJKE, HIGH);
      digitalWrite(PIN_RELEJ_NEPARNE_KAZALJKE, LOW);
    }
    vrijemePocetkaImpulsa = millis();
    drugaFaza = true;
  }
  // Complete impulse cycle after second phase duration
  else if (drugaFaza && proteklo >= 3000UL) {
    zavrsiImpuls();
  }
}

// Update LCD display with current time
void osvjeziLCDPrikaz() {
  static unsigned long lastRefresh = 0;
  unsigned long now = millis();
  
  if (now - lastRefresh < 500) {
    return;
  }
  lastRefresh = now;
  
  if (meniJeAktivan) {
    return;  // Menu will handle LCD
  }
  
  lcd.setCursor(0, 0);
  lcd.print("                ");  // Clear line
  lcd.setCursor(0, 0);
  
  char timeStr[20];
  snprintf(timeStr, sizeof(timeStr), "%02d:%02d:%02d %s",
    trenutnoVrijeme.hour(), trenutnoVrijeme.minute(), trenutnoVrijeme.second(),
    trenutniIzvor.c_str());
  lcd.print(timeStr);
  
  lcd.setCursor(0, 1);
  lcd.print("                ");  // Clear line
  lcd.setCursor(0, 1);
  
  char dateStr[20];
  snprintf(dateStr, sizeof(dateStr), "%02d.%02d.%04d Kaz:%d",
    trenutnoVrijeme.day(), trenutnoVrijeme.month(), trenutnoVrijeme.year(),
    K_minuta);
  lcd.print(dateStr);
}

// Process keypad input
void provjeriTipke() {
  static unsigned long lastCheck = 0;
  unsigned long now = millis();
  
  if (now - lastCheck < 50) {
    return;
  }
  lastCheck = now;
  
  // Menu toggle on SELECT key
  if (digitalRead(PIN_KEY_SELECT) == LOW) {
    delay(50);  // Debounce
    if (digitalRead(PIN_KEY_SELECT) == LOW) {
      meniJeAktivan = !meniJeAktivan;
      
      if (meniJeAktivan) {
        Serial.println(F("Menu ON"));
        lcd.clear();
        lcd.print("Menu Mode");
      } else {
        Serial.println(F("Menu OFF"));
      }
      
      while (digitalRead(PIN_KEY_SELECT) == LOW) {
        delay(10);
      }
      delay(50);
    }
  }
  
  // Other key handling would go here for menu navigation
}

// ==================== ESP8266 SERIAL COMMUNICATION ====================

void obradiESPSerijsku() {
  while (Serial1.available()) {
    char c = Serial1.read();
    
    if (c == '\n') {
      espUlazniBuffer.trim();
      
      if (espUlazniBuffer.length() > 0) {
        Serial.print(F("ESP: "));
        Serial.println(espUlazniBuffer);
        
        // Process NTP update
        if (espUlazniBuffer.startsWith("NTP:")) {
          String iso = espUlazniBuffer.substring(4);
          Serial.print(F("NTP time received: "));
          Serial.println(iso);
          
          // Parse ISO format and update RTC
          // Format: YYYY-MM-DDTHH:MM:SSZ
          int year = iso.substring(0, 4).toInt();
          int month = iso.substring(5, 7).toInt();
          int day = iso.substring(8, 10).toInt();
          int hour = iso.substring(11, 13).toInt();
          int minute = iso.substring(14, 16).toInt();
          int second = iso.substring(17, 19).toInt();
          
          if (year >= 2024) {
            DateTime ntpTime(year, month, day, hour, minute, second);
            rtc.adjust(ntpTime);
            trenutnoVrijeme = ntpTime;
            trenutniIzvor = "NTP";
            
            Serial.print(F("RTC updated from NTP: "));
            Serial.println(iso);
            
            // Check if hands need correction after NTP sync
            int novaRazlika = izracunajRazliku(ntpTime);
            if (!jeSinkronizirana(novaRazlika)) {
              Serial.println(F("NTP sync: Hand correction needed"));
              korekcija_u_tijeku = true;
              trenutnaBrojImpulsa = 0;
              vremePosljednjegImpulsa = 0;
            }
          }
        }
      }
      
      espUlazniBuffer = "";
    } else if (c != '\r') {
      espUlazniBuffer += c;
    }
  }
}

// ==================== MAIN SETUP ====================

void setup() {
  // Initialize serial for PC communication (Serial/USB)
  inicijalizirajSerije();
  
  // Initialize I2C bus
  Wire.begin();
  delay(100);
  
  // Initialize LCD
  inicijalizirajLCD();
  
  // Initialize RTC
  inicijalizirajRTC();
  
  // Initialize external EEPROM I2C communication
  Serial.println(F("External EEPROM check..."));
  
  // Initialize relay pins
  inicijalizirajReleje();
  
  // Initialize bells and hammers
  inicijalizirajZvona();
  
  // Initialize plate
  inicijalizirajPlocu();
  
  // Initialize keypad
  inicijalizirajTipke();
  
  // Initialize watchdog
  inicijalizirajWatchdog();
  
  // Load K-minuta from EEPROM
  ucitajKminutu();
  
  // Display boot information
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Boot sync...");
  lcd.setCursor(0, 1);
  lcd.print("K:");
  lcd.print(K_minuta);
  
  // Perform boot synchronization
  pokreniBootSinkronizaciju();
  
  // Display ready message
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Ready!");
  lcd.setCursor(0, 1);
  lcd.print("K:");
  lcd.print(K_minuta);
  
  Serial.println(F("\nBOOT COMPLETE - Ready for normal operation"));
  
  delay(2000);
  lcd.clear();
  
  osvjeziWatchdog();
}

// ==================== MAIN LOOP ====================

void loop() {
  osvjeziWatchdog();
  
  // Update time from RTC
  updateTime();
  
  // Process ESP8266 serial communication
  obradiESPSerijsku();
  
  // Process keypad input
  provjeriTipke();
  
  // Update LCD display
  osvjeziLCDPrikaz();
  
  // Manage hand impulses
  upravljajNormalnoOtkucavanje();
  
  osvjeziWatchdog();
  
  delay(10);  // Small delay to prevent busy-waiting
}

/* END CODE */
