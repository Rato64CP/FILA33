// menu_system.h - LCD izbornik toranjskog sata
#pragma once

#include <stdint.h>

typedef enum {
  MENU_STATE_DISPLAY_TIME,        // Glavni prikaz toranjskog sata
  MENU_STATE_MAIN_MENU,           // Glavni meni
  MENU_STATE_SETTINGS,            // Postavke
  MENU_STATE_CLOCK_SETTINGS,      // Maticni sat
  MENU_STATE_DCF_CONFIG,          // DCF postavke
  MENU_STATE_NTP_CONFIG,          // NTP postavke
  MENU_STATE_HAND_SETTINGS,       // Kazaljke
  MENU_STATE_PLATE_SETTINGS,      // Okretna ploca
  MENU_STATE_PLATE_ADJUST,        // Namjestanje okretne ploce
  MENU_STATE_NETWORK_SETTINGS,    // Mreza
  MENU_STATE_SYSTEM_SETTINGS,     // Sustav
  MENU_STATE_HAND_CORRECTION,     // Rucno namjestanje kazaljki
  MENU_STATE_TIME_ADJUST,         // Rucno namjestanje maticnog sata
  MENU_STATE_WIFI_IP_DISPLAY,     // Prikaz lokalne WiFi IP adrese
  MENU_STATE_INFO_DISPLAY,        // Informacije o toranjskom satu
  MENU_STATE_CONFIRMATION,        // Potvrda akcije
  MENU_STATE_QUIET_HOURS,         // Uredjivanje tihih sati
  MENU_STATE_NAIL_SETTINGS,       // Uredjivanje trajanja cavala
  MENU_STATE_SUNCE_SETTINGS,      // Uredjivanje suncevih dogadaja
  MENU_STATE_MODE_SELECT          // Odabir moda otkucavanja
} MenuState;

typedef enum {
  KEY_UP,
  KEY_DOWN,
  KEY_LEFT,
  KEY_RIGHT,
  KEY_SELECT,
  KEY_BACK,
  KEY_NONE
} KeyEvent;

void inicijalizirajMenuSistem();
void upravljajMenuSistemom();
void obradiKluc(KeyEvent event);
MenuState dohvatiMenuState();
void povratakNaGlavniPrikaz();
void osvjeziLCDZaMeni();
