// src/main.ino – Glavni program automatike sata

#include "lcd_display.h"
#include "rtc_vrijeme.h"
#include "otkucavanje.h"
#include "zvonjenje.h"
#include "tipke.h"
#include "postavke.h"
#include "kazaljke_sata.h"

void setup() {
  inicijalizirajLCD();
  inicijalizirajRTC();
  inicijalizirajTipke();
  ucitajPostavke();
  inicijalizirajZvona();
  inicijalizirajKazaljke();
}

void loop() {
  if (uPostavkama()) prikaziPostavke();
  else prikaziSat();

  provjeriTipke();
  upravljajZvonom();
  upravljajOtkucavanjem();
  upravljajKazaljkama();
}
