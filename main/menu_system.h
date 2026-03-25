// menu_system.h – Comprehensive 6-key LCD menu system with state management
#pragma once

#include <stdint.h>

typedef enum {
  MENU_STATE_DISPLAY_TIME,        // Main clock display
  MENU_STATE_MAIN_MENU,           // Main menu (5 options)
  MENU_STATE_SETTINGS,            // Settings submenu
  MENU_STATE_HAND_CORRECTION,     // Hand correction mode
  MENU_STATE_TIME_ADJUST,         // Manual time adjustment
  MENU_STATE_QUIET_HOURS,         // Podešavanje tihih sati satnih otkucaja
  MENU_STATE_MODE_SELECT,         // Operation mode selection
  MENU_STATE_WIFI_CONFIG,         // WiFi configuration
  MENU_STATE_INFO_DISPLAY,        // System information display
  MENU_STATE_CONFIRMATION,        // Confirmation dialog
  MENU_STATE_PASSWORD_ENTRY       // Password entry for admin functions
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

// Menu system initialization
void inicijalizirajMenuSistem();

// Main menu loop – call from loop()
void upravljajMenuSistemom();

// Process key input (called after debouncing)
void obradiKluc(KeyEvent event);

// Get current menu state
MenuState dohvatiMenuState();

// Force return to main clock display
void povratakNaGlavniPrikaz();

// Update LCD display based on current menu state
void osvjeziLCDZaMeni();

// Get current selected menu item index
int dohvatiOdabraniIndex();

// Confirm action in confirmation dialog
void potvrdiAkciju(bool da);

// Enter password mode
void ulaziUManjuLozinkom();
