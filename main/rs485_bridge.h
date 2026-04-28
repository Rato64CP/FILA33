// rs485_bridge.h - Robusni RS485 master sloj toranjskog sata
#pragma once

#include <stdbool.h>

void inicijalizirajRS485();
void obradiRS485();
bool posaljiRS485Komandu(const char* komanda);
bool jeRS485VezaPrekinuta();
