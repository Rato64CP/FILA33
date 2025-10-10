// postavke.h
#pragma once

void ucitajPostavke();
void spremiPostavke();
void resetPostavke();

extern int satOd;
extern int satDo;
extern unsigned int pauzaIzmeduUdaraca;
extern unsigned int trajanjeImpulsaCekicaMs;
extern unsigned long trajanjeZvonjenjaMs;

bool jeDozvoljenoOtkucavanjeUSatu(int sat);
unsigned int dohvatiTrajanjeImpulsaCekica();
unsigned int dohvatiPauzuIzmeduUdaraca();
unsigned long dohvatiTrajanjeZvonjenjaMs();
