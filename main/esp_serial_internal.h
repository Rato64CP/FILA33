// esp_serial_internal.h - Interna podjela serijskog sloja prema ESP32 mostu
#pragma once

#include <Arduino.h>
#include <avr/pgmspace.h>
#include <RTClib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_serial.h"
#include "time_glob.h"
#include "zvonjenje.h"
#include "podesavanja_piny.h"
#include "otkucavanje.h"
#include "slavljenje_mrtvacko.h"
#include "pc_serial.h"
#include "prekidac_tisine.h"
#include "kazaljke_sata.h"
#include "okretna_ploca.h"
#include "postavke.h"
#include "lcd_display.h"
#include "sunceva_automatika.h"
#include "watchdog.h"
#include "pogrebne_skripte.h"

extern HardwareSerial& espSerijskiPort;

extern const unsigned long ESP_BRZINA;
extern const uint16_t ESP_ULAZNI_BUFFER_MAX;
extern const unsigned long WIFI_POCETNA_ODGODA_NAKON_NAPAJANJA_MS;
extern const unsigned long WIFI_STATUS_DRUGI_UPIT_ODGODA_MS;
extern const unsigned long WIFI_STATUS_RECOVERY_INTERVAL_MS;
extern const uint8_t NTP_SIGURNA_SEKUNDA_MIN;
extern const uint8_t NTP_SIGURNA_SEKUNDA_MAX;
extern const uint16_t MIN_NTP_GODINA;
extern const uint16_t MAX_NTP_GODINA;
extern const uint32_t NTP_KLJUC_NEPOSTAVLJEN;

extern char ulazniBuffer[];
extern uint16_t ulazniBufferDuljina;
extern bool ntpCekanjePrijavljeno;
extern bool wifiPovezanNaESP;
extern char zadnjaLokalnaWiFiIP[16];
extern char zadnjaWiFiMACAdresa[18];
extern bool wifiPocetnaOdgodaAktivna;
extern unsigned long wifiPocetnaOdgodaDoMs;
extern unsigned long vrijemePrvogWiFiStatusUpitaMs;
extern unsigned long zadnjiWiFiStatusRecoveryUpitMs;
extern bool drugiWiFiStatusUpitPoslan;
extern bool prioritetniNtpZahtjevNaCekanju;
extern uint32_t zadnjiAutomatskiNtpZahtjevMinutniKljuc;
extern uint32_t zadnjiAutomatskiNtpZahtjevSatniKljuc;

struct ESPStatusSnapshot {
  bool vrijemePotvrdjeno;
  bool ntpOmogucen;
  bool kazaljkeUSinkronu;
  int handPosition;
  bool plocaUSinkronu;
  int platePosition;
  bool slavljenje;
  bool mrtvacko;
  bool otkucavanje;
  bool zvono1;
  bool zvono2;
  uint8_t pogrebnaSkriptaTip;
  bool sunceJutro;
  bool suncePodne;
  bool sunceVecer;
  bool tihiMod;
};

extern bool zadnjiStatusPushInicijaliziran;
extern ESPStatusSnapshot zadnjiStatusPushSnapshot;

enum ESPCmdIshod {
  ESP_CMD_NEPOZNATA = 0,
  ESP_CMD_OK = 1,
  ESP_CMD_ZAUZETO = 2
};

void resetirajUlazniBuffer();
void trimBuffer();
void trimJednolinijskiTekstESP(char* tekst);
void logirajLinijuESP(const __FlashStringHelper* prefiks, const char* sadrzaj);

void obradiESPRedak();

void posaljiStatusESPU();
ESPStatusSnapshot dohvatiTrenutniStatusSnapshotZaESP();
bool jesuStatusSnapshotiJednaki(const ESPStatusSnapshot& a,
                                const ESPStatusSnapshot& b);
bool obradiESPStatusnuLiniju(const char* linija);

void potvrdiWiFiPovezanostAkoTreba(const __FlashStringHelper* razlog);
bool jeAktivnaPocetnaOdgodaWiFi();
void zatraziWiFiStatusESP();
bool parsirajNTPPayload(const char* payload,
                        DateTime& dt,
                        uint16_t& milisekunde,
                        bool& imaEksplicitanDST,
                        bool& dstAktivanIzvori);
bool obradiESPWiFiINtpLiniju(const char* linija);

void posaljiSustavskePostavkeESPu();
void posaljiPostavkeStapicaESPu();
void posaljiBATPostavkeESPu();
void posaljiSuncevePostavkeESPu();
void posaljiMisePostavkeESPu();
void posaljiNepomicneBlagdaneESPu();
void posaljiPomicneBlagdaneESPu();
bool spremiSustavskePostavkeIzESPa(char* payload);
bool spremiPostavkeStapicaIzESPa(char* payload);
bool spremiBATPostavkeIzESPa(char* payload);
bool spremiSuncevePostavkeIzESPa(char* payload);
bool spremiMisePostavkeIzESPa(char* payload);
bool spremiNepomicneBlagdaneIzESPa(char* payload);
bool spremiPomicneBlagdaneIzESPa(char* payload);
bool spremiSetupWiFiPostavkeIzESPa(const char* payload);
void posaljiKonfiguracijuESPuNakonZahtjeva();
bool obradiESPPostavkeLiniju(const char* linija);

ESPCmdIshod obradiESPCmdLiniju(const char* komanda);
