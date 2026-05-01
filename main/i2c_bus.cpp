#include "i2c_bus.h"

#include <Wire.h>

void pripremiI2CSabirnicuSigurno() {
  Wire.begin();
  #if defined(WIRE_HAS_TIMEOUT) || defined(TWBR)
  Wire.setWireTimeout(25000, true);
  #endif
}
