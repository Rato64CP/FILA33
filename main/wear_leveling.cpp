// wear_leveling.cpp – EEPROM wear-leveling implementacija
#include <Arduino.h>
#include "wear_leveling.h"
#include "i2c_eeprom.h"
#include "pc_serial.h"

namespace WearLeveling {

// Implementacija čitanja sa external EEPROM-a (24C32 na RTC pločici)
bool procitajSlot(int adresa, void* cilj, size_t duljina) {
  if (adresa < 0 || duljina == 0) {
    return false;
  }
  
  // Koristi vanjski EEPROM (I2C 24C32)
  bool uspjeh = VanjskiEEPROM::procitaj(adresa, cilj, duljina);
  
  if (!uspjeh) {
    String log = F("WearLeveling: procitaj FAILED adresa=");
    log += adresa;
    log += F(" duljina=");
    log += duljina;
    posaljiPCLog(log);
  }
  
  return uspjeh;
}

// Implementacija pisanja u external EEPROM
bool napisiSlot(int adresa, const void* izvor, size_t duljina) {
  if (adresa < 0 || duljina == 0 || !izvor) {
    return false;
  }
  
  // Koristi vanjski EEPROM (I2C 24C32)
  bool uspjeh = VanjskiEEPROM::zapisi(adresa, izvor, duljina);
  
  if (!uspjeh) {
    String log = F("WearLeveling: zapisi FAILED adresa=");
    log += adresa;
    log += F(" duljina=");
    log += duljina;
    posaljiPCLog(log);
  } else {
    String log = F("WearLeveling: zapisi OK adresa=");
    log += adresa;
    log += F(" duljina=");
    log += duljina;
    posaljiPCLog(log);
  }
  
  return uspjeh;
}

}  // namespace WearLeveling