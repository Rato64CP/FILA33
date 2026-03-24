// main.ino – Glavni orkestrator sustava toranjskog sata
#include <Arduino.h>

#include "lcd_display.h"
#include "i2c_eeprom.h"
#include "time_glob.h"
#include "pc_serial.h"
#include "postavke.h"
#include "tipke.h"
#include "esp_serial.h"
#include "debouncing.h"
#include "zvonjenje.h"
#include "otkucavanje.h"
#include "menu_system.h"
#include "kazaljke_sata.h"
#include "okretna_ploca.h"
#include "dcf_sync.h"
#include "watchdog.h"
#include "mqtt_handler.h"
#include "power_recovery.h"

void setup() {
  inicijalizirajLCD();
  inicijalizirajPCSerijsku();

  VanjskiEEPROM::inicijaliziraj();
  inicijalizirajRTC();
  ucitajPostavke();

  inicijalizirajTipke();
  inicijalizirajESP();
  inicijalizirajMQTT();
  inicijalizirajMenuSistem();

  inicijalizirajZvona();
  inicijalizirajOtkucavanje();
  inicijalizirajKazaljke();
  inicijalizirajPlocu();
  inicijalizirajDCF();

  inicijalizirajPowerRecovery();
  odradiBootRecovery();
  inicijalizirajWatchdog();
}

void loop() {
  osvjeziWatchdog();

  obradiESPSerijskuKomunikaciju();
  upravljajMenuSistemom();
  provjeriTipke();

  upravljajZvonom();
  upravljajOtkucavanjem();
  upravljajKazaljkama();
  upravljajPlocom();

  upravljajMQTT();
  osvjeziDCFSinkronizaciju();
  spremiKriticalnoStanje();

  osvjeziWatchdog();
}
