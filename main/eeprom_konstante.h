// eeprom_konstante.h – CONSOLIDATED EEPROM ADDRESS DEFINITIONS
// SINGLE SOURCE OF TRUTH for all EEPROM address assignments
// 24C32 external EEPROM (4096 bytes total)
// Wear-leveling: 6 slots per data type to extend EEPROM lifespan

#ifndef EEPROM_KONSTANTE_H
#define EEPROM_KONSTANTE_H

#include <stdint.h>

namespace EepromLayout {

// ==================== HAND POSITION (K-MINUTA) ====================
// Software position 0-719 for 12-hour cycle (0-720 minutes)
// Stored in external EEPROM for power-loss recovery

constexpr int BAZA_KAZALJKE = 0;                    // Base address for K-minuta storage
constexpr int SLOTOVI_KAZALJKE = 6;                 // 6 wear-leveling slots
constexpr int SLOT_SIZE_KAZALJKE = 4;               // sizeof(int)

// ==================== ROTATING PLATE POSITION ====================
// Plate position 0-63 representing 15-minute intervals
// 64 positions cover full operating window (04:59-20:44)

constexpr int BAZA_POZICIJA_PLOCE = 
  BAZA_KAZALJKE + (SLOTOVI_KAZALJKE * SLOT_SIZE_KAZALJKE);
constexpr int SLOTOVI_POZICIJA_PLOCE = 6;           // 6 wear-leveling slots
constexpr int SLOT_SIZE_POZICIJA_PLOCE = 4;         // sizeof(int)

// ==================== PLATE OFFSET MINUTES ====================
// Minutes offset (0-14) within 15-minute block for rotation trigger timing

constexpr int BAZA_OFFSET_MINUTA = 
  BAZA_POZICIJA_PLOCE + (SLOTOVI_POZICIJA_PLOCE * SLOT_SIZE_POZICIJA_PLOCE);
constexpr int SLOTOVI_OFFSET_MINUTA = 4;            // 4 wear-leveling slots
constexpr int SLOT_SIZE_OFFSET_MINUTA = 4;          // sizeof(int)

// ==================== TIME SOURCE TRACKING ====================
// Tracks which source provided current time (RTC, NTP, or DCF)

struct ZadnjaSinkronizacija {
  int izvor;          // 0=RTC, 1=NTP, 2=DCF
  uint32_t timestamp; // Unix timestamp of last sync
};

constexpr int BAZA_ZADNJA_SINKRONIZACIJA = 
  BAZA_OFFSET_MINUTA + (SLOTOVI_OFFSET_MINUTA * SLOT_SIZE_OFFSET_MINUTA);
constexpr int SLOTOVI_ZADNJA_SINKRONIZACIJA = 6;    // 6 wear-leveling slots
constexpr int SLOT_SIZE_ZADNJA_SINKRONIZACIJA = 
  sizeof(ZadnjaSinkronizacija);

// ==================== SYSTEM SETTINGS ====================
// User-configurable settings persisted in EEPROM
// Includes bell/hammer timing, WiFi credentials, operation hours

struct PostavkeSpremnik {
  // Bell operation hours
  int satOd;                              // Hour to start striking (e.g., 6)
  int satDo;                              // Hour to stop striking (e.g., 22)
  
  // Plate operation window (in minutes from midnight)
  int plocaPocetakMinuta;                 // Plate start time (e.g., 299 = 04:59)
  int plocaKrajMinuta;                    // Plate end time (e.g., 1244 = 20:44)
  
  // Bell/hammer timing
  unsigned int trajanjeImpulsaCekicaMs;   // Hammer pulse duration (ms)
  unsigned int pauzaIzmeduUdaraca;        // Pause between strikes (ms)
  unsigned long trajanjeZvonjenjaRadniMs; // Bell duration weekdays (ms)
  unsigned long trajanjeZvonjenjaNedjeljaMs; // Bell duration Sunday (ms)
  unsigned long trajanjeSlavljenjaMs;     // Celebration mode duration (ms)
  
  // Bell configuration
  uint8_t brojZvona;                      // Number of bells (1 or 2)
  
  // Security
  char pristupLozinka[9];                 // Admin password (8 chars + null)
  
  // Network settings
  char wifiSsid[33];                      // WiFi network name (32 chars + null)
  char wifiLozinka[33];                   // WiFi password (32 chars + null)
  bool koristiDhcp;                       // Use DHCP or static IP
  char statickaIp[16];                    // Static IP address (15 chars + null)
  char mreznaMaska[16];                   // Subnet mask (15 chars + null)
  char zadaniGateway[16];                 // Default gateway (15 chars + null)
};

constexpr int BAZA_POSTAVKE = 
  BAZA_ZADNJA_SINKRONIZACIJA + (SLOTOVI_ZADNJA_SINKRONIZACIJA * SLOT_SIZE_ZADNJA_SINKRONIZACIJA);
constexpr int SLOTOVI_POSTAVKE = 6;                 // 6 wear-leveling slots
constexpr int SLOT_SIZE_POSTAVKE = 
  sizeof(PostavkeSpremnik);

// ==================== POWER RECOVERY STATE ====================
// System state for recovery after power loss
// Includes hand position, plate position, RTC time, checksum

struct SystemStateBackup {
  uint32_t hand_position_k_minuta;        // K-minuta position (0-719)
  uint32_t plate_position;                // Plate position (0-63)
  uint32_t offset_minuta;                 // Plate offset (0-14)
  uint32_t rtc_timestamp;                 // RTC time as Unix timestamp
  uint16_t checksum;                      // Checksum for validation
};

constexpr int BAZA_BOOT_FLAGS = 
  BAZA_POSTAVKE + (SLOTOVI_POSTAVKE * SLOT_SIZE_POSTAVKE);
constexpr int SLOTOVI_BOOT_FLAGS = 6;               // 6 wear-leveling slots
constexpr int SLOT_SIZE_BOOT_FLAGS = 
  sizeof(SystemStateBackup);

// ==================== MEMORY MAP SUMMARY ====================
// Total EEPROM size: 4096 bytes
//
// Address Range    | Data Type              | Size      | Slots | Total
// 0-23             | K-minuta (int)         | 4 bytes   | 6     | 24 bytes
// 24-47            | Plate position (int)   | 4 bytes   | 6     | 24 bytes
// 48-63            | Offset minuta (int)    | 4 bytes   | 4     | 16 bytes
// 64-127           | Sync info (struct)     | 8 bytes   | 6     | 48 bytes
// 128-527          | Settings (struct)      | 64 bytes  | 6     | 384 bytes
// 528-767          | Boot state (struct)    | 40 bytes  | 6     | 240 bytes
// 768-4095         | RESERVED               | -         | -     | 3328 bytes
//
// TOTAL USED: 736 bytes (~18% of 4096 bytes)
// RESERVED:   3360 bytes (~82% - for future expansion)

// ==================== VALIDATION MACROS ====================

// Verify EEPROM layout doesn't exceed capacity
static_assert(
  (BAZA_BOOT_FLAGS + SLOTOVI_BOOT_FLAGS * SLOT_SIZE_BOOT_FLAGS) <= 4096,
  "EEPROM layout exceeds 24C32 capacity (4096 bytes)"
);

}  // namespace EepromLayout

#endif // EEPROM_KONSTANTE_H