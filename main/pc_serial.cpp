// pc_serial.cpp - PC serijska komunikacija za dijagnostiku
#include <Arduino.h>
#include "pc_serial.h"
#include "postavke.h"

void inicijalizirajPCSerijsku() {
  Serial.begin(115200);
  Serial.println(F("============================================"));
  Serial.println(F("ZVONKO v. 1.0 - RTC+NTP sinkronizacija"));
  Serial.println(F("============================================"));
}

void posaljiPCLog(const __FlashStringHelper* poruka) {
  if (!jePCLogiranjeOmoguceno()) {
    return;
  }
  Serial.print(F("[LOG] "));
  Serial.println(poruka);
}

void posaljiPCLog(const String& poruka) {
  if (!jePCLogiranjeOmoguceno()) {
    return;
  }
  Serial.print(F("[LOG] "));
  Serial.println(poruka);
}

void posaljiPCLog(const char* poruka) {
  if (!jePCLogiranjeOmoguceno()) {
    return;
  }
  Serial.print(F("[LOG] "));
  Serial.println(poruka);
}
