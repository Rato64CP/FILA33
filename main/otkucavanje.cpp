// otkucavanje.cpp - Upravljanje redovnim satnim i polusatnim otkucavanjem
// Mehanicko upravljanje cekicima preko relejnih impulsa za toranjski sat

#include <Arduino.h>
#include <avr/pgmspace.h>
#include <RTClib.h>

#include "otkucavanje.h"

#include "slavljenje_mrtvacko.h"
#include "otkucavanje_interno.h"
#include "sunceva_automatika.h"
#include "zvonjenje.h"
#include "podesavanja_piny.h"
#include "prekidac_tisine.h"
#include "time_glob.h"
#include "postavke.h"
#include "lcd_display.h"
#include "pc_serial.h"

namespace {

const unsigned long TRAJANJE_IMPULSA_CEKICA_DEFAULT = 150UL;
const unsigned long PAUZA_MEZI_UDARACA_DEFAULT = 400UL;
const unsigned long SATNO_OTKUCAJ_PAUZA_MS = 2000UL;  // 2 s izmedu satnih udaraca
const unsigned long SIGURNOSNI_MAX_TRAJANJE_CEKICA_MS = 150UL;
const unsigned long SIGURNOSNI_MAX_OVERRIDE_TRAJANJE_CEKICA_MS = 300UL;

enum VrstaOtkucavanja {
  OTKUCAVANJE_NONE = 0,
  OTKUCAVANJE_CEKIC1 = 1,
  OTKUCAVANJE_CEKIC2 = 2
};

enum ModOtkucavanja {
  MOD_OTKUCAJ_ISKLJUCEN = 0,
  MOD_OTKUCAJ_KLASICNI = 1,
  MOD_OTKUCAJ_KVARTALNI = 2
};

struct OtkucavanjeStanje {
  VrstaOtkucavanja vrsta;
  int preostaliUdarci;
  unsigned long vrijemePocetkaMs;
  bool cekicAktivan;
  int aktivniPin;
  unsigned long vrijemeZadnjeAktivacije;
  unsigned long trajanjeImpulsaMs;
  unsigned long pauzaIzmeduUdaracaMs;
};

struct SigurnostCekicaStanje {
  bool aktivan[2];
  unsigned long vrijemeAktivacijeMs[2];
  unsigned long trajanjeMs[2];
};

OtkucavanjeStanje otkucavanje = {
    OTKUCAVANJE_NONE,
    0,
    0UL,
    false,
    -1,
    0UL,
    TRAJANJE_IMPULSA_CEKICA_DEFAULT,
    PAUZA_MEZI_UDARACA_DEFAULT};
bool blokadaOtkucavanja = false;
bool globalnaBlokadaOtkucavanja = false;
SigurnostCekicaStanje sigurnostCekica = {{false, false}, {0UL, 0UL}, {0UL, 0UL}};

void kopirajFlashTekst(const __FlashStringHelper* tekst, char* odrediste, size_t velicina) {
  if (odrediste == nullptr || velicina == 0) {
    return;
  }

  if (tekst == nullptr) {
    odrediste[0] = '\0';
    return;
  }

  strncpy_P(odrediste, reinterpret_cast<const char*>(tekst), velicina - 1);
  odrediste[velicina - 1] = '\0';
}

int dohvatiIndeksCekicaZaPin(int pin) {
  if (pin == PIN_CEKIC_MUSKI) {
    return 0;
  }
  if (pin == PIN_CEKIC_ZENSKI) {
    return 1;
  }
  return -1;
}

unsigned long normalizirajSigurnoTrajanjeCekicaMs(unsigned long trazenoTrajanjeMs,
                                                  unsigned long sigurnosniMaxMs) {
  if (trazenoTrajanjeMs == 0UL) {
    trazenoTrajanjeMs = TRAJANJE_IMPULSA_CEKICA_DEFAULT;
  }
  if (trazenoTrajanjeMs > sigurnosniMaxMs) {
    trazenoTrajanjeMs = sigurnosniMaxMs;
  }
  return trazenoTrajanjeMs;
}

void aktivirajCekic_Internal(int pin,
                             unsigned long trazenoTrajanjeMs,
                             unsigned long sigurnosniMaxMs = SIGURNOSNI_MAX_TRAJANJE_CEKICA_MS) {
  const int indeks = dohvatiIndeksCekicaZaPin(pin);
  if (indeks < 0) {
    return;
  }

  digitalWrite(pin, HIGH);
  sigurnostCekica.aktivan[indeks] = true;
  sigurnostCekica.vrijemeAktivacijeMs[indeks] = millis();
  sigurnostCekica.trajanjeMs[indeks] =
      normalizirajSigurnoTrajanjeCekicaMs(trazenoTrajanjeMs, sigurnosniMaxMs);
}

void deaktivirajCekic_Internal(int pin) {
  const int indeks = dohvatiIndeksCekicaZaPin(pin);
  if (indeks < 0) {
    return;
  }

  digitalWrite(pin, LOW);
  sigurnostCekica.aktivan[indeks] = false;
  sigurnostCekica.vrijemeAktivacijeMs[indeks] = 0UL;
  sigurnostCekica.trajanjeMs[indeks] = 0UL;
}

void ponistiAktivnoOtkucavanje(bool jeOtkazivanje, const __FlashStringHelper* razlog = nullptr) {
  if (otkucavanje.aktivniPin >= 0) {
    deaktivirajCekic_Internal(otkucavanje.aktivniPin);
  }
  otkucavanje.vrsta = OTKUCAVANJE_NONE;
  otkucavanje.preostaliUdarci = 0;
  otkucavanje.cekicAktivan = false;
  otkucavanje.aktivniPin = -1;
  otkucavanje.vrijemePocetkaMs = 0UL;
  otkucavanje.vrijemeZadnjeAktivacije = 0UL;
  otkucavanje.trajanjeImpulsaMs = TRAJANJE_IMPULSA_CEKICA_DEFAULT;
  otkucavanje.pauzaIzmeduUdaracaMs = PAUZA_MEZI_UDARACA_DEFAULT;

  if (razlog != nullptr) {
    char razlogBuffer[56];
    char log[96];
    kopirajFlashTekst(razlog, razlogBuffer, sizeof(razlogBuffer));
    snprintf_P(log, sizeof(log), PSTR("%s (%s)"),
               jeOtkazivanje ? "Otkucavanje: operacija otkazana"
                             : "Otkucavanje: sekvenca dovrsena",
               razlogBuffer);
    posaljiPCLog(log);
  } else {
    posaljiPCLog(jeOtkazivanje ? F("Otkucavanje: operacija otkazana")
                               : F("Otkucavanje: sekvenca dovrsena"));
  }
}

void pokreniSljedeciUdarac() {
  if (otkucavanje.preostaliUdarci <= 0) {
    ponistiAktivnoOtkucavanje(false, F("nema preostalih udaraca"));
    return;
  }

  aktivirajCekic_Internal(otkucavanje.aktivniPin, otkucavanje.trajanjeImpulsaMs);
  otkucavanje.cekicAktivan = true;
  otkucavanje.vrijemeZadnjeAktivacije = millis();
  otkucavanje.preostaliUdarci--;

  char log[40];
  snprintf_P(log, sizeof(log), PSTR("Udarac: preostalo=%d"), otkucavanje.preostaliUdarci);
  posaljiPCLog(log);
}

void pokreniSekvencuOtkucavanja(VrstaOtkucavanja vrsta,
                                int brojUdaraca,
                                int pinCekica,
                                unsigned long trajanjeImpulsaMs,
                                unsigned long pauzaIzmeduUdaracaMs,
                                const __FlashStringHelper* opisSekvence) {
  if (brojUdaraca < 1) {
    return;
  }

  if (otkucavanje.vrsta != OTKUCAVANJE_NONE) {
    return;
  }

  if (!jeOperacijaCekicaDozvoljena()) {
    posaljiPCLog(F("Otkucavanje: blokirano (inercija ili user blok)"));
    return;
  }

  otkucavanje.vrsta = vrsta;
  otkucavanje.preostaliUdarci = brojUdaraca;
  otkucavanje.aktivniPin = pinCekica;
  otkucavanje.vrijemePocetkaMs = 0UL;
  otkucavanje.cekicAktivan = false;
  otkucavanje.vrijemeZadnjeAktivacije = 0UL;
  otkucavanje.trajanjeImpulsaMs =
      normalizirajSigurnoTrajanjeCekicaMs(trajanjeImpulsaMs, SIGURNOSNI_MAX_TRAJANJE_CEKICA_MS);
  otkucavanje.pauzaIzmeduUdaracaMs =
      (pauzaIzmeduUdaracaMs == 0UL) ? PAUZA_MEZI_UDARACA_DEFAULT : pauzaIzmeduUdaracaMs;

  char opisSekvenceBuffer[40];
  char log[96];
  kopirajFlashTekst(opisSekvence, opisSekvenceBuffer, sizeof(opisSekvenceBuffer));
  snprintf_P(log, sizeof(log), PSTR("Otkucavanje: %s, udaraca=%d"), opisSekvenceBuffer, brojUdaraca);
  posaljiPCLog(log);

  if (pinCekica == PIN_CEKIC_MUSKI) {
    signalizirajHammer1_Active();
  } else if (pinCekica == PIN_CEKIC_ZENSKI) {
    signalizirajHammer2_Active();
  }

  pokreniSljedeciUdarac();
}

void primijeniSigurnosniLimitCekica(unsigned long sadaMs) {
  for (int indeks = 0; indeks < 2; ++indeks) {
    if (!sigurnostCekica.aktivan[indeks]) {
      continue;
    }

    const unsigned long proteklo = sadaMs - sigurnostCekica.vrijemeAktivacijeMs[indeks];
    if (proteklo < sigurnostCekica.trajanjeMs[indeks]) {
      continue;
    }

    if (indeks == 0) {
      deaktivirajCekic_Internal(PIN_CEKIC_MUSKI);
    } else {
      deaktivirajCekic_Internal(PIN_CEKIC_ZENSKI);
    }
  }

  if (otkucavanje.cekicAktivan && !jeCekicSigurnosnoAktivanZaPosebniNacin(otkucavanje.aktivniPin)) {
    otkucavanje.cekicAktivan = false;
    otkucavanje.vrijemeZadnjeAktivacije = sadaMs;
  }
}

}  // namespace

bool jeOperacijaCekicaDozvoljena() {
  if (blokadaOtkucavanja || globalnaBlokadaOtkucavanja) {
    return false;
  }

  if (jeLiInerciaAktivna()) {
    return false;
  }

  return true;
}

void prekiniAktivnoOtkucavanjeZbogPosebnogNacina(const __FlashStringHelper* razlog) {
  if (otkucavanje.vrsta == OTKUCAVANJE_NONE) {
    return;
  }

  ponistiAktivnoOtkucavanje(true, razlog);
}

unsigned long dohvatiTrajanjeImpulsaCekicaZaPosebneNacineMs() {
  return normalizirajSigurnoTrajanjeCekicaMs(
      dohvatiTrajanjeImpulsaCekica(), SIGURNOSNI_MAX_TRAJANJE_CEKICA_MS);
}

void deaktivirajObaCekicaZaPosebniNacin() {
  deaktivirajCekic_Internal(PIN_CEKIC_MUSKI);
  deaktivirajCekic_Internal(PIN_CEKIC_ZENSKI);
}

void aktivirajJedanCekicZaPosebniNacin(int pin, unsigned long trazenoTrajanjeMs) {
  deaktivirajObaCekicaZaPosebniNacin();
  // Posebni nacini toranjskog sata smiju zatraziti dulji impuls od korisnicke
  // postavke, ali i dalje ostaju pod strogim sigurnosnim limitom releja.
  aktivirajCekic_Internal(pin, trazenoTrajanjeMs, SIGURNOSNI_MAX_OVERRIDE_TRAJANJE_CEKICA_MS);

  if (pin == PIN_CEKIC_MUSKI) {
    signalizirajHammer1_Active();
  } else if (pin == PIN_CEKIC_ZENSKI) {
    signalizirajHammer2_Active();
  }
}

void aktivirajObaCekicaZaPosebniNacin(unsigned long trazenoTrajanjeMs) {
  deaktivirajObaCekicaZaPosebniNacin();
  aktivirajCekic_Internal(
      PIN_CEKIC_MUSKI, trazenoTrajanjeMs, SIGURNOSNI_MAX_OVERRIDE_TRAJANJE_CEKICA_MS);
  aktivirajCekic_Internal(
      PIN_CEKIC_ZENSKI, trazenoTrajanjeMs, SIGURNOSNI_MAX_OVERRIDE_TRAJANJE_CEKICA_MS);
  signalizirajHammer1_Active();
  signalizirajHammer2_Active();
}

bool jeCekicSigurnosnoAktivanZaPosebniNacin(int pin) {
  const int indeks = dohvatiIndeksCekicaZaPin(pin);
  return (indeks >= 0) ? sigurnostCekica.aktivan[indeks] : false;
}

void otkucajSate(int broj) {
  if (broj < 1 || broj > 12) {
    return;
  }

  pokreniSekvencuOtkucavanja(OTKUCAVANJE_CEKIC1,
                             broj,
                             PIN_CEKIC_MUSKI,
                             dohvatiTrajanjeImpulsaCekicaZaPosebneNacineMs(),
                             SATNO_OTKUCAJ_PAUZA_MS,
                             F("cekic 1 - puni sat"));
}

void otkucajPolasata() {
  pokreniSekvencuOtkucavanja(OTKUCAVANJE_CEKIC2,
                             1,
                             PIN_CEKIC_ZENSKI,
                             dohvatiTrajanjeImpulsaCekicaZaPosebneNacineMs(),
                             dohvatiPauzuIzmeduUdaraca(),
                             F("cekic 2 - pola sata"));
}

void inicijalizirajOtkucavanje() {
  pinMode(PIN_CEKIC_MUSKI, OUTPUT);
  pinMode(PIN_CEKIC_ZENSKI, OUTPUT);
  digitalWrite(PIN_CEKIC_MUSKI, LOW);
  digitalWrite(PIN_CEKIC_ZENSKI, LOW);

  otkucavanje.vrsta = OTKUCAVANJE_NONE;
  otkucavanje.preostaliUdarci = 0;
  otkucavanje.vrijemePocetkaMs = 0UL;
  otkucavanje.cekicAktivan = false;
  otkucavanje.aktivniPin = -1;
  otkucavanje.vrijemeZadnjeAktivacije = 0UL;
  otkucavanje.trajanjeImpulsaMs = TRAJANJE_IMPULSA_CEKICA_DEFAULT;
  otkucavanje.pauzaIzmeduUdaracaMs = PAUZA_MEZI_UDARACA_DEFAULT;

  sigurnostCekica.aktivan[0] = false;
  sigurnostCekica.aktivan[1] = false;
  sigurnostCekica.vrijemeAktivacijeMs[0] = 0UL;
  sigurnostCekica.vrijemeAktivacijeMs[1] = 0UL;
  sigurnostCekica.trajanjeMs[0] = 0UL;
  sigurnostCekica.trajanjeMs[1] = 0UL;

  inicijalizirajSlavljenjeIMrtvacko();
  blokadaOtkucavanja = false;

  posaljiPCLog(F("Otkucavanje: inicijalizirano"));
}

void upravljajOtkucavanjem() {
  const unsigned long sadaMs = millis();
  const DateTime sada = dohvatiTrenutnoVrijeme();
  const uint8_t modOtkucavanja = dohvatiModOtkucavanja();
  const bool tihiRezimAktivan = jePrekidacTisineAktivan();

  primijeniSigurnosniLimitCekica(sadaMs);
  upravljajSlavljenjemIMrtvackim(sadaMs);

  if (jeLiInerciaAktivna()) {
    if (jeSlavljenjeUTijeku()) {
      zaustaviSlavljenje();
    }
    if (jeMrtvackoUTijeku()) {
      zaustaviMrtvacko();
    }
    if (otkucavanje.vrsta != OTKUCAVANJE_NONE) {
      ponistiAktivnoOtkucavanje(true, F("aktivna inercija zvona"));
    }
  }

  if (tihiRezimAktivan && otkucavanje.vrsta != OTKUCAVANJE_NONE) {
    ponistiAktivnoOtkucavanje(true, F("aktivan tihi rezim"));
    return;
  }

  if (modOtkucavanja == MOD_OTKUCAJ_ISKLJUCEN && otkucavanje.vrsta != OTKUCAVANJE_NONE) {
    ponistiAktivnoOtkucavanje(true, F("mod otkucavanja je iskljucen"));
    return;
  }

  if (otkucavanje.vrsta == OTKUCAVANJE_NONE) {
    static int zadnjaMinuta = -1;
    if (sada.minute() != zadnjaMinuta) {
      zadnjaMinuta = sada.minute();

      if (!jeSlavljenjeUTijeku() && !jeMrtvackoUTijeku()) {
        const bool batAktivan =
            jeBATPeriodAktivanZaSatneOtkucaje(sada.hour(), sada.minute()) ||
            jeJutarnjeZvonjenjeOtvoriloOtkucavanje(sada);
        const bool otkucavanjeDozvoljenoUSatu = jeDozvoljenoOtkucavanjeUSatu(sada.hour());

        if (modOtkucavanja == MOD_OTKUCAJ_ISKLJUCEN) {
          // Otkucavanje je namjerno iskljuceno u postavkama toranjskog sata.
        } else if (modOtkucavanja == MOD_OTKUCAJ_KVARTALNI) {
          if (sada.minute() == 0 || sada.minute() == 15 || sada.minute() == 30 ||
              sada.minute() == 45) {
            if (!otkucavanjeDozvoljenoUSatu) {
              posaljiPCLog(F("Kvartalno otkucavanje preskoceno: izvan raspona rada"));
            } else if (tihiRezimAktivan) {
              posaljiPCLog(F("Kvartalno otkucavanje preskoceno: tihi rezim"));
            } else if (!batAktivan) {
              posaljiPCLog(F("Kvartalno otkucavanje preskoceno: izvan BAT raspona"));
            } else if (sada.minute() == 0) {
              pokreniSekvencuOtkucavanja(OTKUCAVANJE_CEKIC1,
                                       4,
                                       PIN_CEKIC_MUSKI,
                                       dohvatiTrajanjeImpulsaCekicaZaPosebneNacineMs(),
                                       SATNO_OTKUCAJ_PAUZA_MS,
                                       F("opcija 2, puni sat"));
            } else if (sada.minute() == 15) {
              pokreniSekvencuOtkucavanja(OTKUCAVANJE_CEKIC2,
                                       1,
                                       PIN_CEKIC_ZENSKI,
                                       dohvatiTrajanjeImpulsaCekicaZaPosebneNacineMs(),
                                       SATNO_OTKUCAJ_PAUZA_MS,
                                       F("opcija 2, HH:15"));
            } else if (sada.minute() == 30) {
              pokreniSekvencuOtkucavanja(OTKUCAVANJE_CEKIC1,
                                       2,
                                       PIN_CEKIC_MUSKI,
                                       dohvatiTrajanjeImpulsaCekicaZaPosebneNacineMs(),
                                       SATNO_OTKUCAJ_PAUZA_MS,
                                       F("opcija 2, HH:30"));
            } else {
              pokreniSekvencuOtkucavanja(OTKUCAVANJE_CEKIC2,
                                       3,
                                       PIN_CEKIC_ZENSKI,
                                       dohvatiTrajanjeImpulsaCekicaZaPosebneNacineMs(),
                                       SATNO_OTKUCAJ_PAUZA_MS,
                                       F("opcija 2, HH:45"));
            }
          }
        } else if (sada.minute() == 0) {
          int broj = sada.hour() % 12;
          if (broj == 0) {
            broj = 12;
          }

          if (otkucavanjeDozvoljenoUSatu && batAktivan && !tihiRezimAktivan) {
            otkucajSate(broj);
          } else if (tihiRezimAktivan) {
            posaljiPCLog(F("Satno otkucavanje preskoceno: tihi rezim"));
          } else if (!batAktivan) {
            posaljiPCLog(F("Satno otkucavanje preskoceno: izvan BAT raspona"));
          }
        } else if (sada.minute() == 30) {
          if (otkucavanjeDozvoljenoUSatu && batAktivan && !tihiRezimAktivan) {
            otkucajPolasata();
          } else if (tihiRezimAktivan) {
            posaljiPCLog(F("Polusatno otkucavanje preskoceno: tihi rezim"));
          } else if (!batAktivan) {
            posaljiPCLog(F("Polusatno otkucavanje preskoceno: izvan BAT raspona"));
          }
        }
      }
    }

    return;
  }

  if (!jeOperacijaCekicaDozvoljena() && otkucavanje.vrsta != OTKUCAVANJE_NONE) {
    ponistiAktivnoOtkucavanje(true, F("blokada ili inercija tijekom sekvence"));
    return;
  }

  unsigned long trajanjeImpulsa = otkucavanje.trajanjeImpulsaMs;
  unsigned long pauzaMezi = otkucavanje.pauzaIzmeduUdaracaMs;

  if (trajanjeImpulsa == 0UL) {
    trajanjeImpulsa = TRAJANJE_IMPULSA_CEKICA_DEFAULT;
  }
  if (pauzaMezi == 0UL) {
    pauzaMezi = PAUZA_MEZI_UDARACA_DEFAULT;
  }

  if (otkucavanje.cekicAktivan) {
    const unsigned long proteklo = sadaMs - otkucavanje.vrijemeZadnjeAktivacije;
    if (proteklo >= trajanjeImpulsa) {
      deaktivirajCekic_Internal(otkucavanje.aktivniPin);
      otkucavanje.cekicAktivan = false;
      otkucavanje.vrijemeZadnjeAktivacije = sadaMs;
    }
  } else {
    if (otkucavanje.vrijemeZadnjeAktivacije == 0UL) {
      pokreniSljedeciUdarac();
    } else {
      const unsigned long proteklo = sadaMs - otkucavanje.vrijemeZadnjeAktivacije;
      if (proteklo >= pauzaMezi) {
        if (otkucavanje.preostaliUdarci > 0) {
          pokreniSljedeciUdarac();
        } else {
          ponistiAktivnoOtkucavanje(false, F("odradjeni svi udarci"));
        }
      }
    }
  }
}

void postaviBlokaduOtkucavanja(bool blokiraj) {
  if (blokadaOtkucavanja == blokiraj) {
    return;
  }

  blokadaOtkucavanja = blokiraj;

  if (blokadaOtkucavanja) {
    posaljiPCLog(F("Blokada otkucavanja: UKLJUCENA"));

    if (otkucavanje.vrsta != OTKUCAVANJE_NONE) {
      ponistiAktivnoOtkucavanje(true, F("korisnicka blokada otkucavanja"));
    }
    if (jeSlavljenjeUTijeku()) {
      zaustaviSlavljenje();
    }
    if (jeMrtvackoUTijeku()) {
      zaustaviMrtvacko();
    }
  } else {
    posaljiPCLog(F("Blokada otkucavanja: ISKLJUCENA"));
  }
}

void postaviGlobalnuBlokaduOtkucavanja(bool blokiraj) {
  if (globalnaBlokadaOtkucavanja == blokiraj) {
    return;
  }

  globalnaBlokadaOtkucavanja = blokiraj;

  if (globalnaBlokadaOtkucavanja) {
    posaljiPCLog(F("Globalna blokada otkucavanja: UKLJUCENA"));

    if (otkucavanje.vrsta != OTKUCAVANJE_NONE) {
      ponistiAktivnoOtkucavanje(true, F("globalna blokada otkucavanja"));
    }
    if (jeSlavljenjeUTijeku()) {
      zaustaviSlavljenje();
    }
    if (jeMrtvackoUTijeku()) {
      zaustaviMrtvacko();
    }
    deaktivirajObaCekicaZaPosebniNacin();
  } else {
    posaljiPCLog(F("Globalna blokada otkucavanja: ISKLJUCENA"));
  }
}

bool jeOtkucavanjeUTijeku() {
  return otkucavanje.vrsta != OTKUCAVANJE_NONE;
}
