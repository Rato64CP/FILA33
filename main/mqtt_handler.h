// mqtt_handler.h – Home Assistant MQTT integration with 9 entities + 8 services
#pragma once

#include <Arduino.h>

// Initialize MQTT handler for Home Assistant integration
void inicijalizirajMQTT();

// Main MQTT update loop (call from main loop)
void upravljajMQTT();

// Publish current system status to Home Assistant
void objaviStatusMQTT();

// Subscribe to Home Assistant commands
void pretplaciNaKomandeHA();

// Process received MQTT commands
void obradiMQTTKomandu(const String& tema, const String& poruka);

// Publish entity state to Home Assistant
void objavi(const String& tema, const String& vrijednost);

// Subscribe to MQTT topic
void pretplati(const String& tema);

// Check MQTT connection status
bool jeMQTTPovezan();

// Reconnect to MQTT broker
void reconnectMQTT();