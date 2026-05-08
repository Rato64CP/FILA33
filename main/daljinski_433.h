// daljinski_433.h - 433 MHz prijemnik SRX882 za toranjski sat
#pragma once

// Inicijalizira SRX882 data ulaz i internu obradu okvira.
void inicijalizirajDaljinski433();

// Obraduje primljene 433 MHz okvire bez blokiranja glavne petlje.
void obradiDaljinski433();
