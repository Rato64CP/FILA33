// pc_serial.h - Komunikacija s PC-om preko Serial0 (USB)
#pragma once

void inicijalizirajPCSerijsku();
void posaljiPCLog(const __FlashStringHelper* poruka);
void posaljiPCLog(const String& poruka);
void posaljiPCLog(const char* poruka);
