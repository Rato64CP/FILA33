// mqtt_handler.cpp - Pojednostavljena MQTT integracija za toranjski sat.
// MQTT se koristi samo za 4 komandne teme:
// 1. toranj/sat/service/bell1
// 2. toranj/sat/service/bell2
// 3. toranj/sat/service/slavljenje
// 4. toranj/sat/service/mrtvacko

#include <Arduino.h>
#include "mqtt_handler.h"
#include "zvonjenje.h"
#include "esp_serial.h"
#include "pc_serial.h"
#include "postavke.h"

#define BASE_TOPIC "toranj/sat"

static bool mqtt_connected = false;
static bool mqttPretplatePoslane = false;
static unsigned long last_mqtt_reconnect = 0;
static unsigned long last_connection_check = 0;
static const unsigned long MQTT_RECONNECT_INTERVAL = 10000UL;
static const unsigned long MQTT_STATUS_INTERVAL = 5000UL;

static bool jePayloadUkljucen(const String& poruka) {
  return poruka == "on" || poruka == "true" || poruka == "1" || poruka == "start";
}

static bool jePayloadIskljucen(const String& poruka) {
  return poruka == "off" || poruka == "false" || poruka == "0" || poruka == "stop";
}

static void pretplatiTemu(const char* tema) {
  char komanda[192];
  snprintf(komanda, sizeof(komanda), "MQTT:SUB|%s", tema);
  posaljiESPKomandu(komanda);
}

static void posaljiPretplateAkoTreba() {
  if (!mqtt_connected || mqttPretplatePoslane) {
    return;
  }

  pretplatiTemu(BASE_TOPIC "/service/bell1");
  pretplatiTemu(BASE_TOPIC "/service/bell2");
  pretplatiTemu(BASE_TOPIC "/service/slavljenje");
  pretplatiTemu(BASE_TOPIC "/service/mrtvacko");
  mqttPretplatePoslane = true;
  posaljiPCLog(F("MQTT: pretplacene 4 komandne teme"));
}

void obradiMQTTKomandu(const String& tema, const String& poruka) {
  posaljiPCLog(String(F("MQTT: primljena komanda ")) + tema + " = " + poruka);

  if (tema.endsWith("/service/bell1")) {
    if (jePayloadUkljucen(poruka)) {
      ukljuciZvono(1);
      posaljiPCLog(F("MQTT: BELL1 ON"));
    } else if (jePayloadIskljucen(poruka)) {
      iskljuciZvono(1);
      posaljiPCLog(F("MQTT: BELL1 OFF"));
    }
    return;
  }

  if (tema.endsWith("/service/bell2")) {
    if (jePayloadUkljucen(poruka)) {
      ukljuciZvono(2);
      posaljiPCLog(F("MQTT: BELL2 ON"));
    } else if (jePayloadIskljucen(poruka)) {
      iskljuciZvono(2);
      posaljiPCLog(F("MQTT: BELL2 OFF"));
    }
    return;
  }

  if (tema.endsWith("/service/slavljenje")) {
    if (jePayloadUkljucen(poruka)) {
      zapocniSlavljenje();
      posaljiPCLog(F("MQTT: SLAVLJENJE ON"));
    } else if (jePayloadIskljucen(poruka)) {
      zaustaviSlavljenje();
      posaljiPCLog(F("MQTT: SLAVLJENJE OFF"));
    }
    return;
  }

  if (tema.endsWith("/service/mrtvacko")) {
    if (jePayloadUkljucen(poruka)) {
      zapocniMrtvacko();
      posaljiPCLog(F("MQTT: MRTVACKO ON"));
    } else if (jePayloadIskljucen(poruka)) {
      zaustaviMrtvacko();
      posaljiPCLog(F("MQTT: MRTVACKO OFF"));
    }
    return;
  }

  if (tema.indexOf("/service/") >= 0) {
    posaljiPCLog(String(F("MQTT: ignorirana nepodrzana tema ")) + tema);
  }
}

bool jeMQTTPovezan() {
  return mqtt_connected;
}

void reconnectMQTT() {
  if (mqtt_connected || !jeMQTTOmogucen()) {
    return;
  }

  unsigned long sada = millis();
  if (sada - last_mqtt_reconnect < MQTT_RECONNECT_INTERVAL) {
    return;
  }
  last_mqtt_reconnect = sada;

  posaljiESPKomandu("MQTT:CONNECT_SAVED");
  posaljiPCLog(F("MQTT: reconnect pokusaj poslan ESP-u sa spremljenom konfiguracijom"));
}

void inicijalizirajMQTT() {
  mqtt_connected = false;
  mqttPretplatePoslane = false;
  last_mqtt_reconnect = 0;
  last_connection_check = 0;

  if (!jeMQTTOmogucen()) {
    posaljiPCLog(F("MQTT: inicijalizacija preskocena (iskljuceno u postavkama)"));
    return;
  }

  posaljiPCLog(F("MQTT: inicijaliziran minimalni skup od 4 komandne teme"));
  reconnectMQTT();
}

void upravljajMQTT() {
  if (!jeMQTTOmogucen()) {
    if (mqtt_connected) {
      mqtt_connected = false;
      mqttPretplatePoslane = false;
    }
    return;
  }

  unsigned long sada = millis();
  if (sada - last_connection_check > MQTT_STATUS_INTERVAL) {
    last_connection_check = sada;
    posaljiESPKomandu("MQTT:STATUS");
  }

  if (!mqtt_connected) {
    reconnectMQTT();
    return;
  }

  posaljiPretplateAkoTreba();
}

void obradiMQTTLinijuIzESPa(const String& line) {
  if (!jeMQTTOmogucen()) {
    return;
  }

  if (!line.startsWith("MQTT:")) {
    return;
  }

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

  if (line == "MQTT:CONNECTED") {
    if (!mqtt_connected) {
      mqtt_connected = true;
      mqttPretplatePoslane = false;
      posaljiPCLog(F("MQTT: spojen na broker"));
    }
    return;
  }

  if (line == "MQTT:DISCONNECTED") {
    if (mqtt_connected) {
      mqtt_connected = false;
      mqttPretplatePoslane = false;
      posaljiPCLog(F("MQTT: odspojen od brokera"));
    }
    return;
  }

  posaljiPCLog(String(F("MQTT: nepoznata linija s ESP-a: ")) + line);
}
