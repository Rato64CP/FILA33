#pragma once

// system_config.h
//
// Ova datoteka ostaje samo kao kompatibilni podsjetnik za starije biljeske
// o arhitekturi toranjskog sata. Stvarna inicijalizacija i glavna petlja danas
// zive u main/main.ino kroz eksplicitne includeove i izravne pozive modula.
//
// Vazno:
// - MQTT runtime vise nije dio aktivnog firmwarea.
// - EEPROM polja za MQTT i dalje ostaju u eeprom_konstante.h i postavke.cpp
//   radi kompatibilnosti sa starim zapisima.
// - Ovdje se namjerno vise ne odrzavaju zastarjeli makroi inicijalizacije,
//   kako ne bi odudarali od stvarnog ponasanja toranjskog sata.
