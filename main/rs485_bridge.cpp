// rs485_bridge.cpp - Robusni RS485 master za toranjski sat
#include <Arduino.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rs485_bridge.h"
#include "pc_serial.h"
#include "podesavanja_piny.h"
#include "postavke.h"

namespace {

constexpr unsigned long RS485_BRZINA = 9600UL;
constexpr size_t RS485_ULAZNI_BUFFER_MAX = 72;
constexpr size_t RS485_OKVIR_MAX = 72;
constexpr size_t RS485_SADRZAJ_MAX = 32;
constexpr unsigned int RS485_SMJER_STABILIZACIJA_US = 150;
constexpr unsigned long RS485_ACK_TIMEOUT_MS = 200UL;
constexpr uint8_t RS485_MAKS_PONAVLJANJA = 3;
constexpr unsigned long RS485_HEARTBEAT_INTERVAL_MS = 15000UL;
constexpr uint8_t RS485_MAKS_UZASTOPNIH_NEUSPJELIH_HEARTBEATOVA = 3;
constexpr uint8_t RS485_CILJNI_ID = 1;

enum VrstaRs485Poruke {
  RS485_PORUKA_NEPOZNATO = 0,
  RS485_PORUKA_CMD,
  RS485_PORUKA_ACK,
  RS485_PORUKA_PONG,
  RS485_PORUKA_NAK
};

struct Rs485PrimljenaPoruka {
  bool valjana;
  uint8_t id;
  uint16_t seq;
  VrstaRs485Poruke vrsta;
  char sadrzaj[RS485_SADRZAJ_MAX];
};

struct Rs485PendingKomanda {
  bool aktivna;
  bool heartbeat;
  uint16_t seq;
  uint8_t brojPokusaja;
  unsigned long zadnjeSlanjeMs;
  char sadrzaj[RS485_SADRZAJ_MAX];
  char okvir[RS485_OKVIR_MAX];
};

static char rs485UlazniBuffer[RS485_ULAZNI_BUFFER_MAX + 1];
static uint8_t rs485UlazniBufferDuljina = 0;
static bool rs485Inicijaliziran = false;
static bool rs485NadzorAktivan = false;
static bool rs485VezaPrekinuta = false;
static uint8_t rs485NeuspjeliHeartbeatovi = 0;
static uint16_t rs485SljedeciSeq = 1;
static unsigned long rs485ZadnjiHeartbeatMs = 0;
static Rs485PendingKomanda pendingKomanda = {false, false, 0, 0, 0, "", ""};

static void prebaciRS485UPrijem() {
  digitalWrite(PIN_RS485_SMJER, LOW);
}

static void prebaciRS485UPredaju() {
  digitalWrite(PIN_RS485_SMJER, HIGH);
}

static void resetirajRS485UlazniBuffer() {
  rs485UlazniBufferDuljina = 0;
  rs485UlazniBuffer[0] = '\0';
}

static uint16_t izracunajRs485Checksum(const char* tekst) {
  uint16_t checksum = 0;
  if (tekst == nullptr) {
    return 0;
  }

  while (*tekst != '\0') {
    checksum = static_cast<uint16_t>(checksum + static_cast<uint8_t>(*tekst));
    ++tekst;
  }
  return checksum;
}

static void ocistiPendingKomandu() {
  pendingKomanda.aktivna = false;
  pendingKomanda.heartbeat = false;
  pendingKomanda.seq = 0;
  pendingKomanda.brojPokusaja = 0;
  pendingKomanda.zadnjeSlanjeMs = 0;
  pendingKomanda.sadrzaj[0] = '\0';
  pendingKomanda.okvir[0] = '\0';
}

static void oznaciRs485VezuIspravnom() {
  const bool bilaPrekinuta = rs485VezaPrekinuta;
  rs485VezaPrekinuta = false;
  rs485NeuspjeliHeartbeatovi = 0;

  if (bilaPrekinuta) {
    posaljiPCLog(F("RS485: veza s tornjem ponovno potvrdena"));
  }
}

static void oznaciRs485VezuPrekinutom(const __FlashStringHelper* razlog) {
  rs485VezaPrekinuta = true;
  if (razlog != nullptr) {
    posaljiPCLog(razlog);
  }
}

static void sinkronizirajRs485Omogucenost() {
  if (!jeRS485Omogucen()) {
    const bool biloAktivno =
        rs485NadzorAktivan || rs485VezaPrekinuta || pendingKomanda.aktivna ||
        rs485UlazniBufferDuljina > 0;
    rs485NadzorAktivan = false;
    rs485VezaPrekinuta = false;
    rs485NeuspjeliHeartbeatovi = 0;
    rs485ZadnjiHeartbeatMs = 0;
    ocistiPendingKomandu();
    resetirajRS485UlazniBuffer();

    while (RS485_SERIJSKI_PORT.available() > 0) {
      RS485_SERIJSKI_PORT.read();
    }

    if (biloAktivno) {
      posaljiPCLog(F("RS485: nadzor i slanje iskljuceni kroz sustavske postavke"));
    }
    return;
  }

  if (!rs485NadzorAktivan) {
    rs485NadzorAktivan = true;
    rs485ZadnjiHeartbeatMs = 0;
    posaljiPCLog(F("RS485: nadzor aktivan kroz sustavske postavke"));
  }
}

static bool posaljiRs485Okvir(const char* okvir) {
  if (!rs485Inicijaliziran || okvir == nullptr || okvir[0] == '\0') {
    return false;
  }

  prebaciRS485UPredaju();
  delayMicroseconds(RS485_SMJER_STABILIZACIJA_US);
  RS485_SERIJSKI_PORT.println(okvir);
  RS485_SERIJSKI_PORT.flush();
  delayMicroseconds(RS485_SMJER_STABILIZACIJA_US);
  prebaciRS485UPrijem();

  char log[96];
  snprintf(log, sizeof(log), "RS485 TX: %s", okvir);
  posaljiPCLog(log);
  return true;
}

static bool pripremiPendingKomandu(const char* sadrzaj, bool heartbeat) {
  if (sadrzaj == nullptr || sadrzaj[0] == '\0' || strlen(sadrzaj) >= RS485_SADRZAJ_MAX) {
    return false;
  }

  if (pendingKomanda.aktivna) {
    posaljiPCLog(F("RS485: nova naredba odbijena jer prethodna jos ceka ACK"));
    return false;
  }

  if (rs485SljedeciSeq == 0) {
    rs485SljedeciSeq = 1;
  }

  char osnovniPayload[RS485_OKVIR_MAX];
  const int osnovniPayloadDuljina =
      snprintf(osnovniPayload,
               sizeof(osnovniPayload),
               "ID:%02u|CMD:%s|SEQ:%u",
               static_cast<unsigned>(RS485_CILJNI_ID),
               sadrzaj,
               static_cast<unsigned>(rs485SljedeciSeq));
  if (osnovniPayloadDuljina <= 0 ||
      static_cast<size_t>(osnovniPayloadDuljina) >= sizeof(osnovniPayload)) {
    posaljiPCLog(F("RS485: naredba je preduga za konfigurirani okvir"));
    return false;
  }

  const uint16_t checksum = izracunajRs485Checksum(osnovniPayload);
  const int okvirDuljina =
      snprintf(pendingKomanda.okvir,
               sizeof(pendingKomanda.okvir),
               "<%s|CRC:%u>",
               osnovniPayload,
               static_cast<unsigned>(checksum));
  if (okvirDuljina <= 0 ||
      static_cast<size_t>(okvirDuljina) >= sizeof(pendingKomanda.okvir)) {
    posaljiPCLog(F("RS485: okvir je predug za slanje"));
    return false;
  }

  strncpy(pendingKomanda.sadrzaj, sadrzaj, sizeof(pendingKomanda.sadrzaj) - 1);
  pendingKomanda.sadrzaj[sizeof(pendingKomanda.sadrzaj) - 1] = '\0';
  pendingKomanda.seq = rs485SljedeciSeq++;
  pendingKomanda.heartbeat = heartbeat;
  pendingKomanda.brojPokusaja = 0;
  pendingKomanda.zadnjeSlanjeMs = 0;
  pendingKomanda.aktivna = true;
  return true;
}

static bool ponovnoPosaljiPendingKomandu() {
  if (!pendingKomanda.aktivna) {
    return false;
  }

  if (!posaljiRs485Okvir(pendingKomanda.okvir)) {
    return false;
  }

  pendingKomanda.zadnjeSlanjeMs = millis();
  ++pendingKomanda.brojPokusaja;
  return true;
}

static bool jeSamoZnamenke(const char* tekst) {
  if (tekst == nullptr || tekst[0] == '\0') {
    return false;
  }

  while (*tekst != '\0') {
    if (!isdigit(static_cast<unsigned char>(*tekst))) {
      return false;
    }
    ++tekst;
  }
  return true;
}

static bool parsirajRs485Poruku(const char* okvir, Rs485PrimljenaPoruka& poruka) {
  poruka.valjana = false;
  poruka.id = 0;
  poruka.seq = 0;
  poruka.vrsta = RS485_PORUKA_NEPOZNATO;
  poruka.sadrzaj[0] = '\0';

  if (okvir == nullptr) {
    return false;
  }

  const size_t duljina = strlen(okvir);
  if (duljina < 3 || okvir[0] != '<' || okvir[duljina - 1] != '>') {
    return false;
  }

  char kopija[RS485_OKVIR_MAX];
  const size_t duljinaKopije = duljina - 2;
  if (duljinaKopije >= sizeof(kopija)) {
    return false;
  }

  memcpy(kopija, okvir + 1, duljinaKopije);
  kopija[duljinaKopije] = '\0';

  char* crcToken = strstr(kopija, "|CRC:");
  if (crcToken == nullptr) {
    return false;
  }

  char* crcVrijednost = crcToken + 5;
  if (!jeSamoZnamenke(crcVrijednost)) {
    return false;
  }

  const uint16_t procitaniChecksum = static_cast<uint16_t>(atoi(crcVrijednost));
  *crcToken = '\0';
  const uint16_t izracunaniChecksum = izracunajRs485Checksum(kopija);
  if (procitaniChecksum != izracunaniChecksum) {
    posaljiPCLog(F("RS485 RX: checksum ne odgovara, odbacujem okvir"));
    return false;
  }

  char* context = nullptr;
  char* token = strtok_r(kopija, "|", &context);
  bool imaId = false;
  bool imaSeq = false;
  bool imaVrstu = false;

  while (token != nullptr) {
    if (strncmp(token, "ID:", 3) == 0 && jeSamoZnamenke(token + 3)) {
      poruka.id = static_cast<uint8_t>(atoi(token + 3));
      imaId = true;
    } else if (strncmp(token, "SEQ:", 4) == 0 && jeSamoZnamenke(token + 4)) {
      poruka.seq = static_cast<uint16_t>(atoi(token + 4));
      imaSeq = true;
    } else if (strncmp(token, "CMD:", 4) == 0) {
      poruka.vrsta = RS485_PORUKA_CMD;
      strncpy(poruka.sadrzaj, token + 4, sizeof(poruka.sadrzaj) - 1);
      poruka.sadrzaj[sizeof(poruka.sadrzaj) - 1] = '\0';
      imaVrstu = true;
    } else if (strncmp(token, "ACK:", 4) == 0) {
      poruka.vrsta = RS485_PORUKA_ACK;
      strncpy(poruka.sadrzaj, token + 4, sizeof(poruka.sadrzaj) - 1);
      poruka.sadrzaj[sizeof(poruka.sadrzaj) - 1] = '\0';
      imaVrstu = true;
    } else if (strncmp(token, "PONG:", 5) == 0) {
      poruka.vrsta = RS485_PORUKA_PONG;
      strncpy(poruka.sadrzaj, token + 5, sizeof(poruka.sadrzaj) - 1);
      poruka.sadrzaj[sizeof(poruka.sadrzaj) - 1] = '\0';
      imaVrstu = true;
    } else if (strcmp(token, "PONG") == 0) {
      poruka.vrsta = RS485_PORUKA_PONG;
      strcpy(poruka.sadrzaj, "OK");
      imaVrstu = true;
    } else if (strncmp(token, "NAK:", 4) == 0) {
      poruka.vrsta = RS485_PORUKA_NAK;
      strncpy(poruka.sadrzaj, token + 4, sizeof(poruka.sadrzaj) - 1);
      poruka.sadrzaj[sizeof(poruka.sadrzaj) - 1] = '\0';
      imaVrstu = true;
    }

    token = strtok_r(nullptr, "|", &context);
  }

  poruka.valjana = imaId && imaSeq && imaVrstu;
  return poruka.valjana;
}

static void obradiRs485Poruku(const Rs485PrimljenaPoruka& poruka) {
  if (!poruka.valjana) {
    return;
  }

  if (poruka.id != RS485_CILJNI_ID) {
    posaljiPCLog(F("RS485 RX: okvir za drugi ID, preskacem"));
    return;
  }

  char log[96];
  snprintf(log,
           sizeof(log),
           "RS485 RX valjano: vrsta=%u seq=%u sadrzaj=%s",
           static_cast<unsigned>(poruka.vrsta),
           static_cast<unsigned>(poruka.seq),
           poruka.sadrzaj);
  posaljiPCLog(log);

  if (poruka.vrsta == RS485_PORUKA_NAK) {
    oznaciRs485VezuPrekinutom(F("[ERR] RS485: toranj je vratio NAK"));
    ocistiPendingKomandu();
    return;
  }

  if (!pendingKomanda.aktivna) {
    oznaciRs485VezuIspravnom();
    return;
  }

  if (poruka.seq != pendingKomanda.seq) {
    posaljiPCLog(F("RS485 RX: seq ne odgovara trenutnoj naredbi, odbacujem"));
    return;
  }

  if (pendingKomanda.heartbeat) {
    const bool valjaniHeartbeatOdgovor =
        poruka.vrsta == RS485_PORUKA_PONG ||
        (poruka.vrsta == RS485_PORUKA_ACK && strcmp(poruka.sadrzaj, "PING") == 0);
    if (!valjaniHeartbeatOdgovor) {
      posaljiPCLog(F("RS485 RX: heartbeat odgovor nije valjan"));
      return;
    }
  } else {
    if (poruka.vrsta != RS485_PORUKA_ACK ||
        strcmp(poruka.sadrzaj, pendingKomanda.sadrzaj) != 0) {
      posaljiPCLog(F("RS485 RX: ACK ne odgovara poslanoj naredbi"));
      return;
    }
  }

  oznaciRs485VezuIspravnom();
  ocistiPendingKomandu();
}

static void obradiRS485Redak() {
  while (rs485UlazniBufferDuljina > 0 &&
         (rs485UlazniBuffer[rs485UlazniBufferDuljina - 1] == ' ' ||
          rs485UlazniBuffer[rs485UlazniBufferDuljina - 1] == '\t')) {
    rs485UlazniBuffer[--rs485UlazniBufferDuljina] = '\0';
  }

  if (rs485UlazniBufferDuljina == 0) {
    resetirajRS485UlazniBuffer();
    return;
  }

  Rs485PrimljenaPoruka poruka{};
  if (!parsirajRs485Poruku(rs485UlazniBuffer, poruka)) {
    char log[96];
    snprintf(log, sizeof(log), "RS485 RX nevaljano: %s", rs485UlazniBuffer);
    posaljiPCLog(log);
    resetirajRS485UlazniBuffer();
    return;
  }

  obradiRs485Poruku(poruka);
  resetirajRS485UlazniBuffer();
}

static void obradiIstekPendingKomande() {
  if (!pendingKomanda.aktivna) {
    return;
  }

  const unsigned long sadaMs = millis();
  if ((sadaMs - pendingKomanda.zadnjeSlanjeMs) < RS485_ACK_TIMEOUT_MS) {
    return;
  }

  if (pendingKomanda.brojPokusaja < RS485_MAKS_PONAVLJANJA) {
    char log[96];
    snprintf(log,
             sizeof(log),
             "RS485: nema odgovora za %s, ponavljam pokusaj %u/%u",
             pendingKomanda.sadrzaj,
             static_cast<unsigned>(pendingKomanda.brojPokusaja + 1),
             static_cast<unsigned>(RS485_MAKS_PONAVLJANJA));
    posaljiPCLog(log);
    ponovnoPosaljiPendingKomandu();
    return;
  }

  if (pendingKomanda.heartbeat) {
    ++rs485NeuspjeliHeartbeatovi;

    char log[96];
    snprintf(log,
             sizeof(log),
             "RS485: heartbeat nije potvrden (%u/%u)",
             static_cast<unsigned>(rs485NeuspjeliHeartbeatovi),
             static_cast<unsigned>(RS485_MAKS_UZASTOPNIH_NEUSPJELIH_HEARTBEATOVA));
    posaljiPCLog(log);

    if (rs485NeuspjeliHeartbeatovi >= RS485_MAKS_UZASTOPNIH_NEUSPJELIH_HEARTBEATOVA) {
      oznaciRs485VezuPrekinutom(F("[ERR] RS485: VEZA TORANJ PREKINUTA"));
    }
  } else {
    oznaciRs485VezuPrekinutom(F("[ERR] RS485: toranj ne odgovara na naredbu"));
  }

  ocistiPendingKomandu();
}

static void pokreniHeartbeatAkoTreba() {
  if (!rs485NadzorAktivan || pendingKomanda.aktivna) {
    return;
  }

  const unsigned long sadaMs = millis();
  if (rs485ZadnjiHeartbeatMs != 0 &&
      (sadaMs - rs485ZadnjiHeartbeatMs) < RS485_HEARTBEAT_INTERVAL_MS) {
    return;
  }

  if (!pripremiPendingKomandu("PING", true)) {
    return;
  }

  if (ponovnoPosaljiPendingKomandu()) {
    rs485ZadnjiHeartbeatMs = sadaMs;
  } else {
    ocistiPendingKomandu();
  }
}

}  // namespace

void inicijalizirajRS485() {
  pinMode(PIN_RS485_SMJER, OUTPUT);
  prebaciRS485UPrijem();
  RS485_SERIJSKI_PORT.begin(RS485_BRZINA);
  resetirajRS485UlazniBuffer();
  ocistiPendingKomandu();
  rs485Inicijaliziran = true;
  rs485NadzorAktivan = false;
  rs485VezaPrekinuta = false;
  rs485NeuspjeliHeartbeatovi = 0;
  rs485SljedeciSeq = 1;
  rs485ZadnjiHeartbeatMs = 0;

  posaljiPCLog(F("RS485: master inicijaliziran na Serial1, CRC/ACK/heartbeat spremni"));
}

void obradiRS485() {
  if (!rs485Inicijaliziran) {
    return;
  }

  sinkronizirajRs485Omogucenost();
  if (!jeRS485Omogucen()) {
    return;
  }

  while (RS485_SERIJSKI_PORT.available() > 0) {
    const char znak = static_cast<char>(RS485_SERIJSKI_PORT.read());
    if (znak == '\r') {
      continue;
    }

    if (znak == '\n') {
      obradiRS485Redak();
      continue;
    }

    if (rs485UlazniBufferDuljina < RS485_ULAZNI_BUFFER_MAX) {
      rs485UlazniBuffer[rs485UlazniBufferDuljina++] = znak;
      rs485UlazniBuffer[rs485UlazniBufferDuljina] = '\0';
    } else {
      posaljiPCLog(F("RS485 RX: preduga linija, odbacujem buffer"));
      resetirajRS485UlazniBuffer();
    }
  }

  obradiIstekPendingKomande();
  pokreniHeartbeatAkoTreba();
}

bool posaljiRS485Komandu(const char* komanda) {
  if (!rs485Inicijaliziran || !jeRS485Omogucen() || komanda == nullptr ||
      komanda[0] == '\0') {
    return false;
  }

  if (!pripremiPendingKomandu(komanda, false)) {
    return false;
  }

  if (!ponovnoPosaljiPendingKomandu()) {
    ocistiPendingKomandu();
    return false;
  }

  return true;
}

bool jeRS485VezaPrekinuta() {
  return rs485NadzorAktivan && rs485VezaPrekinuta;
}
