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
#include "i2c_eeprom.h"
#include "pc_serial.h"

static String formatirajDatumVrijeme(const DateTime& dt) {
  char buff[20];
  snprintf(buff, sizeof(buff), "%02d.%02d.%04d %02d:%02d:%02d", dt.day(), dt.month(), dt.year(), dt.hour(), dt.minute(), dt.second());
  return String(buff);
}

static void posaljiPocetniPregledNaPC() {
  DateTime sada = dohvatiTrenutnoVrijeme();
  posaljiPCLog(String(F("Pokretanje u ")) + formatirajDatumVrijeme(sada) + String(F(" (izvor: ")) + dohvatiIzvorVremena() + F(")"));

  DateTime zadnjaSync = getZadnjeSinkroniziranoVrijeme();
  if (zadnjaSync.unixtime() > 0) {
    posaljiPCLog(String(F("Zadnja sinkronizacija: ")) + formatirajDatumVrijeme(zadnjaSync));
  } else {
    posaljiPCLog(F("Zadnja sinkronizacija: nema zapisa"));
  }

  int memorKazMin = dohvatiMemoriraneKazaljkeMinuta();
  int satKaz = memorKazMin / 60;
  int minKaz = memorKazMin % 60;
  posaljiPCLog(String(F("Memorirane kazaljke: ")) + satKaz + F(":") + minKaz);

  int pozPloca = dohvatiPozicijuPloce();
  posaljiPCLog(String(F("Pozicija okretne ploce: ")) + pozPloca);

  bool kazSink = suKazaljkeUSinkronu();
  bool plocaSink = jePlocaUSinkronu();
  posaljiPCLog(String(F("Stanje sinkronizacije - kazaljke: ")) + (kazSink ? F("DA") : F("NE")) +
               F(", ploca: ") + (plocaSink ? F("DA") : F("NE")));
}

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
  VanjskiEEPROM::inicijaliziraj();
  inicijalizirajRTC();
  inicijalizirajPCSerijsku();
  ucitajPostavke();
  inicijalizirajTipke();
  inicijalizirajESP();
  posaljiWifiPostavkeESP();
  inicijalizirajZvona();
  inicijalizirajKazaljke();
  inicijalizirajPlocu();
  inicijalizirajDCF();
  inicijalizirajWatchdog();

  prikaziPocetneInformacije();
  // Pošalji početni pregled stanja na PC serijski port radi lakšeg nadzora
  posaljiPocetniPregledNaPC();
  osvjeziWatchdog();

  bool trebaKazaljke = !suKazaljkeUSinkronu();
  bool trebaPlocu = !jePlocaUSinkronu();
  if (trebaKazaljke || trebaPlocu) {
    postaviLCDBlinkanje(true);
  }

  if (trebaPlocu) {
    kompenzirajPlocu(false);
  }
  if (trebaKazaljke) {
    kompenzirajKazaljke(false);
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
