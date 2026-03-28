// pc_serial.h – Komunikacija sa PC-om via Serial0 (USB)
#pragma once

void inicijalizirajPCSerijsku();
void posaljiPCLog(const __FlashStringHelper* poruka);
void posaljiPCLog(const String& poruka);
void posaljiPCLog(const char* poruka);
