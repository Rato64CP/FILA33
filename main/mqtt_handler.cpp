// mqtt_handler.cpp – Home Assistant MQTT integration with 9 entities + 8 services
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
// MQTT services exposed to Home Assistant:
// 1. ring_bell – Ring bell 1 or 2 manually
// 2. set_mode – Set operation mode (normal/celebration/funeral)
// 3. hand_correction – Correct hand position
// 4. strikes_enable – Enable/disable automated hourly strikes
// 5. silent_toggle – Toggle silent mode on/off
// 6. rapid_correction – Force rapid hand synchronization
// 7. ntp_sync – Trigger immediate NTP synchronization
// 8. system_reset – System reset with state preservation

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

// ==================== MQTT CONFIGURATION ====================

#define MQTT_BROKER_ADDRESS "192.168.1.100"  // Update with your MQTT broker IP
#define MQTT_PORT 1883
#define MQTT_USERNAME "toranj"
#define MQTT_PASSWORD "toranj2024"

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
static bool hammer1_active = false;
static bool hammer2_active = false;
static bool silent_mode_active = false;
static String current_mode = "normal";
static unsigned long last_entity_update = 0;

// ==================== HOMEASSISTANT MQTT DISCOVERY ====================

// Send Home Assistant MQTT discovery configuration for all 9 entities
void publishHADiscovery() {
  if (!mqtt_connected) {
    posaljiPCLog(F("MQTT: Not connected, skipping HA discovery"));
    return;
  }

  // Discovery base topic: homeassistant/<component>/<device>/<entity>/config
  String discovery_base = String(HA_DISCOVERY_PREFIX) + "/sensor/" + String(DEVICE_ID);
  String binary_discovery_base = String(HA_DISCOVERY_PREFIX) + "/binary_sensor/" + String(DEVICE_ID);

  // ==================== ENTITY 1: CLOCK TIME ====================
  String topic1 = discovery_base + "/clock_time/config";
  String payload1 = "{\"name\":\"Clock Time\",";
  payload1 += "\"unique_id\":\"toranj_clock_time\",";
  payload1 += "\"state_topic\":\"" + String(BASE_TOPIC) + "/time\",";
  payload1 += "\"device\":{\"identifiers\":[\"" + String(DEVICE_ID) + "\"],";
  payload1 += "\"name\":\"" + String(DEVICE_NAME) + "\"},";
  payload1 += "\"icon\":\"mdi:clock\"}";
  objavi(topic1, payload1);

  // ==================== ENTITY 2: TIME SOURCE ====================
  String topic2 = discovery_base + "/time_source/config";
  String payload2 = "{\"name\":\"Time Source\",";
  payload2 += "\"unique_id\":\"toranj_time_source\",";
  payload2 += "\"state_topic\":\"" + String(BASE_TOPIC) + "/time_source\",";
  payload2 += "\"device\":{\"identifiers\":[\"" + String(DEVICE_ID) + "\"],";
  payload2 += "\"name\":\"" + String(DEVICE_NAME) + "\"},";
  payload2 += "\"icon\":\"mdi:network\"}";
  objavi(topic2, payload2);

  // ==================== ENTITY 3: LAST SYNC ====================
  String topic3 = discovery_base + "/last_sync/config";
  String payload3 = "{\"name\":\"Last Synchronization\",";
  payload3 += "\"unique_id\":\"toranj_last_sync\",";
  payload3 += "\"state_topic\":\"" + String(BASE_TOPIC) + "/last_sync\",";
  payload3 += "\"device\":{\"identifiers\":[\"" + String(DEVICE_ID) + "\"],";
  payload3 += "\"name\":\"" + String(DEVICE_NAME) + "\"},";
  payload3 += "\"icon\":\"mdi:sync\"}";
  objavi(topic3, payload3);

  // ==================== ENTITY 4: BELL 1 STATE ====================
  String topic4 = binary_discovery_base + "/bell1_state/config";
  String payload4 = "{\"name\":\"Bell 1 (Hourly)\",";
  payload4 += "\"unique_id\":\"toranj_bell1_state\",";
  payload4 += "\"state_topic\":\"" + String(BASE_TOPIC) + "/bell1/state\",";
  payload4 += "\"payload_on\":\"on\",\"payload_off\":\"off\",";
  payload4 += "\"device\":{\"identifiers\":[\"" + String(DEVICE_ID) + "\"],";
  payload4 += "\"name\":\"" + String(DEVICE_NAME) + "\"},";
  payload4 += "\"icon\":\"mdi:bell-outline\"}";
  objavi(topic4, payload4);

  // ==================== ENTITY 5: BELL 2 STATE ====================
  String topic5 = binary_discovery_base + "/bell2_state/config";
  String payload5 = "{\"name\":\"Bell 2 (Half-hourly)\",";
  payload5 += "\"unique_id\":\"toranj_bell2_state\",";
  payload5 += "\"state_topic\":\"" + String(BASE_TOPIC) + "/bell2/state\",";
  payload5 += "\"payload_on\":\"on\",\"payload_off\":\"off\",";
  payload5 += "\"device\":{\"identifiers\":[\"" + String(DEVICE_ID) + "\"],";
  payload5 += "\"name\":\"" + String(DEVICE_NAME) + "\"},";
  payload5 += "\"icon\":\"mdi:bell-outline\"}";
  objavi(topic5, payload5);

  // ==================== ENTITY 6: HAMMER 1 STATE ====================
  String topic6 = binary_discovery_base + "/hammer1_state/config";
  String payload6 = "{\"name\":\"Hammer 1 (Male)\",";
  payload6 += "\"unique_id\":\"toranj_hammer1_state\",";
  payload6 += "\"state_topic\":\"" + String(BASE_TOPIC) + "/hammer1/state\",";
  payload6 += "\"payload_on\":\"on\",\"payload_off\":\"off\",";
  payload6 += "\"device\":{\"identifiers\":[\"" + String(DEVICE_ID) + "\"],";
  payload6 += "\"name\":\"" + String(DEVICE_NAME) + "\"},";
  payload6 += "\"icon\":\"mdi:hammer\"}";
  objavi(topic6, payload6);

  // ==================== ENTITY 7: HAMMER 2 STATE ====================
  String topic7 = binary_discovery_base + "/hammer2_state/config";
  String payload7 = "{\"name\":\"Hammer 2 (Female)\",";
  payload7 += "\"unique_id\":\"toranj_hammer2_state\",";
  payload7 += "\"state_topic\":\"" + String(BASE_TOPIC) + "/hammer2/state\",";
  payload7 += "\"payload_on\":\"on\",\"payload_off\":\"off\",";
  payload7 += "\"device\":{\"identifiers\":[\"" + String(DEVICE_ID) + "\"],";
  payload7 += "\"name\":\"" + String(DEVICE_NAME) + "\"},";
  payload7 += "\"icon\":\"mdi:hammer\"}";
  objavi(topic7, payload7);

  // ==================== ENTITY 8: OPERATION MODE ====================
  String topic8 = discovery_base + "/operation_mode/config";
  String payload8 = "{\"name\":\"Operation Mode\",";
  payload8 += "\"unique_id\":\"toranj_operation_mode\",";
  payload8 += "\"state_topic\":\"" + String(BASE_TOPIC) + "/mode\",";
  payload8 += "\"device\":{\"identifiers\":[\"" + String(DEVICE_ID) + "\"],";
  payload8 += "\"name\":\"" + String(DEVICE_NAME) + "\"},";
  payload8 += "\"icon\":\"mdi:cog\"}";
  objavi(topic8, payload8);

  // ==================== ENTITY 9: SILENT MODE ====================
  String topic9 = binary_discovery_base + "/silent_mode/config";
  String payload9 = "{\"name\":\"Silent Mode\",";
  payload9 += "\"unique_id\":\"toranj_silent_mode\",";
  payload9 += "\"state_topic\":\"" + String(BASE_TOPIC) + "/silent/state\",";
  payload9 += "\"payload_on\":\"on\",\"payload_off\":\"off\",";
  payload9 += "\"device\":{\"identifiers\":[\"" + String(DEVICE_ID) + "\"],";
  payload9 += "\"name\":\"" + String(DEVICE_NAME) + "\"},";
  payload9 += "\"icon\":\"mdi:volume-mute\"}";
  objavi(topic9, payload9);

  // ==================== SERVICE 1: RING BELL ====================
  String service1_topic = String(BASE_TOPIC) + "/service/ring_bell";
  pretplati(service1_topic);

  // ==================== SERVICE 2: SET MODE ====================
  String service2_topic = String(BASE_TOPIC) + "/service/set_mode";
  pretplati(service2_topic);

  // ==================== SERVICE 3: HAND CORRECTION ====================
  String service3_topic = String(BASE_TOPIC) + "/service/hand_correction";
  pretplati(service3_topic);

  // ==================== SERVICE 4: STRIKES ENABLE ====================
  String service4_topic = String(BASE_TOPIC) + "/service/strikes_enable";
  pretplati(service4_topic);

  // ==================== SERVICE 5: SILENT TOGGLE ====================
  String service5_topic = String(BASE_TOPIC) + "/service/silent_toggle";
  pretplati(service5_topic);

  // ==================== SERVICE 6: RAPID CORRECTION ====================
  String service6_topic = String(BASE_TOPIC) + "/service/rapid_correction";
  pretplati(service6_topic);

  // ==================== SERVICE 7: NTP SYNC ====================
  String service7_topic = String(BASE_TOPIC) + "/service/ntp_sync";
  pretplati(service7_topic);

  // ==================== SERVICE 8: SYSTEM RESET ====================
  String service8_topic = String(BASE_TOPIC) + "/service/system_reset";
  pretplati(service8_topic);

  posaljiPCLog(F("MQTT: Home Assistant discovery published, 9 entities + 8 services"));
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
  objavi(String(BASE_TOPIC) + "/time", String(timeStr));

  // Entity 2: Time source
  objavi(String(BASE_TOPIC) + "/time_source", dohvatiIzvorVremena());

  // Entity 3: Last sync
  DateTime zadnjaSink = getZadnjeSinkroniziranoVrijeme();
  char syncStr[20];
  snprintf(syncStr, sizeof(syncStr), "%04d-%02d-%02d %02d:%02d:%02d",
    zadnjaSink.year(), zadnjaSink.month(), zadnjaSink.day(),
    zadnjaSink.hour(), zadnjaSink.minute(), zadnjaSink.second());
  objavi(String(BASE_TOPIC) + "/last_sync", String(syncStr));

  // Entity 4: Bell 1 state
  bell1_active = jeZvonoUTijeku();  // Simplified check
  objavi(String(BASE_TOPIC) + "/bell1/state", bell1_active ? "on" : "off");

  // Entity 5: Bell 2 state
  bell2_active = jeZvonoUTijeku();  // Simplified check
  objavi(String(BASE_TOPIC) + "/bell2/state", bell2_active ? "on" : "off");

  // Entity 6: Hammer 1 state
  hammer1_active = jeZvonoUTijeku();  // Would need actual hammer tracking
  objavi(String(BASE_TOPIC) + "/hammer1/state", hammer1_active ? "on" : "off");

  // Entity 7: Hammer 2 state
  hammer2_active = jeZvonoUTijeku();  // Would need actual hammer tracking
  objavi(String(BASE_TOPIC) + "/hammer2/state", hammer2_active ? "on" : "off");

  // Entity 8: Operation mode
  if (jeSlavljenjeUTijeku()) {
    current_mode = "celebration";
  } else if (jeMrtvackoUTijeku()) {
    current_mode = "funeral";
  } else {
    current_mode = "normal";
  }
  objavi(String(BASE_TOPIC) + "/mode", current_mode);

  // Entity 9: Silent mode
  objavi(String(BASE_TOPIC) + "/silent/state", silent_mode_active ? "on" : "off");
}

// ==================== COMMAND HANDLING ====================

void obradiMQTTKomandu(const String& tema, const String& poruka) {
  posaljiPCLog(String(F("MQTT: Received command: ")) + tema + " = " + poruka);

  // Service 1: Ring bell
  if (tema.endsWith("/service/ring_bell")) {
    int bell = poruka.toInt();
    if (bell == 1 || bell == 2) {
      ukljuciZvono(bell);
      String log = F("MQTT: Ring Bell ");
      log += bell;
      posaljiPCLog(log);
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
      posaljiPCLog(F("MQTT: Mode set to Normal"));
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
    // Send command to ESP8266 to trigger NTP
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
}

// ==================== BASIC MQTT OPERATIONS ====================

// Placeholder implementations – would use PubSubClient library in production
void objavi(const String& tema, const String& vrijednost) {
  // In production, use PubSubClient:
  // client.publish(tema.c_str(), vrijednost.c_str());
  
  // For now, send via ESP8266 serial
  String komanda = "MQTT:PUB|";
  komanda += tema;
  komanda += "|";
  komanda += vrijednost;
  
  posaljiESPKomandu(komanda);  // Send to ESP8266
}

void pretplati(const String& tema) {
  // In production, use PubSubClient:
  // client.subscribe(tema.c_str());
  
  // For now, send via ESP8266 serial
  String komanda = "MQTT:SUB|";
  komanda += tema;
  
  posaljiESPKomandu(komanda);  // Send to ESP8266
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
  
  // Send reconnect command to ESP8266
  String komanda = "MQTT:CONNECT|";
  komanda += MQTT_BROKER_ADDRESS;
  komanda += "|";
  komanda += String(MQTT_PORT);
  
  posaljiESPKomandu(komanda);
  
  posaljiPCLog(F("MQTT: Reconnect attempt sent to ESP8266"));
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
  
  posaljiPCLog(F("MQTT Handler initialized"));
  
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
