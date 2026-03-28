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
#include "mrtvacko_thumbwheel.h"
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
  primijeniLCDPozadinskoOsvjetljenje(jeLCDPozadinskoOsvjetljenjeUkljuceno());

  inicijalizirajTipke();
  inicijalizirajESP();
  if (jeMQTTOmogucen()) {
    inicijalizirajMQTT();
  } else {
    posaljiPCLog(F("MQTT: onemogućen u postavkama, inicijalizacija preskočena"));
  }
  inicijalizirajMenuSistem();

  inicijalizirajZvona();
  inicijalizirajOtkucavanje();
  inicijalizirajMrtvackoThumbwheel();
  inicijalizirajKazaljke();
  inicijalizirajPlocu();
  if (jeDCFOmogucen()) {
    inicijalizirajDCF();
  } else {
    posaljiPCLog(F("DCF77: onemogucen u postavkama, inicijalizacija preskocena"));
  }

  inicijalizirajWatchdog();
  oznaciWatchdogReset(jeWatchdogResetDetektiran());
  oznaciGubitakNapajanja(jePowerLossResetDetektiran());
  inicijalizirajPowerRecovery();
  odradiBootRecovery();
}

void loop() {
  osvjeziWatchdog();

  obradiESPSerijskuKomunikaciju();
  upravljajMenuSistemom();
  provjeriTipke();

  upravljajZvonom();
  osvjeziMrtvackoThumbwheel();
  upravljajOtkucavanjem();
  upravljajKorekcijomKazaljki();
  upravljajPlocom();

  if (jeMQTTOmogucen()) {
    upravljajMQTT();
  }
  if (jeDCFOmogucen()) {
    osvjeziDCFSinkronizaciju();
  }
  spremiKriticalnoStanje();

  osvjeziWatchdog();
}
