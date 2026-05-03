// ups_nadzor.cpp - Nadzor mreznog napona i UPS moda toranjskog sata
#include <Arduino.h>

#include "ups_nadzor.h"

#include "podesavanja_piny.h"
#include "postavke.h"
#include "pc_serial.h"
#include "lcd_display.h"
#include "prekidac_tisine.h"
#include "zvonjenje.h"
#include "otkucavanje.h"
#include "slavljenje_mrtvacko.h"

namespace {

constexpr unsigned long UPS_DEBOUNCE_MS = 250UL;

bool zadnjeSirovoStanjeMreze = true;
bool stabilnoStanjeMreze = true;
bool upsModAktivan = false;
bool blokadePrimijenjene = false;
unsigned long zadnjaPromjenaSirovogStanjaMs = 0UL;

bool procitajSirovoStanjeMreze() {
  // Optokapler je predviden kao open-collector prema masi:
  // LOW = mreza prisutna, HIGH = nema mreze / rad s UPS-a.
  return digitalRead(PIN_NADZOR_MREZE) == LOW;
}

void primijeniUPSBlokade(bool aktivno) {
  postaviBlokaduZvonaUPS(aktivno);
  postaviBlokaduOtkucavanjaUPS(aktivno);

  if (aktivno) {
    zaustaviSlavljenje();
    zaustaviMrtvacko();
  }
}

void azurirajUPSModIzStabilnogStanja(bool pocetnaSinkronizacija) {
  const bool noviUPSModAktivan = jeUPSModOmogucen() && !stabilnoStanjeMreze;
  if (upsModAktivan == noviUPSModAktivan && blokadePrimijenjene == noviUPSModAktivan) {
    return;
  }

  upsModAktivan = noviUPSModAktivan;
  primijeniUPSBlokade(upsModAktivan);
  blokadePrimijenjene = upsModAktivan;
  osvjeziSignalizacijuTihogRezima();
  prisiliOsvjezavanjeGlavnogPrikazaLCD();

  if (pocetnaSinkronizacija) {
    if (upsModAktivan) {
      posaljiPCLog(
          F("UPS mod: pri pokretanju nema mreze, blokiram zvona, cekice i kazaljke; ploca ostaje aktivna"));
    }
    return;
  }

  if (upsModAktivan) {
    posaljiPCLog(
        F("UPS mod: nestanak mreze, blokiram zvona, cekice i kazaljke; ploca ostaje aktivna"));
  } else {
    posaljiPCLog(
        F("UPS mod: mreza vracena ili je nadzor iskljucen, ponovno dozvoljavam zvona, cekice i kazaljke"));
  }
}

}  // namespace

void inicijalizirajUPSNadzor() {
  pinMode(PIN_NADZOR_MREZE, INPUT_PULLUP);

  const bool sirovoStanje = procitajSirovoStanjeMreze();
  zadnjeSirovoStanjeMreze = sirovoStanje;
  stabilnoStanjeMreze = sirovoStanje;
  zadnjaPromjenaSirovogStanjaMs = millis();
  upsModAktivan = false;
  blokadePrimijenjene = false;

  azurirajUPSModIzStabilnogStanja(true);
}

void osvjeziUPSNadzor() {
  const bool sirovoStanje = procitajSirovoStanjeMreze();
  if (sirovoStanje != zadnjeSirovoStanjeMreze) {
    zadnjeSirovoStanjeMreze = sirovoStanje;
    zadnjaPromjenaSirovogStanjaMs = millis();
  }

  if (stabilnoStanjeMreze != zadnjeSirovoStanjeMreze &&
      (millis() - zadnjaPromjenaSirovogStanjaMs) >= UPS_DEBOUNCE_MS) {
    stabilnoStanjeMreze = zadnjeSirovoStanjeMreze;
  }

  azurirajUPSModIzStabilnogStanja(false);
}

bool jeUPSModAktivan() {
  return upsModAktivan;
}
