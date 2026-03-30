#include <iostream>
#include "main/eeprom_konstante.h"
int main(){
  using namespace EepromLayout;
  std::cout << "sizeof(PostavkeSpremnik)=" << sizeof(PostavkeSpremnik) << "\n";
  std::cout << "BAZA_POSTAVKE=" << BAZA_POSTAVKE << "\n";
  std::cout << "SLOT_SIZE_POSTAVKE=" << SLOT_SIZE_POSTAVKE << "\n";
  std::cout << "POSTAVKE_END=" << (BAZA_POSTAVKE + SLOTOVI_POSTAVKE * SLOT_SIZE_POSTAVKE) << "\n";
  std::cout << "BAZA_BOOT_FLAGS=" << BAZA_BOOT_FLAGS << "\n";
  std::cout << "sizeof(SystemStateBackup)=" << sizeof(SystemStateBackup) << "\n";
  std::cout << "BOOT_END=" << (BAZA_BOOT_FLAGS + SLOTOVI_BOOT_FLAGS * SLOT_SIZE_BOOT_FLAGS) << "\n";
  std::cout << "BAZA_UNIFIED_STANJE=" << BAZA_UNIFIED_STANJE << "\n";
  std::cout << "sizeof(UnifiedMotionState)=" << sizeof(UnifiedMotionState) << "\n";
  std::cout << "UNIFIED_END=" << (BAZA_UNIFIED_STANJE + SLOTOVI_UNIFIED_STANJE * SLOT_SIZE_UNIFIED_STANJE) << "\n";
  std::cout << "BAZA_DST_STATUS=" << BAZA_DST_STATUS << "\n";
  std::cout << "sizeof(DSTStatus)=" << sizeof(DSTStatus) << "\n";
  std::cout << "DST_END=" << (BAZA_DST_STATUS + SLOTOVI_DST_STATUS * SLOT_SIZE_DST_STATUS) << "\n";
  std::cout << "BAZA_EEPROM_DIJAGNOSTIKA=" << BAZA_EEPROM_DIJAGNOSTIKA << "\n";
  std::cout << "DIAG_END=" << (BAZA_EEPROM_DIJAGNOSTIKA + VELICINA_EEPROM_DIJAGNOSTIKA) << "\n";
  return 0;
}
