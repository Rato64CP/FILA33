// src/main.ino – Glavni program automatike sata

#include "lcd_display.h"
#include "time_glob.h"
#include "otkucavanje.h"
#include "zvonjenje.h"
#include "tipke.h"
#include "postavke.h"
#include "kazaljke_sata.h"
#include "okretna_ploca.h"
#include "esp_serial.h"
#include "vrijeme_izvor.h"
#include "dcf_sync.h"
#include "watchdog.h"

static void prikaziPocetneInformacije() {
  prikaziPoruku("RZV Ver 1.0", "WIFI MQTT");
  odradiPauzuSaLCD(2000);

  DateTime zadnjaSync = getZadnjeSinkroniziranoVrijeme();
  int memorKazMin = dohvatiMemoriraneKazaljkeMinuta();
  int satKaz = memorKazMin / 60;
  int minKaz = memorKazMin % 60;
  int pozPloca = dohvatiPozicijuPloce();

  if (zadnjaSync.unixtime() > 0) {
    char red2[17];
    snprintf(red2, sizeof(red2), "Syn %02d.%02d %02d:%02d",
             zadnjaSync.day(), zadnjaSync.month(), zadnjaSync.hour(), zadnjaSync.minute());
    prikaziPoruku("Pokretanje...", red2);
    odradiPauzuSaLCD(1200);
  } else {
    prikaziPoruku("Pokretanje...", "Provjera" );
    odradiPauzuSaLCD(800);
  }

  char info[17];
  snprintf(info, sizeof(info), "Kaz %02d:%02d P:%02d", satKaz, minKaz, pozPloca);
  prikaziPoruku("Memorirani pod", info);
  odradiPauzuSaLCD(1200);
}

void setup() {
  inicijalizirajLCD();
  inicijalizirajRTC();
  ucitajPostavke();
  inicijalizirajTipke();
  inicijalizirajESP();
  inicijalizirajZvona();
  inicijalizirajKazaljke();
  inicijalizirajPlocu();
  inicijalizirajDCF();
  inicijalizirajWatchdog();

  prikaziPocetneInformacije();
  osvjeziWatchdog();

  bool trebaKazaljke = !suKazaljkeUSinkronu();
  bool trebaPlocu = !jePlocaUSinkronu();
  if (trebaKazaljke || trebaPlocu) {
    postaviLCDBlinkanje(true);
  }

  if (trebaKazaljke) {
    kompenzirajKazaljke(false);
  }
  if (trebaPlocu) {
    kompenzirajPlocu(false);
  }

  postaviLCDBlinkanje(false);
  oznaciKazaljkeKaoSinkronizirane();
  oznaciPlocuKaoSinkroniziranu();
  prikaziSat();
}

void loop() {
  osvjeziWatchdog();
  obradiESPSerijskuKomunikaciju();

  if (uPostavkama()) prikaziPostavke();
  else prikaziSat();

  provjeriTipke();
  upravljajZvonom();
  upravljajOtkucavanjem();
  upravljajKazaljkama();
  upravljajPlocom();
  osvjeziDCFSinkronizaciju();
  osvjeziWatchdog();
}
