// eeprom_konstante.h - CONSOLIDATED EEPROM ADDRESS DEFINITIONS
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

constexpr int BAZA_KAZALJKE = 0;
constexpr int SLOTOVI_KAZALJKE = 6;
constexpr int SLOT_SIZE_KAZALJKE = 4;

// ==================== ROTATING PLATE STATE ====================
// Autoritativno stanje okretne ploce za toranjski sat:
// - format "XXP" ili "XXN" (npr. 00P, 00N, 63N)
// - XX je pozicija 00-63
// - P = korak zapocet (prva faza), N = korak dovrsen (stabilno)

constexpr int BAZA_STANJE_PLOCE =
  BAZA_KAZALJKE + (SLOTOVI_KAZALJKE * SLOT_SIZE_KAZALJKE);
constexpr int SLOTOVI_STANJE_PLOCE = 6;
constexpr int SLOT_SIZE_STANJE_PLOCE = 4;

// ==================== PLATE OFFSET MINUTES ====================
// Minutes offset (0-14) within 15-minute block for rotation trigger timing

constexpr int BAZA_OFFSET_MINUTA =
  BAZA_STANJE_PLOCE + (SLOTOVI_STANJE_PLOCE * SLOT_SIZE_STANJE_PLOCE);
constexpr int SLOTOVI_OFFSET_MINUTA = 4;
constexpr int SLOT_SIZE_OFFSET_MINUTA = 4;

// ==================== TIME SOURCE TRACKING ====================
// Tracks which source provided current time (RTC, NTP, or DCF)

struct ZadnjaSinkronizacija {
  int izvor;
  uint32_t timestamp;
};

constexpr int BAZA_ZADNJA_SINKRONIZACIJA =
  BAZA_OFFSET_MINUTA + (SLOTOVI_OFFSET_MINUTA * SLOT_SIZE_OFFSET_MINUTA);
constexpr int SLOTOVI_ZADNJA_SINKRONIZACIJA = 6;
constexpr int SLOT_SIZE_ZADNJA_SINKRONIZACIJA = sizeof(ZadnjaSinkronizacija);

// ==================== SYSTEM SETTINGS ====================
// User-configurable settings persisted in EEPROM
// Includes bell/hammer timing, LCD, WiFi credentials, and operation hours

struct PostavkeSpremnik {
  uint16_t potpis;
  uint8_t verzija;

  // Bell operation hours
  int satOd;
  int satDo;
  int tihiSatiOd;
  int tihiSatiDo;

  // Plate operation window (minutes from midnight)
  int plocaPocetakMinuta;
  int plocaKrajMinuta;

  // Bell/hammer timing
  unsigned int trajanjeImpulsaCekicaMs;
  unsigned int pauzaIzmeduUdaraca;
  uint8_t trajanjeZvonjenjaRadniMin;
  uint8_t trajanjeZvonjenjaNedjeljaMin;
  uint8_t trajanjeSlavljenjaMin;
  uint8_t slavljenjePrijeZvonjenja;

  // Konfiguracija zvona i rasporeda cavala
  uint8_t brojZvona;
  uint8_t brojMjestaZaCavle;
  uint8_t cavliRadni[4];
  uint8_t cavliNedjelja[4];
  uint8_t cavaoSlavljenje;
  uint8_t cavaoMrtvacko;

  // Security
  char pristupLozinka[9];

  // Network and LCD settings
  char wifiSsid[33];
  char wifiLozinka[33];
  bool koristiDhcp;
  bool mqttOmogucen;
  bool lcdPozadinskoOsvjetljenje;
  uint8_t modSlavljenja;
  char statickaIp[16];
  char mreznaMaska[16];
  char zadaniGateway[16];
  char mqttBroker[40];
  uint16_t mqttPort;
  char mqttKorisnik[33];
  char mqttLozinka[33];
  char ntpServer[40];
  bool dcfOmogucen;
  bool wifiOmogucen;
  bool imaKazaljke;
  uint16_t checksum;
};

constexpr uint16_t POSTAVKE_POTPIS = 0x5453;
constexpr uint8_t POSTAVKE_VERZIJA = 10;

constexpr int BAZA_POSTAVKE =
  BAZA_ZADNJA_SINKRONIZACIJA + (SLOTOVI_ZADNJA_SINKRONIZACIJA * SLOT_SIZE_ZADNJA_SINKRONIZACIJA);
constexpr int SLOTOVI_POSTAVKE = 6;
constexpr int SLOT_SIZE_POSTAVKE = sizeof(PostavkeSpremnik);

// ==================== POWER RECOVERY STATE ====================
// System state for recovery after power loss

struct SystemStateBackup {
  uint32_t hand_position_k_minuta;
  uint32_t plate_position;
  uint32_t offset_minuta;
  // Legacy zapisi koriste RTC unix timestamp, a novi recovery zapisi
  // monotono rastucu sekvencu radi pouzdanog odabira najnovijeg slota.
  uint32_t rtc_timestamp;
  uint16_t checksum;
};

constexpr int BAZA_BOOT_FLAGS =
  BAZA_POSTAVKE + (SLOTOVI_POSTAVKE * SLOT_SIZE_POSTAVKE);
constexpr int SLOTOVI_BOOT_FLAGS = 6;
constexpr int SLOT_SIZE_BOOT_FLAGS = sizeof(SystemStateBackup);

// ==================== JEDINSTVENO STANJE KRETANJA ====================
// Jedinstveni state-model za toranjski sat (kazaljke + okretna ploca).

struct UnifiedMotionState {
  uint16_t hand_position;
  uint8_t hand_active;
  uint8_t hand_relay;
  uint32_t hand_start_ms;
  uint8_t plate_position;
  uint8_t plate_phase;
  uint8_t version;
  uint8_t reserved;
};

constexpr uint8_t UNIFIED_STANJE_VERZIJA = 2;

constexpr int BAZA_UNIFIED_STANJE =
  BAZA_BOOT_FLAGS + (SLOTOVI_BOOT_FLAGS * SLOT_SIZE_BOOT_FLAGS);
constexpr int SLOTOVI_UNIFIED_STANJE = 8;
constexpr int SLOT_SIZE_UNIFIED_STANJE = sizeof(UnifiedMotionState);

// ==================== DST STATUS TORANJSKOG SATA ====================
// Pamti radi li toranjski sat trenutno u CET ili CEST modu kako bi
// automatski prijelaz radio i bez ESP/NTP veze nakon restarta.

struct DSTStatus {
  uint16_t potpis;
  uint8_t dstAktivan;
  uint8_t reserved;
  uint16_t checksum;
};

constexpr uint16_t DST_STATUS_POTPIS = 0x4453;

constexpr int BAZA_DST_STATUS =
  BAZA_UNIFIED_STANJE + (SLOTOVI_UNIFIED_STANJE * SLOT_SIZE_UNIFIED_STANJE);
constexpr int SLOTOVI_DST_STATUS = 4;
constexpr int SLOT_SIZE_DST_STATUS = sizeof(DSTStatus);

// ==================== EEPROM DIJAGNOSTIKA ====================
// Zasebna adresa za provjeru zdravlja 24C32 EEPROM-a toranjskog sata.
// Namjerno je odvojena od recovery i wear-leveling slotova kako health-check
// ne bi mogao prepisati stanje kazaljki, ploce ili backup nakon restarta.

constexpr int BAZA_EEPROM_DIJAGNOSTIKA =
  BAZA_DST_STATUS + (SLOTOVI_DST_STATUS * SLOT_SIZE_DST_STATUS);
constexpr int VELICINA_EEPROM_DIJAGNOSTIKA = sizeof(uint32_t);

// ==================== VALIDATION MACROS ====================

static_assert(
  (BAZA_EEPROM_DIJAGNOSTIKA + VELICINA_EEPROM_DIJAGNOSTIKA) <= 4096,
  "EEPROM layout exceeds 24C32 capacity (4096 bytes)"
);

}  // namespace EepromLayout

#endif // EEPROM_KONSTANTE_H
