// main.ino - Glavni orkestrator sustava toranjskog sata
#include <Arduino.h>

#include "lcd_display.h"
#include "i2c_bus.h"
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
#include "watchdog.h"
#include "power_recovery.h"
#include "sunceva_automatika.h"
#include "mise_automatika.h"
#include "pogrebne_skripte.h"
#include "prekidac_tisine.h"
#include "rs485_bridge.h"
#include "ups_nadzor.h"
#include "daljinski_433.h"

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
      PIN_RELEJ_NOCNE_RASVJETE,
      PIN_LAMPICA_ZVONO_1,
      PIN_LAMPICA_ZVONO_2,
      PIN_LAMPICA_SLAVLJENJE,
      PIN_LAMPICA_MRTVACKO,
      PIN_LAMPICA_TIHI_REZIM,
      PIN_LAMPICA_SUNCE_JUTRO,
      PIN_LAMPICA_SUNCE_PODNE,
      PIN_LAMPICA_SUNCE_VECER,
      PIN_RS485_SMJER
  };

  for (uint8_t i = 0; i < (sizeof(izlazniPinovi) / sizeof(izlazniPinovi[0])); ++i) {
    digitalWrite(izlazniPinovi[i], LOW);
    pinMode(izlazniPinovi[i], OUTPUT);
  }
}

}  // namespace

void setup() {
  pripremiResetFlagsMCU();
  inicijalizirajSigurnaPocetnaStanjaIzlaza();

  pripremiI2CSabirnicuSigurno();
  inicijalizirajLCD();
  inicijalizirajPCSerijsku();
  inicijalizirajRS485();

  posaljiPCLog(VanjskiEEPROM::inicijaliziraj()
                   ? F("EEPROM: vanjski EEPROM dostupan")
                   : F("EEPROM: vanjski EEPROM nije dostupan"));
  inicijalizirajRTC();
  ucitajPostavke();
  primijeniLCDPozadinskoOsvjetljenje(jeLCDPozadinskoOsvjetljenjeUkljuceno());
  inicijalizirajUPSNadzor();

  inicijalizirajTipke();
  inicijalizirajDaljinski433();
  inicijalizirajESP();
  inicijalizirajMenuSistem();

  inicijalizirajZvona();
  inicijalizirajSuncevuAutomatiku();
  inicijalizirajMiseAutomatiku();
  inicijalizirajPogrebneSkripte();
  inicijalizirajOtkucavanje();
  inicijalizirajPrekidacTisine();
  inicijalizirajMrtvackoThumbwheel();

  // Watchdog i boot recovery moraju odraditi prije inicijalizacije
  // izlaza kazaljki i ploce. U suprotnom toranjski sat moze kratko
  // podici relej iz starog aktivnog stanja prije nego sto recovery
  // resetira prekinuti korak za pravilno 6-sekundno ponavljanje.
  inicijalizirajWatchdog();
  inicijalizirajPowerRecovery();
  odradiBootRecovery();

  inicijalizirajKazaljke();
  inicijalizirajPlocu();
  primijeniSafeModeAkoTreba();
}

void loop() {
  osvjeziWatchdog();

  if (jeSafeModeAktivan()) {
    primijeniSafeModeAkoTreba();
    if (provjeriOtkljucavanjeSafeMode() && otkljucajSafeMode()) {
      prikaziPoruku(F("SUSTAV OTKLJUCAN"), F("NASTAVLJAM RAD"));
      osvjeziWatchdog();
      delay(1200);
      return;
    }
    prikaziZakljucaniSustav();
    osvjeziWatchdog();
    return;
  }

  obradiESPSerijskuKomunikaciju();
  obradiRS485();
  upravljajMenuSistemom();
  provjeriTipke();
  osvjeziPrekidacTisine();
  osvjeziUPSNadzor();
  dohvatiTrenutnoVrijeme();
  postaviGlobalnuBlokaduZvona(jeRtcIzlazniFailSafeAktivan());
  postaviGlobalnuBlokaduOtkucavanja(jeRtcIzlazniFailSafeAktivan());
  postaviBlokaduOtkucavanja(!jeVrijemePotvrdjenoZaAutomatiku());
  obradiDaljinski433();

  upravljajZvonom();
  upravljajOtkucavanjem();
  upravljajSuncevomAutomatikom();
  upravljajMiseAutomatikom();
  upravljajPogrebnimSkriptama(millis());
  osvjeziMrtvackoThumbwheel();
  upravljajKorekcijomKazaljki();
  upravljajPlocom();
  osvjeziStatusPushPremaESP();
  obradiAutomatskiNTPZahtjevESP();
  spremiKriticalnoStanje();
  osvjeziPowerRecoveryDijagnostiku();

  osvjeziWatchdog();
}
