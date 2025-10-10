// dcf_decoder.cpp
#include <Arduino.h>
#include "dcf_decoder.h"
#include "vrijeme_izvor.h"
#include <RTClib.h>
#include "podesavanja_piny.h"
#include "time_glob.h"

volatile bool edgeDetected = false;
volatile unsigned long edgeTime = 0;
unsigned long lastBitTime = 0;
uint8_t bitBuffer[59];
int bitIndex = 0;
bool frameStarted = false;

void handleDCFSIGNAL() {
  edgeTime = micros();
  edgeDetected = true;
}

void inicijalizirajDCF() {
  pinMode(DCF_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(DCF_PIN), handleDCFSIGNAL, FALLING);
}

void dekodirajDCFSignal() {
  if (!edgeDetected) return;
  edgeDetected = false;

  unsigned long pulseLength = micros() - edgeTime;

  if (pulseLength >= 150000 && pulseLength < 200000) {
    bitBuffer[bitIndex++] = 0;
  } else if (pulseLength >= 200000 && pulseLength < 250000) {
    bitBuffer[bitIndex++] = 1;
  } else {
    bitIndex = 0;
    frameStarted = false;
    return;
  }

  if (bitIndex >= 59) {
    frameStarted = true;
    bitIndex = 0;
    dekodirajFrame();
  }
}

void dekodirajFrame() {
  int minute = bitBuffer[21] + (bitBuffer[22] << 1) + (bitBuffer[23] << 2) + (bitBuffer[24] << 3)
               + (bitBuffer[25] << 4) + (bitBuffer[26] << 5) + (bitBuffer[27] << 6);
  int hour = bitBuffer[29] + (bitBuffer[30] << 1) + (bitBuffer[31] << 2) + (bitBuffer[32] << 3)
             + (bitBuffer[33] << 4) + (bitBuffer[34] << 5);
  int day = bitBuffer[36] + (bitBuffer[37] << 1) + (bitBuffer[38] << 2) + (bitBuffer[39] << 3)
            + (bitBuffer[40] << 4) + (bitBuffer[41] << 5);
  int month = bitBuffer[45] + (bitBuffer[46] << 1) + (bitBuffer[47] << 2) + (bitBuffer[48] << 3)
              + (bitBuffer[49] << 4);
  int year = bitBuffer[50] + (bitBuffer[51] << 1) + (bitBuffer[52] << 2) + (bitBuffer[53] << 3)
             + (bitBuffer[54] << 4) + (bitBuffer[55] << 5) + 2000;

  DateTime dt(year, month, day, hour, minute, 0);
  postaviVrijemeIzDCF(dt);
}
