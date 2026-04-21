#pragma once

// system_config.h
//
// Ova datoteka ostaje samo kao kompatibilni podsjetnik za starije biljeske
// o arhitekturi toranjskog sata. Stvarna inicijalizacija i glavna petlja danas
// zive u main/main.ino kroz eksplicitne includeove i izravne pozive modula.
//
// Vazno:
// - MQTT runtime i pripadna EEPROM polja vise nisu dio aktivnog firmwarea.
// - Ovdje se namjerno vise ne odrzavaju zastarjeli makroi inicijalizacije,
//   kako ne bi odudarali od stvarnog ponasanja toranjskog sata.
