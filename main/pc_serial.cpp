// pc_serial.cpp – PC serijska komunikacija za debugging
#include <Arduino.h>
#include "pc_serial.h"

void inicijalizirajPCSerijsku() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {
    delay(10);
  }
  Serial.println(F("============================================"));
  Serial.println(F("Toranjski sat v1.0 - RTC+NTP+DCF sinkronizacija"));
  Serial.println(F("============================================"));
}

void posaljiPCLog(const String& poruka) {
  Serial.print(F("[LOG] "));
  Serial.println(poruka);
}

void posaljiPCLog(const char* poruka) {
  Serial.print(F("[LOG] "));
  Serial.println(poruka);
}