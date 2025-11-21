#include "wear_leveling.h"

// CRC-16/CCITT-FALSE implementacija za provjeru valjanosti zapisa.
uint16_t WearLeveling::izracunajCRC(const uint8_t* podaci, size_t duljina) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < duljina; ++i) {
    crc ^= static_cast<uint16_t>(podaci[i]) << 8;
    for (uint8_t b = 0; b < 8; ++b) {
      if (crc & 0x8000) {
        crc = (crc << 1) ^ 0x1021;
      } else {
        crc <<= 1;
      }
    }
  }
  return crc;
}

