// otkucavanje.cpp - REFACTORED: Hammer Striking System WITH Celebration/Funeral
// Mechanical hammer striking (via relay impulses) with celebration and funeral modes
// CELEBRATION and FUNERAL modes are ONLY in this file (NOT in zvonjenje.cpp)

#include <Arduino.h>
#include <RTClib.h>
#include "otkucavanje.h"
#include "zvonjenje.h"
#include "podesavanja_piny.h"
#include "time_glob.h"
#include "postavke.h"
#include "lcd_display.h"
#include "pc_serial.h"
#include "debouncing.h"

// ==================== HAMMER TIMING CONSTANTS ====================

const unsigned long TRAJANJE_IMPULSA_CEKICA_DEFAULT = 150UL;
const unsigned long PAUZA_MEZI_UDARACA_DEFAULT = 400UL;
const unsigned long SATNO_OTKUCAJ_PAUZA_MS = 2000UL;  // 2 s izmedu satnih udaraca

// Slavljenje mod 1: tocan uzorak 1-2-2 s definiranim pauzama
const unsigned long SLAVLJENJE_TRAJANJE_UDARCA_MS = 150UL;
const unsigned long SLAVLJENJE_PAUZA_NAKON_CEKIC1_MS = 300UL;
const unsigned long SLAVLJENJE_PAUZA_NAKON_CEKIC2_MS = 150UL;

// Slavljenje mod 2: neprekinuti slijed 1-2-1-2 s istim impulsima i pauzama
const unsigned long SLAVLJENJE_PAUZA_MOD2_MS = 150UL;

// Mrtvacko: oba cekica 150 ms, zatim 10 s pauza
const unsigned long MRTVACKO_TRAJANJE_UDARCA_MS = 150UL;
const unsigned long MRTVACKO_PAUZA_MS = 10000UL;

// ==================== STATE MACHINE CONSTANTS ====================

enum VrstaOtkucavanja {
  OTKUCAVANJE_NONE = 0,
  OTKUCAVANJE_SATI = 1,
  OTKUCAVANJE_POLA = 2
};

// ==================== STATE VARIABLES ====================

static struct {
  VrstaOtkucavanja vrsta;
  int preostali_udarci;
  unsigned long vrijeme_pocetka_ms;
  bool cekic_aktivan;
  int aktivni_pin;
  unsigned long vrijeme_zadnje_aktivacije;
} otkucavanje = {
  OTKUCAVANJE_NONE,
  0,
  0,
  false,
  -1,
  0
};

// Stanje slavljenja: beskonacni slijed 1,2,2 dok je nacin rada aktivan
static struct {
  bool slavljenje_aktivno;
  unsigned long vrijeme_pocetka_ms;
  int trenutni_korak;
  uint8_t aktivni_mod;
  unsigned long vrijeme_koraka_ms;
  bool cekic_aktivan;
  int aktivni_pin;
} slavljenje = {
  false,
  0,
  0,
  1,
  0,
  false,
  -1
};

// Stanje mrtvackog: oba cekica zajedno pa duga pauza
static struct {
  bool mrtvacko_aktivno;
  unsigned long vrijeme_pocetka_ms;
  bool cekici_aktivni;
  unsigned long vrijeme_faze_ms;
} mrtvacko = {
  false,
  0,
  false,
  0
};

static bool blokada_otkucavanja = false;
static DateTime zadnje_izmjereno_vrijeme;

// ==================== HELPER FUNCTIONS ====================

static void aktivirajCekic_Internal(int pin) {
  if (pin == PIN_CEKIC_MUSKI || pin == PIN_CEKIC_ZENSKI) {
    digitalWrite(pin, HIGH);
  }
}

static void deaktivirajCekic_Internal(int pin) {
  if (pin == PIN_CEKIC_MUSKI || pin == PIN_CEKIC_ZENSKI) {
    digitalWrite(pin, LOW);
  }
}

// Sigurnosno gasenje oba cekica
static void deaktivirajObaCekica_Internal() {
  deaktivirajCekic_Internal(PIN_CEKIC_MUSKI);
  deaktivirajCekic_Internal(PIN_CEKIC_ZENSKI);
}

static int dohvatiPinSlavljenjaZaKorak(uint8_t mod, int korak) {
  if (mod == 2) {
    return (korak % 2 == 0) ? PIN_CEKIC_MUSKI : PIN_CEKIC_ZENSKI;
  }

  return (korak == 0) ? PIN_CEKIC_MUSKI : PIN_CEKIC_ZENSKI;
}

static int dohvatiBrojKorakaSlavljenja(uint8_t mod) {
  return (mod == 2) ? 4 : 3;
}

static unsigned long dohvatiPauzuSlavljenjaNakonKoraka(uint8_t mod, int korak) {
  if (mod == 2) {
    return SLAVLJENJE_PAUZA_MOD2_MS;
  }

  return (korak == 0) ? SLAVLJENJE_PAUZA_NAKON_CEKIC1_MS : SLAVLJENJE_PAUZA_NAKON_CEKIC2_MS;
}

static bool jeOperacijaDozvoljena() {
  if (blokada_otkucavanja) {
    return false;
  }

  if (jeLiInerciaAktivna()) {
    return false;
  }

  return true;
}

static void ponistiAktivnoOtkucavanje(bool jeOtkazivanje, const __FlashStringHelper* razlog = nullptr) {
  if (otkucavanje.aktivni_pin >= 0) {
    deaktivirajCekic_Internal(otkucavanje.aktivni_pin);
  }
  otkucavanje.vrsta = OTKUCAVANJE_NONE;
  otkucavanje.preostali_udarci = 0;
  otkucavanje.cekic_aktivan = false;
  otkucavanje.aktivni_pin = -1;
  otkucavanje.vrijeme_pocetka_ms = 0;
  otkucavanje.vrijeme_zadnje_aktivacije = 0;

  String log = jeOtkazivanje
      ? String(F("Otkucavanje: operacija otkazana"))
      : String(F("Otkucavanje: sekvenca dovrsena"));
  if (razlog != nullptr) {
    log += F(" (");
    log += String(razlog);
    log += ')';
  }
  posaljiPCLog(log);
}

static void pokreniSljedeciUdarac() {
  if (otkucavanje.preostali_udarci <= 0) {
    ponistiAktivnoOtkucavanje(false, F("nema preostalih udaraca"));
    return;
  }

  aktivirajCekic_Internal(otkucavanje.aktivni_pin);
  otkucavanje.cekic_aktivan = true;
  otkucavanje.vrijeme_zadnje_aktivacije = millis();
  otkucavanje.preostali_udarci--;

  String log = F("Udarac: preostalo=");
  log += otkucavanje.preostali_udarci;
  posaljiPCLog(log);
}

// ==================== NORMAL STRIKING SEQUENCE ====================

void otkucajSate(int broj) {
  if (broj < 1 || broj > 12) {
    return;
  }

  if (otkucavanje.vrsta != OTKUCAVANJE_NONE) {
    return;
  }

  if (!jeOperacijaDozvoljena()) {
    posaljiPCLog(F("Otkucavanje: blokirano (inercija ili user blok)"));
    return;
  }

  otkucavanje.vrsta = OTKUCAVANJE_SATI;
  otkucavanje.preostali_udarci = broj;
  otkucavanje.aktivni_pin = PIN_CEKIC_MUSKI;
  otkucavanje.vrijeme_pocetka_ms = 0;
  otkucavanje.cekic_aktivan = false;

  String log = F("Otkucavanje: poceti sat sa ");
  log += broj;
  log += F(" udaraca");
  posaljiPCLog(log);
  signalizirajHammer1_Active();

  pokreniSljedeciUdarac();
}

void otkucajPolasata() {
  if (otkucavanje.vrsta != OTKUCAVANJE_NONE) {
    return;
  }

  if (!jeOperacijaDozvoljena()) {
    posaljiPCLog(F("Otkucavanje: blokirano (inercija ili user blok)"));
    return;
  }

  otkucavanje.vrsta = OTKUCAVANJE_POLA;
  otkucavanje.preostali_udarci = 1;
  otkucavanje.aktivni_pin = PIN_CEKIC_ZENSKI;
  otkucavanje.vrijeme_pocetka_ms = 0;
  otkucavanje.cekic_aktivan = false;

  posaljiPCLog(F("Otkucavanje: jedan udarac za pola sata"));
  signalizirajHammer2_Active();

  pokreniSljedeciUdarac();
}

// ==================== CELEBRATION MODE ====================

// Pokretanje slavljenja: odabrani slijed iz postavki
void zapocniSlavljenje() {
  unsigned long sadaMs = millis();
  const uint8_t modSlavljenja = dohvatiModSlavljenja();

  if (!jeOperacijaDozvoljena()) {
    posaljiPCLog(F("Slavljenje: ne moze se pokrenuti (inercija ili blok)"));
    return;
  }

  if (mrtvacko.mrtvacko_aktivno) {
    posaljiPCLog(F("Slavljenje: odbijeno - mrtvacko je aktivno (mutual exclusion)"));
    return;
  }

  slavljenje.slavljenje_aktivno = true;
  slavljenje.vrijeme_pocetka_ms = sadaMs;
  slavljenje.trenutni_korak = 0;
  slavljenje.aktivni_mod = modSlavljenja;
  slavljenje.vrijeme_koraka_ms = sadaMs;
  slavljenje.cekic_aktivan = true;
  slavljenje.aktivni_pin = dohvatiPinSlavljenjaZaKorak(modSlavljenja, 0);
  aktivirajCekic_Internal(slavljenje.aktivni_pin);

  String log = F("Slavljenje: pokrenuto (mod ");
  log += modSlavljenja;
  log += (modSlavljenja == 2) ? F(", uzorak C1-C2-C1-C2)") : F(", uzorak C1-C2-C2)");
  posaljiPCLog(log);
  signalizirajCelebration_Mode();
}

void zaustaviSlavljenje() {
  if (slavljenje.slavljenje_aktivno) {
    slavljenje.slavljenje_aktivno = false;
    deaktivirajObaCekica_Internal();
    slavljenje.cekic_aktivan = false;
    slavljenje.aktivni_pin = -1;
    posaljiPCLog(F("Slavljenje: zaustavljeno"));
  }
}

bool jeSlavljenjeUTijeku() {
  return slavljenje.slavljenje_aktivno;
}

// Azuriranje slavljenja (neblokirajuci automat stanja)
static void azurirajSlavljenje(unsigned long sadaMs) {
  if (!slavljenje.slavljenje_aktivno) {
    return;
  }

  const unsigned long proteklo = sadaMs - slavljenje.vrijeme_koraka_ms;
  const unsigned long trazena_pauza =
      dohvatiPauzuSlavljenjaNakonKoraka(slavljenje.aktivni_mod, slavljenje.trenutni_korak);

  if (slavljenje.cekic_aktivan) {
    if (proteklo >= SLAVLJENJE_TRAJANJE_UDARCA_MS) {
      deaktivirajCekic_Internal(slavljenje.aktivni_pin);
      slavljenje.cekic_aktivan = false;
      slavljenje.aktivni_pin = -1;
      slavljenje.vrijeme_koraka_ms = sadaMs;
    }
    return;
  }

  if (proteklo >= trazena_pauza) {
    const int brojKoraka = dohvatiBrojKorakaSlavljenja(slavljenje.aktivni_mod);
    slavljenje.trenutni_korak = (slavljenje.trenutni_korak + 1) % brojKoraka;
    const int sljedeci_pin =
        dohvatiPinSlavljenjaZaKorak(slavljenje.aktivni_mod, slavljenje.trenutni_korak);
    aktivirajCekic_Internal(sljedeci_pin);
    slavljenje.aktivni_pin = sljedeci_pin;
    slavljenje.cekic_aktivan = true;
    slavljenje.vrijeme_koraka_ms = sadaMs;
  }
}

// ==================== FUNERAL MODE ====================

// Pokretanje mrtvackog: oba cekica 150 ms, zatim 10 s pauza
void zapocniMrtvacko() {
  unsigned long sadaMs = millis();

  if (!jeOperacijaDozvoljena()) {
    posaljiPCLog(F("Mrtvacko: ne moze se pokrenuti (inercija ili blok)"));
    return;
  }

  if (slavljenje.slavljenje_aktivno) {
    posaljiPCLog(F("Mrtvacko: odbijeno - slavljenje je aktivno (mutual exclusion)"));
    return;
  }

  mrtvacko.mrtvacko_aktivno = true;
  mrtvacko.vrijeme_pocetka_ms = sadaMs;
  mrtvacko.cekici_aktivni = true;
  mrtvacko.vrijeme_faze_ms = sadaMs;
  aktivirajCekic_Internal(PIN_CEKIC_MUSKI);
  aktivirajCekic_Internal(PIN_CEKIC_ZENSKI);

  posaljiPCLog(F("Mrtvacko: pokrenuto (oba cekica 150ms / pauza 10s)"));
  signalizirajFuneral_Mode();
}

void zaustaviMrtvacko() {
  if (mrtvacko.mrtvacko_aktivno) {
    mrtvacko.mrtvacko_aktivno = false;
    deaktivirajObaCekica_Internal();
    mrtvacko.cekici_aktivni = false;
    posaljiPCLog(F("Mrtvacko: zaustavljeno"));
  }
}

bool jeMrtvackoUTijeku() {
  return mrtvacko.mrtvacko_aktivno;
}

// Azuriranje mrtvackog (neblokirajuci automat stanja)
static void azurirajMrtvacko(unsigned long sadaMs) {
  if (!mrtvacko.mrtvacko_aktivno) {
    return;
  }

  const unsigned long proteklo = sadaMs - mrtvacko.vrijeme_faze_ms;

  if (mrtvacko.cekici_aktivni) {
    if (proteklo >= MRTVACKO_TRAJANJE_UDARCA_MS) {
      deaktivirajObaCekica_Internal();
      mrtvacko.cekici_aktivni = false;
      mrtvacko.vrijeme_faze_ms = sadaMs;
    }
    return;
  }

  if (proteklo >= MRTVACKO_PAUZA_MS) {
    aktivirajCekic_Internal(PIN_CEKIC_MUSKI);
    aktivirajCekic_Internal(PIN_CEKIC_ZENSKI);
    mrtvacko.cekici_aktivni = true;
    mrtvacko.vrijeme_faze_ms = sadaMs;
  }
}

// ==================== BUTTON DEBOUNCING (PIN 43 & 42) ====================

// Dok je tipka slavljenja spojena na GND, slavljenje traje.
static void provjeriDugmeSlavljenja() {
  SwitchState novoStanje = SWITCH_RELEASED;
  if (!obradiDebouncedInput(PIN_KEY_CELEBRATION, 30, &novoStanje)) {
    return;
  }

  if (novoStanje == SWITCH_PRESSED) {
    if (!slavljenje.slavljenje_aktivno) {
      zapocniSlavljenje();
      posaljiPCLog(F("Dugme: slavljenje pokrenuto"));
    }
  } else if (slavljenje.slavljenje_aktivno) {
    zaustaviSlavljenje();
    posaljiPCLog(F("Dugme: slavljenje zaustavljeno"));
  }
}

static void provjeriDugmeMrtvackog() {
  SwitchState novoStanje = SWITCH_RELEASED;
  if (!obradiDebouncedInput(PIN_KEY_FUNERAL, 30, &novoStanje)) {
    return;
  }

  if (novoStanje == SWITCH_PRESSED) {
    if (mrtvacko.mrtvacko_aktivno) {
      zaustaviMrtvacko();
      posaljiPCLog(F("Dugme: mrtvacko zaustavljeno"));
    } else {
      zapocniMrtvacko();
      posaljiPCLog(F("Dugme: mrtvacko pokrenuto"));
    }
  }
}

// ==================== NORMAL HOUR STRIKING MANAGEMENT ====================

void inicijalizirajOtkucavanje() {
  // Izlazi za cekice
  pinMode(PIN_CEKIC_MUSKI, OUTPUT);
  pinMode(PIN_CEKIC_ZENSKI, OUTPUT);
  digitalWrite(PIN_CEKIC_MUSKI, LOW);
  digitalWrite(PIN_CEKIC_ZENSKI, LOW);

  // Ulazi za tipke (slavljenje / mrtvacko)
  pinMode(PIN_KEY_CELEBRATION, INPUT_PULLUP);
  pinMode(PIN_KEY_FUNERAL, INPUT_PULLUP);

  // Pocetno stanje modula
  otkucavanje.vrsta = OTKUCAVANJE_NONE;
  otkucavanje.preostali_udarci = 0;
  otkucavanje.vrijeme_pocetka_ms = 0;
  otkucavanje.cekic_aktivan = false;
  otkucavanje.aktivni_pin = -1;
  otkucavanje.vrijeme_zadnje_aktivacije = 0;

  slavljenje.slavljenje_aktivno = false;
  slavljenje.vrijeme_pocetka_ms = 0;
  slavljenje.trenutni_korak = 0;
  slavljenje.aktivni_mod = 1;
  slavljenje.vrijeme_koraka_ms = 0;
  slavljenje.cekic_aktivan = false;
  slavljenje.aktivni_pin = -1;

  mrtvacko.mrtvacko_aktivno = false;
  mrtvacko.vrijeme_pocetka_ms = 0;
  mrtvacko.cekici_aktivni = false;
  mrtvacko.vrijeme_faze_ms = 0;

  zadnje_izmjereno_vrijeme = dohvatiTrenutnoVrijeme();
  blokada_otkucavanja = false;

  posaljiPCLog(F("Otkucavanje: inicijalizirano"));
}

void upravljajOtkucavanjem() {
  unsigned long sadaMs = millis();
  DateTime sada = dohvatiTrenutnoVrijeme();

  provjeriDugmeSlavljenja();
  provjeriDugmeMrtvackog();

  azurirajSlavljenje(sadaMs);
  azurirajMrtvacko(sadaMs);

  // Ako se pojavi inercija, odmah zaustavi sve aktivne nacine rada cekica.
  if (jeLiInerciaAktivna()) {
    if (slavljenje.slavljenje_aktivno) {
      zaustaviSlavljenje();
    }
    if (mrtvacko.mrtvacko_aktivno) {
      zaustaviMrtvacko();
    }
    if (otkucavanje.vrsta != OTKUCAVANJE_NONE) {
      ponistiAktivnoOtkucavanje(true, F("aktivna inercija zvona"));
    }
  }

  if (otkucavanje.vrsta == OTKUCAVANJE_NONE) {
    static int zadnja_minuta = -1;
    if (sada.minute() != zadnja_minuta) {
      zadnja_minuta = sada.minute();

      if (sada.minute() == 0 && !jeSlavljenjeUTijeku() && !jeMrtvackoUTijeku()) {
        int broj = sada.hour() % 12;
        if (broj == 0) broj = 12;

        bool tihiSatiAktivni = jeTihiPeriodAktivanZaSatneOtkucaje(sada.hour());
        if (jeDozvoljenoOtkucavanjeUSatu(sada.hour()) && !tihiSatiAktivni) {
          otkucajSate(broj);
        } else if (tihiSatiAktivni) {
          posaljiPCLog(F("Satno otkucavanje preskoceno: tihi sati"));
        }
      } else if (sada.minute() == 30 && !jeSlavljenjeUTijeku() && !jeMrtvackoUTijeku()) {
        bool tihiSatiAktivni = jeTihiPeriodAktivanZaSatneOtkucaje(sada.hour());
        if (jeDozvoljenoOtkucavanjeUSatu(sada.hour()) && !tihiSatiAktivni) {
          otkucajPolasata();
        } else if (tihiSatiAktivni) {
          posaljiPCLog(F("Polusatno otkucavanje preskoceno: tihi sati"));
        }
      }
    }

    return;
  }

  if (!jeOperacijaDozvoljena() && otkucavanje.vrsta != OTKUCAVANJE_NONE) {
    ponistiAktivnoOtkucavanje(true, F("blokada ili inercija tijekom sekvence"));
    return;
  }

  unsigned long trajanje_impulsa = dohvatiTrajanjeImpulsaCekica();
  unsigned long pauza_mezi = dohvatiPauzuIzmeduUdaraca();

  if (otkucavanje.vrsta == OTKUCAVANJE_SATI) {
    trajanje_impulsa = TRAJANJE_IMPULSA_CEKICA_DEFAULT;
    pauza_mezi = SATNO_OTKUCAJ_PAUZA_MS;
  }

  if (trajanje_impulsa == 0) trajanje_impulsa = TRAJANJE_IMPULSA_CEKICA_DEFAULT;
  if (pauza_mezi == 0) pauza_mezi = PAUZA_MEZI_UDARACA_DEFAULT;

  if (otkucavanje.cekic_aktivan) {
    unsigned long proteklo = sadaMs - otkucavanje.vrijeme_zadnje_aktivacije;
    if (proteklo >= trajanje_impulsa) {
      deaktivirajCekic_Internal(otkucavanje.aktivni_pin);
      otkucavanje.cekic_aktivan = false;
      otkucavanje.vrijeme_zadnje_aktivacije = sadaMs;
    }
  } else {
    if (otkucavanje.vrijeme_zadnje_aktivacije == 0) {
      pokreniSljedeciUdarac();
    } else {
      unsigned long proteklo = sadaMs - otkucavanje.vrijeme_zadnje_aktivacije;
      if (proteklo >= pauza_mezi) {
        if (otkucavanje.preostali_udarci > 0) {
          pokreniSljedeciUdarac();
        } else {
          ponistiAktivnoOtkucavanje(false, F("odradjeni svi udarci"));
        }
      }
    }
  }
}

// ==================== USER BLOCKING CONTROL ====================

void postaviBlokaduOtkucavanja(bool blokiraj) {
  if (blokada_otkucavanja == blokiraj) {
    return;
  }

  blokada_otkucavanja = blokiraj;

  if (blokada_otkucavanja) {
    posaljiPCLog(F("Blokada otkucavanja: UKLJUCENA"));

    if (otkucavanje.vrsta != OTKUCAVANJE_NONE) {
      ponistiAktivnoOtkucavanje(true, F("korisnicka blokada otkucavanja"));
    }
    if (slavljenje.slavljenje_aktivno) {
      zaustaviSlavljenje();
    }
    if (mrtvacko.mrtvacko_aktivno) {
      zaustaviMrtvacko();
    }
  } else {
    posaljiPCLog(F("Blokada otkucavanja: ISKLJUCENA"));
  }
}
