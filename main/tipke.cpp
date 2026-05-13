// tipke.cpp - Upravljanje lokalnim tipkama i menijem toranjskog sata
#include <Arduino.h>
#include <avr/pgmspace.h>
#include "tipke.h"
#include "lcd_display.h"
#include "pc_serial.h"
#include "debouncing.h"
#include "esp_serial.h"
#include "menu_system.h"
#include "podesavanja_piny.h"
#include "power_recovery.h"
#include "watchdog.h"

namespace {

static const uint8_t BROJ_LOGICKIH_TIPKI = 6;
static const uint8_t DEBOUNCE_TIPKE_MS = 30;
static const unsigned long DUGI_PRITISAK_SETUP_KOMBINACIJE_MS = 1500UL;
static const unsigned long DUGI_PRITISAK_RESET_KOMBINACIJE_MS = 5000UL;
static const unsigned long DUGI_PRITISAK_SAFE_MODE_OTKLJUCAVANJE_MS = 5000UL;

struct DefinicijaTipke {
  uint8_t pin;
  KeyEvent event;
  const char* naziv;
};

struct DebounceTipke {
  bool zadnjeSirovoPritisnuto;
  bool stabilnoPritisnuto;
  unsigned long zadnjaPromjenaMs;
};

static const DefinicijaTipke TIPKE[BROJ_LOGICKIH_TIPKI] = {
  {PIN_TIPKA_GORE, KEY_UP, "GORE"},
  {PIN_TIPKA_DOLJE, KEY_DOWN, "DOLJE"},
  {PIN_TIPKA_LIJEVO, KEY_LEFT, "LIJEVO"},
  {PIN_TIPKA_DESNO, KEY_RIGHT, "DESNO"},
  {PIN_TIPKA_DA, KEY_SELECT, "DA"},
  {PIN_TIPKA_NE, KEY_BACK, "NE"}
};

static DebounceTipke debounceTipki[BROJ_LOGICKIH_TIPKI];
static bool setupKombinacijaAktivna = false;
static bool setupKombinacijaObradena = false;
static unsigned long setupKombinacijaPocetakMs = 0;
static bool resetKombinacijaAktivna = false;
static bool resetKombinacijaObradena = false;
static unsigned long resetKombinacijaPocetakMs = 0;
static bool safeModeSelectZadnjeSirovoPritisnuto = false;
static bool safeModeSelectStabilnoPritisnuto = false;
static bool safeModeSelectOtkljucavanjeObradeno = false;
static unsigned long safeModeSelectZadnjaPromjenaMs = 0;
static unsigned long safeModeSelectVrijemePritiskaMs = 0;

static bool jeTipkaSirovoPritisnuta(uint8_t indeksTipke) {
  if (indeksTipke >= BROJ_LOGICKIH_TIPKI) {
    return false;
  }
  return (digitalRead(TIPKE[indeksTipke].pin) == LOW);
}

static bool jeResetKombinacijaSirovoPritisnuta() {
  return digitalRead(PIN_TIPKA_GORE) == LOW &&
         digitalRead(PIN_TIPKA_DOLJE) == LOW;
}

static void inicijalizirajPinoveTipki() {
  for (uint8_t i = 0; i < BROJ_LOGICKIH_TIPKI; ++i) {
    pinMode(TIPKE[i].pin, INPUT_PULLUP);
  }
}

static void inicijalizirajDebounceTipki() {
  for (uint8_t i = 0; i < BROJ_LOGICKIH_TIPKI; ++i) {
    debounceTipki[i].zadnjeSirovoPritisnuto = false;
    debounceTipki[i].stabilnoPritisnuto = false;
    debounceTipki[i].zadnjaPromjenaMs = 0;
  }
}

static void obradiPritisakTipke(const DefinicijaTipke& tipka) {
  if ((tipka.event == KEY_UP || tipka.event == KEY_DOWN) &&
      jeResetKombinacijaSirovoPritisnuta()) {
    return;
  }

  if (tipka.event == KEY_SELECT && jeLatchedFaultAktivan()) {
    if (potvrdiLatchedFault()) {
      posaljiPCLog(F("Power Recovery: latched fault potvrdjen tipkom SELECT"));
    }
    return;
  }

  if (tipka.event == KEY_SELECT && jeUpozorenjeRtcBaterijeAktivno()) {
    potvrdiUpozorenjeRtcBaterije();
    posaljiPCLog(F("RTC: upozorenje za bateriju potvrdjeno tipkom SELECT"));
    return;
  }

  char log[48];
  snprintf_P(log, sizeof(log), PSTR("Tipka izbornika: %s"), tipka.naziv);
  posaljiPCLog(log);
  obradiKluc(tipka.event);
}

static void obradiLogickuTipku(uint8_t indeksTipke, unsigned long sadaMs) {
  DebounceTipke& stanje = debounceTipki[indeksTipke];
  const bool sirovoPritisnuto = jeTipkaSirovoPritisnuta(indeksTipke);

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
    obradiPritisakTipke(TIPKE[indeksTipke]);
  }
}

static bool jeTipkaStabilnoPritisnuta(KeyEvent trazeniEvent) {
  for (uint8_t i = 0; i < BROJ_LOGICKIH_TIPKI; ++i) {
    if (TIPKE[i].event == trazeniEvent) {
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

static void provjeriResetKombinacijuGoreDolje(unsigned long sadaMs) {
  const bool gorePritisnuto = jeTipkaStabilnoPritisnuta(KEY_UP);
  const bool doljePritisnuto = jeTipkaStabilnoPritisnuta(KEY_DOWN);

  if (!gorePritisnuto || !doljePritisnuto) {
    resetKombinacijaAktivna = false;
    resetKombinacijaObradena = false;
    resetKombinacijaPocetakMs = 0;
    return;
  }

  if (!resetKombinacijaAktivna) {
    resetKombinacijaAktivna = true;
    resetKombinacijaObradena = false;
    resetKombinacijaPocetakMs = sadaMs;
    posaljiPCLog(F("Tipke: gore+dolje drzim -> servisni reset ako potraje 5 sekundi"));
    return;
  }

  if (resetKombinacijaObradena ||
      (sadaMs - resetKombinacijaPocetakMs) < DUGI_PRITISAK_RESET_KOMBINACIJE_MS) {
    return;
  }

  resetKombinacijaObradena = true;
  posaljiPCLog(F("Tipke: gore+dolje (dugo) -> pokrecem kontrolirani reset toranjskog sata"));
  zatraziResetWatchdogom();
}

}  // namespace

void inicijalizirajTipke() {
  inicijalizirajDebouncing();
  inicijalizirajPinoveTipki();
  inicijalizirajDebounceTipki();

  posaljiPCLog(F("Tipke: inicijalizirano 6 direktnih tipki lokalnog izbornika"));
}

void provjeriTipke() {
  const unsigned long sadaMs = millis();
  for (uint8_t i = 0; i < BROJ_LOGICKIH_TIPKI; ++i) {
    obradiLogickuTipku(i, sadaMs);
  }

  provjeriSetupKombinacijuLijevoDesno(sadaMs);
  provjeriResetKombinacijuGoreDolje(sadaMs);
}

bool provjeriOtkljucavanjeSafeMode() {
  const unsigned long sadaMs = millis();
  const bool selectSirovoPritisnuto = (digitalRead(PIN_TIPKA_DA) == LOW);

  if (selectSirovoPritisnuto != safeModeSelectZadnjeSirovoPritisnuto) {
    safeModeSelectZadnjeSirovoPritisnuto = selectSirovoPritisnuto;
    safeModeSelectZadnjaPromjenaMs = sadaMs;
  }

  if ((sadaMs - safeModeSelectZadnjaPromjenaMs) < DEBOUNCE_TIPKE_MS) {
    return false;
  }

  if (selectSirovoPritisnuto != safeModeSelectStabilnoPritisnuto) {
    safeModeSelectStabilnoPritisnuto = selectSirovoPritisnuto;
    if (safeModeSelectStabilnoPritisnuto) {
      safeModeSelectVrijemePritiskaMs = sadaMs;
      safeModeSelectOtkljucavanjeObradeno = false;
    } else {
      safeModeSelectOtkljucavanjeObradeno = false;
    }
    return false;
  }

  if (!safeModeSelectStabilnoPritisnuto || safeModeSelectOtkljucavanjeObradeno) {
    return false;
  }

  if ((sadaMs - safeModeSelectVrijemePritiskaMs) < DUGI_PRITISAK_SAFE_MODE_OTKLJUCAVANJE_MS) {
    return false;
  }

  safeModeSelectOtkljucavanjeObradeno = true;
  posaljiPCLog(F("Tipka DA / SELECT (dugo): zahtjev za servisno otkljucavanje safe mode-a"));
  return true;
}
