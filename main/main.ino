// main.ino - Glavni orkestrator sustava toranjskog sata
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
#include "power_recovery.h"
#include "sunceva_automatika.h"
#include "misna_automatika.h"

void setup() {
  inicijalizirajLCD();
  inicijalizirajPCSerijsku();

  posaljiPCLog(VanjskiEEPROM::inicijaliziraj()
                   ? F("EEPROM: vanjski EEPROM dostupan")
                   : F("EEPROM: vanjski EEPROM nije dostupan"));
  inicijalizirajRTC();
  ucitajPostavke();
  primijeniLCDPozadinskoOsvjetljenje(jeLCDPozadinskoOsvjetljenjeUkljuceno());

  inicijalizirajTipke();
  inicijalizirajESP();
  inicijalizirajMenuSistem();

  inicijalizirajZvona();
  inicijalizirajSuncevuAutomatiku();
  inicijalizirajMisnuAutomatiku();
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
  postaviBlokaduOtkucavanja(!jeVrijemePotvrdjenoZaAutomatiku());

  upravljajZvonom();
  upravljajOtkucavanjem();
  upravljajSuncevomAutomatikom();
  upravljajMisnomAutomatikom();
  osvjeziMrtvackoThumbwheel();
  upravljajKorekcijomKazaljki();
  upravljajPlocom();
  obradiAutomatskiNTPZahtjevESP();

  if (jeDCFOmogucen()) {
    osvjeziDCFSinkronizaciju();
  }
  spremiKriticalnoStanje();

  osvjeziWatchdog();
}
