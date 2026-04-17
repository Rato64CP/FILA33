// otkucavanje_interno.h - Interni most izmedu otkucavanja i posebnih nacina rada
#pragma once

#include <Arduino.h>

// Provjerava smiju li posebni nacini preuzeti cekice toranjskog sata.
bool jeOperacijaCekicaDozvoljena();

// Prekida aktivno satno otkucavanje kad posebni nacin preuzme cekice.
void prekiniAktivnoOtkucavanjeZbogPosebnogNacina(const __FlashStringHelper* razlog);

// Vraca sigurno trajanje impulsa cekica koje vrijedi i za posebne nacine.
unsigned long dohvatiTrajanjeImpulsaCekicaZaPosebneNacineMs();

// Upravljanje relejima cekica za posebne nacine rada.
void aktivirajJedanCekicZaPosebniNacin(int pin, unsigned long trazenoTrajanjeMs);
void aktivirajObaCekicaZaPosebniNacin(unsigned long trazenoTrajanjeMs);
void deaktivirajObaCekicaZaPosebniNacin();

// Provjera stvarnog sigurnosnog stanja cekica nakon softverskog limita.
bool jeCekicSigurnosnoAktivanZaPosebniNacin(int pin);
