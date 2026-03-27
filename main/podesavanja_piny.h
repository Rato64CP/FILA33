// podesavanja_piny.h – CONSOLIDATED PIN DEFINITIONS WITH HEADER GUARDS
// SINGLE SOURCE OF TRUTH for all hardware pin assignments
// Arduino Mega 2560 pinout for tower clock system

#ifndef PODESAVANJA_PINY_H
#define PODESAVANJA_PINY_H

// ==================== RELAY CONTROL PINS ====================
// Impulse relay pins for hand position control (K-minuta tracking)
// 6-second pulses via ULN2803 optocoupler drivers to 5V relays

#define PIN_RELEJ_PARNE_KAZALJKE      22  // Even relay (first phase)
#define PIN_RELEJ_NEPARNE_KAZALJKE    23  // Odd relay (second phase)

// Impulse relay pins for rotating plate control (position tracking)
#define PIN_RELEJ_PARNE_PLOCE         24  // Even relay for plate (first phase)
#define PIN_RELEJ_NEPARNE_PLOCE       25  // Odd relay for plate (second phase)

// ==================== BELL AND HAMMER PINS ====================
// Bell control for hourly (Bell 1) and half-hourly (Bell 2) strikes

#define PIN_ZVONO_1                   26  // Bell 1 - hourly strikes
#define PIN_ZVONO_2                   27  // Bell 2 - half-hourly strikes

// Hammer pins - blocked during bell movement with inertia delays
#define PIN_CEKIC_MUSKI               28  // Hammer 1 - male (hourly)
#define PIN_CEKIC_ZENSKI              29  // Hammer 2 - female (half-hourly)

// ==================== ROTATING PLATE MECHANICAL INPUTS ====================
// 5 mechanical cam sensors for plate position triggering
// Used to detect position and trigger bells/celebrations

#define PIN_ULAZA_PLOCE_1             30  // Input 1 - bell 1 (weekdays)
#define PIN_ULAZA_PLOCE_2             31  // Input 2 - bell 2 (weekdays)
#define PIN_ULAZA_PLOCE_3             32  // Input 3 - bell 1 (Sundays)
#define PIN_ULAZA_PLOCE_4             33  // Input 4 - bell 2 (Sundays)
#define PIN_ULAZA_PLOCE_5             34  // Input 5 - celebration trigger

// ==================== TIME SYNCHRONIZATION INPUTS ====================
// DCF77 receiver - external synchronization source

#define PIN_DCF_SIGNAL                35  // DCF77 digital signal (LOW = impulse)
#define PIN_RTC_SQW                   2   // DS3231 SQW 1 Hz takt za precizno okidanje

// ==================== I2C BUS ====================
// I2C communication for RTC (DS3231) and external EEPROM (24C32)
// Arduino Mega I2C: SDA=20, SCL=21 (fixed pins)

#define PIN_SDA                       20  // I2C SDA - serial data
#define PIN_SCL                       21  // I2C SCL - serial clock

// ==================== KEYPAD CONTROL ====================
// 6-key navigation keypad for LCD menu system

#define PIN_KEY_UP                    36  // UP navigation
#define PIN_KEY_DOWN                  37  // DOWN navigation
#define PIN_KEY_LEFT                  38  // LEFT navigation
#define PIN_KEY_RIGHT                 39  // RIGHT navigation
#define PIN_KEY_SELECT                40  // SELECT confirmation
#define PIN_KEY_BACK                  41  // BACK/MENU exit

// ==================== CELEBRATION AND FUNERAL BUTTONS ====================
// New buttons for celebration and funeral modes with mutual exclusion

#define PIN_KEY_CELEBRATION           43  // Celebration button toggle (PIN 43)
#define PIN_KEY_FUNERAL               42  // Funeral button toggle (PIN 42)

// ==================== MANUAL BELL TOGGLE INPUTS ====================
// Fizičke sklopke na GND za ručno upravljanje zvonima toranjskog sata

#define PIN_BELL1_SWITCH              44  // Ručna sklopka za BELL 1 (LOW=ON)
#define PIN_BELL2_SWITCH              45  // Ručna sklopka za BELL 2 (LOW=ON)

// ==================== SERIAL COMMUNICATION ====================
// Arduino Mega provides 4 hardware serial ports (Serial, Serial1-3)

// Serial0 (USB):  115200 baud - PC debugging/logging
// Serial3:        9600 baud   - ESP8266 MQTT gateway (Rx3=pin15, Tx3=pin14)

// All PIN assignments consolidated in this single header file
// No duplicate definitions allowed in other source files

#endif  // PODESAVANJA_PINY_H
