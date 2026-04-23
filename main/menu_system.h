// menu_system.h - LCD izbornik toranjskog sata
#pragma once

#include <stdint.h>

typedef enum {
  MENU_STATE_DISPLAY_TIME,        // Glavni prikaz toranjskog sata
  MENU_STATE_MAIN_MENU,           // Glavni meni
  MENU_STATE_CLOCK_SETTINGS,      // Maticni sat
  MENU_STATE_HAND_SETTINGS,       // Kazaljke
  MENU_STATE_PLATE_SETTINGS,      // Kompaktne postavke okretne ploce
  MENU_STATE_NETWORK_SETTINGS,    // Mreza
  MENU_STATE_SYSTEM_SETTINGS,     // Sustav
  MENU_STATE_WIFI_IP_DISPLAY,     // Prikaz lokalne WiFi IP adrese
  MENU_STATE_QUIET_HOURS,         // Uredjivanje tihih sati
  MENU_STATE_NAIL_SETTINGS,       // Uredjivanje postavki stapica
  MENU_STATE_SUNCE_SETTINGS,      // Uredjivanje suncevih dogadaja
  MENU_STATE_HOLIDAY_SETTINGS     // Uredjivanje blagdanskog slavljenja
} MenuState;

typedef enum {
  KEY_UP,
  KEY_DOWN,
  KEY_LEFT,
  KEY_RIGHT,
  KEY_SELECT,
  KEY_BACK,
  KEY_DIGIT_0,
  KEY_DIGIT_1,
  KEY_DIGIT_2,
  KEY_DIGIT_3,
  KEY_DIGIT_4,
  KEY_DIGIT_5,
  KEY_DIGIT_6,
  KEY_DIGIT_7,
  KEY_DIGIT_8,
  KEY_DIGIT_9,
  KEY_NONE
} KeyEvent;

void inicijalizirajMenuSistem();
void upravljajMenuSistemom();
void obradiKluc(KeyEvent event);
MenuState dohvatiMenuState();
void povratakNaGlavniPrikaz();
void osvjeziLCDZaMeni();
