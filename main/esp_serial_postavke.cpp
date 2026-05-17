// esp_serial_postavke.cpp - SETREQ/SETCFG i setup konfiguracija za ESP32 most
#include "esp_serial_internal.h"

namespace {

bool parsirajBoolZastavicuSustava(const char* vrijednost, bool& izlaz) {
  if (vrijednost == nullptr || vrijednost[0] == '\0' || vrijednost[1] != '\0') {
    return false;
  }

  if (vrijednost[0] == '0') {
    izlaz = false;
    return true;
  }

  if (vrijednost[0] == '1') {
    izlaz = true;
    return true;
  }

  return false;
}

bool parsirajUIntPoljeSustava(const char* vrijednost, unsigned int& izlaz) {
  if (vrijednost == nullptr || vrijednost[0] == '\0') {
    return false;
  }

  unsigned long akumulator = 0;
  for (const char* pokazivac = vrijednost; *pokazivac != '\0'; ++pokazivac) {
    if (!isDigit(*pokazivac)) {
      return false;
    }

    akumulator = akumulator * 10UL + static_cast<unsigned long>(*pokazivac - '0');
    if (akumulator > 60000UL) {
      return false;
    }
  }

  izlaz = static_cast<unsigned int>(akumulator);
  return true;
}

bool parsirajIntPoljeSustava(const char* vrijednost, int& izlaz) {
  if (vrijednost == nullptr || vrijednost[0] == '\0') {
    return false;
  }

  bool negativan = false;
  const char* pokazivac = vrijednost;
  if (*pokazivac == '-') {
    negativan = true;
    ++pokazivac;
  }

  if (*pokazivac == '\0') {
    return false;
  }

  long akumulator = 0L;
  while (*pokazivac != '\0') {
    if (!isDigit(*pokazivac)) {
      return false;
    }

    akumulator = akumulator * 10L + static_cast<long>(*pokazivac - '0');
    if (akumulator > 60000L) {
      return false;
    }
    ++pokazivac;
  }

  izlaz = static_cast<int>(negativan ? -akumulator : akumulator);
  return true;
}

}  // namespace

void posaljiSustavskePostavkeESPu() {
  char linija[112];
  snprintf_P(linija,
             sizeof(linija),
             PSTR("SET:SUSTAV|lcd=%d|log=%d|rs=%d|ups=%d|koc=%d|inr1=%u|inr2=%u|imp=%u"),
             jeLCDPozadinskoOsvjetljenjeUkljuceno() ? 1 : 0,
             jePCLogiranjeOmoguceno() ? 1 : 0,
             jeRS485Omogucen() ? 1 : 0,
             jeUPSModOmogucen() ? 1 : 0,
             jeKocnicaZvonaOmogucena() ? 1 : 0,
             static_cast<unsigned>(dohvatiInercijuZvona1Sekunde()),
             static_cast<unsigned>(dohvatiInercijuZvona2Sekunde()),
             static_cast<unsigned>(dohvatiTrajanjeImpulsaCekica()));
  espSerijskiPort.println(linija);
}

void posaljiPostavkeStapicaESPu() {
  char linija[64];
  snprintf_P(linija,
             sizeof(linija),
             PSTR("SET:STAPICI|tr=%u|tn=%u|ts=%u|odg=%u"),
             static_cast<unsigned>(dohvatiTrajanjeZvonjenjaRadniMin()),
             static_cast<unsigned>(dohvatiTrajanjeZvonjenjaNedjeljaMin()),
             static_cast<unsigned>(dohvatiTrajanjeSlavljenjaMin()),
             static_cast<unsigned>(dohvatiOdgoduSlavljenjaSekunde()));
  espSerijskiPort.println(linija);
}

void posaljiBATPostavkeESPu() {
  char linija[72];
  snprintf_P(linija,
             sizeof(linija),
             PSTR("SET:BAT|od=%d|do=%d|otk=%u|sl=%u|mr=%u"),
             dohvatiBATPeriodOdSata(),
             dohvatiBATPeriodDoSata(),
             static_cast<unsigned>(dohvatiModOtkucavanja()),
             static_cast<unsigned>(dohvatiModSlavljenja()),
             static_cast<unsigned>(dohvatiModMrtvackog()));
  espSerijskiPort.println(linija);
}

void posaljiSuncevePostavkeESPu() {
  char linija[128];
  snprintf_P(linija,
             sizeof(linija),
             PSTR("SET:SUNCE|ju=%d|jb=%u|jo=%d|pu=%d|pb=%u|vu=%d|vb=%u|vo=%d|nr=%d"),
             jeSuncevDogadajOmogucen(SUNCEVI_DOGADAJ_JUTRO) ? 1 : 0,
             static_cast<unsigned>(dohvatiZvonoZaSuncevDogadaj(SUNCEVI_DOGADAJ_JUTRO)),
             dohvatiOdgoduSuncevogDogadajaMin(SUNCEVI_DOGADAJ_JUTRO),
             jeSuncevDogadajOmogucen(SUNCEVI_DOGADAJ_PODNE) ? 1 : 0,
             static_cast<unsigned>(dohvatiZvonoZaSuncevDogadaj(SUNCEVI_DOGADAJ_PODNE)),
             jeSuncevDogadajOmogucen(SUNCEVI_DOGADAJ_VECER) ? 1 : 0,
             static_cast<unsigned>(dohvatiZvonoZaSuncevDogadaj(SUNCEVI_DOGADAJ_VECER)),
             dohvatiOdgoduSuncevogDogadajaMin(SUNCEVI_DOGADAJ_VECER),
             jeNocnaRasvjetaOmogucena() ? 1 : 0);
  espSerijskiPort.println(linija);
}

void posaljiMisePostavkeESPu() {
  RedoviteMisePostavke redoviteMise;
  dohvatiRedoviteMise(redoviteMise);

  char linija[80];
  snprintf_P(linija,
             sizeof(linija),
             PSTR("SET:MISE|rd=%u,%u,%u|nd=%u,%u,%u"),
             redoviteMise.dnevnaOmogucena ? 1U : 0U,
             static_cast<unsigned>(redoviteMise.dnevnaSatMise),
             static_cast<unsigned>(redoviteMise.dnevnaMinutaMise),
             redoviteMise.nedjeljnaOmogucena ? 1U : 0U,
             static_cast<unsigned>(redoviteMise.nedjeljnaSatMise),
             static_cast<unsigned>(redoviteMise.nedjeljnaMinutaMise));
  espSerijskiPort.println(linija);
}

void posaljiNepomicneBlagdaneESPu() {
  char linija[320];
  int duljina = snprintf_P(linija, sizeof(linija), PSTR("SET:BLAGDANI_NEP"));
  if (duljina < 0) {
    return;
  }

  for (uint8_t i = 0; i < BROJ_NEPOMICNIH_BLAGDANA && duljina < static_cast<int>(sizeof(linija)); ++i) {
    NepomicniBlagdanPostavka blagdan;
    dohvatiNepomicniBlagdan(i, blagdan);
    duljina += snprintf_P(linija + duljina,
                          sizeof(linija) - static_cast<size_t>(duljina),
                          PSTR("|f%u=%u,%u,%u"),
                          static_cast<unsigned>(i),
                          blagdan.omogucen ? 1U : 0U,
                          static_cast<unsigned>(blagdan.satMise),
                          static_cast<unsigned>(blagdan.minutaMise));
  }

  espSerijskiPort.println(linija);
}

void posaljiPomicneBlagdaneESPu() {
  char linija[192];
  int duljina = snprintf_P(linija, sizeof(linija), PSTR("SET:BLAGDANI_POM"));
  if (duljina < 0) {
    return;
  }

  for (uint8_t i = 0; i < BROJ_POMICNIH_BLAGDANA && duljina < static_cast<int>(sizeof(linija)); ++i) {
    PomicniBlagdanPostavka blagdan;
    dohvatiPomicniBlagdan(i, blagdan);
    duljina += snprintf_P(linija + duljina,
                          sizeof(linija) - static_cast<size_t>(duljina),
                          PSTR("|p%u=%u,%u,%u"),
                          static_cast<unsigned>(i),
                          blagdan.omogucen ? 1U : 0U,
                          static_cast<unsigned>(blagdan.satMise),
                          static_cast<unsigned>(blagdan.minutaMise));
  }

  espSerijskiPort.println(linija);
}

void posaljiKonfiguracijuESPuNakonZahtjeva() {
  posaljiWifiPostavkeESP();
  posaljiWiFiStatusESP();
  posaljiNTPPostavkeESP();
  posaljiSustavskePostavkeESPu();
  posaljiPostavkeStapicaESPu();
  posaljiBATPostavkeESPu();
  posaljiSuncevePostavkeESPu();
  posaljiMisePostavkeESPu();
  posaljiNepomicneBlagdaneESPu();
  posaljiPomicneBlagdaneESPu();
  zatraziWiFiStatusESP();
  posaljiPCLog(F("Mrezni most zatrazio osvjezavanje konfiguracije"));
}

bool spremiSustavskePostavkeIzESPa(char* payload) {
  if (payload == nullptr || payload[0] == '\0') {
    return false;
  }

  bool lcdPoznato = false;
  bool logPoznato = false;
  bool rsPoznato = false;
  bool upsPoznato = false;
  bool kocPoznato = false;
  bool lcdUkljuceno = false;
  bool logOmogucen = false;
  bool rsOmogucen = false;
  bool upsOmogucen = false;
  bool kocOmogucena = false;
  unsigned int inr1 = 0;
  unsigned int inr2 = 0;
  unsigned int impuls = 0;
  bool inr1Poznat = false;
  bool inr2Poznat = false;
  bool impulsPoznat = false;

  char* context = nullptr;
  for (char* polje = strtok_r(payload, "|", &context);
       polje != nullptr;
       polje = strtok_r(nullptr, "|", &context)) {
    trimJednolinijskiTekstESP(polje);

    char* jednako = strchr(polje, '=');
    if (jednako == nullptr) {
      return false;
    }

    *jednako = '\0';
    char* kljuc = polje;
    char* vrijednost = jednako + 1;
    trimJednolinijskiTekstESP(kljuc);
    trimJednolinijskiTekstESP(vrijednost);

    if (strcmp(kljuc, "lcd") == 0) {
      if (!parsirajBoolZastavicuSustava(vrijednost, lcdUkljuceno)) return false;
      lcdPoznato = true;
    } else if (strcmp(kljuc, "log") == 0) {
      if (!parsirajBoolZastavicuSustava(vrijednost, logOmogucen)) return false;
      logPoznato = true;
    } else if (strcmp(kljuc, "rs") == 0) {
      if (!parsirajBoolZastavicuSustava(vrijednost, rsOmogucen)) return false;
      rsPoznato = true;
    } else if (strcmp(kljuc, "ups") == 0) {
      if (!parsirajBoolZastavicuSustava(vrijednost, upsOmogucen)) return false;
      upsPoznato = true;
    } else if (strcmp(kljuc, "koc") == 0) {
      if (!parsirajBoolZastavicuSustava(vrijednost, kocOmogucena)) return false;
      kocPoznato = true;
    } else if (strcmp(kljuc, "inr1") == 0) {
      if (!parsirajUIntPoljeSustava(vrijednost, inr1)) return false;
      inr1Poznat = true;
    } else if (strcmp(kljuc, "inr2") == 0) {
      if (!parsirajUIntPoljeSustava(vrijednost, inr2)) return false;
      inr2Poznat = true;
    } else if (strcmp(kljuc, "imp") == 0) {
      if (!parsirajUIntPoljeSustava(vrijednost, impuls)) return false;
      impulsPoznat = true;
    } else {
      return false;
    }
  }

  if (!lcdPoznato || !logPoznato || !rsPoznato || !upsPoznato || !kocPoznato ||
      !inr1Poznat || !inr2Poznat || !impulsPoznat) {
    return false;
  }

  postaviLCDPozadinskoOsvjetljenje(lcdUkljuceno);
  postaviPCLogiranjeOmoguceno(logOmogucen);
  postaviRS485Omogucen(rsOmogucen);
  postaviUPSModOmogucen(upsOmogucen);
  postaviKocnicuZvonaOmoguceno(kocOmogucena);
  postaviInercijeZvona(static_cast<uint8_t>(inr1), static_cast<uint8_t>(inr2));
  postaviTrajanjeImpulsaCekica(impuls);

  char log[128];
  snprintf_P(log,
             sizeof(log),
             PSTR("Mrezni most je spremio sustavske postavke: lcd=%d log=%d rs=%d ups=%d koc=%d inr1=%u inr2=%u imp=%u"),
             lcdUkljuceno ? 1 : 0,
             logOmogucen ? 1 : 0,
             rsOmogucen ? 1 : 0,
             upsOmogucen ? 1 : 0,
             kocOmogucena ? 1 : 0,
             inr1,
             inr2,
             impuls);
  posaljiPCLog(log);
  return true;
}

bool spremiPostavkeStapicaIzESPa(char* payload) {
  if (payload == nullptr || payload[0] == '\0') {
    return false;
  }

  unsigned int trajanjeRadni = 0;
  unsigned int trajanjeNedjelja = 0;
  unsigned int trajanjeSlavljenja = 0;
  unsigned int odgoda = 0;
  bool trajanjeRadniPoznato = false;
  bool trajanjeNedjeljaPoznato = false;
  bool trajanjeSlavljenjaPoznato = false;
  bool odgodaPoznata = false;

  char* context = nullptr;
  for (char* polje = strtok_r(payload, "|", &context);
       polje != nullptr;
       polje = strtok_r(nullptr, "|", &context)) {
    trimJednolinijskiTekstESP(polje);

    char* jednako = strchr(polje, '=');
    if (jednako == nullptr) {
      return false;
    }

    *jednako = '\0';
    char* kljuc = polje;
    char* vrijednost = jednako + 1;
    trimJednolinijskiTekstESP(kljuc);
    trimJednolinijskiTekstESP(vrijednost);

    if (strcmp(kljuc, "tr") == 0) {
      if (!parsirajUIntPoljeSustava(vrijednost, trajanjeRadni)) return false;
      trajanjeRadniPoznato = true;
    } else if (strcmp(kljuc, "tn") == 0) {
      if (!parsirajUIntPoljeSustava(vrijednost, trajanjeNedjelja)) return false;
      trajanjeNedjeljaPoznato = true;
    } else if (strcmp(kljuc, "ts") == 0) {
      if (!parsirajUIntPoljeSustava(vrijednost, trajanjeSlavljenja)) return false;
      trajanjeSlavljenjaPoznato = true;
    } else if (strcmp(kljuc, "odg") == 0) {
      if (!parsirajUIntPoljeSustava(vrijednost, odgoda)) return false;
      odgodaPoznata = true;
    } else {
      return false;
    }
  }

  if (!trajanjeRadniPoznato || !trajanjeNedjeljaPoznato ||
      !trajanjeSlavljenjaPoznato || !odgodaPoznata) {
    return false;
  }

  postaviPostavkeCavala(static_cast<uint8_t>(trajanjeRadni),
                        static_cast<uint8_t>(trajanjeNedjelja),
                        static_cast<uint8_t>(trajanjeSlavljenja),
                        static_cast<uint8_t>(odgoda));

  char log[96];
  snprintf_P(log,
             sizeof(log),
             PSTR("Mrezni most je spremio postavke stapica: TR=%u TN=%u TS=%u S=%u"),
             trajanjeRadni,
             trajanjeNedjelja,
             trajanjeSlavljenja,
             odgoda);
  posaljiPCLog(log);
  return true;
}

bool spremiBATPostavkeIzESPa(char* payload) {
  if (payload == nullptr || payload[0] == '\0') {
    return false;
  }

  int satOd = 0;
  int satDo = 0;
  unsigned int modOtkucavanja = 0;
  unsigned int modSlavljenja = 0;
  unsigned int modMrtvackog = 0;
  bool satOdPoznat = false;
  bool satDoPoznat = false;
  bool modOtkucavanjaPoznat = false;
  bool modSlavljenjaPoznat = false;
  bool modMrtvackogPoznat = false;

  char* context = nullptr;
  for (char* polje = strtok_r(payload, "|", &context);
       polje != nullptr;
       polje = strtok_r(nullptr, "|", &context)) {
    trimJednolinijskiTekstESP(polje);

    char* jednako = strchr(polje, '=');
    if (jednako == nullptr) {
      return false;
    }

    *jednako = '\0';
    char* kljuc = polje;
    char* vrijednost = jednako + 1;
    trimJednolinijskiTekstESP(kljuc);
    trimJednolinijskiTekstESP(vrijednost);

    if (strcmp(kljuc, "od") == 0) {
      if (!parsirajIntPoljeSustava(vrijednost, satOd)) return false;
      satOdPoznat = true;
    } else if (strcmp(kljuc, "do") == 0) {
      if (!parsirajIntPoljeSustava(vrijednost, satDo)) return false;
      satDoPoznat = true;
    } else if (strcmp(kljuc, "otk") == 0) {
      if (!parsirajUIntPoljeSustava(vrijednost, modOtkucavanja)) return false;
      modOtkucavanjaPoznat = true;
    } else if (strcmp(kljuc, "sl") == 0) {
      if (!parsirajUIntPoljeSustava(vrijednost, modSlavljenja)) return false;
      modSlavljenjaPoznat = true;
    } else if (strcmp(kljuc, "mr") == 0) {
      if (!parsirajUIntPoljeSustava(vrijednost, modMrtvackog)) return false;
      modMrtvackogPoznat = true;
    } else {
      return false;
    }
  }

  if (!satOdPoznat || !satDoPoznat || !modOtkucavanjaPoznat ||
      !modSlavljenjaPoznat || !modMrtvackogPoznat) {
    return false;
  }

  postaviKompaktnePostavkeOtkucavanja(
      satOd,
      satDo,
      static_cast<uint8_t>(modOtkucavanja),
      static_cast<uint8_t>(modSlavljenja),
      static_cast<uint8_t>(modMrtvackog));

  char log[112];
  snprintf_P(log,
             sizeof(log),
             PSTR("Mrezni most je spremio BAT postavke: od=%d do=%d OTK=%u S=%u M=%u"),
             satOd,
             satDo,
             modOtkucavanja,
             modSlavljenja,
             modMrtvackog);
  posaljiPCLog(log);
  return true;
}

bool spremiSuncevePostavkeIzESPa(char* payload) {
  if (payload == nullptr || payload[0] == '\0') {
    return false;
  }

  bool jutroOmoguceno = false;
  bool podneOmoguceno = false;
  bool vecerOmoguceno = false;
  bool nocnaRasvjeta = false;
  unsigned int jutroZvono = 0;
  unsigned int podneZvono = 0;
  unsigned int vecerZvono = 0;
  int jutroOdgoda = 0;
  int vecerOdgoda = 0;
  bool jutroOmogucenoPoznato = false;
  bool podneOmogucenoPoznato = false;
  bool vecerOmogucenoPoznato = false;
  bool nocnaRasvjetaPoznata = false;
  bool jutroZvonoPoznato = false;
  bool podneZvonoPoznato = false;
  bool vecerZvonoPoznato = false;
  bool jutroOdgodaPoznata = false;
  bool vecerOdgodaPoznata = false;

  char* context = nullptr;
  for (char* polje = strtok_r(payload, "|", &context);
       polje != nullptr;
       polje = strtok_r(nullptr, "|", &context)) {
    trimJednolinijskiTekstESP(polje);

    char* jednako = strchr(polje, '=');
    if (jednako == nullptr) {
      return false;
    }

    *jednako = '\0';
    char* kljuc = polje;
    char* vrijednost = jednako + 1;
    trimJednolinijskiTekstESP(kljuc);
    trimJednolinijskiTekstESP(vrijednost);

    if (strcmp(kljuc, "ju") == 0) {
      if (!parsirajBoolZastavicuSustava(vrijednost, jutroOmoguceno)) return false;
      jutroOmogucenoPoznato = true;
    } else if (strcmp(kljuc, "jb") == 0) {
      if (!parsirajUIntPoljeSustava(vrijednost, jutroZvono)) return false;
      jutroZvonoPoznato = true;
    } else if (strcmp(kljuc, "jo") == 0) {
      if (!parsirajIntPoljeSustava(vrijednost, jutroOdgoda)) return false;
      jutroOdgodaPoznata = true;
    } else if (strcmp(kljuc, "pu") == 0) {
      if (!parsirajBoolZastavicuSustava(vrijednost, podneOmoguceno)) return false;
      podneOmogucenoPoznato = true;
    } else if (strcmp(kljuc, "pb") == 0) {
      if (!parsirajUIntPoljeSustava(vrijednost, podneZvono)) return false;
      podneZvonoPoznato = true;
    } else if (strcmp(kljuc, "vu") == 0) {
      if (!parsirajBoolZastavicuSustava(vrijednost, vecerOmoguceno)) return false;
      vecerOmogucenoPoznato = true;
    } else if (strcmp(kljuc, "vb") == 0) {
      if (!parsirajUIntPoljeSustava(vrijednost, vecerZvono)) return false;
      vecerZvonoPoznato = true;
    } else if (strcmp(kljuc, "vo") == 0) {
      if (!parsirajIntPoljeSustava(vrijednost, vecerOdgoda)) return false;
      vecerOdgodaPoznata = true;
    } else if (strcmp(kljuc, "nr") == 0) {
      if (!parsirajBoolZastavicuSustava(vrijednost, nocnaRasvjeta)) return false;
      nocnaRasvjetaPoznata = true;
    } else {
      return false;
    }
  }

  if (!jutroOmogucenoPoznato || !jutroZvonoPoznato || !jutroOdgodaPoznata ||
      !podneOmogucenoPoznato || !podneZvonoPoznato ||
      !vecerOmogucenoPoznato || !vecerZvonoPoznato || !vecerOdgodaPoznata ||
      !nocnaRasvjetaPoznata) {
    return false;
  }

  postaviSuncevDogadaj(
      SUNCEVI_DOGADAJ_JUTRO,
      jutroOmoguceno,
      static_cast<uint8_t>(jutroZvono),
      jutroOdgoda);
  postaviSuncevDogadaj(
      SUNCEVI_DOGADAJ_PODNE,
      podneOmoguceno,
      static_cast<uint8_t>(podneZvono),
      0);
  postaviSuncevDogadaj(
      SUNCEVI_DOGADAJ_VECER,
      vecerOmoguceno,
      static_cast<uint8_t>(vecerZvono),
      vecerOdgoda);
  postaviNocnuRasvjetuOmoguceno(nocnaRasvjeta);

  char log[128];
  snprintf_P(log,
             sizeof(log),
             PSTR("Mrezni most je spremio sunceve postavke: J=%d/%u/%d P=%d/%u V=%d/%u/%d NR=%d"),
             jutroOmoguceno ? 1 : 0,
             jutroZvono,
             jutroOdgoda,
             podneOmoguceno ? 1 : 0,
             podneZvono,
             vecerOmoguceno ? 1 : 0,
             vecerZvono,
             vecerOdgoda,
             nocnaRasvjeta ? 1 : 0);
  posaljiPCLog(log);
  return true;
}

bool spremiMisePostavkeIzESPa(char* payload) {
  if (payload == nullptr || payload[0] == '\0') {
    return false;
  }

  RedoviteMisePostavke redoviteMise = {false, 0, 0, false, 0, 0};
  bool dnevnaMisaPoznata = false;
  bool nedjeljnaMisaPoznata = false;

  char* context = nullptr;
  for (char* polje = strtok_r(payload, "|", &context);
       polje != nullptr;
       polje = strtok_r(nullptr, "|", &context)) {
    trimJednolinijskiTekstESP(polje);

    char* jednako = strchr(polje, '=');
    if (jednako == nullptr) {
      return false;
    }

    *jednako = '\0';
    char* kljuc = polje;
    char* vrijednost = jednako + 1;
    trimJednolinijskiTekstESP(kljuc);
    trimJednolinijskiTekstESP(vrijednost);

    unsigned int omogucena = 0;
    unsigned int sat = 0;
    unsigned int minuta = 0;
    if (sscanf_P(vrijednost, PSTR("%u,%u,%u"), &omogucena, &sat, &minuta) != 3) {
      return false;
    }

    if (strcmp(kljuc, "rd") == 0) {
      redoviteMise.dnevnaOmogucena = omogucena != 0;
      redoviteMise.dnevnaSatMise = static_cast<uint8_t>(sat);
      redoviteMise.dnevnaMinutaMise = static_cast<uint8_t>(minuta);
      dnevnaMisaPoznata = true;
    } else if (strcmp(kljuc, "nd") == 0) {
      redoviteMise.nedjeljnaOmogucena = omogucena != 0;
      redoviteMise.nedjeljnaSatMise = static_cast<uint8_t>(sat);
      redoviteMise.nedjeljnaMinutaMise = static_cast<uint8_t>(minuta);
      nedjeljnaMisaPoznata = true;
    } else {
      return false;
    }
  }

  if (!dnevnaMisaPoznata || !nedjeljnaMisaPoznata) {
    return false;
  }

  postaviRedoviteMise(redoviteMise);
  posaljiPCLog(F("Mrezni most je spremio misne postavke toranjskog sata"));
  return true;
}

bool spremiNepomicneBlagdaneIzESPa(char* payload) {
  if (payload == nullptr || payload[0] == '\0') {
    return false;
  }

  NepomicniBlagdanPostavka nepomicni[BROJ_NEPOMICNIH_BLAGDANA] = {};
  PomicniBlagdanPostavka pomicni[BROJ_POMICNIH_BLAGDANA] = {};
  bool nepomicniPoznati[BROJ_NEPOMICNIH_BLAGDANA] = {};

  for (uint8_t i = 0; i < BROJ_POMICNIH_BLAGDANA; ++i) {
    dohvatiPomicniBlagdan(i, pomicni[i]);
  }

  char* context = nullptr;
  for (char* polje = strtok_r(payload, "|", &context);
       polje != nullptr;
       polje = strtok_r(nullptr, "|", &context)) {
    trimJednolinijskiTekstESP(polje);

    char* jednako = strchr(polje, '=');
    if (jednako == nullptr) {
      return false;
    }

    *jednako = '\0';
    char* kljuc = polje;
    char* vrijednost = jednako + 1;
    trimJednolinijskiTekstESP(kljuc);
    trimJednolinijskiTekstESP(vrijednost);

    if (kljuc[0] != 'f' || kljuc[1] == '\0') {
      return false;
    }

    char* krajIndeksa = nullptr;
    const unsigned long indeksBroj = strtoul(kljuc + 1, &krajIndeksa, 10);
    if (krajIndeksa == nullptr || *krajIndeksa != '\0') {
      return false;
    }

    const uint8_t indeks = static_cast<uint8_t>(indeksBroj);
    if (indeks >= BROJ_NEPOMICNIH_BLAGDANA) {
      return false;
    }

    unsigned int omogucen = 0;
    unsigned int sat = 0;
    unsigned int minuta = 0;
    if (sscanf_P(vrijednost, PSTR("%u,%u,%u"), &omogucen, &sat, &minuta) != 3) {
      return false;
    }

    nepomicni[indeks].omogucen = omogucen != 0;
    nepomicni[indeks].satMise = static_cast<uint8_t>(sat);
    nepomicni[indeks].minutaMise = static_cast<uint8_t>(minuta);
    nepomicniPoznati[indeks] = true;
  }

  for (uint8_t i = 0; i < BROJ_NEPOMICNIH_BLAGDANA; ++i) {
    if (!nepomicniPoznati[i]) {
      return false;
    }
  }

  postaviBlagdanskeMise(nepomicni,
                        BROJ_NEPOMICNIH_BLAGDANA,
                        pomicni,
                        BROJ_POMICNIH_BLAGDANA);
  posaljiPCLog(F("Mrezni most je spremio nepomicne blagdane toranjskog sata"));
  return true;
}

bool spremiPomicneBlagdaneIzESPa(char* payload) {
  if (payload == nullptr || payload[0] == '\0') {
    return false;
  }

  NepomicniBlagdanPostavka nepomicni[BROJ_NEPOMICNIH_BLAGDANA] = {};
  PomicniBlagdanPostavka pomicni[BROJ_POMICNIH_BLAGDANA] = {};
  bool pomicniPoznati[BROJ_POMICNIH_BLAGDANA] = {};

  for (uint8_t i = 0; i < BROJ_NEPOMICNIH_BLAGDANA; ++i) {
    dohvatiNepomicniBlagdan(i, nepomicni[i]);
  }

  char* context = nullptr;
  for (char* polje = strtok_r(payload, "|", &context);
       polje != nullptr;
       polje = strtok_r(nullptr, "|", &context)) {
    trimJednolinijskiTekstESP(polje);

    char* jednako = strchr(polje, '=');
    if (jednako == nullptr) {
      return false;
    }

    *jednako = '\0';
    char* kljuc = polje;
    char* vrijednost = jednako + 1;
    trimJednolinijskiTekstESP(kljuc);
    trimJednolinijskiTekstESP(vrijednost);

    if (kljuc[0] != 'p' || kljuc[1] == '\0') {
      return false;
    }

    char* krajIndeksa = nullptr;
    const unsigned long indeksBroj = strtoul(kljuc + 1, &krajIndeksa, 10);
    if (krajIndeksa == nullptr || *krajIndeksa != '\0') {
      return false;
    }

    const uint8_t indeks = static_cast<uint8_t>(indeksBroj);
    if (indeks >= BROJ_POMICNIH_BLAGDANA) {
      return false;
    }

    unsigned int omogucen = 0;
    unsigned int sat = 0;
    unsigned int minuta = 0;
    if (sscanf_P(vrijednost, PSTR("%u,%u,%u"), &omogucen, &sat, &minuta) != 3) {
      return false;
    }

    pomicni[indeks].omogucen = omogucen != 0;
    pomicni[indeks].satMise = static_cast<uint8_t>(sat);
    pomicni[indeks].minutaMise = static_cast<uint8_t>(minuta);
    pomicniPoznati[indeks] = true;
  }

  for (uint8_t i = 0; i < BROJ_POMICNIH_BLAGDANA; ++i) {
    if (!pomicniPoznati[i]) {
      return false;
    }
  }

  postaviBlagdanskeMise(nepomicni,
                        BROJ_NEPOMICNIH_BLAGDANA,
                        pomicni,
                        BROJ_POMICNIH_BLAGDANA);
  posaljiPCLog(F("Mrezni most je spremio pomicne blagdane toranjskog sata"));
  return true;
}

bool spremiSetupWiFiPostavkeIzESPa(const char* payload) {
  if (payload == nullptr || payload[0] == '\0') {
    return false;
  }

  const char* granica = strchr(payload, '|');
  if (granica == nullptr) {
    return false;
  }

  const size_t duljinaSsid = static_cast<size_t>(granica - payload);
  const char* lozinka = granica + 1;
  const size_t duljinaLozinke = strlen(lozinka);

  if (duljinaSsid == 0 || duljinaSsid > 32 || duljinaLozinke == 0 || duljinaLozinke > 32) {
    return false;
  }

  char ssid[33];
  char lozinkaBuffer[33];
  memcpy(ssid, payload, duljinaSsid);
  ssid[duljinaSsid] = '\0';
  memcpy(lozinkaBuffer, lozinka, duljinaLozinke);
  lozinkaBuffer[duljinaLozinke] = '\0';

  for (size_t i = 0; i < duljinaSsid; ++i) {
    const char znak = ssid[i];
    if (znak == '|' || znak == '\r' || znak == '\n') {
      return false;
    }
  }

  for (size_t i = 0; i < duljinaLozinke; ++i) {
    const char znak = lozinkaBuffer[i];
    if (znak == '|' || znak == '\r' || znak == '\n') {
      return false;
    }
  }

  postaviWiFiPodatkeZaSetup(ssid, lozinkaBuffer);
  postaviWiFiOmogucen(true);
  posaljiWifiPostavkeESP();
  posaljiWiFiStatusESP();
  zatraziWiFiStatusESP();

  char log[88];
  snprintf_P(log,
             sizeof(log),
             PSTR("Setup WiFi: spremljen novi SSID=%s preko mreznog mosta"),
             ssid);
  posaljiPCLog(log);
  return true;
}

bool obradiESPPostavkeLiniju(const char* linija) {
  if (strcmp(linija, "CFGREQ") == 0) {
    posaljiKonfiguracijuESPuNakonZahtjeva();
    return true;
  }

  if (strncmp(linija, "SETUPWIFI:", 10) == 0) {
    if (spremiSetupWiFiPostavkeIzESPa(linija + 10)) {
      espSerijskiPort.println(F("ACK:SETUPWIFI"));
    } else {
      espSerijskiPort.println(F("ERR:SETUPWIFI"));
    }
    return true;
  }

  if (strcmp(linija, "SETREQ:SUSTAV") == 0) {
    posaljiSustavskePostavkeESPu();
    posaljiPCLog(F("Mrezni most je zatrazio sustavske postavke toranjskog sata"));
    return true;
  }

  if (strcmp(linija, "SETREQ:STAPICI") == 0) {
    posaljiPostavkeStapicaESPu();
    posaljiPCLog(F("Mrezni most je zatrazio postavke stapica toranjskog sata"));
    return true;
  }

  if (strcmp(linija, "SETREQ:BAT") == 0) {
    posaljiBATPostavkeESPu();
    posaljiPCLog(F("Mrezni most je zatrazio BAT postavke toranjskog sata"));
    return true;
  }

  if (strcmp(linija, "SETREQ:SUNCE") == 0) {
    posaljiSuncevePostavkeESPu();
    posaljiPCLog(F("Mrezni most je zatrazio sunceve postavke toranjskog sata"));
    return true;
  }

  if (strcmp(linija, "SETREQ:MISE") == 0) {
    posaljiMisePostavkeESPu();
    posaljiPCLog(F("Mrezni most je zatrazio misne postavke toranjskog sata"));
    return true;
  }

  if (strcmp(linija, "SETREQ:BLAGDANI_NEP") == 0) {
    posaljiNepomicneBlagdaneESPu();
    posaljiPCLog(F("Mrezni most je zatrazio nepomicne blagdane toranjskog sata"));
    return true;
  }

  if (strcmp(linija, "SETREQ:BLAGDANI_POM") == 0) {
    posaljiPomicneBlagdaneESPu();
    posaljiPCLog(F("Mrezni most je zatrazio pomicne blagdane toranjskog sata"));
    return true;
  }

  if (strncmp(linija, "SETCFG:SUSTAV|", 14) == 0) {
    if (spremiSustavskePostavkeIzESPa(const_cast<char*>(linija + 14))) {
      posaljiSustavskePostavkeESPu();
      espSerijskiPort.println(F("ACK:SETCFG"));
    } else {
      espSerijskiPort.println(F("ERR:SETCFG"));
      posaljiPCLog(F("Mrezni most je poslao neispravne sustavske postavke toranjskog sata"));
    }
    return true;
  }

  if (strncmp(linija, "SETCFG:STAPICI|", 15) == 0) {
    if (spremiPostavkeStapicaIzESPa(const_cast<char*>(linija + 15))) {
      posaljiPostavkeStapicaESPu();
      espSerijskiPort.println(F("ACK:SETCFG"));
    } else {
      espSerijskiPort.println(F("ERR:SETCFG"));
      posaljiPCLog(F("Mrezni most je poslao neispravne postavke stapica toranjskog sata"));
    }
    return true;
  }

  if (strncmp(linija, "SETCFG:BAT|", 11) == 0) {
    if (spremiBATPostavkeIzESPa(const_cast<char*>(linija + 11))) {
      posaljiBATPostavkeESPu();
      espSerijskiPort.println(F("ACK:SETCFG"));
    } else {
      espSerijskiPort.println(F("ERR:SETCFG"));
      posaljiPCLog(F("Mrezni most je poslao neispravne BAT postavke toranjskog sata"));
    }
    return true;
  }

  if (strncmp(linija, "SETCFG:SUNCE|", 13) == 0) {
    if (spremiSuncevePostavkeIzESPa(const_cast<char*>(linija + 13))) {
      posaljiSuncevePostavkeESPu();
      espSerijskiPort.println(F("ACK:SETCFG"));
    } else {
      espSerijskiPort.println(F("ERR:SETCFG"));
      posaljiPCLog(F("Mrezni most je poslao neispravne sunceve postavke toranjskog sata"));
    }
    return true;
  }

  if (strncmp(linija, "SETCFG:MISE|", 12) == 0) {
    if (spremiMisePostavkeIzESPa(const_cast<char*>(linija + 12))) {
      posaljiMisePostavkeESPu();
      espSerijskiPort.println(F("ACK:SETCFG"));
    } else {
      espSerijskiPort.println(F("ERR:SETCFG"));
      posaljiPCLog(F("Mrezni most je poslao neispravne misne postavke toranjskog sata"));
    }
    return true;
  }

  if (strncmp(linija, "SETCFG:BLAGDANI_NEP|", 20) == 0) {
    if (spremiNepomicneBlagdaneIzESPa(const_cast<char*>(linija + 20))) {
      posaljiNepomicneBlagdaneESPu();
      espSerijskiPort.println(F("ACK:SETCFG"));
    } else {
      espSerijskiPort.println(F("ERR:SETCFG"));
      posaljiPCLog(F("Mrezni most je poslao neispravne nepomicne blagdane toranjskog sata"));
    }
    return true;
  }

  if (strncmp(linija, "SETCFG:BLAGDANI_POM|", 20) == 0) {
    if (spremiPomicneBlagdaneIzESPa(const_cast<char*>(linija + 20))) {
      posaljiPomicneBlagdaneESPu();
      espSerijskiPort.println(F("ACK:SETCFG"));
    } else {
      espSerijskiPort.println(F("ERR:SETCFG"));
      posaljiPCLog(F("Mrezni most je poslao neispravne pomicne blagdane toranjskog sata"));
    }
    return true;
  }

  return false;
}
