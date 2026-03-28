// mqtt_handler.cpp – Home Assistant MQTT integration with 9 entities + vise servisa
// MQTT entities exposed to Home Assistant:
// 1. clock_time (sensor) – Current clock time
// 2. time_source (sensor) – RTC/NTP/DCF source
// 3. last_sync (sensor) – Timestamp of last synchronization
// 4. bell1_state (binary_sensor) – Bell 1 ringing status
// 5. bell2_state (binary_sensor) – Bell 2 ringing status
// 6. hammer1_state (binary_sensor) – Hammer 1 active status
// 7. hammer2_state (binary_sensor) – Hammer 2 active status
// 8. operation_mode (sensor) – Normal/Celebration/Funeral/Waiting
// 9. silent_mode (binary_sensor) – Silent mode status
//
// MQTT servisi za toranjski sat:
// 1. ring_bell – genericko zvono 1 ili 2
// 2. bell1 – izravno ukljuci/iskljuci zvono 1
// 3. bell2 – izravno ukljuci/iskljuci zvono 2
// 4. set_mode – postavi normal/celebration/funeral
// 5. slavljenje – izravno ukljuci/iskljuci slavljenje
// 6. mrtvacko – izravno ukljuci/iskljuci mrtvacko zvonjenje
// 7. hand_correction – korekcija kazaljki
// 8. strikes_enable – ukljuci/iskljuci satne otkucaje
// 9. silent_toggle – lokalna MQTT zastavica za tihi mod
// 10. rapid_correction – prisilna budna korekcija
// 11. ntp_sync – trenutna NTP sinkronizacija
// 12. system_reset – restart sustava

#include <Arduino.h>
#include "mqtt_handler.h"
#include "time_glob.h"
#include "kazaljke_sata.h"
#include "okretna_ploca.h"
#include "zvonjenje.h"
#include "otkucavanje.h"
#include "esp_serial.h"
#include "pc_serial.h"
#include "postavke.h"

// Home Assistant discovery prefix
#define HA_DISCOVERY_PREFIX "homeassistant"

// Device identifier for Home Assistant
#define DEVICE_ID "toranj_sat_01"
#define DEVICE_NAME "Toranjski Sat"

// Base MQTT topic for this device
#define BASE_TOPIC "toranj/sat"

// ==================== STATE VARIABLES ====================

static bool mqtt_connected = false;
static unsigned long last_status_publish = 0;
static unsigned long last_mqtt_reconnect = 0;
static const unsigned long STATUS_PUBLISH_INTERVAL = 30000;  // 30 seconds
static const unsigned long MQTT_RECONNECT_INTERVAL = 10000;  // 10 seconds

// Entity states
static bool bell1_active = false;
static bool bell2_active = false;
static bool bell3_active = false;
static bool bell4_active = false;
static bool hammer1_active = false;
static bool hammer2_active = false;
static bool silent_mode_active = false;
static const char* current_mode = "normal";
static unsigned long last_entity_update = 0;

static bool jePayloadUkljucen(const String& poruka) {
  return poruka == "on" || poruka == "true" || poruka == "1" || poruka == "start";
}

static bool jePayloadIskljucen(const String& poruka) {
  return poruka == "off" || poruka == "false" || poruka == "0" || poruka == "stop";
}

static void sastaviTemu(char* odrediste, size_t velicina, const char* sufiks) {
  snprintf(odrediste, velicina, "%s/%s", BASE_TOPIC, sufiks);
}

static void objaviStanjeNaTemu(const char* sufiks, const char* vrijednost) {
  char tema[96];
  sastaviTemu(tema, sizeof(tema), sufiks);
  objavi(tema, vrijednost);
}

static void objaviDiscoveryKonfiguraciju(const char* komponenta,
                                         const char* objektId,
                                         const char* naziv,
                                         const char* uniqueId,
                                         const char* stateSufiks,
                                         const char* dodatniJson) {
  char tema[160];
  char payload[320];
  snprintf(tema, sizeof(tema), "%s/%s/%s/%s/config",
           HA_DISCOVERY_PREFIX, komponenta, DEVICE_ID, objektId);
  snprintf(payload, sizeof(payload),
           "{\"name\":\"%s\",\"unique_id\":\"%s\",\"state_topic\":\"%s/%s\","
           "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"%s\"},%s}",
           naziv, uniqueId, BASE_TOPIC, stateSufiks, DEVICE_ID, DEVICE_NAME, dodatniJson);
  objavi(tema, payload);
}

// ==================== HOMEASSISTANT MQTT DISCOVERY ====================

// Send Home Assistant MQTT discovery configuration for all 9 entities
void publishHADiscovery() {
  if (!mqtt_connected) {
    posaljiPCLog(F("MQTT: Not connected, skipping HA discovery"));
    return;
  }

  objaviDiscoveryKonfiguraciju("sensor", "clock_time", "Clock Time",
                               "toranj_clock_time", "time",
                               "\"icon\":\"mdi:clock\"");
  objaviDiscoveryKonfiguraciju("sensor", "time_source", "Time Source",
                               "toranj_time_source", "time_source",
                               "\"icon\":\"mdi:network\"");
  objaviDiscoveryKonfiguraciju("sensor", "last_sync", "Last Synchronization",
                               "toranj_last_sync", "last_sync",
                               "\"icon\":\"mdi:sync\"");
  objaviDiscoveryKonfiguraciju("binary_sensor", "bell1_state", "Bell 1 (Hourly)",
                               "toranj_bell1_state", "bell1/state",
                               "\"payload_on\":\"on\",\"payload_off\":\"off\","
                               "\"icon\":\"mdi:bell-outline\"");
  objaviDiscoveryKonfiguraciju("binary_sensor", "bell2_state", "Bell 2 (Half-hourly)",
                               "toranj_bell2_state", "bell2/state",
                               "\"payload_on\":\"on\",\"payload_off\":\"off\","
                               "\"icon\":\"mdi:bell-outline\"");
  objaviDiscoveryKonfiguraciju("binary_sensor", "bell3_state", "Bell 3",
                               "toranj_bell3_state", "bell3/state",
                               "\"payload_on\":\"on\",\"payload_off\":\"off\","
                               "\"icon\":\"mdi:bell-outline\"");
  objaviDiscoveryKonfiguraciju("binary_sensor", "bell4_state", "Bell 4",
                               "toranj_bell4_state", "bell4/state",
                               "\"payload_on\":\"on\",\"payload_off\":\"off\","
                               "\"icon\":\"mdi:bell-outline\"");
  objaviDiscoveryKonfiguraciju("binary_sensor", "hammer1_state", "Hammer 1 (Male)",
                               "toranj_hammer1_state", "hammer1/state",
                               "\"payload_on\":\"on\",\"payload_off\":\"off\","
                               "\"icon\":\"mdi:hammer\"");
  objaviDiscoveryKonfiguraciju("binary_sensor", "hammer2_state", "Hammer 2 (Female)",
                               "toranj_hammer2_state", "hammer2/state",
                               "\"payload_on\":\"on\",\"payload_off\":\"off\","
                               "\"icon\":\"mdi:hammer\"");
  objaviDiscoveryKonfiguraciju("sensor", "operation_mode", "Operation Mode",
                               "toranj_operation_mode", "mode",
                               "\"icon\":\"mdi:cog\"");
  objaviDiscoveryKonfiguraciju("binary_sensor", "silent_mode", "Silent Mode",
                               "toranj_silent_mode", "silent/state",
                               "\"payload_on\":\"on\",\"payload_off\":\"off\","
                               "\"icon\":\"mdi:volume-mute\"");

  static const char* MQTT_SERVISI[] = {
    BASE_TOPIC "/service/ring_bell",
    BASE_TOPIC "/service/set_mode",
    BASE_TOPIC "/service/bell1",
    BASE_TOPIC "/service/bell2",
    BASE_TOPIC "/service/bell3",
    BASE_TOPIC "/service/bell4",
    BASE_TOPIC "/service/slavljenje",
    BASE_TOPIC "/service/mrtvacko",
    BASE_TOPIC "/service/hand_correction",
    BASE_TOPIC "/service/strikes_enable",
    BASE_TOPIC "/service/silent_toggle",
    BASE_TOPIC "/service/rapid_correction",
    BASE_TOPIC "/service/ntp_sync",
    BASE_TOPIC "/service/system_reset"
  };

  for (uint8_t i = 0; i < (sizeof(MQTT_SERVISI) / sizeof(MQTT_SERVISI[0])); ++i) {
    pretplati(MQTT_SERVISI[i]);
  }

  posaljiPCLog(F("MQTT: discovery objavljen, ukljucene teme za zvona, slavljenje i mrtvacko"));
}

// ==================== STATUS PUBLISHING ====================

void objaviStatusMQTT() {
  if (!mqtt_connected) {
    return;
  }

  unsigned long sada = millis();
  
  // Publish every 30 seconds
  if (sada - last_status_publish < STATUS_PUBLISH_INTERVAL) {
    return;
  }
  last_status_publish = sada;

  // Get current time
  DateTime vrijeme = dohvatiTrenutnoVrijeme();
  
  // Entity 1: Clock time
  char timeStr[20];
  snprintf(timeStr, sizeof(timeStr), "%02d:%02d:%02d",
    vrijeme.hour(), vrijeme.minute(), vrijeme.second());
  objaviStanjeNaTemu("time", timeStr);

  // Entity 2: Time source
  objaviStanjeNaTemu("time_source", dohvatiOznakuIzvoraVremena());

  // Entity 3: Last sync
  DateTime zadnjaSink = getZadnjeSinkroniziranoVrijeme();
  char syncStr[20];
  snprintf(syncStr, sizeof(syncStr), "%04d-%02d-%02d %02d:%02d:%02d",
    zadnjaSink.year(), zadnjaSink.month(), zadnjaSink.day(),
    zadnjaSink.hour(), zadnjaSink.minute(), zadnjaSink.second());
  objaviStanjeNaTemu("last_sync", syncStr);

  // Entity 4: Bell 1 state
  bell1_active = jeZvonoAktivno(1);
  objaviStanjeNaTemu("bell1/state", bell1_active ? "on" : "off");

  // Entity 5: Bell 2 state
  bell2_active = jeZvonoAktivno(2);
  objaviStanjeNaTemu("bell2/state", bell2_active ? "on" : "off");

  // Entity 6: Bell 3 state
  bell3_active = jeZvonoAktivno(3);
  objaviStanjeNaTemu("bell3/state", bell3_active ? "on" : "off");

  // Entity 7: Bell 4 state
  bell4_active = jeZvonoAktivno(4);
  objaviStanjeNaTemu("bell4/state", bell4_active ? "on" : "off");

  // Entity 8: Hammer 1 state
  hammer1_active = jeZvonoUTijeku();  // Would need actual hammer tracking
  objaviStanjeNaTemu("hammer1/state", hammer1_active ? "on" : "off");

  // Entity 9: Hammer 2 state
  hammer2_active = jeZvonoUTijeku();  // Would need actual hammer tracking
  objaviStanjeNaTemu("hammer2/state", hammer2_active ? "on" : "off");

  // Entity 10: Operation mode
  if (jeSlavljenjeUTijeku()) {
    current_mode = "celebration";
  } else if (jeMrtvackoUTijeku()) {
    current_mode = "funeral";
  } else {
    current_mode = "normal";
  }
  objaviStanjeNaTemu("mode", current_mode);

  // Entity 11: Silent mode
  objaviStanjeNaTemu("silent/state", silent_mode_active ? "on" : "off");
}

// ==================== COMMAND HANDLING ====================

void obradiMQTTKomandu(const String& tema, const String& poruka) {
  posaljiPCLog(String(F("MQTT: Received command: ")) + tema + " = " + poruka);

  // Service 1: Ring bell
  if (tema.endsWith("/service/ring_bell")) {
    int bell = poruka.toInt();
    if (bell >= 1 && bell <= 4) {
      ukljuciZvono(bell);
      String log = F("MQTT: Ring Bell ");
      log += bell;
      posaljiPCLog(log);
    }
    return;
  }

  if (tema.endsWith("/service/bell1")) {
    if (jePayloadUkljucen(poruka)) {
      ukljuciZvono(1);
      posaljiPCLog(F("MQTT: Bell1 ON"));
    } else if (jePayloadIskljucen(poruka)) {
      iskljuciZvono(1);
      posaljiPCLog(F("MQTT: Bell1 OFF"));
    }
    return;
  }

  if (tema.endsWith("/service/bell2")) {
    if (jePayloadUkljucen(poruka)) {
      ukljuciZvono(2);
      posaljiPCLog(F("MQTT: Bell2 ON"));
    } else if (jePayloadIskljucen(poruka)) {
      iskljuciZvono(2);
      posaljiPCLog(F("MQTT: Bell2 OFF"));
    }
    return;
  }

  if (tema.endsWith("/service/bell3")) {
    if (jePayloadUkljucen(poruka)) {
      ukljuciZvono(3);
      posaljiPCLog(F("MQTT: Bell3 ON"));
    } else if (jePayloadIskljucen(poruka)) {
      iskljuciZvono(3);
      posaljiPCLog(F("MQTT: Bell3 OFF"));
    }
    return;
  }

  if (tema.endsWith("/service/bell4")) {
    if (jePayloadUkljucen(poruka)) {
      ukljuciZvono(4);
      posaljiPCLog(F("MQTT: Bell4 ON"));
    } else if (jePayloadIskljucen(poruka)) {
      iskljuciZvono(4);
      posaljiPCLog(F("MQTT: Bell4 OFF"));
    }
    return;
  }

  // Service 2: Set mode (normal/celebration/funeral)
  if (tema.endsWith("/service/set_mode")) {
    if (poruka == "celebration") {
      zapocniSlavljenje();
      posaljiPCLog(F("MQTT: Mode set to Celebration"));
    } else if (poruka == "funeral") {
      zapocniMrtvacko();
      posaljiPCLog(F("MQTT: Mode set to Funeral"));
    } else if (poruka == "normal") {
      zaustaviSlavljenje();
      zaustaviMrtvacko();
      posaljiPCLog(F("MQTT: Mode set to Normal"));
    }
    return;
  }

  if (tema.endsWith("/service/slavljenje")) {
    if (jePayloadUkljucen(poruka)) {
      zapocniSlavljenje();
      posaljiPCLog(F("MQTT: Slavljenje ON"));
    } else if (jePayloadIskljucen(poruka)) {
      zaustaviSlavljenje();
      posaljiPCLog(F("MQTT: Slavljenje OFF"));
    }
    return;
  }

  if (tema.endsWith("/service/mrtvacko")) {
    if (jePayloadUkljucen(poruka)) {
      zapocniMrtvacko();
      posaljiPCLog(F("MQTT: Mrtvacko ON"));
    } else if (jePayloadIskljucen(poruka)) {
      zaustaviMrtvacko();
      posaljiPCLog(F("MQTT: Mrtvacko OFF"));
    }
    return;
  }

  // Service 3: Hand correction
  if (tema.endsWith("/service/hand_correction")) {
    // Format: "HH:MM"
    int colon = poruka.indexOf(':');
    if (colon > 0) {
      int sat = poruka.substring(0, colon).toInt();
      int minuta = poruka.substring(colon + 1).toInt();
      postaviRucnuPozicijuKazaljki(sat, minuta);
      String log = F("MQTT: Hand correction to ");
      log += sat;
      log += F(":");
      log += minuta;
      posaljiPCLog(log);
    }
    return;
  }

  // Service 4: Strikes enable/disable
  if (tema.endsWith("/service/strikes_enable")) {
    bool enable = (poruka == "on" || poruka == "true");
    postaviBlokaduOtkucavanja(!enable);
    String logMsg = F("MQTT: Strikes ");
    logMsg += String(enable ? "enabled" : "disabled");
    posaljiPCLog(logMsg);
    return;
  }

  // Service 5: Silent toggle
  if (tema.endsWith("/service/silent_toggle")) {
    silent_mode_active = !silent_mode_active;
    String logMsg = F("MQTT: Silent mode ");
    logMsg += String(silent_mode_active ? "on" : "off");
    posaljiPCLog(logMsg);
    return;
  }

  // Service 6: Rapid correction
  if (tema.endsWith("/service/rapid_correction")) {
    pokreniBudnoKorekciju();
    posaljiPCLog(F("MQTT: Rapid hand correction initiated"));
    return;
  }

  // Service 7: NTP sync (request from ESP8266)
  if (tema.endsWith("/service/ntp_sync")) {
    posaljiPCLog(F("MQTT: NTP sync requested (via ESP8266)"));
    posaljiNTPPostavkeESP();
    posaljiWifiPostavkeESP();
    return;
  }

  // Service 8: System reset
  if (tema.endsWith("/service/system_reset")) {
    posaljiPCLog(F("MQTT: System reset initiated"));
    // Graceful shutdown and restart
    delay(1000);
    asm volatile ("jmp 0");  // Software reset for Arduino Mega
    return;
  }

  if (tema.indexOf("/service/") >= 0) {
    posaljiPCLog(String(F("MQTT: servis jos nije implementiran za toranjski sat: ")) + tema);
  }
}

// ==================== BASIC MQTT OPERATIONS ====================

// Placeholder implementations – would use PubSubClient library in production
void objavi(const char* tema, const char* vrijednost) {
  // In production, use PubSubClient:
  // client.publish(tema.c_str(), vrijednost.c_str());

  char komanda[384];
  snprintf(komanda, sizeof(komanda), "MQTT:PUB|%s|%s", tema, vrijednost);
  posaljiESPKomandu(komanda);
}

void objavi(const String& tema, const String& vrijednost) {
  objavi(tema.c_str(), vrijednost.c_str());
}

void pretplati(const char* tema) {
  // In production, use PubSubClient:
  // client.subscribe(tema.c_str());

  char komanda[192];
  snprintf(komanda, sizeof(komanda), "MQTT:SUB|%s", tema);
  posaljiESPKomandu(komanda);
}

void pretplati(const String& tema) {
  pretplati(tema.c_str());
}

bool jeMQTTPovezan() {
  // In production, use PubSubClient:
  // return client.connected();
  
  return mqtt_connected;
}

void reconnectMQTT() {
  if (mqtt_connected) {
    return;
  }

  unsigned long sada = millis();
  
  if (sada - last_mqtt_reconnect < MQTT_RECONNECT_INTERVAL) {
    return;
  }
  last_mqtt_reconnect = sada;
  
  char komanda[192];
  snprintf(komanda, sizeof(komanda), "MQTT:CONNECT|%s|%u|%s|%s",
           dohvatiMQTTBroker(), dohvatiMQTTPort(),
           dohvatiMQTTKorisnika(), dohvatiMQTTLozinku());
  posaljiESPKomandu(komanda);

  char log[128];
  snprintf(log, sizeof(log), "MQTT: reconnect pokusaj poslan ESP-u za broker %s:%u",
           dohvatiMQTTBroker(), dohvatiMQTTPort());
  posaljiPCLog(log);
}

// ==================== INITIALIZATION & MAIN LOOP ====================

void inicijalizirajMQTT() {
  if (!jeMQTTOmogucen()) {
    posaljiPCLog(F("MQTT: inicijalizacija preskočena (onemogućeno u postavkama)"));
    return;
  }

  mqtt_connected = false;
  last_status_publish = 0;
  last_mqtt_reconnect = 0;
  silent_mode_active = false;
  current_mode = "normal";

  char log[128];
  snprintf(log, sizeof(log), "MQTT handler inicijaliziran, broker=%s:%u",
           dohvatiMQTTBroker(), dohvatiMQTTPort());
  posaljiPCLog(log);
  
  // Attempt initial connection
  reconnectMQTT();
  
  // Schedule HA discovery for next update
  delay(1000);
}

void upravljajMQTT() {
  if (!jeMQTTOmogucen()) {
    return;
  }

  // Check for MQTT connection status from ESP8266
  static unsigned long last_connection_check = 0;
  unsigned long sada = millis();
  
  if (sada - last_connection_check > 5000) {
    last_connection_check = sada;
    
    // Check if connected by querying ESP8266
    posaljiESPKomandu("MQTT:STATUS");
  }
  
  // Try to reconnect if not connected
  if (!mqtt_connected) {
    reconnectMQTT();
  }
  
  // Publish current status periodically
  objaviStatusMQTT();
  
  // Publish Home Assistant discovery once
  static bool discovery_published = false;
  if (!discovery_published && mqtt_connected) {
    publishHADiscovery();
    discovery_published = true;
  }
  
}

void obradiMQTTLinijuIzESPa(const String& line) {
  if (!jeMQTTOmogucen()) {
    return;
  }

  if (!line.startsWith("MQTT:")) {
    return;
  }

  // Parse ESP8266 MQTT callback format: "MQTT:MSG|topic|payload"
  if (line.startsWith("MQTT:MSG|")) {
    int pipe1 = line.indexOf('|', 9);
    int pipe2 = line.lastIndexOf('|');

    if (pipe1 > 0 && pipe2 > pipe1) {
      String tema = line.substring(9, pipe1);
      String poruka = line.substring(pipe2 + 1);
      obradiMQTTKomandu(tema, poruka);
    }
    return;
  }

  // Parse connection status: "MQTT:CONNECTED" or "MQTT:DISCONNECTED"
  if (line == "MQTT:CONNECTED") {
    if (!mqtt_connected) {
      mqtt_connected = true;
      posaljiPCLog(F("MQTT: Connected to broker"));
    }
    return;
  }

  if (line == "MQTT:DISCONNECTED") {
    if (mqtt_connected) {
      mqtt_connected = false;
      posaljiPCLog(F("MQTT: Disconnected from broker"));
    }
    return;
  }

  if (line.startsWith("MQTT:")) {
    posaljiPCLog(String(F("MQTT: Nepoznata linija s ESP-a: ")) + line);
  }
}
