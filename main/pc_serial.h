// pc_serial.h
#pragma once
#include <Arduino.h>

// Inicijalizira USB serijsku komunikaciju prema PC-u.
void inicijalizirajPCSerijsku();

// Salje tekstualnu poruku na PC s vremenskom oznakom za lakse otklanjanje gresaka.
void posaljiPCLog(const __FlashStringHelper* poruka);
void posaljiPCLog(const String& poruka);
