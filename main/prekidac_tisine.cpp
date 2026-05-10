// prekidac_tisine.cpp - Jedinstveni tihi rezim iz kip prekidaca i uskrsne tisine
#include <Arduino.h>

#include "prekidac_tisine.h"

#include "debouncing.h"
#include "otkucavanje.h"
#include "pc_serial.h"
#include "podesavanja_piny.h"
#include "time_glob.h"
#include "ups_nadzor.h"
#include "zvonjenje.h"

namespace {

bool tihiRezimAktivan = false;
bool rucniPrekidacTisineAktivan = false;
bool webTihiRezimAktivan = false;
bool rucnoPrisilnoIskljucenjeTijekomUskrsa = false;
bool uskrsnaTisinaAktivna = false;

void primijeniLampicuTihogRezima() {
  digitalWrite(PIN_LAMPICA_TIHI_REZIM, (tihiRezimAktivan || jeUPSModAktivan()) ? HIGH : LOW);
}

bool procitajFizickoStanjePrekidacaTisine() {
  return digitalRead(PIN_PREKIDAC_TISINE) == LOW;
}

void primijeniJedinstveniTihiRezim(bool inicijalno) {
  const bool noviTihiRezim =
      rucniPrekidacTisineAktivan ||
      webTihiRezimAktivan ||
      (uskrsnaTisinaAktivna && !rucnoPrisilnoIskljucenjeTijekomUskrsa);
  if (!inicijalno && tihiRezimAktivan == noviTihiRezim) {
    return;
  }

  tihiRezimAktivan = noviTihiRezim;
  primijeniLampicuTihogRezima();
  postaviBlokaduZvonaTihiRezim(tihiRezimAktivan);
  postaviBlokaduOtkucavanjaTihiRezim(tihiRezimAktivan);

  if (inicijalno) {
    posaljiPCLog(tihiRezimAktivan
                     ? F("Tihi rezim: inicijalno AKTIVAN")
                     : F("Tihi rezim: inicijalno NEAKTIVAN"));
  } else {
    posaljiPCLog(tihiRezimAktivan
                     ? F("Tihi rezim: AKTIVAN")
                     : F("Tihi rezim: NEAKTIVAN"));
  }
}

}  // namespace

void inicijalizirajPrekidacTisine() {
  pinMode(PIN_PREKIDAC_TISINE, INPUT_PULLUP);
  pinMode(PIN_LAMPICA_TIHI_REZIM, OUTPUT);
  digitalWrite(PIN_LAMPICA_TIHI_REZIM, LOW);
  rucniPrekidacTisineAktivan = procitajFizickoStanjePrekidacaTisine();
  webTihiRezimAktivan = false;
  rucnoPrisilnoIskljucenjeTijekomUskrsa = false;
  uskrsnaTisinaAktivna = jeUskrsnaTisinaAktivna(dohvatiTrenutnoVrijeme());
  primijeniJedinstveniTihiRezim(true);
}

void osvjeziPrekidacTisine() {
  SwitchState novoStanje = SWITCH_RELEASED;
  if (obradiDebouncedInput(PIN_PREKIDAC_TISINE, 30, &novoStanje)) {
    // Fizicki kip prekidac postaje autoritet nad rucnim tihim modom
    // i gasi prethodno webom zadanu virtualnu blokadu toranjskog sata.
    webTihiRezimAktivan = false;
    rucniPrekidacTisineAktivan = (novoStanje == SWITCH_PRESSED);
    if (rucniPrekidacTisineAktivan) {
      rucnoPrisilnoIskljucenjeTijekomUskrsa = false;
      posaljiPCLog(F("Prekidac tisina: UKLJUCEN"));
    } else {
      rucnoPrisilnoIskljucenjeTijekomUskrsa = uskrsnaTisinaAktivna;
      posaljiPCLog(rucnoPrisilnoIskljucenjeTijekomUskrsa
                       ? F("Prekidac tisina: ISKLJUCEN, rucno nadjacava uskrsnu tisinu")
                       : F("Prekidac tisina: ISKLJUCEN"));
    }
  }

  const bool novaUskrsnaTisina = jeUskrsnaTisinaAktivna(dohvatiTrenutnoVrijeme());
  if (novaUskrsnaTisina != uskrsnaTisinaAktivna) {
    uskrsnaTisinaAktivna = novaUskrsnaTisina;
    if (!uskrsnaTisinaAktivna && !rucniPrekidacTisineAktivan) {
      rucnoPrisilnoIskljucenjeTijekomUskrsa = false;
    }
    posaljiPCLog(uskrsnaTisinaAktivna
                     ? F("Tihi rezim: uskrsna tisina aktivna")
                     : F("Tihi rezim: uskrsna tisina zavrsena"));
  }

  primijeniJedinstveniTihiRezim(false);
}

bool jePrekidacTisineAktivan() {
  return tihiRezimAktivan;
}

void postaviWebTihiRezim(bool aktivan) {
  if (webTihiRezimAktivan == aktivan) {
    return;
  }

  webTihiRezimAktivan = aktivan;
  posaljiPCLog(webTihiRezimAktivan
                   ? F("Tihi rezim: web virtualni prekidac UKLJUCEN")
                   : F("Tihi rezim: web virtualni prekidac ISKLJUCEN"));
  primijeniJedinstveniTihiRezim(false);
}

void osvjeziSignalizacijuTihogRezima() {
  primijeniLampicuTihogRezima();
}
