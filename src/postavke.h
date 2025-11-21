// postavke.h
#pragma once
#include <stdint.h>

void ucitajPostavke();
void spremiPostavke();
void resetPostavke();

extern int satOd;
extern int satDo;
extern unsigned int pauzaIzmeduUdaraca;
extern unsigned int trajanjeImpulsaCekicaMs;
extern unsigned long trajanjeZvonjenjaRadniMs;
extern unsigned long trajanjeZvonjenjaNedjeljaMs;
extern unsigned long trajanjeSlavljenjaMs;
extern uint8_t brojZvona;

bool jeDozvoljenoOtkucavanjeUSatu(int sat);
unsigned int dohvatiTrajanjeImpulsaCekica();
unsigned int dohvatiPauzuIzmeduUdaraca();
unsigned long dohvatiTrajanjeZvonjenjaRadniMs();
unsigned long dohvatiTrajanjeZvonjenjaNedjeljaMs();
unsigned long dohvatiTrajanjeSlavljenjaMs();
uint8_t dohvatiBrojZvona();

void postaviTrajanjeImpulsaCekica(unsigned int trajanjeMs);
void postaviRasponOtkucavanja(int odSat, int doSat);
void postaviTrajanjeZvonjenjaRadni(unsigned long trajanjeMs);
void postaviTrajanjeZvonjenjaNedjelja(unsigned long trajanjeMs);
void postaviTrajanjeSlavljenja(unsigned long trajanjeMs);
void postaviBrojZvona(uint8_t broj);
