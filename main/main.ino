// main.ino - Glavni orkestrator sustava toranjskog sata
#include <Arduino.h>

#include "lcd_display.h"
#include "i2c_eeprom.h"
#include "time_glob.h"
#include "pc_serial.h"
#include "postavke.h"
#include "tipke.h"
#include "esp_serial.h"
#include "podesavanja_piny.h"
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
#include "prekidac_tisine.h"

namespace {

void inicijalizirajSigurnaPocetnaStanjaIzlaza() {
  const uint8_t izlazniPinovi[] = {
      PIN_RELEJ_PARNE_KAZALJKE,
      PIN_RELEJ_NEPARNE_KAZALJKE,
      PIN_RELEJ_PARNE_PLOCE,
      PIN_RELEJ_NEPARNE_PLOCE,
      PIN_ZVONO_1,
      PIN_ZVONO_2,
      PIN_CEKIC_MUSKI,
      PIN_CEKIC_ZENSKI,
      PIN_LAMPICA_ZVONO_1,
      PIN_LAMPICA_ZVONO_2,
      PIN_LAMPICA_SLAVLJENJE,
      PIN_LAMPICA_MRTVACKO,
      PIN_LAMPICA_TIHI_REZIM
  };

  for (uint8_t i = 0; i < (sizeof(izlazniPinovi) / sizeof(izlazniPinovi[0])); ++i) {
    digitalWrite(izlazniPinovi[i], LOW);
    pinMode(izlazniPinovi[i], OUTPUT);
  }
}

}  // namespace

void setup() {
  inicijalizirajSigurnaPocetnaStanjaIzlaza();

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
  inicijalizirajPrekidacTisine();
  inicijalizirajMrtvackoThumbwheel();

  // Watchdog i boot recovery moraju odraditi prije inicijalizacije
  // izlaza kazaljki i ploce. U suprotnom toranjski sat moze kratko
  // podici relej iz starog aktivnog stanja prije nego sto recovery
  // resetira prekinuti korak za pravilno 6-sekundno ponavljanje.
  inicijalizirajWatchdog();
  oznaciWatchdogReset(jeWatchdogResetDetektiran());
  oznaciGubitakNapajanja(jePowerLossResetDetektiran());
  inicijalizirajPowerRecovery();
  odradiBootRecovery();

  inicijalizirajKazaljke();
  inicijalizirajPlocu();
  if (jeDCFOmogucen()) {
    inicijalizirajDCF();
  } else {
    posaljiPCLog(F("DCF77: onemogucen u postavkama, inicijalizacija preskocena"));
  }
}

void loop() {
  osvjeziWatchdog();

  obradiESPSerijskuKomunikaciju();
  upravljajMenuSistemom();
  provjeriTipke();
  osvjeziPrekidacTisine();
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
