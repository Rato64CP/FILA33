// postavke.cpp - Upravljanje postavkama toranjskog sata i EEPROM-om
#include <Arduino.h>
#include <avr/pgmspace.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "postavke.h"
#include "eeprom_konstante.h"
#include "wear_leveling.h"
#include "i2c_eeprom.h"
#include "pc_serial.h"
#include "time_glob.h"

namespace {

constexpr int PLOCA_ZADANI_POCETAK_MINUTA = 4 * 60 + 59;
constexpr int PLOCA_ZADANI_KRAJ_MINUTA = 20 * 60 + 44;
constexpr int PLOCA_MINUTNI_BLOK = 15;
constexpr int PLOCA_ZADNJA_CETVRT = 23 * 60 + 59;
constexpr int OTKUCAVANJE_CIJELI_DAN_OD = 0;
constexpr int OTKUCAVANJE_CIJELI_DAN_DO = 23;
constexpr uint8_t FIKSNI_BROJ_ZVONA = 2;
constexpr uint8_t FIKSNI_BROJ_MJESTA_ZA_CAVLE = 5;
constexpr uint8_t FIKSNI_CAVAO_SLAVLJENJA = 5;
constexpr int16_t DOPUSTENE_SUNCEVE_ODGODE_MIN[] = {-30, -20, -10, 0, 10, 20, 30};
constexpr size_t BROJ_DOPUSTENIH_SUNCEVIH_ODGODA =
    sizeof(DOPUSTENE_SUNCEVE_ODGODE_MIN) / sizeof(DOPUSTENE_SUNCEVE_ODGODE_MIN[0]);
constexpr uint8_t MASKA_SUNCEVIH_DOGADAJA = 0x07;
constexpr uint8_t MASKA_BLAGDANSKOG_SLAVLJENJA = 0x07;
constexpr uint8_t MASKA_BLAGDANSKIH_RAZDOBLJA = 0x07;
constexpr uint8_t ZASTAVICA_RAD_BEZ_KOCNICE_ZVONA = 0x40;
constexpr uint8_t ZASTAVICA_UPS_MODA = 0x80;
constexpr uint8_t SVI_SVETI_ZADANI_POCETAK_SAT = 15;
constexpr uint8_t SVI_SVETI_ZADANI_ZAVRSETAK_SAT = 8;
constexpr int8_t POMICNI_BLAGDAN_MIN_POMAK_OD_USKRSA = -120;
constexpr int8_t POMICNI_BLAGDAN_MAX_POMAK_OD_USKRSA = 120;
constexpr uint8_t ZADANA_MINUTA_BLAGDANSKE_MISE = 0;

struct ZadaniNepomicniBlagdanInfo {
  uint8_t mjesec;
  uint8_t dan;
};

struct ZadaniPomicniBlagdanInfo {
  int8_t pomakOdUskrsaDana;
};

struct NepomicniBlagdanRadno {
  bool omogucen;
  uint8_t satMise;
  uint8_t minutaMise;
};

struct PomicniBlagdanRadno {
  bool omogucen;
  uint8_t satMise;
  uint8_t minutaMise;
};

constexpr ZadaniNepomicniBlagdanInfo ZADANI_NEPOMICNI_BLAGDANI[BROJ_NEPOMICNIH_BLAGDANA] = {
  {1, 1},   // Nova Godina
  {1, 6},   // Bogojavljenje
  {2, 2},   // Svjecnica
  {2, 10},  // Alojzije Stepinac
  {3, 19},  // Sveti Josip
  {3, 25},  // Blagovijest
  {6, 13},  // Sveti Ante
  {6, 29},  // Sveti Petar
  {8, 15},  // Velika Gospa
  {11, 1},  // Svi Sveti
  {11, 2},  // Dusni dan
  {12, 24}, // Badnjak
  {12, 25}, // Bozic
  {12, 26}, // Sveti Stjepan
  {12, 27}  // Sveti Ivan
};

constexpr ZadaniPomicniBlagdanInfo ZADANI_POMICNI_BLAGDANI[BROJ_POMICNIH_BLAGDANA] = {
  {-46}, // Pepelnica
  {-3},  // Veliki cetvrtak
  {0},   // Uskrs
  {1},   // Uskrsni ponedjeljak
  {39},  // Uzasasce
  {60},  // Tijelovo
  {68}   // Srce Isusovo
};

struct RadnePostavke {
  int satOd;
  int satDo;
  int tihiSatiOd;
  int tihiSatiDo;
  int plocaPocetakMinuta;
  int plocaKrajMinuta;
  unsigned int trajanjeImpulsaCekicaMs;
  unsigned int pauzaIzmeduUdaraca;
  uint8_t trajanjeZvonjenjaRadniMin;
  uint8_t trajanjeZvonjenjaNedjeljaMin;
  uint8_t trajanjeSlavljenjaMin;
  uint8_t slavljenjePrijeZvonjenja;
  uint8_t inercijaZvona1Sekunde;
  uint8_t inercijaZvona2Sekunde;
  uint8_t cavliRadni[4];
  uint8_t cavliNedjelja[4];
  bool koristiDhcp;
  bool lcdPozadinskoOsvjetljenje;
  bool logiranjeOmoguceno;
  uint8_t blagdaniSlavljenjeMaska;
  uint8_t blagdaniRazdobljaMaska;
  bool sviSvetiOmoguceno;
  uint8_t sviSvetiPocetakSat;
  uint8_t sviSvetiZavrsetakSat;
  uint8_t modSlavljenja;
  uint8_t modOtkucavanja;
  uint8_t modMrtvackog;
  bool ntpOmogucen;
  bool wifiOmogucen;
  bool rs485Omogucen;
  bool upsModOmogucen;
  bool kocnicaZvonaOmogucena;
  bool imaKazaljke;
  uint8_t maskaSuncevihDogadaja;
  bool nocnaRasvjetaOmogucena;
  uint8_t zvonaSuncevihDogadaja[SUNCEVI_DOGADAJ_BROJ];
  int16_t odgodeSuncevihDogadajaMin[SUNCEVI_DOGADAJ_BROJ];
  NepomicniBlagdanRadno nepomicniBlagdani[BROJ_NEPOMICNIH_BLAGDANA];
  PomicniBlagdanRadno pomicniBlagdani[BROJ_POMICNIH_BLAGDANA];
  bool dnevnaMisaOmogucena;
  uint8_t dnevnaSatMise;
  uint8_t dnevnaMinutaMise;
  bool nedjeljnaMisaOmogucena;
  uint8_t nedjeljnaSatMise;
  uint8_t nedjeljnaMinutaMise;
};

enum AktivnaMreznaSekcija {
  MREZNA_SEKCIJA_NISTA = 0,
  MREZNA_SEKCIJA_WIFI,
  MREZNA_SEKCIJA_SINKRONIZACIJA
};

static void invalidirajMrezniCache();

static EepromLayout::PostavkeSpremnik napraviZadanePostavke() {
  EepromLayout::PostavkeSpremnik zadane = {
    EepromLayout::POSTAVKE_POTPIS,
    EepromLayout::POSTAVKE_VERZIJA,
    OTKUCAVANJE_CIJELI_DAN_OD,
    OTKUCAVANJE_CIJELI_DAN_DO,
    22,
    6,
    PLOCA_ZADANI_POCETAK_MINUTA,
    PLOCA_ZADANI_KRAJ_MINUTA,
    150,
    400,
    2,
    3,
    2,
    15,
    90,
    90,
    FIKSNI_BROJ_ZVONA,
    FIKSNI_BROJ_MJESTA_ZA_CAVLE,
    {1, 2, 0, 0},
    {3, 4, 0, 0},
    FIKSNI_CAVAO_SLAVLJENJA,
    "SVETI PETAR",
    "cista2906",
    true,
    true,
    true,
    0,
    0,
    0,
    SVI_SVETI_ZADANI_POCETAK_SAT,
    SVI_SVETI_ZADANI_ZAVRSETAK_SAT,
    1,
    2,
    1,
    "192.168.8.230",
    "255.255.255.0",
    "192.168.8.1",
    "pool.ntp.org",
    true,
    false,
    true,
    0
  };
  return zadane;
}

static EepromLayout::SunceviDogadajiSpremnik napraviZadaneSunceveDogadaje() {
  EepromLayout::SunceviDogadajiSpremnik zadani = {
    EepromLayout::SUNCEVI_DOGADAJI_POTPIS,
    EepromLayout::SUNCEVI_DOGADAJI_VERZIJA,
    0,
    0,
    {1, 1, 1},
    {-20, 0, 20},
    0
  };
  return zadani;
}

static EepromLayout::BlagdaniSpremnik napraviZadaneBlagdane() {
  EepromLayout::BlagdaniSpremnik zadani = {};
  zadani.potpis = EepromLayout::BLAGDANI_POTPIS;
  zadani.verzija = EepromLayout::BLAGDANI_VERZIJA;
  zadani.reserved = 0;
  for (uint8_t i = 0; i < BROJ_NEPOMICNIH_BLAGDANA; ++i) {
    zadani.nepomicni[i].omogucen = 0;
    zadani.nepomicni[i].mjesec = ZADANI_NEPOMICNI_BLAGDANI[i].mjesec;
    zadani.nepomicni[i].dan = ZADANI_NEPOMICNI_BLAGDANI[i].dan;
    zadani.nepomicni[i].satMise = 0;
    zadani.nepomicni[i].minutaMise = ZADANA_MINUTA_BLAGDANSKE_MISE;
  }
  for (uint8_t i = 0; i < BROJ_POMICNIH_BLAGDANA; ++i) {
    zadani.pomicni[i].omogucen = 0;
    zadani.pomicni[i].pomakOdUskrsaDana = ZADANI_POMICNI_BLAGDANI[i].pomakOdUskrsaDana;
    zadani.pomicni[i].satMise = 0;
    zadani.pomicni[i].minutaMise = ZADANA_MINUTA_BLAGDANSKE_MISE;
  }
  return zadani;
}

static EepromLayout::MiseSpremnik napraviZadaneMise() {
  EepromLayout::MiseSpremnik zadane = {};
  zadane.potpis = EepromLayout::MISE_POTPIS;
  zadane.verzija = EepromLayout::MISE_VERZIJA;
  zadane.reserved = 0;
  zadane.dnevna.omogucena = 0;
  zadane.dnevna.satMise = 10;
  zadane.dnevna.minutaMise = 0;
  zadane.dnevna.reserved = 0;
  zadane.nedjeljna.omogucena = 0;
  zadane.nedjeljna.satMise = 10;
  zadane.nedjeljna.minutaMise = 0;
  zadane.nedjeljna.reserved = 0;
  return zadane;
}

static RadnePostavke postavke = {};
static char mrezniTekstBuffer[40] = "";
static bool postavkeUcitane = false;

static bool jeKodiranNtpStatus(const char* ntpServer) {
  return ntpServer != nullptr &&
         (ntpServer[0] == '0' || ntpServer[0] == '1') &&
         ntpServer[1] != '\0';
}

static bool procitajNtpOmogucenostIzTeksta(const char* ntpServer) {
  if (jeKodiranNtpStatus(ntpServer)) {
    return ntpServer[0] == '1';
  }
  return true;
}

static const char* dohvatiNtpServerBezZastavice(const char* ntpServer) {
  if (jeKodiranNtpStatus(ntpServer)) {
    return ntpServer + 1;
  }
  return (ntpServer != nullptr) ? ntpServer : "";
}

static void kodirajNtpServer(char* odrediste,
                             size_t velicina,
                             const char* ntpServer,
                             bool omogucen) {
  if (odrediste == nullptr || velicina < 3) {
    return;
  }

  const char* siguranServer = dohvatiNtpServerBezZastavice(ntpServer);
  if (siguranServer[0] == '\0') {
    siguranServer = "pool.ntp.org";
  }

  odrediste[0] = omogucen ? '1' : '0';
  strncpy(odrediste + 1, siguranServer, velicina - 2);
  odrediste[velicina - 1] = '\0';
}

static int dekodirajPocetakPloceMinuta(int spremljeno) {
  if (spremljeno >= 0) {
    return spremljeno;
  }
  return -spremljeno - 1;
}

static int kodirajPocetakPloceMinuta(int pocetakMinuta, bool aktivna) {
  return aktivna ? pocetakMinuta : -(pocetakMinuta + 1);
}

static int normalizirajMinutuPloceNaBlok(int minute) {
  minute = constrain(minute, 0, PLOCA_ZADNJA_CETVRT);
  return minute;
}

static uint8_t ogranicenoTrajanjeCavla(uint8_t trajanjeMin) {
  if (trajanjeMin <= 2) {
    return 2;
  }
  if (trajanjeMin >= 4) {
    return 4;
  }
  return 3;
}

static unsigned int ogranicenoTrajanjeImpulsaCekicaMs(unsigned int trajanjeMs) {
  return constrain(trajanjeMs, 10U, 300U);
}

static uint8_t ogranicenaInercijaZvonaSekunde(uint8_t sekunde) {
  return static_cast<uint8_t>(constrain(static_cast<int>(sekunde), 10, 180));
}

static bool jeValjanSuncevDogadaj(uint8_t dogadaj) {
  return dogadaj < SUNCEVI_DOGADAJ_BROJ;
}

static bool jeValjanoBlagdanskoRazdoblje(uint8_t razdoblje) {
  return razdoblje < BLAGDAN_RAZDOBLJE_BROJ;
}

static uint8_t ogranicenoZvonoSuncevogDogadaja(uint8_t zvono) {
  return (zvono >= 1 && zvono <= 2) ? zvono : 1;
}

static int16_t ogranicenaOdgodaSuncevogDogadajaMin(int odgodaMin) {
  int16_t najblizaOdgoda = DOPUSTENE_SUNCEVE_ODGODE_MIN[0];
  int najmanjaRazlika = abs(odgodaMin - static_cast<int>(najblizaOdgoda));

  for (size_t i = 1; i < BROJ_DOPUSTENIH_SUNCEVIH_ODGODA; ++i) {
    const int16_t kandidat = DOPUSTENE_SUNCEVE_ODGODE_MIN[i];
    const int razlika = abs(odgodaMin - static_cast<int>(kandidat));
    if (razlika < najmanjaRazlika) {
      najblizaOdgoda = kandidat;
      najmanjaRazlika = razlika;
    }
  }

  return najblizaOdgoda;
}

static uint8_t sanitizirajMaskuSuncevihDogadaja(uint8_t maska) {
  return maska & MASKA_SUNCEVIH_DOGADAJA;
}

static uint8_t sanitizirajMaskuBlagdanskogSlavljenja(uint8_t maska) {
  return maska & MASKA_BLAGDANSKOG_SLAVLJENJA;
}

static uint8_t sanitizirajMaskuBlagdanskihRazdoblja(uint8_t maska) {
  return maska & MASKA_BLAGDANSKIH_RAZDOBLJA;
}

static bool jeUnutarTihihSati(int sat, int minuta) {
  sat = constrain(sat, 0, 23);
  minuta = constrain(minuta, 0, 59);

  if (postavke.tihiSatiOd == postavke.tihiSatiDo) {
    return false;
  }

  const int minuteUDanu = sat * 60 + minuta;
  const int tihiOdMinute = postavke.tihiSatiOd * 60;
  const int tihiDoMinute = postavke.tihiSatiDo * 60;

  // Puni sat na pocetku tihog raspona jos smije otkucati.
  // Primjer: za 22-6, trenutak 22:00 je jos dozvoljen, a tisina krece nakon toga.
  if (minuteUDanu == tihiOdMinute) {
    return false;
  }

  if (tihiOdMinute < tihiDoMinute) {
    return minuteUDanu >= tihiOdMinute && minuteUDanu < tihiDoMinute;
  }

  return minuteUDanu >= tihiOdMinute || minuteUDanu < tihiDoMinute;
}

static bool procitajUPSModIzMaskeRazdoblja(uint8_t maska) {
  return (maska & ZASTAVICA_UPS_MODA) != 0;
}

static bool procitajKocnicuZvonaIzMaskeRazdoblja(uint8_t maska) {
  return (maska & ZASTAVICA_RAD_BEZ_KOCNICE_ZVONA) == 0;
}

static uint8_t kodirajMaskuRazdobljaSZastavicama(uint8_t maskaRazdoblja,
                                                 bool upsModOmogucen,
                                                 bool kocnicaZvonaOmogucena) {
  const uint8_t osnovnaMaska = sanitizirajMaskuBlagdanskihRazdoblja(maskaRazdoblja);
  uint8_t rezultat = osnovnaMaska;
  if (!kocnicaZvonaOmogucena) {
    rezultat = static_cast<uint8_t>(rezultat | ZASTAVICA_RAD_BEZ_KOCNICE_ZVONA);
  }
  if (upsModOmogucen) {
    rezultat = static_cast<uint8_t>(rezultat | ZASTAVICA_UPS_MODA);
  }
  return rezultat;
}

static uint8_t sanitizirajPohranjenuMaskuBlagdanskihRazdoblja(uint8_t maska) {
  return kodirajMaskuRazdobljaSZastavicama(
      maska,
      procitajUPSModIzMaskeRazdoblja(maska),
      procitajKocnicuZvonaIzMaskeRazdoblja(maska));
}

static uint8_t ograniceniSviSvetiPocetakSat(uint8_t sat) {
  return (sat <= 20) ? sat : SVI_SVETI_ZADANI_POCETAK_SAT;
}

static uint8_t ograniceniSviSvetiZavrsetakSat(uint8_t sat) {
  return (sat >= 6 && sat <= 23) ? sat : SVI_SVETI_ZADANI_ZAVRSETAK_SAT;
}

static uint8_t brojDanaUMjesecuBezPrijestupneGodine(uint8_t mjesec) {
  switch (mjesec) {
    case 1:
    case 3:
    case 5:
    case 7:
    case 8:
    case 10:
    case 12:
      return 31;
    case 4:
    case 6:
    case 9:
    case 11:
      return 30;
    case 2:
      return 29;
    default:
      return 31;
  }
}

static uint8_t ograniceniSatBlagdanskeMise(uint8_t sat) {
  return static_cast<uint8_t>(constrain(static_cast<int>(sat), 0, 23));
}

static uint8_t ogranicenaMinutaBlagdanskeMise(uint8_t minuta) {
  return static_cast<uint8_t>(constrain(static_cast<int>(minuta), 0, 59));
}

static void sanitizirajRedovituMisuPostavku(bool& omogucena, uint8_t& satMise, uint8_t& minutaMise) {
  if (!omogucena) {
    satMise = 0;
    minutaMise = 0;
    return;
  }

  satMise = ograniceniSatBlagdanskeMise(satMise);
  minutaMise = ogranicenaMinutaBlagdanskeMise(minutaMise);
}

static void postaviZadaniNepomicniBlagdan(uint8_t indeks, NepomicniBlagdanPostavka& blagdan) {
  if (indeks >= BROJ_NEPOMICNIH_BLAGDANA) {
    blagdan.omogucen = false;
    blagdan.mjesec = 0;
    blagdan.dan = 0;
    blagdan.satMise = 0;
    blagdan.minutaMise = 0;
    return;
  }

  blagdan.mjesec = ZADANI_NEPOMICNI_BLAGDANI[indeks].mjesec;
  blagdan.dan = ZADANI_NEPOMICNI_BLAGDANI[indeks].dan;
}

static void postaviZadaniPomicniBlagdan(uint8_t indeks, PomicniBlagdanPostavka& blagdan) {
  if (indeks >= BROJ_POMICNIH_BLAGDANA) {
    blagdan.omogucen = false;
    blagdan.pomakOdUskrsaDana = 0;
    blagdan.satMise = 0;
    blagdan.minutaMise = 0;
    return;
  }

  blagdan.pomakOdUskrsaDana = ZADANI_POMICNI_BLAGDANI[indeks].pomakOdUskrsaDana;
}

static void sanitizirajNepomicniBlagdan(uint8_t indeks, NepomicniBlagdanPostavka& blagdan) {
  postaviZadaniNepomicniBlagdan(indeks, blagdan);
  if (!blagdan.omogucen) {
    blagdan.satMise = 0;
    blagdan.minutaMise = 0;
    return;
  }

  blagdan.satMise = ograniceniSatBlagdanskeMise(blagdan.satMise);
  blagdan.minutaMise = ogranicenaMinutaBlagdanskeMise(blagdan.minutaMise);
}

static void sanitizirajPomicniBlagdan(uint8_t indeks, PomicniBlagdanPostavka& blagdan) {
  postaviZadaniPomicniBlagdan(indeks, blagdan);
  if (!blagdan.omogucen) {
    blagdan.satMise = 0;
    blagdan.minutaMise = 0;
    return;
  }

  blagdan.satMise = ograniceniSatBlagdanskeMise(blagdan.satMise);
  blagdan.minutaMise = ogranicenaMinutaBlagdanskeMise(blagdan.minutaMise);
}

static uint8_t sanitizirajOznakuCavla(uint8_t cavao, uint8_t brojMjestaZaCavle) {
  if (cavao > brojMjestaZaCavle) {
    return 0;
  }
  return cavao;
}

static uint8_t sanitizirajOznakuCavlaZvona(uint8_t cavao) {
  if (cavao > 4) {
    return 0;
  }
  return cavao;
}

static unsigned long minuteUMiliseconde(uint8_t minute) {
  return static_cast<unsigned long>(ogranicenoTrajanjeCavla(minute)) * 60000UL;
}

static uint8_t ogranicenaOdgodaSlavljenjaSekunde(uint8_t sekunde) {
  static const uint8_t DOPUSTENE_ODGODE_S[] = {15, 30, 45, 60};
  uint8_t najbliza = DOPUSTENE_ODGODE_S[0];
  int najmanjaRazlika = abs(static_cast<int>(sekunde) - static_cast<int>(najbliza));

  for (size_t i = 1; i < (sizeof(DOPUSTENE_ODGODE_S) / sizeof(DOPUSTENE_ODGODE_S[0])); ++i) {
    const uint8_t kandidat = DOPUSTENE_ODGODE_S[i];
    const int razlika = abs(static_cast<int>(sekunde) - static_cast<int>(kandidat));
    if (razlika < najmanjaRazlika) {
      najbliza = kandidat;
      najmanjaRazlika = razlika;
    }
  }

  return najbliza;
}

static uint16_t izracunajChecksumPostavki(const EepromLayout::PostavkeSpremnik& ulaz) {
  EepromLayout::PostavkeSpremnik kopija = ulaz;
  kopija.checksum = 0;

  const uint8_t* podaci = reinterpret_cast<const uint8_t*>(&kopija);
  uint16_t suma = 0;
  for (size_t i = 0; i < sizeof(kopija); i++) {
    suma = static_cast<uint16_t>((suma << 1) | (suma >> 15));
    suma = static_cast<uint16_t>(suma + podaci[i] + 0x3D);
  }
  return suma;
}

static uint16_t izracunajChecksumSuncevihDogadaja(
    const EepromLayout::SunceviDogadajiSpremnik& ulaz) {
  EepromLayout::SunceviDogadajiSpremnik kopija = ulaz;
  kopija.checksum = 0;

  const uint8_t* podaci = reinterpret_cast<const uint8_t*>(&kopija);
  uint16_t suma = 0;
  for (size_t i = 0; i < sizeof(kopija); ++i) {
    suma = static_cast<uint16_t>((suma << 1) | (suma >> 15));
    suma = static_cast<uint16_t>(suma + podaci[i] + 0x27);
  }
  return suma;
}

static uint16_t izracunajChecksumBlagdana(const EepromLayout::BlagdaniSpremnik& ulaz) {
  EepromLayout::BlagdaniSpremnik kopija = ulaz;
  kopija.checksum = 0;

  const uint8_t* podaci = reinterpret_cast<const uint8_t*>(&kopija);
  uint16_t suma = 0;
  for (size_t i = 0; i < sizeof(kopija); ++i) {
    suma = static_cast<uint16_t>((suma << 1) | (suma >> 15));
    suma = static_cast<uint16_t>(suma + podaci[i] + 0x19);
  }
  return suma;
}

static uint16_t izracunajChecksumMisa(const EepromLayout::MiseSpremnik& ulaz) {
  EepromLayout::MiseSpremnik kopija = ulaz;
  kopija.checksum = 0;

  const uint8_t* podaci = reinterpret_cast<const uint8_t*>(&kopija);
  uint16_t suma = 0;
  for (size_t i = 0; i < sizeof(kopija); ++i) {
    suma = static_cast<uint16_t>((suma + podaci[i]) & 0xFFFFU);
  }
  return suma;
}

static void pripremiIntegritetPostavki(EepromLayout::PostavkeSpremnik& cilj) {
  cilj.potpis = EepromLayout::POSTAVKE_POTPIS;
  cilj.verzija = EepromLayout::POSTAVKE_VERZIJA;
  cilj.checksum = izracunajChecksumPostavki(cilj);
}

static bool jeValjanMrezniTekst(const char* ulaz, size_t maxDuljina, bool dopustiPrazno) {
  bool imaZnakova = false;
  for (size_t i = 0; i < maxDuljina; i++) {
    const char c = ulaz[i];
    if (c == '\0') {
      return dopustiPrazno ? true : imaZnakova;
    }
    if (!isprint(static_cast<unsigned char>(c))) {
      return false;
    }
    if (c == '|') {
      return false;
    }
    imaZnakova = true;
  }
  return false;
}

static bool jeValjanIPv4Tekst(const char* ulaz, size_t maxDuljina) {
  bool imaZnamenku = false;
  for (size_t i = 0; i < maxDuljina; i++) {
    const char c = ulaz[i];
    if (c == '\0') {
      return imaZnamenku;
    }
    if (!isdigit(static_cast<unsigned char>(c)) && c != '.') {
      return false;
    }
    if (isdigit(static_cast<unsigned char>(c))) {
      imaZnamenku = true;
    }
  }
  return false;
}

static void osigurajNullTerminiraneMreznePostavke(EepromLayout::PostavkeSpremnik& spremnik) {
  spremnik.wifiSsid[sizeof(spremnik.wifiSsid) - 1] = '\0';
  spremnik.wifiLozinka[sizeof(spremnik.wifiLozinka) - 1] = '\0';
  spremnik.statickaIp[sizeof(spremnik.statickaIp) - 1] = '\0';
  spremnik.mreznaMaska[sizeof(spremnik.mreznaMaska) - 1] = '\0';
  spremnik.zadaniGateway[sizeof(spremnik.zadaniGateway) - 1] = '\0';
  spremnik.ntpServer[sizeof(spremnik.ntpServer) - 1] = '\0';
}

static bool sanitizirajMreznaPolja(EepromLayout::PostavkeSpremnik& spremnik) {
  bool biloPromjena = false;
  osigurajNullTerminiraneMreznePostavke(spremnik);

  if (!jeValjanMrezniTekst(spremnik.wifiSsid, sizeof(spremnik.wifiSsid), true)) {
    strncpy(spremnik.wifiSsid, "WiFi", sizeof(spremnik.wifiSsid) - 1);
    spremnik.wifiSsid[sizeof(spremnik.wifiSsid) - 1] = '\0';
    biloPromjena = true;
  }
  if (!jeValjanMrezniTekst(spremnik.wifiLozinka, sizeof(spremnik.wifiLozinka), true)) {
    strncpy(spremnik.wifiLozinka, "password", sizeof(spremnik.wifiLozinka) - 1);
    spremnik.wifiLozinka[sizeof(spremnik.wifiLozinka) - 1] = '\0';
    biloPromjena = true;
  }
  if (!jeValjanIPv4Tekst(spremnik.statickaIp, sizeof(spremnik.statickaIp))) {
    strncpy(spremnik.statickaIp, "192.168.1.100", sizeof(spremnik.statickaIp) - 1);
    spremnik.statickaIp[sizeof(spremnik.statickaIp) - 1] = '\0';
    biloPromjena = true;
  }
  if (!jeValjanIPv4Tekst(spremnik.mreznaMaska, sizeof(spremnik.mreznaMaska))) {
    strncpy(spremnik.mreznaMaska, "255.255.255.0", sizeof(spremnik.mreznaMaska) - 1);
    spremnik.mreznaMaska[sizeof(spremnik.mreznaMaska) - 1] = '\0';
    biloPromjena = true;
  }
  if (!jeValjanIPv4Tekst(spremnik.zadaniGateway, sizeof(spremnik.zadaniGateway))) {
    strncpy(spremnik.zadaniGateway, "192.168.1.1", sizeof(spremnik.zadaniGateway) - 1);
    spremnik.zadaniGateway[sizeof(spremnik.zadaniGateway) - 1] = '\0';
    biloPromjena = true;
  }
  if (!jeValjanMrezniTekst(
          dohvatiNtpServerBezZastavice(spremnik.ntpServer),
          sizeof(spremnik.ntpServer) - (jeKodiranNtpStatus(spremnik.ntpServer) ? 1 : 0),
          false)) {
    kodirajNtpServer(
        spremnik.ntpServer,
        sizeof(spremnik.ntpServer),
        "pool.ntp.org",
        procitajNtpOmogucenostIzTeksta(spremnik.ntpServer));
    biloPromjena = true;
  }
  if (spremnik.wifiOmogucen > 1) {
    spremnik.wifiOmogucen = true;
    biloPromjena = true;
  }
  if (spremnik.rs485Omogucen > 1) {
    spremnik.rs485Omogucen = false;
    biloPromjena = true;
  }

  return biloPromjena;
}

static bool jeValjanEEPROMZapisPostavki(const EepromLayout::PostavkeSpremnik& ucitano) {
  if (ucitano.potpis != EepromLayout::POSTAVKE_POTPIS) {
    return false;
  }
  if (ucitano.verzija != EepromLayout::POSTAVKE_VERZIJA) {
    return false;
  }
  return ucitano.checksum == izracunajChecksumPostavki(ucitano);
}

static bool ucitajAktualniSpremnikSkeniranjem(EepromLayout::PostavkeSpremnik& spremnik) {
  EepromLayout::PostavkeSpremnik kandidat{};
  if (!VanjskiEEPROM::procitaj(
          EepromLayout::BAZA_POSTAVKE, &kandidat, sizeof(kandidat))) {
    return false;
  }
  if (!jeValjanEEPROMZapisPostavki(kandidat)) {
    return false;
  }

  spremnik = kandidat;
  return true;
}

static bool ucitajSpremnikIliZadano(EepromLayout::PostavkeSpremnik& spremnik) {
  if (!ucitajAktualniSpremnikSkeniranjem(spremnik)) {
    spremnik = napraviZadanePostavke();
    return false;
  }

  return true;
}

static bool obrisiSegmentEeproma(int baznaAdresa, int duljina) {
  uint8_t prazno[32];
  memset(prazno, 0xFF, sizeof(prazno));

  for (int adresa = baznaAdresa; adresa < (baznaAdresa + duljina); adresa += static_cast<int>(sizeof(prazno))) {
    const size_t blok =
        static_cast<size_t>(min(static_cast<int>(sizeof(prazno)), (baznaAdresa + duljina) - adresa));
    if (!VanjskiEEPROM::zapisi(adresa, prazno, blok)) {
      return false;
    }
  }

  return true;
}

static void ocistiSegmenteNakonPromjeneLayouta() {
  bool uspjeh = true;

  uspjeh &= obrisiSegmentEeproma(
      EepromLayout::BAZA_ZADNJA_SINKRONIZACIJA,
      EepromLayout::SLOTOVI_ZADNJA_SINKRONIZACIJA * EepromLayout::SLOT_SIZE_ZADNJA_SINKRONIZACIJA);
  uspjeh &= obrisiSegmentEeproma(
      EepromLayout::BAZA_POSTAVKE,
      EepromLayout::SLOTOVI_POSTAVKE * EepromLayout::SLOT_SIZE_POSTAVKE);
  uspjeh &= obrisiSegmentEeproma(
      EepromLayout::BAZA_DST_STATUS,
      EepromLayout::SLOTOVI_DST_STATUS * EepromLayout::SLOT_SIZE_DST_STATUS);
  uspjeh &= obrisiSegmentEeproma(
      EepromLayout::BAZA_SUNCEVI_DOGADAJI,
      EepromLayout::SLOTOVI_SUNCEVI_DOGADAJI * EepromLayout::SLOT_SIZE_SUNCEVI_DOGADAJI);
  uspjeh &= obrisiSegmentEeproma(
      EepromLayout::BAZA_BLAGDANI,
      EepromLayout::SLOTOVI_BLAGDANI * EepromLayout::SLOT_SIZE_BLAGDANI);
  uspjeh &= obrisiSegmentEeproma(
      EepromLayout::BAZA_MISE,
      EepromLayout::SLOTOVI_MISE * EepromLayout::SLOT_SIZE_MISE);
  uspjeh &= WearLeveling::obrisiSveMetapodatke();

  resetirajIzvorSinkronizacijeNaRTC();
  invalidirajMrezniCache();

  posaljiPCLog(uspjeh
                   ? F("Postavke: EEPROM segmenti pocisceni za novu reviziju")
                   : F("Postavke: WARNING - ciscenje EEPROM segmenata nije potpuno uspjelo"));
}

static bool sanitizirajRadnaPolja(EepromLayout::PostavkeSpremnik& spremnik) {
  bool trebaSpremiti = false;

  if (spremnik.satOd < 0 || spremnik.satOd > 23) {
    spremnik.satOd = OTKUCAVANJE_CIJELI_DAN_OD;
    trebaSpremiti = true;
  }
  if (spremnik.satDo < 0 || spremnik.satDo > 23) {
    spremnik.satDo = OTKUCAVANJE_CIJELI_DAN_DO;
    trebaSpremiti = true;
  }
  if (spremnik.tihiSatiOd < 0 || spremnik.tihiSatiOd > 23) {
    spremnik.tihiSatiOd = 22;
    trebaSpremiti = true;
  }
  if (spremnik.tihiSatiDo < 0 || spremnik.tihiSatiDo > 23) {
    spremnik.tihiSatiDo = 6;
    trebaSpremiti = true;
  }
  {
    const bool plocaAktivna = spremnik.plocaPocetakMinuta >= 0;
    int plocaPocetak = dekodirajPocetakPloceMinuta(spremnik.plocaPocetakMinuta);
    int plocaKraj = spremnik.plocaKrajMinuta;

    if (plocaPocetak < 0 || plocaPocetak > (23 * 60 + 59) ||
        plocaKraj < 0 || plocaKraj > (23 * 60 + 59)) {
      plocaPocetak = PLOCA_ZADANI_POCETAK_MINUTA;
      plocaKraj = PLOCA_ZADANI_KRAJ_MINUTA;
      spremnik.plocaPocetakMinuta = kodirajPocetakPloceMinuta(plocaPocetak, true);
      spremnik.plocaKrajMinuta = plocaKraj;
      trebaSpremiti = true;
    } else {
      const int normaliziraniPocetak = normalizirajMinutuPloceNaBlok(plocaPocetak);
      int normaliziraniKraj = normalizirajMinutuPloceNaBlok(plocaKraj);
      if (plocaAktivna && normaliziraniKraj <= normaliziraniPocetak) {
        normaliziraniKraj = min(normaliziraniPocetak + PLOCA_MINUTNI_BLOK, PLOCA_ZADNJA_CETVRT);
      }

      const int kodiraniPocetak = kodirajPocetakPloceMinuta(normaliziraniPocetak, plocaAktivna);
      if (spremnik.plocaPocetakMinuta != kodiraniPocetak ||
          spremnik.plocaKrajMinuta != normaliziraniKraj) {
        spremnik.plocaPocetakMinuta = kodiraniPocetak;
        spremnik.plocaKrajMinuta = normaliziraniKraj;
        trebaSpremiti = true;
      }
    }
  }
  if (spremnik.trajanjeImpulsaCekicaMs < 10 || spremnik.trajanjeImpulsaCekicaMs > 300) {
    spremnik.trajanjeImpulsaCekicaMs = 150;
    trebaSpremiti = true;
  }
  if (spremnik.pauzaIzmeduUdaraca < 100) {
    spremnik.pauzaIzmeduUdaraca = 400;
    trebaSpremiti = true;
  }
  if (spremnik.trajanjeZvonjenjaRadniMin < 2 || spremnik.trajanjeZvonjenjaRadniMin > 4) {
    spremnik.trajanjeZvonjenjaRadniMin = 2;
    trebaSpremiti = true;
  }
  if (spremnik.trajanjeZvonjenjaNedjeljaMin < 2 || spremnik.trajanjeZvonjenjaNedjeljaMin > 4) {
    spremnik.trajanjeZvonjenjaNedjeljaMin = 3;
    trebaSpremiti = true;
  }
  if (spremnik.trajanjeSlavljenjaMin < 2 || spremnik.trajanjeSlavljenjaMin > 4) {
    spremnik.trajanjeSlavljenjaMin = 2;
    trebaSpremiti = true;
  }
  {
    const uint8_t novaOdgodaSekunde =
        ogranicenaOdgodaSlavljenjaSekunde(spremnik.slavljenjePrijeZvonjenja);
    if (spremnik.slavljenjePrijeZvonjenja != novaOdgodaSekunde) {
      spremnik.slavljenjePrijeZvonjenja = novaOdgodaSekunde;
      trebaSpremiti = true;
    }
  }
  if (spremnik.brojZvona != FIKSNI_BROJ_ZVONA) {
    spremnik.brojZvona = FIKSNI_BROJ_ZVONA;
    trebaSpremiti = true;
  }
  if (spremnik.brojMjestaZaCavle != FIKSNI_BROJ_MJESTA_ZA_CAVLE) {
    spremnik.brojMjestaZaCavle = FIKSNI_BROJ_MJESTA_ZA_CAVLE;
    trebaSpremiti = true;
  }

  for (uint8_t i = 0; i < 4; i++) {
    const uint8_t noviRadni = sanitizirajOznakuCavlaZvona(
        sanitizirajOznakuCavla(spremnik.cavliRadni[i], spremnik.brojMjestaZaCavle));
    if (noviRadni != spremnik.cavliRadni[i]) {
      spremnik.cavliRadni[i] = noviRadni;
      trebaSpremiti = true;
    }

    const uint8_t noviNedjelja = sanitizirajOznakuCavlaZvona(
        sanitizirajOznakuCavla(spremnik.cavliNedjelja[i], spremnik.brojMjestaZaCavle));
    if (noviNedjelja != spremnik.cavliNedjelja[i]) {
      spremnik.cavliNedjelja[i] = noviNedjelja;
      trebaSpremiti = true;
    }
  }

  const uint8_t noviSlavljenje = FIKSNI_CAVAO_SLAVLJENJA;
  if (noviSlavljenje != spremnik.cavaoSlavljenje) {
    spremnik.cavaoSlavljenje = noviSlavljenje;
    trebaSpremiti = true;
  }

  if (spremnik.modSlavljenja < 1 || spremnik.modSlavljenja > 2) {
    spremnik.modSlavljenja = 1;
    trebaSpremiti = true;
  }

  if (spremnik.modOtkucavanja > 2) {
    spremnik.modOtkucavanja = 2;
    trebaSpremiti = true;
  }

  if (spremnik.modMrtvackog < 1 || spremnik.modMrtvackog > 2) {
    spremnik.modMrtvackog = 1;
    trebaSpremiti = true;
  }

  const uint8_t novaInercijaZvona1 = ogranicenaInercijaZvonaSekunde(spremnik.inercijaZvona1Sekunde);
  if (novaInercijaZvona1 != spremnik.inercijaZvona1Sekunde) {
    spremnik.inercijaZvona1Sekunde = novaInercijaZvona1;
    trebaSpremiti = true;
  }

  const uint8_t novaInercijaZvona2 = ogranicenaInercijaZvonaSekunde(spremnik.inercijaZvona2Sekunde);
  if (novaInercijaZvona2 != spremnik.inercijaZvona2Sekunde) {
    spremnik.inercijaZvona2Sekunde = novaInercijaZvona2;
    trebaSpremiti = true;
  }

  if (spremnik.imaKazaljke > 1) {
    spremnik.imaKazaljke = true;
    trebaSpremiti = true;
  }

  if (spremnik.logiranjeOmoguceno > 1) {
    spremnik.logiranjeOmoguceno = true;
    trebaSpremiti = true;
  }

  {
    const uint8_t novaMaska = sanitizirajMaskuBlagdanskogSlavljenja(spremnik.blagdaniSlavljenjeMaska);
    if (novaMaska != spremnik.blagdaniSlavljenjeMaska) {
      spremnik.blagdaniSlavljenjeMaska = novaMaska;
      trebaSpremiti = true;
    }
  }

  {
    const uint8_t novaMaska =
        sanitizirajPohranjenuMaskuBlagdanskihRazdoblja(spremnik.blagdaniRazdobljaMaska);
    if (novaMaska != spremnik.blagdaniRazdobljaMaska) {
      spremnik.blagdaniRazdobljaMaska = novaMaska;
      trebaSpremiti = true;
    }
  }

  if (spremnik.sviSvetiOmoguceno > 1) {
    spremnik.sviSvetiOmoguceno = false;
    trebaSpremiti = true;
  }

  {
    const uint8_t noviSat = ograniceniSviSvetiPocetakSat(spremnik.sviSvetiPocetakSat);
    if (noviSat != spremnik.sviSvetiPocetakSat) {
      spremnik.sviSvetiPocetakSat = noviSat;
      trebaSpremiti = true;
    }
  }

  {
    const uint8_t noviSat = ograniceniSviSvetiZavrsetakSat(spremnik.sviSvetiZavrsetakSat);
    if (noviSat != spremnik.sviSvetiZavrsetakSat) {
      spremnik.sviSvetiZavrsetakSat = noviSat;
      trebaSpremiti = true;
    }
  }

  return trebaSpremiti;
}

static void ucitajRadnePostavkeIzSpremnika(const EepromLayout::PostavkeSpremnik& spremnik) {
  postavke.satOd = spremnik.satOd;
  postavke.satDo = spremnik.satDo;
  postavke.tihiSatiOd = spremnik.tihiSatiOd;
  postavke.tihiSatiDo = spremnik.tihiSatiDo;
  postavke.plocaPocetakMinuta = spremnik.plocaPocetakMinuta;
  postavke.plocaKrajMinuta = spremnik.plocaKrajMinuta;
  postavke.trajanjeImpulsaCekicaMs = spremnik.trajanjeImpulsaCekicaMs;
  postavke.pauzaIzmeduUdaraca = spremnik.pauzaIzmeduUdaraca;
  postavke.trajanjeZvonjenjaRadniMin = spremnik.trajanjeZvonjenjaRadniMin;
  postavke.trajanjeZvonjenjaNedjeljaMin = spremnik.trajanjeZvonjenjaNedjeljaMin;
  postavke.trajanjeSlavljenjaMin = spremnik.trajanjeSlavljenjaMin;
  postavke.slavljenjePrijeZvonjenja = spremnik.slavljenjePrijeZvonjenja;
  postavke.inercijaZvona1Sekunde = spremnik.inercijaZvona1Sekunde;
  postavke.inercijaZvona2Sekunde = spremnik.inercijaZvona2Sekunde;
  memcpy(postavke.cavliRadni, spremnik.cavliRadni, sizeof(postavke.cavliRadni));
  memcpy(postavke.cavliNedjelja, spremnik.cavliNedjelja, sizeof(postavke.cavliNedjelja));
  postavke.koristiDhcp = spremnik.koristiDhcp;
  postavke.lcdPozadinskoOsvjetljenje = spremnik.lcdPozadinskoOsvjetljenje;
  postavke.logiranjeOmoguceno = spremnik.logiranjeOmoguceno;
  postavke.blagdaniSlavljenjeMaska =
      sanitizirajMaskuBlagdanskogSlavljenja(spremnik.blagdaniSlavljenjeMaska);
  postavke.blagdaniRazdobljaMaska =
      sanitizirajMaskuBlagdanskihRazdoblja(spremnik.blagdaniRazdobljaMaska);
  postavke.upsModOmogucen = procitajUPSModIzMaskeRazdoblja(spremnik.blagdaniRazdobljaMaska);
  postavke.kocnicaZvonaOmogucena =
      procitajKocnicuZvonaIzMaskeRazdoblja(spremnik.blagdaniRazdobljaMaska);
  postavke.sviSvetiOmoguceno = spremnik.sviSvetiOmoguceno;
  postavke.sviSvetiPocetakSat = ograniceniSviSvetiPocetakSat(spremnik.sviSvetiPocetakSat);
  postavke.sviSvetiZavrsetakSat = ograniceniSviSvetiZavrsetakSat(spremnik.sviSvetiZavrsetakSat);
  postavke.modSlavljenja = spremnik.modSlavljenja;
  postavke.modOtkucavanja = spremnik.modOtkucavanja;
  postavke.modMrtvackog = spremnik.modMrtvackog;
  postavke.ntpOmogucen = procitajNtpOmogucenostIzTeksta(spremnik.ntpServer);
  postavke.wifiOmogucen = spremnik.wifiOmogucen;
  postavke.rs485Omogucen = spremnik.rs485Omogucen;
  postavke.imaKazaljke = spremnik.imaKazaljke;
}

static void upisiRadnePostavkeUSpremnik(EepromLayout::PostavkeSpremnik& spremnik) {
  spremnik.satOd = postavke.satOd;
  spremnik.satDo = postavke.satDo;
  spremnik.tihiSatiOd = postavke.tihiSatiOd;
  spremnik.tihiSatiDo = postavke.tihiSatiDo;
  spremnik.plocaPocetakMinuta = postavke.plocaPocetakMinuta;
  spremnik.plocaKrajMinuta = postavke.plocaKrajMinuta;
  spremnik.trajanjeImpulsaCekicaMs = postavke.trajanjeImpulsaCekicaMs;
  spremnik.pauzaIzmeduUdaraca = postavke.pauzaIzmeduUdaraca;
  spremnik.trajanjeZvonjenjaRadniMin = postavke.trajanjeZvonjenjaRadniMin;
  spremnik.trajanjeZvonjenjaNedjeljaMin = postavke.trajanjeZvonjenjaNedjeljaMin;
  spremnik.trajanjeSlavljenjaMin = postavke.trajanjeSlavljenjaMin;
  spremnik.slavljenjePrijeZvonjenja = postavke.slavljenjePrijeZvonjenja;
  spremnik.inercijaZvona1Sekunde = postavke.inercijaZvona1Sekunde;
  spremnik.inercijaZvona2Sekunde = postavke.inercijaZvona2Sekunde;
  spremnik.brojZvona = FIKSNI_BROJ_ZVONA;
  spremnik.brojMjestaZaCavle = FIKSNI_BROJ_MJESTA_ZA_CAVLE;
  memcpy(spremnik.cavliRadni, postavke.cavliRadni, sizeof(spremnik.cavliRadni));
  memcpy(spremnik.cavliNedjelja, postavke.cavliNedjelja, sizeof(spremnik.cavliNedjelja));
  spremnik.cavaoSlavljenje = FIKSNI_CAVAO_SLAVLJENJA;
  spremnik.koristiDhcp = postavke.koristiDhcp;
  spremnik.lcdPozadinskoOsvjetljenje = postavke.lcdPozadinskoOsvjetljenje;
  spremnik.logiranjeOmoguceno = postavke.logiranjeOmoguceno;
  spremnik.blagdaniSlavljenjeMaska =
      sanitizirajMaskuBlagdanskogSlavljenja(postavke.blagdaniSlavljenjeMaska);
  spremnik.blagdaniRazdobljaMaska =
      kodirajMaskuRazdobljaSZastavicama(
          postavke.blagdaniRazdobljaMaska,
          postavke.upsModOmogucen,
          postavke.kocnicaZvonaOmogucena);
  spremnik.sviSvetiOmoguceno = postavke.sviSvetiOmoguceno;
  spremnik.sviSvetiPocetakSat = ograniceniSviSvetiPocetakSat(postavke.sviSvetiPocetakSat);
  spremnik.sviSvetiZavrsetakSat =
      ograniceniSviSvetiZavrsetakSat(postavke.sviSvetiZavrsetakSat);
  spremnik.modSlavljenja = postavke.modSlavljenja;
  spremnik.modOtkucavanja = postavke.modOtkucavanja;
  spremnik.modMrtvackog = postavke.modMrtvackog;
  spremnik.wifiOmogucen = postavke.wifiOmogucen;
  spremnik.rs485Omogucen = postavke.rs485Omogucen;
  spremnik.imaKazaljke = postavke.imaKazaljke;
}

static void invalidirajMrezniCache() {
  memset(mrezniTekstBuffer, 0, sizeof(mrezniTekstBuffer));
}

static const char* dohvatiMrezniTekstIzSpremnika(AktivnaMreznaSekcija trazenaSekcija) {
  EepromLayout::PostavkeSpremnik spremnik = napraviZadanePostavke();
  ucitajSpremnikIliZadano(spremnik);
  sanitizirajMreznaPolja(spremnik);

  switch (trazenaSekcija) {
    case MREZNA_SEKCIJA_WIFI:
      strncpy(mrezniTekstBuffer, spremnik.wifiSsid, sizeof(mrezniTekstBuffer) - 1);
      break;
    case MREZNA_SEKCIJA_SINKRONIZACIJA:
      strncpy(mrezniTekstBuffer, spremnik.ntpServer, sizeof(mrezniTekstBuffer) - 1);
      break;
    case MREZNA_SEKCIJA_NISTA:
    default:
      mrezniTekstBuffer[0] = '\0';
      break;
  }
  mrezniTekstBuffer[sizeof(mrezniTekstBuffer) - 1] = '\0';
  return mrezniTekstBuffer;
}

static const char* dohvatiWifiSsidIzSpremnika() {
  EepromLayout::PostavkeSpremnik spremnik = napraviZadanePostavke();
  ucitajSpremnikIliZadano(spremnik);
  sanitizirajMreznaPolja(spremnik);
  strncpy(mrezniTekstBuffer, spremnik.wifiSsid, sizeof(mrezniTekstBuffer) - 1);
  mrezniTekstBuffer[sizeof(mrezniTekstBuffer) - 1] = '\0';
  return mrezniTekstBuffer;
}

static const char* dohvatiWifiLozinkuIzSpremnika() {
  EepromLayout::PostavkeSpremnik spremnik = napraviZadanePostavke();
  ucitajSpremnikIliZadano(spremnik);
  sanitizirajMreznaPolja(spremnik);
  strncpy(mrezniTekstBuffer, spremnik.wifiLozinka, sizeof(mrezniTekstBuffer) - 1);
  mrezniTekstBuffer[sizeof(mrezniTekstBuffer) - 1] = '\0';
  return mrezniTekstBuffer;
}

static const char* dohvatiStatickuIpIzSpremnika() {
  EepromLayout::PostavkeSpremnik spremnik = napraviZadanePostavke();
  ucitajSpremnikIliZadano(spremnik);
  sanitizirajMreznaPolja(spremnik);
  strncpy(mrezniTekstBuffer, spremnik.statickaIp, sizeof(mrezniTekstBuffer) - 1);
  mrezniTekstBuffer[sizeof(mrezniTekstBuffer) - 1] = '\0';
  return mrezniTekstBuffer;
}

static const char* dohvatiMreznuMaskuIzSpremnika() {
  EepromLayout::PostavkeSpremnik spremnik = napraviZadanePostavke();
  ucitajSpremnikIliZadano(spremnik);
  sanitizirajMreznaPolja(spremnik);
  strncpy(mrezniTekstBuffer, spremnik.mreznaMaska, sizeof(mrezniTekstBuffer) - 1);
  mrezniTekstBuffer[sizeof(mrezniTekstBuffer) - 1] = '\0';
  return mrezniTekstBuffer;
}

static const char* dohvatiGatewayIzSpremnika() {
  EepromLayout::PostavkeSpremnik spremnik = napraviZadanePostavke();
  ucitajSpremnikIliZadano(spremnik);
  sanitizirajMreznaPolja(spremnik);
  strncpy(mrezniTekstBuffer, spremnik.zadaniGateway, sizeof(mrezniTekstBuffer) - 1);
  mrezniTekstBuffer[sizeof(mrezniTekstBuffer) - 1] = '\0';
  return mrezniTekstBuffer;
}

static void spremiSpremnikPostavki(EepromLayout::PostavkeSpremnik& spremnik) {
  sanitizirajRadnaPolja(spremnik);
  sanitizirajMreznaPolja(spremnik);
  pripremiIntegritetPostavki(spremnik);
  const bool zapisano =
      VanjskiEEPROM::zapisi(EepromLayout::BAZA_POSTAVKE, &spremnik, sizeof(spremnik));

  EepromLayout::PostavkeSpremnik provjera{};
  const bool procitano = zapisano &&
                         VanjskiEEPROM::procitaj(
                             EepromLayout::BAZA_POSTAVKE, &provjera, sizeof(provjera)) &&
                         jeValjanEEPROMZapisPostavki(provjera);

  if (!procitano) {
    posaljiPCLog(F("Postavke: ERROR - spremanje ili provjera EEPROM zapisa nije uspjela"));
  } else {
    char log[96];
    snprintf_P(log,
               sizeof(log),
               PSTR("Postavke: EEPROM potvrden OTK=%u S=%u M=%u"),
               static_cast<unsigned>(provjera.modOtkucavanja),
               static_cast<unsigned>(provjera.modSlavljenja),
               static_cast<unsigned>(provjera.modMrtvackog));
    posaljiPCLog(log);
  }

  ucitajRadnePostavkeIzSpremnika(spremnik);
  invalidirajMrezniCache();
}

static bool jeValjanEEPROMZapisSuncevihDogadaja(
    const EepromLayout::SunceviDogadajiSpremnik& spremnik) {
  return spremnik.potpis == EepromLayout::SUNCEVI_DOGADAJI_POTPIS &&
         spremnik.verzija == EepromLayout::SUNCEVI_DOGADAJI_VERZIJA &&
         spremnik.checksum == izracunajChecksumSuncevihDogadaja(spremnik);
}

static bool sanitizirajSunceveDogadajeSpremnik(EepromLayout::SunceviDogadajiSpremnik& spremnik) {
  bool trebaSpremiti = false;

  const uint8_t novaMaska = sanitizirajMaskuSuncevihDogadaja(spremnik.maskaDogadaja);
  if (novaMaska != spremnik.maskaDogadaja) {
    spremnik.maskaDogadaja = novaMaska;
    trebaSpremiti = true;
  }

  if (spremnik.nocnaRasvjetaOmogucena > 1) {
    spremnik.nocnaRasvjetaOmogucena = false;
    trebaSpremiti = true;
  }

  for (uint8_t i = 0; i < SUNCEVI_DOGADAJ_BROJ; ++i) {
    const uint8_t novoZvono = ogranicenoZvonoSuncevogDogadaja(spremnik.zvona[i]);
    if (novoZvono != spremnik.zvona[i]) {
      spremnik.zvona[i] = novoZvono;
      trebaSpremiti = true;
    }

    const int16_t novaOdgoda =
        (i == SUNCEVI_DOGADAJ_PODNE) ? 0 : ogranicenaOdgodaSuncevogDogadajaMin(spremnik.odgodeMin[i]);
    if (novaOdgoda != spremnik.odgodeMin[i]) {
      spremnik.odgodeMin[i] = novaOdgoda;
      trebaSpremiti = true;
    }
  }

  return trebaSpremiti;
}

static void pripremiIntegritetSuncevihDogadaja(EepromLayout::SunceviDogadajiSpremnik& spremnik) {
  spremnik.potpis = EepromLayout::SUNCEVI_DOGADAJI_POTPIS;
  spremnik.verzija = EepromLayout::SUNCEVI_DOGADAJI_VERZIJA;
  spremnik.checksum = izracunajChecksumSuncevihDogadaja(spremnik);
}

static void pripremiIntegritetBlagdana(EepromLayout::BlagdaniSpremnik& spremnik) {
  spremnik.potpis = EepromLayout::BLAGDANI_POTPIS;
  spremnik.verzija = EepromLayout::BLAGDANI_VERZIJA;
  spremnik.checksum = izracunajChecksumBlagdana(spremnik);
}

static void ucitajSunceveDogadajeIzSpremnika(const EepromLayout::SunceviDogadajiSpremnik& spremnik) {
  postavke.maskaSuncevihDogadaja = sanitizirajMaskuSuncevihDogadaja(spremnik.maskaDogadaja);
  postavke.nocnaRasvjetaOmogucena = spremnik.nocnaRasvjetaOmogucena;
  memcpy(postavke.zvonaSuncevihDogadaja, spremnik.zvona, sizeof(postavke.zvonaSuncevihDogadaja));
  memcpy(
      postavke.odgodeSuncevihDogadajaMin, spremnik.odgodeMin, sizeof(postavke.odgodeSuncevihDogadajaMin));
}

static void upisiSunceveDogadajeUSpremnik(EepromLayout::SunceviDogadajiSpremnik& spremnik) {
  spremnik.maskaDogadaja = sanitizirajMaskuSuncevihDogadaja(postavke.maskaSuncevihDogadaja);
  spremnik.nocnaRasvjetaOmogucena = postavke.nocnaRasvjetaOmogucena;
  memcpy(spremnik.zvona, postavke.zvonaSuncevihDogadaja, sizeof(spremnik.zvona));
  memcpy(spremnik.odgodeMin, postavke.odgodeSuncevihDogadajaMin, sizeof(spremnik.odgodeMin));
}

static void spremiSunceveDogadajeSpremnik(EepromLayout::SunceviDogadajiSpremnik& spremnik) {
  sanitizirajSunceveDogadajeSpremnik(spremnik);
  pripremiIntegritetSuncevihDogadaja(spremnik);
  const bool zapisano = VanjskiEEPROM::zapisi(
      EepromLayout::BAZA_SUNCEVI_DOGADAJI, &spremnik, sizeof(spremnik));
  EepromLayout::SunceviDogadajiSpremnik provjera{};
  const bool procitano = zapisano &&
                         VanjskiEEPROM::procitaj(
                             EepromLayout::BAZA_SUNCEVI_DOGADAJI, &provjera, sizeof(provjera)) &&
                         jeValjanEEPROMZapisSuncevihDogadaja(provjera);
  posaljiPCLog(procitano
                   ? F("Sunce: EEPROM zapis potvrden")
                   : F("Sunce: ERROR - spremanje ili provjera EEPROM zapisa nije uspjela"));
  ucitajSunceveDogadajeIzSpremnika(spremnik);
}

static bool jeValjanEEPROMZapisBlagdana(const EepromLayout::BlagdaniSpremnik& spremnik) {
  return spremnik.potpis == EepromLayout::BLAGDANI_POTPIS &&
         spremnik.verzija == EepromLayout::BLAGDANI_VERZIJA &&
         spremnik.checksum == izracunajChecksumBlagdana(spremnik);
}

static bool sanitizirajBlagdaneSpremnik(EepromLayout::BlagdaniSpremnik& spremnik) {
  bool trebaSpremiti = false;

  for (uint8_t i = 0; i < BROJ_NEPOMICNIH_BLAGDANA; ++i) {
    NepomicniBlagdanPostavka blagdan = {
      spremnik.nepomicni[i].omogucen != 0,
      spremnik.nepomicni[i].mjesec,
      spremnik.nepomicni[i].dan,
      spremnik.nepomicni[i].satMise,
      spremnik.nepomicni[i].minutaMise
    };
    const NepomicniBlagdanPostavka prije = blagdan;
    sanitizirajNepomicniBlagdan(i, blagdan);

    if (blagdan.omogucen != prije.omogucen ||
        blagdan.mjesec != prije.mjesec ||
        blagdan.dan != prije.dan ||
        blagdan.satMise != prije.satMise ||
        blagdan.minutaMise != prije.minutaMise ||
        spremnik.nepomicni[i].omogucen > 1) {
      spremnik.nepomicni[i].omogucen = blagdan.omogucen ? 1 : 0;
      spremnik.nepomicni[i].mjesec = blagdan.mjesec;
      spremnik.nepomicni[i].dan = blagdan.dan;
      spremnik.nepomicni[i].satMise = blagdan.satMise;
      spremnik.nepomicni[i].minutaMise = blagdan.minutaMise;
      trebaSpremiti = true;
    }
  }

  for (uint8_t i = 0; i < BROJ_POMICNIH_BLAGDANA; ++i) {
    PomicniBlagdanPostavka blagdan = {
      spremnik.pomicni[i].omogucen != 0,
      spremnik.pomicni[i].pomakOdUskrsaDana,
      spremnik.pomicni[i].satMise,
      spremnik.pomicni[i].minutaMise
    };
    const PomicniBlagdanPostavka prije = blagdan;
    sanitizirajPomicniBlagdan(i, blagdan);

    if (blagdan.omogucen != prije.omogucen ||
        blagdan.pomakOdUskrsaDana != prije.pomakOdUskrsaDana ||
        blagdan.satMise != prije.satMise ||
        blagdan.minutaMise != prije.minutaMise ||
        spremnik.pomicni[i].omogucen > 1) {
      spremnik.pomicni[i].omogucen = blagdan.omogucen ? 1 : 0;
      spremnik.pomicni[i].pomakOdUskrsaDana = blagdan.pomakOdUskrsaDana;
      spremnik.pomicni[i].satMise = blagdan.satMise;
      spremnik.pomicni[i].minutaMise = blagdan.minutaMise;
      trebaSpremiti = true;
    }
  }

  if (spremnik.reserved != 0) {
    spremnik.reserved = 0;
    trebaSpremiti = true;
  }

  return trebaSpremiti;
}

static void ucitajBlagdaneIzSpremnika(const EepromLayout::BlagdaniSpremnik& spremnik) {
  for (uint8_t i = 0; i < BROJ_NEPOMICNIH_BLAGDANA; ++i) {
    NepomicniBlagdanPostavka blagdan = {
      spremnik.nepomicni[i].omogucen != 0,
      spremnik.nepomicni[i].mjesec,
      spremnik.nepomicni[i].dan,
      spremnik.nepomicni[i].satMise,
      spremnik.nepomicni[i].minutaMise
    };
    sanitizirajNepomicniBlagdan(i, blagdan);
    postavke.nepomicniBlagdani[i].omogucen = blagdan.omogucen;
    postavke.nepomicniBlagdani[i].satMise = blagdan.satMise;
    postavke.nepomicniBlagdani[i].minutaMise = blagdan.minutaMise;
  }

  for (uint8_t i = 0; i < BROJ_POMICNIH_BLAGDANA; ++i) {
    PomicniBlagdanPostavka blagdan = {
      spremnik.pomicni[i].omogucen != 0,
      spremnik.pomicni[i].pomakOdUskrsaDana,
      spremnik.pomicni[i].satMise,
      spremnik.pomicni[i].minutaMise
    };
    sanitizirajPomicniBlagdan(i, blagdan);
    postavke.pomicniBlagdani[i].omogucen = blagdan.omogucen;
    postavke.pomicniBlagdani[i].satMise = blagdan.satMise;
    postavke.pomicniBlagdani[i].minutaMise = blagdan.minutaMise;
  }
}

static void upisiBlagdaneUSpremnik(EepromLayout::BlagdaniSpremnik& spremnik) {
  for (uint8_t i = 0; i < BROJ_NEPOMICNIH_BLAGDANA; ++i) {
    NepomicniBlagdanPostavka blagdan = {
      postavke.nepomicniBlagdani[i].omogucen,
      0,
      0,
      postavke.nepomicniBlagdani[i].satMise,
      postavke.nepomicniBlagdani[i].minutaMise
    };
    sanitizirajNepomicniBlagdan(i, blagdan);
    spremnik.nepomicni[i].omogucen = blagdan.omogucen ? 1 : 0;
    spremnik.nepomicni[i].mjesec = blagdan.mjesec;
    spremnik.nepomicni[i].dan = blagdan.dan;
    spremnik.nepomicni[i].satMise = blagdan.satMise;
    spremnik.nepomicni[i].minutaMise = blagdan.minutaMise;
  }

  for (uint8_t i = 0; i < BROJ_POMICNIH_BLAGDANA; ++i) {
    PomicniBlagdanPostavka blagdan = {
      postavke.pomicniBlagdani[i].omogucen,
      0,
      postavke.pomicniBlagdani[i].satMise,
      postavke.pomicniBlagdani[i].minutaMise
    };
    sanitizirajPomicniBlagdan(i, blagdan);
    spremnik.pomicni[i].omogucen = blagdan.omogucen ? 1 : 0;
    spremnik.pomicni[i].pomakOdUskrsaDana = blagdan.pomakOdUskrsaDana;
    spremnik.pomicni[i].satMise = blagdan.satMise;
    spremnik.pomicni[i].minutaMise = blagdan.minutaMise;
  }
}

static bool ucitajBlagdaneSkeniranjem(EepromLayout::BlagdaniSpremnik& spremnik) {
  EepromLayout::BlagdaniSpremnik kandidat{};
  if (!VanjskiEEPROM::procitaj(EepromLayout::BAZA_BLAGDANI, &kandidat, sizeof(kandidat))) {
    return false;
  }
  if (!jeValjanEEPROMZapisBlagdana(kandidat)) {
    return false;
  }

  spremnik = kandidat;
  return true;
}

static void spremiBlagdaneSpremnik(EepromLayout::BlagdaniSpremnik& spremnik) {
  sanitizirajBlagdaneSpremnik(spremnik);
  pripremiIntegritetBlagdana(spremnik);
  const bool zapisano =
      VanjskiEEPROM::zapisi(EepromLayout::BAZA_BLAGDANI, &spremnik, sizeof(spremnik));
  EepromLayout::BlagdaniSpremnik provjera{};
  const bool procitano = zapisano &&
                         VanjskiEEPROM::procitaj(EepromLayout::BAZA_BLAGDANI,
                                                 &provjera,
                                                 sizeof(provjera)) &&
                         jeValjanEEPROMZapisBlagdana(provjera);
  posaljiPCLog(procitano
                   ? F("Blagdani: EEPROM zapis potvrden")
                   : F("Blagdani: ERROR - spremanje ili provjera EEPROM zapisa nije uspjela"));
  ucitajBlagdaneIzSpremnika(spremnik);
}

static void ucitajBlagdane() {
  EepromLayout::BlagdaniSpremnik spremnik = napraviZadaneBlagdane();
  bool ucitano = ucitajBlagdaneSkeniranjem(spremnik);
  bool trebaSpremiti = false;

  if (!ucitano) {
    spremnik = napraviZadaneBlagdane();
    trebaSpremiti = true;
  }

  if (sanitizirajBlagdaneSpremnik(spremnik)) {
    trebaSpremiti = true;
  }

  ucitajBlagdaneIzSpremnika(spremnik);

  if (trebaSpremiti) {
    spremiBlagdaneSpremnik(spremnik);
  }
}

static void pripremiIntegritetMisa(EepromLayout::MiseSpremnik& spremnik) {
  spremnik.potpis = EepromLayout::MISE_POTPIS;
  spremnik.verzija = EepromLayout::MISE_VERZIJA;
  spremnik.checksum = izracunajChecksumMisa(spremnik);
}

static bool jeValjanEEPROMZapisMisa(const EepromLayout::MiseSpremnik& spremnik) {
  return spremnik.potpis == EepromLayout::MISE_POTPIS &&
         spremnik.verzija == EepromLayout::MISE_VERZIJA &&
         spremnik.checksum == izracunajChecksumMisa(spremnik);
}

static bool sanitizirajMiseSpremnik(EepromLayout::MiseSpremnik& spremnik) {
  bool trebaSpremiti = false;

  bool dnevnaOmogucena = spremnik.dnevna.omogucena != 0;
  uint8_t dnevnaSat = spremnik.dnevna.satMise;
  uint8_t dnevnaMinuta = spremnik.dnevna.minutaMise;
  sanitizirajRedovituMisuPostavku(dnevnaOmogucena, dnevnaSat, dnevnaMinuta);
  if (spremnik.dnevna.omogucena > 1 ||
      (spremnik.dnevna.omogucena != 0) != dnevnaOmogucena ||
      spremnik.dnevna.satMise != dnevnaSat ||
      spremnik.dnevna.minutaMise != dnevnaMinuta ||
      spremnik.dnevna.reserved != 0) {
    spremnik.dnevna.omogucena = dnevnaOmogucena ? 1 : 0;
    spremnik.dnevna.satMise = dnevnaSat;
    spremnik.dnevna.minutaMise = dnevnaMinuta;
    spremnik.dnevna.reserved = 0;
    trebaSpremiti = true;
  }

  bool nedjeljnaOmogucena = spremnik.nedjeljna.omogucena != 0;
  uint8_t nedjeljnaSat = spremnik.nedjeljna.satMise;
  uint8_t nedjeljnaMinuta = spremnik.nedjeljna.minutaMise;
  sanitizirajRedovituMisuPostavku(nedjeljnaOmogucena, nedjeljnaSat, nedjeljnaMinuta);
  if (spremnik.nedjeljna.omogucena > 1 ||
      (spremnik.nedjeljna.omogucena != 0) != nedjeljnaOmogucena ||
      spremnik.nedjeljna.satMise != nedjeljnaSat ||
      spremnik.nedjeljna.minutaMise != nedjeljnaMinuta ||
      spremnik.nedjeljna.reserved != 0) {
    spremnik.nedjeljna.omogucena = nedjeljnaOmogucena ? 1 : 0;
    spremnik.nedjeljna.satMise = nedjeljnaSat;
    spremnik.nedjeljna.minutaMise = nedjeljnaMinuta;
    spremnik.nedjeljna.reserved = 0;
    trebaSpremiti = true;
  }

  if (spremnik.reserved != 0) {
    spremnik.reserved = 0;
    trebaSpremiti = true;
  }

  return trebaSpremiti;
}

static void ucitajMiseIzSpremnika(const EepromLayout::MiseSpremnik& spremnik) {
  postavke.dnevnaMisaOmogucena = spremnik.dnevna.omogucena != 0;
  postavke.dnevnaSatMise = spremnik.dnevna.satMise;
  postavke.dnevnaMinutaMise = spremnik.dnevna.minutaMise;
  sanitizirajRedovituMisuPostavku(
      postavke.dnevnaMisaOmogucena, postavke.dnevnaSatMise, postavke.dnevnaMinutaMise);

  postavke.nedjeljnaMisaOmogucena = spremnik.nedjeljna.omogucena != 0;
  postavke.nedjeljnaSatMise = spremnik.nedjeljna.satMise;
  postavke.nedjeljnaMinutaMise = spremnik.nedjeljna.minutaMise;
  sanitizirajRedovituMisuPostavku(
      postavke.nedjeljnaMisaOmogucena, postavke.nedjeljnaSatMise, postavke.nedjeljnaMinutaMise);
}

static void upisiMiseUSpremnik(EepromLayout::MiseSpremnik& spremnik) {
  bool dnevnaOmogucena = postavke.dnevnaMisaOmogucena;
  uint8_t dnevnaSat = postavke.dnevnaSatMise;
  uint8_t dnevnaMinuta = postavke.dnevnaMinutaMise;
  sanitizirajRedovituMisuPostavku(dnevnaOmogucena, dnevnaSat, dnevnaMinuta);
  spremnik.dnevna.omogucena = dnevnaOmogucena ? 1 : 0;
  spremnik.dnevna.satMise = dnevnaSat;
  spremnik.dnevna.minutaMise = dnevnaMinuta;
  spremnik.dnevna.reserved = 0;

  bool nedjeljnaOmogucena = postavke.nedjeljnaMisaOmogucena;
  uint8_t nedjeljnaSat = postavke.nedjeljnaSatMise;
  uint8_t nedjeljnaMinuta = postavke.nedjeljnaMinutaMise;
  sanitizirajRedovituMisuPostavku(nedjeljnaOmogucena, nedjeljnaSat, nedjeljnaMinuta);
  spremnik.nedjeljna.omogucena = nedjeljnaOmogucena ? 1 : 0;
  spremnik.nedjeljna.satMise = nedjeljnaSat;
  spremnik.nedjeljna.minutaMise = nedjeljnaMinuta;
  spremnik.nedjeljna.reserved = 0;
}

static bool ucitajMiseSkeniranjem(EepromLayout::MiseSpremnik& spremnik) {
  EepromLayout::MiseSpremnik kandidat{};
  if (!VanjskiEEPROM::procitaj(EepromLayout::BAZA_MISE, &kandidat, sizeof(kandidat))) {
    return false;
  }
  if (!jeValjanEEPROMZapisMisa(kandidat)) {
    return false;
  }

  spremnik = kandidat;
  return true;
}

static void spremiMiseSpremnik(EepromLayout::MiseSpremnik& spremnik) {
  sanitizirajMiseSpremnik(spremnik);
  pripremiIntegritetMisa(spremnik);
  const bool zapisano =
      VanjskiEEPROM::zapisi(EepromLayout::BAZA_MISE, &spremnik, sizeof(spremnik));
  EepromLayout::MiseSpremnik provjera{};
  const bool procitano = zapisano &&
                         VanjskiEEPROM::procitaj(EepromLayout::BAZA_MISE,
                                                 &provjera,
                                                 sizeof(provjera)) &&
                         jeValjanEEPROMZapisMisa(provjera);
  posaljiPCLog(procitano
                   ? F("Mise: EEPROM zapis potvrden")
                   : F("Mise: ERROR - spremanje ili provjera EEPROM zapisa nije uspjela"));
  ucitajMiseIzSpremnika(spremnik);
}

static void ucitajMise() {
  EepromLayout::MiseSpremnik spremnik = napraviZadaneMise();
  bool ucitano = ucitajMiseSkeniranjem(spremnik);
  bool trebaSpremiti = false;

  if (!ucitano) {
    spremnik = napraviZadaneMise();
    trebaSpremiti = true;
  }

  if (sanitizirajMiseSpremnik(spremnik)) {
    trebaSpremiti = true;
  }

  ucitajMiseIzSpremnika(spremnik);

  if (trebaSpremiti) {
    spremiMiseSpremnik(spremnik);
  }
}

static bool ucitajSunceveDogadajeSkeniranjem(EepromLayout::SunceviDogadajiSpremnik& spremnik) {
  EepromLayout::SunceviDogadajiSpremnik kandidat{};
  if (!VanjskiEEPROM::procitaj(
          EepromLayout::BAZA_SUNCEVI_DOGADAJI, &kandidat, sizeof(kandidat))) {
    return false;
  }
  if (!jeValjanEEPROMZapisSuncevihDogadaja(kandidat)) {
    return false;
  }

  spremnik = kandidat;
  return true;
}

static void ucitajSunceveDogadaje() {
  EepromLayout::SunceviDogadajiSpremnik spremnik = napraviZadaneSunceveDogadaje();
  bool ucitano = ucitajSunceveDogadajeSkeniranjem(spremnik);
  bool trebaSpremiti = false;

  if (!ucitano) {
    spremnik = napraviZadaneSunceveDogadaje();
    trebaSpremiti = true;
  }

  if (sanitizirajSunceveDogadajeSpremnik(spremnik)) {
    trebaSpremiti = true;
  }

  ucitajSunceveDogadajeIzSpremnika(spremnik);

  if (trebaSpremiti) {
    spremiSunceveDogadajeSpremnik(spremnik);
  }
}

}  // namespace

void ucitajPostavke() {
  postavkeUcitane = false;
  EepromLayout::PostavkeSpremnik spremnik = napraviZadanePostavke();
  bool trebaSpremiti = false;
  bool ucitanoIzEeproma = ucitajAktualniSpremnikSkeniranjem(spremnik);

  if (!ucitanoIzEeproma) {
    posaljiPCLog(F("Postavke: nema valjanog zapisa -> cist start s default postavkama"));
    ocistiSegmenteNakonPromjeneLayouta();
    spremnik = napraviZadanePostavke();
    trebaSpremiti = true;
  } else {
    posaljiPCLog(F("Postavke: ucitane iz EEPROM-a"));
  }

  if (sanitizirajRadnaPolja(spremnik)) {
    trebaSpremiti = true;
  }
  if (sanitizirajMreznaPolja(spremnik)) {
    posaljiPCLog(F("Postavke: mrezna polja popravljena zadanim sigurnim vrijednostima"));
    trebaSpremiti = true;
  }

  pripremiIntegritetPostavki(spremnik);
  ucitajRadnePostavkeIzSpremnika(spremnik);
  postavkeUcitane = true;
  invalidirajMrezniCache();
  ucitajSunceveDogadaje();
  ucitajBlagdane();
  ucitajMise();

  char log[256];
  snprintf_P(
      log,
      sizeof(log),
      PSTR("Postavke: sat %d-%d, WiFi: %s SSID=%s, NTP: %s (%s), RS485: %s, UPS: %s, LCD: %s, Kazaljke: %s, Slavljenje: %u, Otkucavanje: %u, Mrtvacko: %u, Stapici TR/TN/TS=%u/%u/%u S=+%u, Zvona=%u, Mjesta=%u, Sunce maska=%u, Nocna rasvjeta=%s"),
      spremnik.satOd,
      spremnik.satDo,
      spremnik.wifiOmogucen ? "ON" : "OFF",
      spremnik.wifiSsid,
      dohvatiNtpServerBezZastavice(spremnik.ntpServer),
      procitajNtpOmogucenostIzTeksta(spremnik.ntpServer) ? "ON" : "OFF",
      spremnik.rs485Omogucen ? "ON" : "OFF",
      procitajUPSModIzMaskeRazdoblja(spremnik.blagdaniRazdobljaMaska) ? "ON" : "OFF",
      spremnik.lcdPozadinskoOsvjetljenje ? "ON" : "OFF",
      spremnik.imaKazaljke ? "ON" : "OFF",
      spremnik.modSlavljenja,
      spremnik.modOtkucavanja,
      spremnik.modMrtvackog,
      spremnik.trajanjeZvonjenjaRadniMin,
      spremnik.trajanjeZvonjenjaNedjeljaMin,
      spremnik.trajanjeSlavljenjaMin,
      spremnik.slavljenjePrijeZvonjenja,
      spremnik.brojZvona,
      spremnik.brojMjestaZaCavle,
      postavke.maskaSuncevihDogadaja,
      postavke.nocnaRasvjetaOmogucena ? "ON" : "OFF");
  posaljiPCLog(log);

  if (trebaSpremiti) {
    spremiSpremnikPostavki(spremnik);
  }
}

uint8_t dohvatiBrojZvona() {
  return FIKSNI_BROJ_ZVONA;
}

uint8_t dohvatiBrojMjestaZaCavle() {
  return FIKSNI_BROJ_MJESTA_ZA_CAVLE;
}

uint8_t dohvatiCavaoRadniZaZvono(uint8_t zvono) {
  if (zvono < 1 || zvono > FIKSNI_BROJ_ZVONA) {
    return 0;
  }
  return sanitizirajOznakuCavla(postavke.cavliRadni[zvono - 1], dohvatiBrojMjestaZaCavle());
}

uint8_t dohvatiCavaoNedjeljaZaZvono(uint8_t zvono) {
  if (zvono < 1 || zvono > FIKSNI_BROJ_ZVONA) {
    return 0;
  }
  return sanitizirajOznakuCavla(postavke.cavliNedjelja[zvono - 1], dohvatiBrojMjestaZaCavle());
}

uint8_t dohvatiCavaoSlavljenja() {
  return FIKSNI_CAVAO_SLAVLJENJA;
}

bool jeDozvoljenoOtkucavanjeUSatu(int sat) {
  sat = constrain(sat, 0, 23);

  if (postavke.satOd == postavke.satDo) {
    return true;
  }

  if (postavke.satOd <= postavke.satDo) {
    return sat >= postavke.satOd && sat <= postavke.satDo;
  }

  return sat >= postavke.satOd || sat <= postavke.satDo;
}

bool jeBATPeriodAktivanZaSatneOtkucaje(int sat, int minuta) {
  return !jeUnutarTihihSati(sat, minuta);
}

int dohvatiBATPeriodOdSata() {
  return postavke.tihiSatiDo;
}

int dohvatiBATPeriodDoSata() {
  return postavke.tihiSatiOd;
}

void postaviKompaktnePostavkeOtkucavanja(int satOd,
                                         int satDo,
                                         uint8_t modOtkucavanja,
                                         uint8_t modSlavljenja,
                                         uint8_t modMrtvackog) {
  satOd = constrain(satOd, 0, 23);
  satDo = constrain(satDo, 0, 23);
  const int tihiPocetak = satDo;
  const int tihiZavrsetak = satOd;
  if (modOtkucavanja > 2) {
    modOtkucavanja = 2;
  }
  if (modSlavljenja < 1 || modSlavljenja > 2) {
    modSlavljenja = 1;
  }
  if (modMrtvackog < 1 || modMrtvackog > 2) {
    modMrtvackog = 1;
  }

  if (postavke.tihiSatiOd == tihiPocetak &&
      postavke.tihiSatiDo == tihiZavrsetak &&
      postavke.modOtkucavanja == modOtkucavanja &&
      postavke.modSlavljenja == modSlavljenja &&
      postavke.modMrtvackog == modMrtvackog) {
    return;
  }

  postavke.tihiSatiOd = tihiPocetak;
  postavke.tihiSatiDo = tihiZavrsetak;
  postavke.modOtkucavanja = modOtkucavanja;
  postavke.modSlavljenja = modSlavljenja;
  postavke.modMrtvackog = modMrtvackog;

  EepromLayout::PostavkeSpremnik spremnik = napraviZadanePostavke();
  ucitajSpremnikIliZadano(spremnik);
  upisiRadnePostavkeUSpremnik(spremnik);
  spremiSpremnikPostavki(spremnik);

  char log[112];
  snprintf_P(log,
             sizeof(log),
             PSTR("Postavke otkucavanja: BAT %d-%d, OTK=%u, S=%u, M=%u"),
             satOd,
             satDo,
             static_cast<unsigned>(postavke.modOtkucavanja),
             static_cast<unsigned>(postavke.modSlavljenja),
             static_cast<unsigned>(postavke.modMrtvackog));
  posaljiPCLog(log);
}

unsigned int dohvatiTrajanjeImpulsaCekica() {
  return ogranicenoTrajanjeImpulsaCekicaMs(postavke.trajanjeImpulsaCekicaMs);
}

unsigned int dohvatiPauzuIzmeduUdaraca() {
  return postavke.pauzaIzmeduUdaraca;
}

void postaviTrajanjeImpulsaCekica(unsigned int trajanjeMs) {
  const unsigned int novoTrajanjeMs = ogranicenoTrajanjeImpulsaCekicaMs(trajanjeMs);
  if (postavke.trajanjeImpulsaCekicaMs == novoTrajanjeMs) {
    return;
  }

  postavke.trajanjeImpulsaCekicaMs = novoTrajanjeMs;

  EepromLayout::PostavkeSpremnik spremnik = napraviZadanePostavke();
  ucitajSpremnikIliZadano(spremnik);
  upisiRadnePostavkeUSpremnik(spremnik);
  spremiSpremnikPostavki(spremnik);

  char log[64];
  snprintf_P(log,
             sizeof(log),
             PSTR("Postavke: trajanje impulsa cekica postavljeno na %u ms"),
             static_cast<unsigned>(postavke.trajanjeImpulsaCekicaMs));
  posaljiPCLog(log);
}

unsigned long dohvatiTrajanjeZvonjenjaRadniMs() {
  return minuteUMiliseconde(postavke.trajanjeZvonjenjaRadniMin);
}

unsigned long dohvatiTrajanjeZvonjenjaNedjeljaMs() {
  return minuteUMiliseconde(postavke.trajanjeZvonjenjaNedjeljaMin);
}

unsigned long dohvatiTrajanjeSlavljenjaMs() {
  return minuteUMiliseconde(postavke.trajanjeSlavljenjaMin);
}

uint8_t dohvatiTrajanjeZvonjenjaRadniMin() {
  return ogranicenoTrajanjeCavla(postavke.trajanjeZvonjenjaRadniMin);
}

uint8_t dohvatiTrajanjeZvonjenjaNedjeljaMin() {
  return ogranicenoTrajanjeCavla(postavke.trajanjeZvonjenjaNedjeljaMin);
}

uint8_t dohvatiTrajanjeSlavljenjaMin() {
  return ogranicenoTrajanjeCavla(postavke.trajanjeSlavljenjaMin);
}

uint8_t dohvatiOdgoduSlavljenjaSekunde() {
  return ogranicenaOdgodaSlavljenjaSekunde(postavke.slavljenjePrijeZvonjenja);
}

uint8_t dohvatiInercijuZvona1Sekunde() {
  return ogranicenaInercijaZvonaSekunde(postavke.inercijaZvona1Sekunde);
}

uint8_t dohvatiInercijuZvona2Sekunde() {
  return ogranicenaInercijaZvonaSekunde(postavke.inercijaZvona2Sekunde);
}

void postaviInercijeZvona(uint8_t inercijaZvona1Sekunde,
                          uint8_t inercijaZvona2Sekunde) {
  const uint8_t novaInercijaZvona1 = ogranicenaInercijaZvonaSekunde(inercijaZvona1Sekunde);
  const uint8_t novaInercijaZvona2 = ogranicenaInercijaZvonaSekunde(inercijaZvona2Sekunde);

  if (postavke.inercijaZvona1Sekunde == novaInercijaZvona1 &&
      postavke.inercijaZvona2Sekunde == novaInercijaZvona2) {
    return;
  }

  postavke.inercijaZvona1Sekunde = novaInercijaZvona1;
  postavke.inercijaZvona2Sekunde = novaInercijaZvona2;

  EepromLayout::PostavkeSpremnik spremnik = napraviZadanePostavke();
  ucitajSpremnikIliZadano(spremnik);
  upisiRadnePostavkeUSpremnik(spremnik);
  spremiSpremnikPostavki(spremnik);

  char log[96];
  snprintf_P(log,
             sizeof(log),
             PSTR("Postavke: inercija zvona Z1=%u s, Z2=%u s"),
             static_cast<unsigned>(postavke.inercijaZvona1Sekunde),
             static_cast<unsigned>(postavke.inercijaZvona2Sekunde));
  posaljiPCLog(log);
}

bool jePlocaKonfigurirana() {
  return postavke.plocaPocetakMinuta >= 0 &&
         dekodirajPocetakPloceMinuta(postavke.plocaPocetakMinuta) != postavke.plocaKrajMinuta;
}

int dohvatiPocetakPloceMinute() {
  return dekodirajPocetakPloceMinuta(postavke.plocaPocetakMinuta);
}

int dohvatiKrajPloceMinute() {
  return postavke.plocaKrajMinuta;
}

const char* dohvatiWifiSsid() {
  return dohvatiWifiSsidIzSpremnika();
}

const char* dohvatiWifiLozinku() {
  return dohvatiWifiLozinkuIzSpremnika();
}

bool jeWiFiOmogucen() {
  return postavke.wifiOmogucen;
}

bool jeRS485Omogucen() {
  return postavke.rs485Omogucen;
}

bool jeUPSModOmogucen() {
  return postavke.upsModOmogucen;
}

bool jeKocnicaZvonaOmogucena() {
  return postavke.kocnicaZvonaOmogucena;
}

bool koristiDhcpMreza() {
  return postavke.koristiDhcp;
}

bool jeLCDPozadinskoOsvjetljenjeUkljuceno() {
  return postavke.lcdPozadinskoOsvjetljenje;
}

bool jePCLogiranjeOmoguceno() {
  return !postavkeUcitane || postavke.logiranjeOmoguceno;
}

bool imaKazaljkeSata() {
  return postavke.imaKazaljke;
}

uint8_t dohvatiModSlavljenja() {
  return postavke.modSlavljenja;
}

uint8_t dohvatiModOtkucavanja() {
  return postavke.modOtkucavanja;
}

uint8_t dohvatiModMrtvackog() {
  return postavke.modMrtvackog;
}

const char* dohvatiStatickuIP() {
  return dohvatiStatickuIpIzSpremnika();
}

const char* dohvatiMreznuMasku() {
  return dohvatiMreznuMaskuIzSpremnika();
}

const char* dohvatiZadaniGateway() {
  return dohvatiGatewayIzSpremnika();
}

const char* dohvatiNTPServer() {
  return dohvatiNtpServerBezZastavice(dohvatiMrezniTekstIzSpremnika(MREZNA_SEKCIJA_SINKRONIZACIJA));
}

bool jeNTPOmogucen() {
  return postavke.ntpOmogucen;
}

bool jeSuncevDogadajOmogucen(uint8_t dogadaj) {
  if (!jeValjanSuncevDogadaj(dogadaj)) {
    return false;
  }
  return (postavke.maskaSuncevihDogadaja & (1U << dogadaj)) != 0;
}

uint8_t dohvatiZvonoZaSuncevDogadaj(uint8_t dogadaj) {
  if (!jeValjanSuncevDogadaj(dogadaj)) {
    return 1;
  }
  return ogranicenoZvonoSuncevogDogadaja(postavke.zvonaSuncevihDogadaja[dogadaj]);
}

int dohvatiOdgoduSuncevogDogadajaMin(uint8_t dogadaj) {
  if (!jeValjanSuncevDogadaj(dogadaj)) {
    return 0;
  }
  if (dogadaj == SUNCEVI_DOGADAJ_PODNE) {
    return 0;
  }
  return ogranicenaOdgodaSuncevogDogadajaMin(postavke.odgodeSuncevihDogadajaMin[dogadaj]);
}

bool jeNocnaRasvjetaOmogucena() {
  return postavke.nocnaRasvjetaOmogucena;
}

bool jeBlagdanskoSlavljenjeOmoguceno(uint8_t dogadaj) {
  if (!jeValjanSuncevDogadaj(dogadaj)) {
    return false;
  }
  return (postavke.blagdaniSlavljenjeMaska & (1U << dogadaj)) != 0;
}

bool jeBlagdanskoRazdobljeOmoguceno(uint8_t razdoblje) {
  if (!jeValjanoBlagdanskoRazdoblje(razdoblje)) {
    return false;
  }
  return (postavke.blagdaniRazdobljaMaska & (1U << razdoblje)) != 0;
}

uint8_t dohvatiMaskuBlagdanskogSlavljenja() {
  return sanitizirajMaskuBlagdanskogSlavljenja(postavke.blagdaniSlavljenjeMaska);
}

uint8_t dohvatiMaskuBlagdanskihRazdoblja() {
  return sanitizirajMaskuBlagdanskihRazdoblja(postavke.blagdaniRazdobljaMaska);
}

bool jeSviSvetiMrtvackoOmoguceno() {
  return postavke.sviSvetiOmoguceno;
}

uint8_t dohvatiSviSvetiPocetakSat() {
  return ograniceniSviSvetiPocetakSat(postavke.sviSvetiPocetakSat);
}

uint8_t dohvatiSviSvetiZavrsetakSat() {
  return ograniceniSviSvetiZavrsetakSat(postavke.sviSvetiZavrsetakSat);
}

void dohvatiNepomicniBlagdan(uint8_t indeks, NepomicniBlagdanPostavka& izlaz) {
  if (indeks >= BROJ_NEPOMICNIH_BLAGDANA) {
    izlaz = {false, 0, 0, 0, 0};
    return;
  }

  izlaz.omogucen = postavke.nepomicniBlagdani[indeks].omogucen;
  izlaz.satMise = postavke.nepomicniBlagdani[indeks].satMise;
  izlaz.minutaMise = postavke.nepomicniBlagdani[indeks].minutaMise;
  sanitizirajNepomicniBlagdan(indeks, izlaz);
}

void dohvatiPomicniBlagdan(uint8_t indeks, PomicniBlagdanPostavka& izlaz) {
  if (indeks >= BROJ_POMICNIH_BLAGDANA) {
    izlaz = {false, 0, 0, 0};
    return;
  }

  izlaz.omogucen = postavke.pomicniBlagdani[indeks].omogucen;
  izlaz.satMise = postavke.pomicniBlagdani[indeks].satMise;
  izlaz.minutaMise = postavke.pomicniBlagdani[indeks].minutaMise;
  sanitizirajPomicniBlagdan(indeks, izlaz);
}

void dohvatiRedoviteMise(RedoviteMisePostavke& izlaz) {
  izlaz.dnevnaOmogucena = postavke.dnevnaMisaOmogucena;
  izlaz.dnevnaSatMise = postavke.dnevnaSatMise;
  izlaz.dnevnaMinutaMise = postavke.dnevnaMinutaMise;
  izlaz.nedjeljnaOmogucena = postavke.nedjeljnaMisaOmogucena;
  izlaz.nedjeljnaSatMise = postavke.nedjeljnaSatMise;
  izlaz.nedjeljnaMinutaMise = postavke.nedjeljnaMinutaMise;
  sanitizirajRedovituMisuPostavku(
      izlaz.dnevnaOmogucena, izlaz.dnevnaSatMise, izlaz.dnevnaMinutaMise);
  sanitizirajRedovituMisuPostavku(
      izlaz.nedjeljnaOmogucena, izlaz.nedjeljnaSatMise, izlaz.nedjeljnaMinutaMise);
}

void postaviWiFiOmogucen(bool omogucen) {
  if (postavke.wifiOmogucen == omogucen) {
    return;
  }

  postavke.wifiOmogucen = omogucen;

  EepromLayout::PostavkeSpremnik spremnik = napraviZadanePostavke();
  ucitajSpremnikIliZadano(spremnik);
  upisiRadnePostavkeUSpremnik(spremnik);
  spremiSpremnikPostavki(spremnik);

  posaljiPCLog(omogucen ? F("Postavke: WiFi ukljucen") : F("Postavke: WiFi iskljucen"));
}

void postaviRS485Omogucen(bool omogucen) {
  if (postavke.rs485Omogucen == omogucen) {
    return;
  }

  postavke.rs485Omogucen = omogucen;

  EepromLayout::PostavkeSpremnik spremnik = napraviZadanePostavke();
  ucitajSpremnikIliZadano(spremnik);
  upisiRadnePostavkeUSpremnik(spremnik);
  spremiSpremnikPostavki(spremnik);

  posaljiPCLog(omogucen ? F("Postavke: RS485 ukljucen") : F("Postavke: RS485 iskljucen"));
}

void postaviUPSModOmogucen(bool omogucen) {
  if (postavke.upsModOmogucen == omogucen) {
    return;
  }

  postavke.upsModOmogucen = omogucen;

  EepromLayout::PostavkeSpremnik spremnik = napraviZadanePostavke();
  ucitajSpremnikIliZadano(spremnik);
  upisiRadnePostavkeUSpremnik(spremnik);
  spremiSpremnikPostavki(spremnik);

  posaljiPCLog(omogucen
                   ? F("Postavke: UPS mod ukljucen")
                   : F("Postavke: UPS mod iskljucen"));
}

void postaviKocnicuZvonaOmoguceno(bool omoguceno) {
  if (postavke.kocnicaZvonaOmogucena == omoguceno) {
    return;
  }

  postavke.kocnicaZvonaOmogucena = omoguceno;

  EepromLayout::PostavkeSpremnik spremnik = napraviZadanePostavke();
  ucitajSpremnikIliZadano(spremnik);
  upisiRadnePostavkeUSpremnik(spremnik);
  spremiSpremnikPostavki(spremnik);

  posaljiPCLog(omoguceno
                   ? F("Postavke: kocnica zvona ukljucena")
                   : F("Postavke: kocnica zvona iskljucena"));
}

void postaviSuncevDogadaj(uint8_t dogadaj, bool omogucen, uint8_t zvono, int odgodaMin) {
  if (!jeValjanSuncevDogadaj(dogadaj)) {
    return;
  }

  const uint8_t novoZvono = ogranicenoZvonoSuncevogDogadaja(zvono);
  const int16_t novaOdgoda =
      (dogadaj == SUNCEVI_DOGADAJ_PODNE) ? 0 : ogranicenaOdgodaSuncevogDogadajaMin(odgodaMin);
  const uint8_t bitDogadaja = static_cast<uint8_t>(1U << dogadaj);
  const bool prethodnoOmogucen = (postavke.maskaSuncevihDogadaja & bitDogadaja) != 0;

  if (prethodnoOmogucen == omogucen &&
      postavke.zvonaSuncevihDogadaja[dogadaj] == novoZvono &&
      postavke.odgodeSuncevihDogadajaMin[dogadaj] == novaOdgoda) {
    return;
  }

  if (omogucen) {
    postavke.maskaSuncevihDogadaja |= bitDogadaja;
  } else {
    postavke.maskaSuncevihDogadaja &= static_cast<uint8_t>(~bitDogadaja);
  }
  postavke.maskaSuncevihDogadaja =
      sanitizirajMaskuSuncevihDogadaja(postavke.maskaSuncevihDogadaja);
  postavke.zvonaSuncevihDogadaja[dogadaj] = novoZvono;
  postavke.odgodeSuncevihDogadajaMin[dogadaj] = novaOdgoda;

  EepromLayout::SunceviDogadajiSpremnik spremnik = napraviZadaneSunceveDogadaje();
  if (!ucitajSunceveDogadajeSkeniranjem(spremnik)) {
    spremnik = napraviZadaneSunceveDogadaje();
  }
  upisiSunceveDogadajeUSpremnik(spremnik);
  spremiSunceveDogadajeSpremnik(spremnik);

  const char* naziv = "nepoznato";
  if (dogadaj == SUNCEVI_DOGADAJ_JUTRO) {
    naziv = "jutro";
  } else if (dogadaj == SUNCEVI_DOGADAJ_PODNE) {
    naziv = "podne";
  } else if (dogadaj == SUNCEVI_DOGADAJ_VECER) {
    naziv = "vecer";
  }

  char log[96];
  snprintf_P(log,
             sizeof(log),
             PSTR("Suncevi dogadaj %s: %s, zvono=%u, odgoda=%d min"),
             naziv,
             jeSuncevDogadajOmogucen(dogadaj) ? "ON" : "OFF",
             dohvatiZvonoZaSuncevDogadaj(dogadaj),
             dohvatiOdgoduSuncevogDogadajaMin(dogadaj));
  posaljiPCLog(log);
}

void postaviNocnuRasvjetuOmoguceno(bool omoguceno) {
  if (postavke.nocnaRasvjetaOmogucena == omoguceno) {
    return;
  }

  postavke.nocnaRasvjetaOmogucena = omoguceno;

  EepromLayout::SunceviDogadajiSpremnik spremnik = napraviZadaneSunceveDogadaje();
  if (!ucitajSunceveDogadajeSkeniranjem(spremnik)) {
    spremnik = napraviZadaneSunceveDogadaje();
  }
  upisiSunceveDogadajeUSpremnik(spremnik);
  spremiSunceveDogadajeSpremnik(spremnik);

  posaljiPCLog(omoguceno ? F("Sunce: nocna rasvjeta ukljucena u automatici")
                         : F("Sunce: nocna rasvjeta iskljucena u automatici"));
}

void postaviBlagdanskePostavke(uint8_t slavljenjeMaska, uint8_t razdobljaMaska) {
  const uint8_t novaSlavljenjeMaska = sanitizirajMaskuBlagdanskogSlavljenja(slavljenjeMaska);
  const uint8_t novaRazdobljaMaska = sanitizirajMaskuBlagdanskihRazdoblja(razdobljaMaska);

  if (postavke.blagdaniSlavljenjeMaska == novaSlavljenjeMaska &&
      postavke.blagdaniRazdobljaMaska == novaRazdobljaMaska) {
    return;
  }

  postavke.blagdaniSlavljenjeMaska = novaSlavljenjeMaska;
  postavke.blagdaniRazdobljaMaska = novaRazdobljaMaska;

  EepromLayout::PostavkeSpremnik spremnik = napraviZadanePostavke();
  ucitajSpremnikIliZadano(spremnik);
  upisiRadnePostavkeUSpremnik(spremnik);
  spremiSpremnikPostavki(spremnik);

  char log[80];
  snprintf_P(log,
             sizeof(log),
             PSTR("Blagdani: slavljenje maska=%u, razdoblja maska=%u"),
             static_cast<unsigned>(novaSlavljenjeMaska),
             static_cast<unsigned>(novaRazdobljaMaska));
  posaljiPCLog(log);
}

void postaviSviSvetiPostavke(bool omoguceno, uint8_t pocetakSat, uint8_t zavrsetakSat) {
  const uint8_t noviPocetakSat = ograniceniSviSvetiPocetakSat(pocetakSat);
  const uint8_t noviZavrsetakSat = ograniceniSviSvetiZavrsetakSat(zavrsetakSat);

  if (postavke.sviSvetiOmoguceno == omoguceno &&
      postavke.sviSvetiPocetakSat == noviPocetakSat &&
      postavke.sviSvetiZavrsetakSat == noviZavrsetakSat) {
    return;
  }

  postavke.sviSvetiOmoguceno = omoguceno;
  postavke.sviSvetiPocetakSat = noviPocetakSat;
  postavke.sviSvetiZavrsetakSat = noviZavrsetakSat;

  EepromLayout::PostavkeSpremnik spremnik = napraviZadanePostavke();
  ucitajSpremnikIliZadano(spremnik);
  upisiRadnePostavkeUSpremnik(spremnik);
  spremiSpremnikPostavki(spremnik);

  char log[80];
  snprintf_P(log,
             sizeof(log),
             PSTR("Svi sveti: %s, P=%u, Z=%u"),
             omoguceno ? "ON" : "OFF",
             static_cast<unsigned>(noviPocetakSat),
             static_cast<unsigned>(noviZavrsetakSat));
  posaljiPCLog(log);
}

void postaviBlagdanskeMise(const NepomicniBlagdanPostavka* nepomicni,
                           uint8_t brojNepomicnih,
                           const PomicniBlagdanPostavka* pomicni,
                           uint8_t brojPomicnih) {
  bool promjena = false;

  for (uint8_t i = 0; i < BROJ_NEPOMICNIH_BLAGDANA; ++i) {
    NepomicniBlagdanPostavka novi = {false, 0, 0, 0, 0};
    postaviZadaniNepomicniBlagdan(i, novi);
    if (nepomicni != nullptr && i < brojNepomicnih) {
      novi.omogucen = nepomicni[i].omogucen;
      novi.satMise = nepomicni[i].satMise;
      novi.minutaMise = nepomicni[i].minutaMise;
    }
    sanitizirajNepomicniBlagdan(i, novi);

    if (postavke.nepomicniBlagdani[i].omogucen != novi.omogucen ||
        postavke.nepomicniBlagdani[i].satMise != novi.satMise ||
        postavke.nepomicniBlagdani[i].minutaMise != novi.minutaMise) {
      postavke.nepomicniBlagdani[i].omogucen = novi.omogucen;
      postavke.nepomicniBlagdani[i].satMise = novi.satMise;
      postavke.nepomicniBlagdani[i].minutaMise = novi.minutaMise;
      promjena = true;
    }
  }

  for (uint8_t i = 0; i < BROJ_POMICNIH_BLAGDANA; ++i) {
    PomicniBlagdanPostavka novi = {false, 0, 0, 0};
    postaviZadaniPomicniBlagdan(i, novi);
    if (pomicni != nullptr && i < brojPomicnih) {
      novi.omogucen = pomicni[i].omogucen;
      novi.satMise = pomicni[i].satMise;
      novi.minutaMise = pomicni[i].minutaMise;
    }
    sanitizirajPomicniBlagdan(i, novi);

    if (postavke.pomicniBlagdani[i].omogucen != novi.omogucen ||
        postavke.pomicniBlagdani[i].satMise != novi.satMise ||
        postavke.pomicniBlagdani[i].minutaMise != novi.minutaMise) {
      postavke.pomicniBlagdani[i].omogucen = novi.omogucen;
      postavke.pomicniBlagdani[i].satMise = novi.satMise;
      postavke.pomicniBlagdani[i].minutaMise = novi.minutaMise;
      promjena = true;
    }
  }

  if (!promjena) {
    return;
  }

  EepromLayout::BlagdaniSpremnik spremnik = napraviZadaneBlagdane();
  if (!ucitajBlagdaneSkeniranjem(spremnik)) {
    spremnik = napraviZadaneBlagdane();
  }
  upisiBlagdaneUSpremnik(spremnik);
  spremiBlagdaneSpremnik(spremnik);

  uint8_t brojAktivnihNepomicnih = 0;
  uint8_t brojAktivnihPomicnih = 0;
  for (uint8_t i = 0; i < BROJ_NEPOMICNIH_BLAGDANA; ++i) {
    if (postavke.nepomicniBlagdani[i].omogucen) {
      ++brojAktivnihNepomicnih;
    }
  }
  for (uint8_t i = 0; i < BROJ_POMICNIH_BLAGDANA; ++i) {
    if (postavke.pomicniBlagdani[i].omogucen) {
      ++brojAktivnihPomicnih;
    }
  }

  char log[96];
  snprintf_P(log,
             sizeof(log),
             PSTR("Blagdani: spremljeni nepomicni=%u, pomicni=%u"),
             static_cast<unsigned>(brojAktivnihNepomicnih),
             static_cast<unsigned>(brojAktivnihPomicnih));
  posaljiPCLog(log);
}

void postaviRedoviteMise(const RedoviteMisePostavke& postavkeMisa) {
  bool dnevnaOmogucena = postavkeMisa.dnevnaOmogucena;
  uint8_t dnevnaSat = postavkeMisa.dnevnaSatMise;
  uint8_t dnevnaMinuta = postavkeMisa.dnevnaMinutaMise;
  bool nedjeljnaOmogucena = postavkeMisa.nedjeljnaOmogucena;
  uint8_t nedjeljnaSat = postavkeMisa.nedjeljnaSatMise;
  uint8_t nedjeljnaMinuta = postavkeMisa.nedjeljnaMinutaMise;
  sanitizirajRedovituMisuPostavku(dnevnaOmogucena, dnevnaSat, dnevnaMinuta);
  sanitizirajRedovituMisuPostavku(nedjeljnaOmogucena, nedjeljnaSat, nedjeljnaMinuta);

  if (postavke.dnevnaMisaOmogucena == dnevnaOmogucena &&
      postavke.dnevnaSatMise == dnevnaSat &&
      postavke.dnevnaMinutaMise == dnevnaMinuta &&
      postavke.nedjeljnaMisaOmogucena == nedjeljnaOmogucena &&
      postavke.nedjeljnaSatMise == nedjeljnaSat &&
      postavke.nedjeljnaMinutaMise == nedjeljnaMinuta) {
    return;
  }

  postavke.dnevnaMisaOmogucena = dnevnaOmogucena;
  postavke.dnevnaSatMise = dnevnaSat;
  postavke.dnevnaMinutaMise = dnevnaMinuta;
  postavke.nedjeljnaMisaOmogucena = nedjeljnaOmogucena;
  postavke.nedjeljnaSatMise = nedjeljnaSat;
  postavke.nedjeljnaMinutaMise = nedjeljnaMinuta;

  EepromLayout::MiseSpremnik spremnik = napraviZadaneMise();
  if (!ucitajMiseSkeniranjem(spremnik)) {
    spremnik = napraviZadaneMise();
  }
  upisiMiseUSpremnik(spremnik);
  spremiMiseSpremnik(spremnik);

  char log[112];
  snprintf_P(log,
             sizeof(log),
             PSTR("Mise: dnevna=%s@%02u:%02u, nedjeljna=%s@%02u:%02u"),
             postavke.dnevnaMisaOmogucena ? "ON" : "OFF",
             static_cast<unsigned>(postavke.dnevnaSatMise),
             static_cast<unsigned>(postavke.dnevnaMinutaMise),
             postavke.nedjeljnaMisaOmogucena ? "ON" : "OFF",
             static_cast<unsigned>(postavke.nedjeljnaSatMise),
             static_cast<unsigned>(postavke.nedjeljnaMinutaMise));
  posaljiPCLog(log);
}

void postaviLCDPozadinskoOsvjetljenje(bool ukljuceno) {
  if (postavke.lcdPozadinskoOsvjetljenje == ukljuceno) {
    return;
  }

  postavke.lcdPozadinskoOsvjetljenje = ukljuceno;

  EepromLayout::PostavkeSpremnik spremnik = napraviZadanePostavke();
  ucitajSpremnikIliZadano(spremnik);
  upisiRadnePostavkeUSpremnik(spremnik);
  spremiSpremnikPostavki(spremnik);

  posaljiPCLog(ukljuceno ? F("Postavke: LCD osvjetljenje ukljuceno")
                         : F("Postavke: LCD osvjetljenje iskljuceno"));
}

void postaviPCLogiranjeOmoguceno(bool omoguceno) {
  if (postavke.logiranjeOmoguceno == omoguceno) {
    return;
  }

  postavke.logiranjeOmoguceno = omoguceno;

  EepromLayout::PostavkeSpremnik spremnik = napraviZadanePostavke();
  ucitajSpremnikIliZadano(spremnik);
  upisiRadnePostavkeUSpremnik(spremnik);
  spremiSpremnikPostavki(spremnik);

  posaljiPCLog(omoguceno ? F("Postavke: PC logiranje ukljuceno")
                         : F("Postavke: PC logiranje iskljuceno"));
}

void postaviKonfiguracijuPloce(bool aktivna, int pocetakMinuta, int krajMinuta) {
  pocetakMinuta = normalizirajMinutuPloceNaBlok(pocetakMinuta);
  krajMinuta = normalizirajMinutuPloceNaBlok(krajMinuta);

  if (aktivna && krajMinuta <= pocetakMinuta) {
    krajMinuta = min(pocetakMinuta + PLOCA_MINUTNI_BLOK, PLOCA_ZADNJA_CETVRT);
  }

  const int kodiraniPocetak = kodirajPocetakPloceMinuta(pocetakMinuta, aktivna);
  if (postavke.plocaPocetakMinuta == kodiraniPocetak &&
      postavke.plocaKrajMinuta == krajMinuta) {
    return;
  }

  postavke.plocaPocetakMinuta = kodiraniPocetak;
  postavke.plocaKrajMinuta = krajMinuta;

  EepromLayout::PostavkeSpremnik spremnik = napraviZadanePostavke();
  ucitajSpremnikIliZadano(spremnik);
  upisiRadnePostavkeUSpremnik(spremnik);
  spremiSpremnikPostavki(spremnik);

  char log[96];
  snprintf_P(
      log,
      sizeof(log),
      PSTR("Postavke ploce: %s %02d:%02d-%02d:%02d"),
      aktivna ? "ON" : "OFF",
      pocetakMinuta / 60,
      pocetakMinuta % 60,
      krajMinuta / 60,
      krajMinuta % 60);
  posaljiPCLog(log);
}

void postaviPostavkeCavala(uint8_t trajanjeRadniMin,
                           uint8_t trajanjeNedjeljaMin,
                           uint8_t trajanjeSlavljenjaMin,
                           uint8_t odgodaSlavljenjaSekunde) {
  const uint8_t novoTrajanjeRadniMin = ogranicenoTrajanjeCavla(trajanjeRadniMin);
  const uint8_t novoTrajanjeNedjeljaMin = ogranicenoTrajanjeCavla(trajanjeNedjeljaMin);
  const uint8_t novoTrajanjeSlavljenjaMin = ogranicenoTrajanjeCavla(trajanjeSlavljenjaMin);
  const uint8_t novaOdgodaSlavljenjaSekunde =
      ogranicenaOdgodaSlavljenjaSekunde(odgodaSlavljenjaSekunde);

  if (postavke.trajanjeZvonjenjaRadniMin == novoTrajanjeRadniMin &&
      postavke.trajanjeZvonjenjaNedjeljaMin == novoTrajanjeNedjeljaMin &&
      postavke.trajanjeSlavljenjaMin == novoTrajanjeSlavljenjaMin &&
      postavke.slavljenjePrijeZvonjenja == novaOdgodaSlavljenjaSekunde) {
    return;
  }

  postavke.trajanjeZvonjenjaRadniMin = novoTrajanjeRadniMin;
  postavke.trajanjeZvonjenjaNedjeljaMin = novoTrajanjeNedjeljaMin;
  postavke.trajanjeSlavljenjaMin = novoTrajanjeSlavljenjaMin;
  postavke.slavljenjePrijeZvonjenja = novaOdgodaSlavljenjaSekunde;

  EepromLayout::PostavkeSpremnik spremnik = napraviZadanePostavke();
  ucitajSpremnikIliZadano(spremnik);
  upisiRadnePostavkeUSpremnik(spremnik);
  spremiSpremnikPostavki(spremnik);

  char log[80];
  snprintf_P(
      log,
      sizeof(log),
      PSTR("Postavke stapica: TR=%u TN=%u TS=%u S=+%u"),
      postavke.trajanjeZvonjenjaRadniMin,
      postavke.trajanjeZvonjenjaNedjeljaMin,
      postavke.trajanjeSlavljenjaMin,
      postavke.slavljenjePrijeZvonjenja);
  posaljiPCLog(log);
}

void postaviWiFiPodatkeZaSetup(const char* ssid, const char* lozinka) {
  if (ssid == nullptr || lozinka == nullptr) {
    return;
  }

  char noviSsid[33];
  char novaLozinka[33];
  strncpy(noviSsid, ssid, sizeof(noviSsid) - 1);
  noviSsid[sizeof(noviSsid) - 1] = '\0';
  strncpy(novaLozinka, lozinka, sizeof(novaLozinka) - 1);
  novaLozinka[sizeof(novaLozinka) - 1] = '\0';

  if (strcmp(dohvatiWifiSsid(), noviSsid) == 0 &&
      strcmp(dohvatiWifiLozinku(), novaLozinka) == 0 &&
      postavke.koristiDhcp) {
    return;
  }

  EepromLayout::PostavkeSpremnik spremnik = napraviZadanePostavke();
  ucitajSpremnikIliZadano(spremnik);
  upisiRadnePostavkeUSpremnik(spremnik);

  strncpy(spremnik.wifiSsid, noviSsid, sizeof(spremnik.wifiSsid) - 1);
  spremnik.wifiSsid[sizeof(spremnik.wifiSsid) - 1] = '\0';
  strncpy(spremnik.wifiLozinka, novaLozinka, sizeof(spremnik.wifiLozinka) - 1);
  spremnik.wifiLozinka[sizeof(spremnik.wifiLozinka) - 1] = '\0';
  spremnik.koristiDhcp = true;
  postavke.koristiDhcp = true;

  spremiSpremnikPostavki(spremnik);

  char log[96];
  snprintf_P(log,
             sizeof(log),
             PSTR("Setup WiFi: spremljen SSID=%s i aktiviran DHCP za novu mrezu"),
             spremnik.wifiSsid);
  posaljiPCLog(log);
}

void postaviNTPOmogucen(bool omogucen) {
  if (postavke.ntpOmogucen == omogucen) {
    return;
  }

  EepromLayout::PostavkeSpremnik spremnik = napraviZadanePostavke();
  ucitajSpremnikIliZadano(spremnik);
  upisiRadnePostavkeUSpremnik(spremnik);
  kodirajNtpServer(
      spremnik.ntpServer,
      sizeof(spremnik.ntpServer),
      dohvatiNtpServerBezZastavice(spremnik.ntpServer),
      omogucen);

  spremiSpremnikPostavki(spremnik);

  posaljiPCLog(omogucen ? F("Postavke: NTP ukljucen")
                        : F("Postavke: NTP iskljucen"));
}

void postaviSinkronizacijskePostavke(const char* ntpServer) {
  if (ntpServer == nullptr) {
    return;
  }

  if (strcmp(dohvatiNTPServer(), ntpServer) == 0) {
    return;
  }

  EepromLayout::PostavkeSpremnik spremnik = napraviZadanePostavke();
  ucitajSpremnikIliZadano(spremnik);
  upisiRadnePostavkeUSpremnik(spremnik);

  kodirajNtpServer(
      spremnik.ntpServer,
      sizeof(spremnik.ntpServer),
      ntpServer,
      procitajNtpOmogucenostIzTeksta(spremnik.ntpServer));

  spremiSpremnikPostavki(spremnik);

  char log[96];
  snprintf_P(
      log,
      sizeof(log),
      PSTR("Postavke: sinkronizacija NTP=%s (%s)"),
      dohvatiNtpServerBezZastavice(spremnik.ntpServer),
      procitajNtpOmogucenostIzTeksta(spremnik.ntpServer) ? "ON" : "OFF");
  posaljiPCLog(log);
}
