// postavke_skladistenje.h - EEPROM helperi postavki toranjskog sata
#pragma once

#include <stdint.h>

#include "eeprom_konstante.h"

uint16_t izracunajChecksumPostavki(const EepromLayout::PostavkeSpremnik& ulaz);
uint16_t izracunajChecksumSuncevihDogadaja(const EepromLayout::SunceviDogadajiSpremnik& ulaz);
uint16_t izracunajChecksumBlagdana(const EepromLayout::BlagdaniSpremnik& ulaz);
uint16_t izracunajChecksumMisa(const EepromLayout::MiseSpremnik& ulaz);
bool jeValjanEEPROMZapisPostavki(const EepromLayout::PostavkeSpremnik& spremnik);
bool jeValjanEEPROMZapisSuncevihDogadaja(const EepromLayout::SunceviDogadajiSpremnik& spremnik);
bool jeValjanEEPROMZapisBlagdana(const EepromLayout::BlagdaniSpremnik& spremnik);
bool jeValjanEEPROMZapisMisa(const EepromLayout::MiseSpremnik& spremnik);
bool ucitajAktualniSpremnikSkeniranjem(EepromLayout::PostavkeSpremnik& spremnik);
bool ucitajSunceveDogadajeSkeniranjem(EepromLayout::SunceviDogadajiSpremnik& spremnik);
bool ucitajBlagdaneSkeniranjem(EepromLayout::BlagdaniSpremnik& spremnik);
bool ucitajMiseSkeniranjem(EepromLayout::MiseSpremnik& spremnik);
bool obrisiSegmentEeproma(int baznaAdresa, int duljina);
