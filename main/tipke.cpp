// tipke.cpp - Upravljanje matricnom tipkovnicom i menijem toranjskog sata
#include <Arduino.h>
#include <avr/pgmspace.h>
#include "tipke.h"
#include "lcd_display.h"
#include "pc_serial.h"
#include "debouncing.h"
#include "esp_serial.h"
#include "menu_system.h"
#include "podesavanja_piny.h"
#include "zvonjenje.h"
#include "slavljenje_mrtvacko.h"

namespace {

static const uint8_t BROJ_REDAKA = 4;
static const uint8_t BROJ_STUPACA = 5;
static const uint8_t BROJ_LOGICKIH_TIPKI = 16;
static const uint8_t DEBOUNCE_TIPKE_MS = 30;
static const unsigned long DUGI_PRITISAK_TIPKE_MS = 1200UL;
static const unsigned long DUGI_PRITISAK_SETUP_KOMBINACIJE_MS = 1500UL;

static const uint8_t PINOVI_REDAKA[BROJ_REDAKA] = {
  PIN_KEYPAD_ROW_0,
  PIN_KEYPAD_ROW_1,
  PIN_KEYPAD_ROW_2,
  PIN_KEYPAD_ROW_3
};

static const uint8_t PINOVI_STUPACA[BROJ_STUPACA] = {
  PIN_KEYPAD_COL_0,
  PIN_KEYPAD_COL_1,
  PIN_KEYPAD_COL_2,
  PIN_KEYPAD_COL_3,
  PIN_KEYPAD_COL_4
};

struct MapiranjeTipke {
  uint8_t redak;
  uint8_t stupac;
  KeyEvent event;
  const char* naziv;
};

struct DebounceMatrice {
  bool zadnjeSirovoPritisnuto;
  bool stabilnoPritisnuto;
  bool odgodenaObradaNaOtpustanju;
  bool dugoZadrzavanjeObradeno;
  unsigned long zadnjaPromjenaMs;
  unsigned long vrijemePritiskaMs;
};

// Raspored je mapiran prema stvarnom ocitanju tipkovnice na toranjskom satu.
static const MapiranjeTipke MAPIRANJA_TIPKI[BROJ_LOGICKIH_TIPKI] = {
  {3, 3, KEY_UP, "UP"},
  {3, 2, KEY_DOWN, "DOWN"},
  {0, 0, KEY_LEFT, "LEFT"},
  {2, 0, KEY_RIGHT, "RIGHT"},
  {3, 0, KEY_SELECT, "ENT"},
  {3, 1, KEY_BACK, "ESC"},
  {1, 0, KEY_DIGIT_0, "0"},
  {0, 3, KEY_DIGIT_1, "1"},
  {1, 3, KEY_DIGIT_2, "2"},
  {2, 3, KEY_DIGIT_3, "3"},
  {0, 2, KEY_DIGIT_4, "4"},
  {1, 2, KEY_DIGIT_5, "5"},
  {2, 2, KEY_DIGIT_6, "6"},
  {0, 1, KEY_DIGIT_7, "7"},
  {1, 1, KEY_DIGIT_8, "8"},
  {2, 1, KEY_DIGIT_9, "9"}
};

static DebounceMatrice debounceTipki[BROJ_LOGICKIH_TIPKI];
static bool setupKombinacijaAktivna = false;
static bool setupKombinacijaObradena = false;
static unsigned long setupKombinacijaPocetakMs = 0;

static void postaviRetkeUMirnoStanje() {
  for (uint8_t i = 0; i < BROJ_REDAKA; ++i) {
    digitalWrite(PINOVI_REDAKA[i], LOW);
    pinMode(PINOVI_REDAKA[i], INPUT);
  }
}

static void inicijalizirajMatricnePinove() {
  postaviRetkeUMirnoStanje();
  for (uint8_t i = 0; i < BROJ_STUPACA; ++i) {
    pinMode(PINOVI_STUPACA[i], INPUT_PULLUP);
  }
}

static void inicijalizirajDebounceMatrice() {
  for (uint8_t i = 0; i < BROJ_LOGICKIH_TIPKI; ++i) {
    debounceTipki[i].zadnjeSirovoPritisnuto = false;
    debounceTipki[i].stabilnoPritisnuto = false;
    debounceTipki[i].odgodenaObradaNaOtpustanju = false;
    debounceTipki[i].dugoZadrzavanjeObradeno = false;
    debounceTipki[i].zadnjaPromjenaMs = 0;
    debounceTipki[i].vrijemePritiskaMs = 0;
  }
}

static void ocitajMatricu(bool stanja[BROJ_REDAKA][BROJ_STUPACA]) {
  for (uint8_t redak = 0; redak < BROJ_REDAKA; ++redak) {
    postaviRetkeUMirnoStanje();
    pinMode(PINOVI_REDAKA[redak], OUTPUT);
    digitalWrite(PINOVI_REDAKA[redak], LOW);
    delayMicroseconds(5);

    for (uint8_t stupac = 0; stupac < BROJ_STUPACA; ++stupac) {
      stanja[redak][stupac] = (digitalRead(PINOVI_STUPACA[stupac]) == LOW);
    }
  }

  postaviRetkeUMirnoStanje();
}

static void obradiPritisakTipke(const MapiranjeTipke& tipka) {
  if (tipka.event == KEY_SELECT && jeUpozorenjeRtcBaterijeAktivno()) {
    potvrdiUpozorenjeRtcBaterije();
    posaljiPCLog(F("RTC: upozorenje za bateriju potvrdjeno tipkom SELECT"));
    return;
  }

  char log[48];
  snprintf_P(log, sizeof(log), PSTR("Tipka matrice: %s"), tipka.naziv);
  posaljiPCLog(log);
  obradiKluc(tipka.event);
}

static bool jeTipkaZaDugoZadrzavanje(const MapiranjeTipke& tipka) {
  return tipka.event == KEY_DIGIT_1 || tipka.event == KEY_DIGIT_2;
}

static bool trebaOdgoditiKratkiPritisak(const MapiranjeTipke& tipka) {
  return jeTipkaZaDugoZadrzavanje(tipka) && dohvatiMenuState() == MENU_STATE_DISPLAY_TIME;
}

static void obradiDugoZadrzavanjeTipke(const MapiranjeTipke& tipka) {
  if (tipka.event == KEY_DIGIT_1) {
    const bool biloAktivno = jeSlavljenjeUTijeku();
    if (biloAktivno) {
      zaustaviSlavljenje();
    } else {
      zapocniSlavljenje();
    }

    const bool sadaAktivno = jeSlavljenjeUTijeku();
    if (sadaAktivno != biloAktivno) {
      posaljiPCLog(sadaAktivno
                       ? F("Tipka matrice 1 (dugo): slavljenje pokrenuto")
                       : F("Tipka matrice 1 (dugo): slavljenje zaustavljeno"));
    } else {
      posaljiPCLog(F("Tipka matrice 1 (dugo): slavljenje nije promijenjeno"));
    }
    return;
  }

  if (tipka.event == KEY_DIGIT_2) {
    const bool biloAktivno = jeMrtvackoUTijeku();
    if (biloAktivno) {
      zaustaviMrtvacko();
    } else {
      zapocniMrtvacko();
    }

    const bool sadaAktivno = jeMrtvackoUTijeku();
    if (sadaAktivno != biloAktivno) {
      posaljiPCLog(sadaAktivno
                       ? F("Tipka matrice 2 (dugo): mrtvacko pokrenuto")
                       : F("Tipka matrice 2 (dugo): mrtvacko zaustavljeno"));
    } else {
      posaljiPCLog(F("Tipka matrice 2 (dugo): mrtvacko nije promijenjeno"));
    }
  }
}

static void obradiLogickuTipku(uint8_t indeksTipke, bool sirovoPritisnuto, unsigned long sadaMs) {
  DebounceMatrice& stanje = debounceTipki[indeksTipke];

  if (sirovoPritisnuto != stanje.zadnjeSirovoPritisnuto) {
    stanje.zadnjeSirovoPritisnuto = sirovoPritisnuto;
    stanje.zadnjaPromjenaMs = sadaMs;
  }

  if ((sadaMs - stanje.zadnjaPromjenaMs) < DEBOUNCE_TIPKE_MS) {
    return;
  }

  if (sirovoPritisnuto == stanje.stabilnoPritisnuto) {
    return;
  }

  stanje.stabilnoPritisnuto = sirovoPritisnuto;
  if (stanje.stabilnoPritisnuto) {
    stanje.vrijemePritiskaMs = sadaMs;
    stanje.dugoZadrzavanjeObradeno = false;
    stanje.odgodenaObradaNaOtpustanju = trebaOdgoditiKratkiPritisak(MAPIRANJA_TIPKI[indeksTipke]);

    if (!stanje.odgodenaObradaNaOtpustanju) {
      obradiPritisakTipke(MAPIRANJA_TIPKI[indeksTipke]);
    }
  } else {
    if (stanje.odgodenaObradaNaOtpustanju && !stanje.dugoZadrzavanjeObradeno) {
      obradiPritisakTipke(MAPIRANJA_TIPKI[indeksTipke]);
    }

    stanje.odgodenaObradaNaOtpustanju = false;
    stanje.dugoZadrzavanjeObradeno = false;
    stanje.vrijemePritiskaMs = 0;
  }
}

static void provjeriDugaZadrzavanjaTipki(unsigned long sadaMs) {
  for (uint8_t i = 0; i < BROJ_LOGICKIH_TIPKI; ++i) {
    DebounceMatrice& stanje = debounceTipki[i];
    const MapiranjeTipke& tipka = MAPIRANJA_TIPKI[i];

    if (!stanje.stabilnoPritisnuto || !stanje.odgodenaObradaNaOtpustanju ||
        stanje.dugoZadrzavanjeObradeno || !jeTipkaZaDugoZadrzavanje(tipka)) {
      continue;
    }

    if ((sadaMs - stanje.vrijemePritiskaMs) < DUGI_PRITISAK_TIPKE_MS) {
      continue;
    }

    stanje.dugoZadrzavanjeObradeno = true;
    obradiDugoZadrzavanjeTipke(tipka);
  }
}

static bool jeTipkaStabilnoPritisnuta(KeyEvent trazeniEvent) {
  for (uint8_t i = 0; i < BROJ_LOGICKIH_TIPKI; ++i) {
    if (MAPIRANJA_TIPKI[i].event == trazeniEvent) {
      return debounceTipki[i].stabilnoPritisnuto;
    }
  }
  return false;
}

static void provjeriSetupKombinacijuLijevoDesno(unsigned long sadaMs) {
  if (dohvatiMenuState() != MENU_STATE_DISPLAY_TIME) {
    setupKombinacijaAktivna = false;
    setupKombinacijaObradena = false;
    setupKombinacijaPocetakMs = 0;
    return;
  }

  const bool lijevoPritisnuto = jeTipkaStabilnoPritisnuta(KEY_LEFT);
  const bool desnoPritisnuto = jeTipkaStabilnoPritisnuta(KEY_RIGHT);

  if (!lijevoPritisnuto || !desnoPritisnuto) {
    setupKombinacijaAktivna = false;
    setupKombinacijaObradena = false;
    setupKombinacijaPocetakMs = 0;
    return;
  }

  if (!setupKombinacijaAktivna) {
    setupKombinacijaAktivna = true;
    setupKombinacijaObradena = false;
    setupKombinacijaPocetakMs = sadaMs;
    return;
  }

  if (setupKombinacijaObradena ||
      (sadaMs - setupKombinacijaPocetakMs) < DUGI_PRITISAK_SETUP_KOMBINACIJE_MS) {
    return;
  }

  setupKombinacijaObradena = true;
  posaljiESPKomandu("SETUPAP:START");
  posaljiPCLog(F("Tipke: lijevo+desno (dugo) -> zahtjev za pokretanje setup WiFi mreze"));
}

}  // namespace

void inicijalizirajTipke() {
  inicijalizirajDebouncing();
  inicijalizirajMatricnePinove();
  inicijalizirajDebounceMatrice();

  posaljiPCLog(F("Tipke: inicijalizirana 4x5 matricna tipkovnica (strelice, Ent, Esc, 0-9)"));
}

void provjeriTipke() {
  bool stanjaMatrice[BROJ_REDAKA][BROJ_STUPACA];
  ocitajMatricu(stanjaMatrice);

  const unsigned long sadaMs = millis();
  for (uint8_t i = 0; i < BROJ_LOGICKIH_TIPKI; ++i) {
    const MapiranjeTipke& tipka = MAPIRANJA_TIPKI[i];
    obradiLogickuTipku(i, stanjaMatrice[tipka.redak][tipka.stupac], sadaMs);
  }

  provjeriDugaZadrzavanjaTipki(sadaMs);
  provjeriSetupKombinacijuLijevoDesno(sadaMs);
}
