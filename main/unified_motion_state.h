#ifndef UNIFIED_MOTION_STATE_H
#define UNIFIED_MOTION_STATE_H

#include "eeprom_konstante.h"

namespace UnifiedMotionStateStore {

bool ucitaj(EepromLayout::UnifiedMotionState& stanje);
EepromLayout::UnifiedMotionState dohvatiIliMigriraj();
void spremiAkoPromjena(const EepromLayout::UnifiedMotionState& stanje);
void logirajStanje(const EepromLayout::UnifiedMotionState& stanje);

}  // namespace UnifiedMotionStateStore

#endif  // UNIFIED_MOTION_STATE_H
