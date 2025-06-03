// src/main.ino – Glavni program automatike sata

#include "lcd_display.h"
#include "time_glob.h"
#include "otkucavanje.h"
#include "zvonjenje.h"
#include "tipke.h"
#include "postavke.h"
#include "kazaljke_sata.h"
#include "okretna_ploca.h"

void setup() {
  inicijalizirajLCD();
  inicijalizirajRTC();
  inicijalizirajKazaljke();
  inicijalizirajPlocu();
  kompenzirajKazaljke(true);
  kompenzirajPlocu(true);
  inicijalizirajTipke();
  ucitajPostavke();
  inicijalizirajZvona();
  inicijalizirajKazaljke();
  inicijalizirajPlocu();
  kompenzirajPlocu(false);
}

void loop() {
  if (uPostavkama()) prikaziPostavke();
  else prikaziSat();

  provjeriTipke();
  upravljajZvonom();
  upravljajOtkucavanjem();
  upravljajPločom();
}
