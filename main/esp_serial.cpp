// esp_serial.cpp - Jezgra serijskog sloja prema ESP32 mreznom mostu
#include "esp_serial_internal.h"

// UART prema mreznom mostu toranjskog sata bira se kroz main/podesavanja_piny.h.
HardwareSerial& espSerijskiPort = ESP_SERIJSKI_PORT;

const unsigned long ESP_BRZINA = 9600;
// Nakon razdvajanja `MISE`, `BLAGDANI_NEP` i `BLAGDANI_POM` najveci serijski
// paketi vise ne trebaju 512 B. Time vracamo dio SRAM-a na `Megi`, a i dalje
// ostavljamo dovoljnu rezervu za najduzi pojedinacni web paket.
const uint16_t ESP_ULAZNI_BUFFER_MAX = 256;
const unsigned long WIFI_POCETNA_ODGODA_NAKON_NAPAJANJA_MS = 120000UL;
const unsigned long WIFI_STATUS_DRUGI_UPIT_ODGODA_MS = 15000UL;
const unsigned long WIFI_STATUS_RECOVERY_INTERVAL_MS = 300000UL;
const uint8_t NTP_SIGURNA_SEKUNDA_MIN = 12;
const uint8_t NTP_SIGURNA_SEKUNDA_MAX = 50;
const uint16_t MIN_NTP_GODINA = 2000;
const uint16_t MAX_NTP_GODINA = 2099;
const uint32_t NTP_KLJUC_NEPOSTAVLJEN = 0xFFFFFFFFUL;

// RTC + NTP tok toranjskog sata mora ostati uskladen s time_glob.cpp:
// - boot krece iz RTC-a
// - NTP ide tek kad je mreza spremna i mehanika miruje
// - nakon povratka napajanja modem/WiFi dobivaju pocetnu odgodu
// - automatski NTP ne smije remetiti osnovni rad sata ni prikaz izvora vremena

char ulazniBuffer[ESP_ULAZNI_BUFFER_MAX + 1];
uint16_t ulazniBufferDuljina = 0;
bool ntpCekanjePrijavljeno = false;
bool wifiPovezanNaESP = false;
char zadnjaLokalnaWiFiIP[16] = "";
char zadnjaWiFiMACAdresa[18] = "";
bool wifiPocetnaOdgodaAktivna = false;
unsigned long wifiPocetnaOdgodaDoMs = 0;
unsigned long vrijemePrvogWiFiStatusUpitaMs = 0;
unsigned long zadnjiWiFiStatusRecoveryUpitMs = 0;
bool drugiWiFiStatusUpitPoslan = false;
bool prioritetniNtpZahtjevNaCekanju = false;
uint32_t zadnjiAutomatskiNtpZahtjevMinutniKljuc = NTP_KLJUC_NEPOSTAVLJEN;
uint32_t zadnjiAutomatskiNtpZahtjevSatniKljuc = NTP_KLJUC_NEPOSTAVLJEN;
bool zadnjiStatusPushInicijaliziran = false;
ESPStatusSnapshot zadnjiStatusPushSnapshot = {};

namespace {

bool jeResetNakonGubitkaNapajanja() {
  const uint8_t mcusr = dohvatiResetFlags();
  const bool imaBrownOutIliPowerOn = (mcusr & ((1 << BORF) | (1 << PORF))) != 0;
  const bool imaVanjskiReset = (mcusr & (1 << EXTRF)) != 0;
  return imaBrownOutIliPowerOn && !imaVanjskiReset;
}

}  // namespace

void resetirajUlazniBuffer() {
  ulazniBuffer[0] = '\0';
  ulazniBufferDuljina = 0;
}

void trimBuffer() {
  while (ulazniBufferDuljina > 0 &&
         (ulazniBuffer[ulazniBufferDuljina - 1] == ' ' ||
          ulazniBuffer[ulazniBufferDuljina - 1] == '\t')) {
    ulazniBuffer[--ulazniBufferDuljina] = '\0';
  }

  size_t pocetak = 0;
  while (pocetak < ulazniBufferDuljina &&
         (ulazniBuffer[pocetak] == ' ' || ulazniBuffer[pocetak] == '\t')) {
    ++pocetak;
  }

  if (pocetak > 0) {
    memmove(ulazniBuffer, ulazniBuffer + pocetak, ulazniBufferDuljina - pocetak + 1);
    ulazniBufferDuljina -= pocetak;
  }
}

void trimJednolinijskiTekstESP(char* tekst) {
  if (tekst == nullptr) {
    return;
  }

  size_t duljina = strlen(tekst);
  while (duljina > 0 &&
         (tekst[duljina - 1] == ' ' || tekst[duljina - 1] == '\t' ||
          tekst[duljina - 1] == '\r' || tekst[duljina - 1] == '\n')) {
    tekst[--duljina] = '\0';
  }

  size_t pocetak = 0;
  while (tekst[pocetak] == ' ' || tekst[pocetak] == '\t') {
    ++pocetak;
  }

  if (pocetak > 0) {
    memmove(tekst, tekst + pocetak, duljina - pocetak + 1);
  }
}

void logirajLinijuESP(const __FlashStringHelper* prefiks, const char* sadrzaj) {
  char log[128];
  strncpy_P(log, reinterpret_cast<PGM_P>(prefiks), sizeof(log) - 1);
  log[sizeof(log) - 1] = '\0';
  strncat(log, sadrzaj, sizeof(log) - strlen(log) - 1);
  posaljiPCLog(log);
}

void inicijalizirajESP() {
  espSerijskiPort.begin(ESP_BRZINA);
  resetirajUlazniBuffer();
  wifiPovezanNaESP = false;
  zadnjaLokalnaWiFiIP[0] = '\0';
  zadnjaWiFiMACAdresa[0] = '\0';
  wifiPocetnaOdgodaAktivna = false;
  wifiPocetnaOdgodaDoMs = 0;
  vrijemePrvogWiFiStatusUpitaMs = 0;
  zadnjiWiFiStatusRecoveryUpitMs = 0;
  drugiWiFiStatusUpitPoslan = false;
  prioritetniNtpZahtjevNaCekanju = false;
  zadnjiAutomatskiNtpZahtjevMinutniKljuc = NTP_KLJUC_NEPOSTAVLJEN;
  zadnjiAutomatskiNtpZahtjevSatniKljuc = NTP_KLJUC_NEPOSTAVLJEN;
  zadnjiStatusPushInicijaliziran = false;
  postaviWiFiStatus(false);
  posaljiPCLog(F("Serijska veza prema mreznom mostu inicijalizirana"));
  // Kratka pauza je samo za stabilizaciju serijske veze tijekom boot-a.
  // Ovdje jos nismo usli u glavnu loop petlju niti su mehanike aktivirane,
  // pa ovih 50 ms ne remeti vremenski kriticne dijelove toranjskog sata.
  delay(50);
  posaljiWifiPostavkeESP();
  posaljiWiFiStatusESP();
  posaljiNTPPostavkeESP();

  if (jeWiFiOmogucen() && jeResetNakonGubitkaNapajanja()) {
    wifiPocetnaOdgodaAktivna = true;
    wifiPocetnaOdgodaDoMs = millis() + WIFI_POCETNA_ODGODA_NAKON_NAPAJANJA_MS;
    posaljiPCLog(F("WiFi/NTP: povratak napajanja detektiran, cekam 120 s prije prve provjere mreze"));
    return;
  }

  zatraziWiFiStatusESP();
}

void posaljiESPKomandu(const char* komanda) {
  espSerijskiPort.println(komanda);
}

bool jeWiFiPovezanNaESP() {
  return wifiPovezanNaESP;
}

const char* dohvatiESPWiFiLokalnuIP() {
  return zadnjaLokalnaWiFiIP;
}

const char* dohvatiESPWiFiMACAdresu() {
  return zadnjaWiFiMACAdresa;
}

void obradiESPSerijskuKomunikaciju() {
  while (espSerijskiPort.available()) {
    const char znak = static_cast<char>(espSerijskiPort.read());

    if (znak == '\r') {
      continue;
    }

    if (znak == '\n') {
      obradiESPRedak();
      continue;
    }

    if (ulazniBufferDuljina < ESP_ULAZNI_BUFFER_MAX) {
      ulazniBuffer[ulazniBufferDuljina++] = znak;
      ulazniBuffer[ulazniBufferDuljina] = '\0';
    } else {
      posaljiPCLog(F("Mrezni most RX: preduga linija, odbacujem buffer"));
      resetirajUlazniBuffer();
    }
  }

  if (wifiPovezanNaESP) {
    vrijemePrvogWiFiStatusUpitaMs = 0;
    zadnjiWiFiStatusRecoveryUpitMs = 0;
    drugiWiFiStatusUpitPoslan = false;
    return;
  }

  if (!jeWiFiOmogucen()) {
    return;
  }

  if (jeAktivnaPocetnaOdgodaWiFi()) {
    return;
  }

  const unsigned long sadaMs = millis();
  if (vrijemePrvogWiFiStatusUpitaMs == 0) {
    zatraziWiFiStatusESP();
    vrijemePrvogWiFiStatusUpitaMs = sadaMs;
    zadnjiWiFiStatusRecoveryUpitMs = sadaMs;
    return;
  }

  if (!drugiWiFiStatusUpitPoslan &&
      (sadaMs - vrijemePrvogWiFiStatusUpitaMs) >= WIFI_STATUS_DRUGI_UPIT_ODGODA_MS) {
    zatraziWiFiStatusESP();
    drugiWiFiStatusUpitPoslan = true;
    zadnjiWiFiStatusRecoveryUpitMs = sadaMs;
    return;
  }

  if ((sadaMs - zadnjiWiFiStatusRecoveryUpitMs) >= WIFI_STATUS_RECOVERY_INTERVAL_MS) {
    zatraziWiFiStatusESP();
    zadnjiWiFiStatusRecoveryUpitMs = sadaMs;
  }
}
