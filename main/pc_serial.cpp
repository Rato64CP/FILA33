// pc_serial.cpp
#include <Arduino.h>
#include <RTClib.h>
#include "time_glob.h"
#include "pc_serial.h"

static const unsigned long PC_BRZINA = 9600;
static bool serijskiSpreman = false;

static String formatirajVremenskuOznaku() {
  DateTime sada = dohvatiTrenutnoVrijeme();
  char oznaka[21];
  snprintf(oznaka, sizeof(oznaka), "%04d-%02d-%02d %02d:%02d:%02d",
           sada.year(), sada.month(), sada.day(), sada.hour(), sada.minute(), sada.second());
  return String(oznaka);
}

void inicijalizirajPCSerijsku() {
  Serial.begin(PC_BRZINA);
  while (!Serial) {
    delay(10);
  }
  serijskiSpreman = true;
  posaljiPCLog(F("USB serijska veza spremna"));
}

void posaljiPCLog(const __FlashStringHelper* poruka) {
  if (!serijskiSpreman) return;
  Serial.print(F("[LOG] "));
  Serial.print(formatirajVremenskuOznaku());
  Serial.print(F(" - "));
  Serial.println(poruka);
}

void posaljiPCLog(const String& poruka) {
  if (!serijskiSpreman) return;
  Serial.print(F("[LOG] "));
  Serial.print(formatirajVremenskuOznaku());
  Serial.print(F(" - "));
  Serial.println(poruka);
}
