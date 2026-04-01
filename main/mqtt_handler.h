// mqtt_handler.h - Pojednostavljena MQTT integracija za toranjski sat.
#pragma once

#include <Arduino.h>

// Inicijalizacija MQTT sloja za 4 komandne teme.
void inicijalizirajMQTT();

// Glavna MQTT obrada iz glavne petlje toranjskog sata.
void upravljajMQTT();

// Obrada primljene MQTT komande s ESP8266 mosta.
void obradiMQTTKomandu(const String& tema, const String& poruka);

// Trenutni status MQTT veze prema brokeru.
bool jeMQTTPovezan();

// Pokretanje reconnect pokušaja na spremljenu konfiguraciju na ESP-u.
void reconnectMQTT();

// Obrada linije s ESP-a koju je procitao serijski sloj (Serial3 owner).
void obradiMQTTLinijuIzESPa(const String& line);
