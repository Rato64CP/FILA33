// postavke.h
#pragma once

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

bool jeDozvoljenoOtkucavanjeUSatu(int sat);
unsigned int dohvatiTrajanjeImpulsaCekica();
unsigned int dohvatiPauzuIzmeduUdaraca();
unsigned long dohvatiTrajanjeZvonjenjaRadniMs();
unsigned long dohvatiTrajanjeZvonjenjaNedjeljaMs();
unsigned long dohvatiTrajanjeSlavljenjaMs();

void postaviTrajanjeImpulsaCekica(unsigned int trajanjeMs);
void postaviRasponOtkucavanja(int odSat, int doSat);
void postaviTrajanjeZvonjenjaRadni(unsigned long trajanjeMs);
void postaviTrajanjeZvonjenjaNedjelja(unsigned long trajanjeMs);
void postaviTrajanjeSlavljenja(unsigned long trajanjeMs);
