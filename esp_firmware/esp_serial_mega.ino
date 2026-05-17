// esp_serial_mega.ino - serijski protokol izmedu ESP32 mreznog mosta i Mega kontrolera

String ocistiJednolinijskiTekst(const String &ulaz, size_t maxDuljina) {
  String izlaz = "";
  size_t limit = ulaz.length();
  if (limit > maxDuljina) {
    limit = maxDuljina;
  }

  izlaz.reserve(limit);
  for (size_t i = 0; i < limit; ++i) {
    char znak = ulaz.charAt(i);
    if (znak == '\r' || znak == '\n') {
      izlaz += ' ';
    } else {
      izlaz += znak;
    }
  }
  return izlaz;
}

void trimJednolinijskiBuffer(char* tekst) {
  if (tekst == nullptr) {
    return;
  }

  size_t duljina = strlen(tekst);
  while (duljina > 0 &&
         (tekst[duljina - 1] == ' ' || tekst[duljina - 1] == '\t' ||
          tekst[duljina - 1] == '\r' || tekst[duljina - 1] == '\n')) {
    tekst[--duljina] = '\0';
  }

  size_t pocetak = 0;
  while (tekst[pocetak] == ' ' || tekst[pocetak] == '\t') {
    ++pocetak;
  }

  if (pocetak > 0) {
    memmove(tekst, tekst + pocetak, duljina - pocetak + 1);
  }
}

void kopirajOcisceniBuffer(const char* ulaz, char* izlaz, size_t velicina) {
  if (izlaz == nullptr || velicina == 0) {
    return;
  }

  if (ulaz == nullptr) {
    izlaz[0] = '\0';
    return;
  }

  size_t indeks = 0;
  while (*ulaz != '\0' && indeks + 1 < velicina) {
    char znak = *ulaz++;
    izlaz[indeks++] = (znak == '\r' || znak == '\n') ? ' ' : znak;
  }
  izlaz[indeks] = '\0';
  trimJednolinijskiBuffer(izlaz);
}

bool jeDecimalniBrojString(const String& vrijednost) {
  if (vrijednost.length() == 0) {
    return false;
  }

  for (size_t i = 0; i < vrijednost.length(); ++i) {
    if (!isDigit(vrijednost.charAt(i))) {
      return false;
    }
  }

  return true;
}

bool jePotpisaniDecimalniBrojString(const String& vrijednost) {
  if (vrijednost.length() == 0) {
    return false;
  }

  if (vrijednost.charAt(0) == '-') {
    return vrijednost.length() > 1 && jeDecimalniBrojString(vrijednost.substring(1));
  }

  return jeDecimalniBrojString(vrijednost);
}

bool posaljiSetupWiFiMegai(const String &ssid, const String &lozinka, String &odgovor, unsigned long timeoutMs) {
  odgovorSetupWiFiPrimljen = false;
  setupWiFiNeuspjeh = false;
  Serial.print("SETUPWIFI:");
  Serial.print(ssid);
  Serial.print("|");
  Serial.println(lozinka);

  unsigned long pocetak = millis();
  while ((millis() - pocetak) < timeoutMs) {
    obradiSerijskiUlaz();
    if (odgovorSetupWiFiPrimljen) {
      if (setupWiFiNeuspjeh) {
        odgovor = "Mega nije prihvatila novu WiFi mrezu";
        return false;
      }

      strncpy(wifiSsid, ssid.c_str(), sizeof(wifiSsid) - 1);
      wifiSsid[sizeof(wifiSsid) - 1] = '\0';
      strncpy(wifiLozinka, lozinka.c_str(), sizeof(wifiLozinka) - 1);
      wifiLozinka[sizeof(wifiLozinka) - 1] = '\0';
      primljenaWifiKonfiguracija = true;
      primijeniWiFiOmogucenost(true);
      setupApZakazanoGasenjeMs = millis() + WIFI_SETUP_GASENJE_NAKON_SPREMANJA_MS;
      odgovor =
          "Nova WiFi mreza je spremljena. Setup mreza ce se uskoro ugasiti, a ESP pokusava spoj na novu mrezu.";
      return true;
    }
    delay(10);
    yield();
  }

  odgovor = "Mega nije potvrdila spremanje nove WiFi mreze";
  return false;
}

static bool procitajStatusZastavicu(const char* payload, const char* oznaka, bool* izlaz) {
  if (payload == nullptr || oznaka == nullptr || izlaz == nullptr) {
    return false;
  }

  const char* pronadeno = strstr(payload, oznaka);
  if (pronadeno == nullptr) {
    return false;
  }

  pronadeno += strlen(oznaka);
  if (*pronadeno == '1') {
    *izlaz = true;
    return true;
  }
  if (*pronadeno == '0') {
    *izlaz = false;
    return true;
  }
  return false;
}

static const char* pronadiVrijednostPolja(const char* payload, const char* oznaka) {
  if (payload == nullptr || oznaka == nullptr) {
    return nullptr;
  }

  const size_t duljinaOznake = strlen(oznaka);
  const char* pokazivac = payload;
  while (pokazivac != nullptr && *pokazivac != '\0') {
    if ((pokazivac == payload || pokazivac[-1] == '|') &&
        strncmp(pokazivac, oznaka, duljinaOznake) == 0) {
      return pokazivac + duljinaOznake;
    }

    pokazivac = strchr(pokazivac, '|');
    if (pokazivac != nullptr) {
      ++pokazivac;
    }
  }

  return nullptr;
}

static bool procitajBoolPolje(const char* payload, const char* oznaka, bool* izlaz) {
  if (izlaz == nullptr) {
    return false;
  }

  const char* vrijednost = pronadiVrijednostPolja(payload, oznaka);
  if (vrijednost == nullptr) {
    return false;
  }

  if (vrijednost[0] == '1' && (vrijednost[1] == '\0' || vrijednost[1] == '|')) {
    *izlaz = true;
    return true;
  }

  if (vrijednost[0] == '0' && (vrijednost[1] == '\0' || vrijednost[1] == '|')) {
    *izlaz = false;
    return true;
  }

  return false;
}

static bool procitajUIntPolje(const char* payload, const char* oznaka, unsigned int* izlaz) {
  if (izlaz == nullptr) {
    return false;
  }

  const char* vrijednost = pronadiVrijednostPolja(payload, oznaka);
  if (vrijednost == nullptr || *vrijednost == '\0') {
    return false;
  }

  unsigned long akumulator = 0UL;
  const char* pokazivac = vrijednost;
  while (*pokazivac != '\0' && *pokazivac != '|') {
    if (!isDigit(*pokazivac)) {
      return false;
    }
    akumulator = akumulator * 10UL + static_cast<unsigned long>(*pokazivac - '0');
    if (akumulator > 60000UL) {
      return false;
    }
    ++pokazivac;
  }

  *izlaz = static_cast<unsigned int>(akumulator);
  return true;
}

static bool procitajIntPolje(const char* payload, const char* oznaka, int* izlaz) {
  if (izlaz == nullptr) {
    return false;
  }

  const char* vrijednost = pronadiVrijednostPolja(payload, oznaka);
  if (vrijednost == nullptr || *vrijednost == '\0') {
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
  while (*pokazivac != '\0' && *pokazivac != '|') {
    if (!isDigit(*pokazivac)) {
      return false;
    }
    akumulator = akumulator * 10L + static_cast<long>(*pokazivac - '0');
    if (akumulator > 60000L) {
      return false;
    }
    ++pokazivac;
  }

  *izlaz = static_cast<int>(negativan ? -akumulator : akumulator);
  return true;
}

bool obradiStatusMegai(const char* payload) {
  bool zvono1Aktivno = false;
  bool zvono2Aktivno = false;
  bool slavljenjeAktivno = false;
  bool mrtvackoAktivno = false;
  unsigned int pogrebnaSkriptaTip = 0;
  bool sunceJutroAktivno = false;
  bool suncePodneAktivno = false;
  bool sunceVecerAktivno = false;
  bool tihiRezimAktivan = false;

  if (!procitajStatusZastavicu(payload, "b1=", &zvono1Aktivno) ||
      !procitajStatusZastavicu(payload, "b2=", &zvono2Aktivno) ||
      !procitajStatusZastavicu(payload, "sl=", &slavljenjeAktivno) ||
      !procitajStatusZastavicu(payload, "mr=", &mrtvackoAktivno) ||
      !procitajUIntPolje(payload, "pk=", &pogrebnaSkriptaTip) ||
      !procitajStatusZastavicu(payload, "sj=", &sunceJutroAktivno) ||
      !procitajStatusZastavicu(payload, "sp=", &suncePodneAktivno) ||
      !procitajStatusZastavicu(payload, "sv=", &sunceVecerAktivno) ||
      !procitajStatusZastavicu(payload, "tm=", &tihiRezimAktivan)) {
    return false;
  }

  megaZvono1Aktivno = zvono1Aktivno;
  megaZvono2Aktivno = zvono2Aktivno;
  megaSlavljenjeAktivno = slavljenjeAktivno;
  megaMrtvackoAktivno = mrtvackoAktivno;
  megaPogrebnaSkriptaTip = static_cast<uint8_t>(pogrebnaSkriptaTip);
  megaSunceJutroAktivno = sunceJutroAktivno;
  megaSuncePodneAktivno = suncePodneAktivno;
  megaSunceVecerAktivno = sunceVecerAktivno;
  megaTihiRezimAktivan = tihiRezimAktivan;
  megaStatusPoznat = true;
  megaStatusZadnjeOsvjezavanjeMs = millis();
  ++megaStatusSerijskiBroj;
  return true;
}

bool obradiSustavskePostavkeMegai(char* payload) {
  bool lcdPozadinskoOsvjetljenje = false;
  bool logiranje = false;
  bool rs485 = false;
  bool upsMod = false;
  bool kocnicaZvona = false;
  unsigned int inercijaZvona1Sekunde = 0;
  unsigned int inercijaZvona2Sekunde = 0;
  unsigned int impulsCekicaMs = 0;

  if (!procitajBoolPolje(payload, "lcd=", &lcdPozadinskoOsvjetljenje) ||
      !procitajBoolPolje(payload, "log=", &logiranje) ||
      !procitajBoolPolje(payload, "rs=", &rs485) ||
      !procitajBoolPolje(payload, "ups=", &upsMod) ||
      !procitajBoolPolje(payload, "koc=", &kocnicaZvona) ||
      !procitajUIntPolje(payload, "inr1=", &inercijaZvona1Sekunde) ||
      !procitajUIntPolje(payload, "inr2=", &inercijaZvona2Sekunde) ||
      !procitajUIntPolje(payload, "imp=", &impulsCekicaMs)) {
    return false;
  }

  megaSustavskePostavke.poznate = true;
  megaSustavskePostavke.lcdPozadinskoOsvjetljenje = lcdPozadinskoOsvjetljenje;
  megaSustavskePostavke.logiranje = logiranje;
  megaSustavskePostavke.rs485 = rs485;
  megaSustavskePostavke.upsMod = upsMod;
  megaSustavskePostavke.kocnicaZvona = kocnicaZvona;
  megaSustavskePostavke.inercijaZvona1Sekunde =
      static_cast<uint8_t>(inercijaZvona1Sekunde);
  megaSustavskePostavke.inercijaZvona2Sekunde =
      static_cast<uint8_t>(inercijaZvona2Sekunde);
  megaSustavskePostavke.impulsCekicaMs = impulsCekicaMs;
  ++megaSustavskePostavke.serijskiBroj;
  return true;
}

bool obradiPostavkeStapicaMegai(char* payload) {
  unsigned int trajanjeRadniMin = 0;
  unsigned int trajanjeNedjeljaMin = 0;
  unsigned int trajanjeSlavljenjaMin = 0;
  unsigned int odgodaSlavljenjaSekunde = 0;

  if (!procitajUIntPolje(payload, "tr=", &trajanjeRadniMin) ||
      !procitajUIntPolje(payload, "tn=", &trajanjeNedjeljaMin) ||
      !procitajUIntPolje(payload, "ts=", &trajanjeSlavljenjaMin) ||
      !procitajUIntPolje(payload, "odg=", &odgodaSlavljenjaSekunde)) {
    return false;
  }

  megaPostavkeStapica.poznate = true;
  megaPostavkeStapica.trajanjeRadniMin = static_cast<uint8_t>(trajanjeRadniMin);
  megaPostavkeStapica.trajanjeNedjeljaMin = static_cast<uint8_t>(trajanjeNedjeljaMin);
  megaPostavkeStapica.trajanjeSlavljenjaMin = static_cast<uint8_t>(trajanjeSlavljenjaMin);
  megaPostavkeStapica.odgodaSlavljenjaSekunde =
      static_cast<uint8_t>(odgodaSlavljenjaSekunde);
  ++megaPostavkeStapica.serijskiBroj;
  return true;
}

bool obradiBATPostavkeMegai(char* payload) {
  unsigned int satOd = 0;
  unsigned int satDo = 0;
  unsigned int modOtkucavanja = 0;
  unsigned int modSlavljenja = 0;
  unsigned int modMrtvackog = 0;

  if (!procitajUIntPolje(payload, "od=", &satOd) ||
      !procitajUIntPolje(payload, "do=", &satDo) ||
      !procitajUIntPolje(payload, "otk=", &modOtkucavanja) ||
      !procitajUIntPolje(payload, "sl=", &modSlavljenja) ||
      !procitajUIntPolje(payload, "mr=", &modMrtvackog)) {
    return false;
  }

  megaBATPostavke.poznate = true;
  megaBATPostavke.satOd = static_cast<uint8_t>(satOd);
  megaBATPostavke.satDo = static_cast<uint8_t>(satDo);
  megaBATPostavke.modOtkucavanja = static_cast<uint8_t>(modOtkucavanja);
  megaBATPostavke.modSlavljenja = static_cast<uint8_t>(modSlavljenja);
  megaBATPostavke.modMrtvackog = static_cast<uint8_t>(modMrtvackog);
  ++megaBATPostavke.serijskiBroj;
  return true;
}

bool obradiSuncevePostavkeMegai(char* payload) {
  bool jutroOmoguceno = false;
  bool podneOmoguceno = false;
  bool vecerOmoguceno = false;
  bool nocnaRasvjeta = false;
  unsigned int jutroZvono = 0;
  unsigned int podneZvono = 0;
  unsigned int vecerZvono = 0;
  int jutroOdgodaMin = 0;
  int vecerOdgodaMin = 0;

  if (!procitajBoolPolje(payload, "ju=", &jutroOmoguceno) ||
      !procitajUIntPolje(payload, "jb=", &jutroZvono) ||
      !procitajIntPolje(payload, "jo=", &jutroOdgodaMin) ||
      !procitajBoolPolje(payload, "pu=", &podneOmoguceno) ||
      !procitajUIntPolje(payload, "pb=", &podneZvono) ||
      !procitajBoolPolje(payload, "vu=", &vecerOmoguceno) ||
      !procitajUIntPolje(payload, "vb=", &vecerZvono) ||
      !procitajIntPolje(payload, "vo=", &vecerOdgodaMin) ||
      !procitajBoolPolje(payload, "nr=", &nocnaRasvjeta)) {
    return false;
  }

  megaSuncevePostavke.poznate = true;
  megaSuncevePostavke.jutroOmoguceno = jutroOmoguceno;
  megaSuncevePostavke.jutroZvono = static_cast<uint8_t>(jutroZvono);
  megaSuncevePostavke.jutroOdgodaMin = static_cast<int8_t>(jutroOdgodaMin);
  megaSuncevePostavke.podneOmoguceno = podneOmoguceno;
  megaSuncevePostavke.podneZvono = static_cast<uint8_t>(podneZvono);
  megaSuncevePostavke.vecerOmoguceno = vecerOmoguceno;
  megaSuncevePostavke.vecerZvono = static_cast<uint8_t>(vecerZvono);
  megaSuncevePostavke.vecerOdgodaMin = static_cast<int8_t>(vecerOdgodaMin);
  megaSuncevePostavke.nocnaRasvjeta = nocnaRasvjeta;
  ++megaSuncevePostavke.serijskiBroj;
  return true;
}

bool obradiMisePostavkeMegai(char* payload) {
  if (payload == nullptr || payload[0] == '\0') {
    return false;
  }

  bool dnevnaMisaPoznata = false;
  bool nedjeljnaMisaPoznata = false;
  bool dnevnaOmogucena = false;
  uint8_t dnevnaSat = 0;
  uint8_t dnevnaMinuta = 0;
  bool nedjeljnaOmogucena = false;
  uint8_t nedjeljnaSat = 0;
  uint8_t nedjeljnaMinuta = 0;

  char* context = nullptr;
  for (char* polje = strtok_r(payload, "|", &context);
       polje != nullptr;
       polje = strtok_r(nullptr, "|", &context)) {
    trimJednolinijskiBuffer(polje);

    char* jednako = strchr(polje, '=');
    if (jednako == nullptr) {
      return false;
    }

    *jednako = '\0';
    char* kljuc = polje;
    char* vrijednost = jednako + 1;
    trimJednolinijskiBuffer(kljuc);
    trimJednolinijskiBuffer(vrijednost);

    unsigned int omogucena = 0;
    unsigned int sat = 0;
    unsigned int minuta = 0;
    if (sscanf(vrijednost, "%u,%u,%u", &omogucena, &sat, &minuta) != 3) {
      return false;
    }

    if (strcmp(kljuc, "rd") == 0) {
      dnevnaOmogucena = omogucena != 0;
      dnevnaSat = static_cast<uint8_t>(sat);
      dnevnaMinuta = static_cast<uint8_t>(minuta);
      dnevnaMisaPoznata = true;
    } else if (strcmp(kljuc, "nd") == 0) {
      nedjeljnaOmogucena = omogucena != 0;
      nedjeljnaSat = static_cast<uint8_t>(sat);
      nedjeljnaMinuta = static_cast<uint8_t>(minuta);
      nedjeljnaMisaPoznata = true;
    } else {
      return false;
    }
  }

  if (!dnevnaMisaPoznata || !nedjeljnaMisaPoznata) {
    return false;
  }

  megaBlagdanskePostavke.dnevnaMisaOmogucena = dnevnaOmogucena;
  megaBlagdanskePostavke.dnevnaMisaSat = dnevnaSat;
  megaBlagdanskePostavke.dnevnaMisaMinuta = dnevnaMinuta;
  megaBlagdanskePostavke.nedjeljnaMisaOmogucena = nedjeljnaOmogucena;
  megaBlagdanskePostavke.nedjeljnaMisaSat = nedjeljnaSat;
  megaBlagdanskePostavke.nedjeljnaMisaMinuta = nedjeljnaMinuta;
  megaBlagdanskePostavke.poznateMise = true;
  ++megaBlagdanskePostavke.serijskiBrojMise;
  return true;
}

bool obradiNepomicneBlagdaneMegai(char* payload) {
  if (payload == nullptr || payload[0] == '\0') {
    return false;
  }

  bool nepomicniPoznati[BROJ_NEPOMICNIH_BLAGDANA] = {};

  char* context = nullptr;
  for (char* polje = strtok_r(payload, "|", &context);
       polje != nullptr;
       polje = strtok_r(nullptr, "|", &context)) {
    trimJednolinijskiBuffer(polje);

    char* jednako = strchr(polje, '=');
    if (jednako == nullptr) {
      return false;
    }

    *jednako = '\0';
    char* kljuc = polje;
    char* vrijednost = jednako + 1;
    trimJednolinijskiBuffer(kljuc);
    trimJednolinijskiBuffer(vrijednost);

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
    if (sscanf(vrijednost, "%u,%u,%u", &omogucen, &sat, &minuta) != 3) {
      return false;
    }

    megaBlagdanskePostavke.nepomicni[indeks].omogucen = omogucen != 0;
    megaBlagdanskePostavke.nepomicni[indeks].satMise = static_cast<uint8_t>(sat);
    megaBlagdanskePostavke.nepomicni[indeks].minutaMise = static_cast<uint8_t>(minuta);
    nepomicniPoznati[indeks] = true;
  }

  for (uint8_t i = 0; i < BROJ_NEPOMICNIH_BLAGDANA; ++i) {
    if (!nepomicniPoznati[i]) {
      return false;
    }
  }

  megaBlagdanskePostavke.poznatiNepomicni = true;
  ++megaBlagdanskePostavke.serijskiBrojNepomicni;
  return true;
}

bool obradiPomicneBlagdaneMegai(char* payload) {
  if (payload == nullptr || payload[0] == '\0') {
    return false;
  }

  bool pomicniPoznati[BROJ_POMICNIH_BLAGDANA] = {};

  char* context = nullptr;
  for (char* polje = strtok_r(payload, "|", &context);
       polje != nullptr;
       polje = strtok_r(nullptr, "|", &context)) {
    trimJednolinijskiBuffer(polje);

    char* jednako = strchr(polje, '=');
    if (jednako == nullptr) {
      return false;
    }

    *jednako = '\0';
    char* kljuc = polje;
    char* vrijednost = jednako + 1;
    trimJednolinijskiBuffer(kljuc);
    trimJednolinijskiBuffer(vrijednost);

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
    if (sscanf(vrijednost, "%u,%u,%u", &omogucen, &sat, &minuta) != 3) {
      return false;
    }

    megaBlagdanskePostavke.pomicni[indeks].omogucen = omogucen != 0;
    megaBlagdanskePostavke.pomicni[indeks].satMise = static_cast<uint8_t>(sat);
    megaBlagdanskePostavke.pomicni[indeks].minutaMise = static_cast<uint8_t>(minuta);
    pomicniPoznati[indeks] = true;
  }

  for (uint8_t i = 0; i < BROJ_POMICNIH_BLAGDANA; ++i) {
    if (!pomicniPoznati[i]) {
      return false;
    }
  }

  megaBlagdanskePostavke.poznatiPomicni = true;
  ++megaBlagdanskePostavke.serijskiBrojPomicni;
  return true;
}

bool osvjeziStatusMegai(bool prisilno) {
  const unsigned long sadaMs = millis();
  if (!prisilno) {
    if (megaStatusPoznat &&
        (sadaMs - megaStatusZadnjeOsvjezavanjeMs) <= STATUS_MAKSIMALNA_STAROST_MS) {
      return true;
    }

    // Home dashboard mora se otvoriti odmah, cak i ako Mega trenutno ne vrati status.
    // Zato ne radimo aktivni serijski upit osim kad je osvjezavanje izricito prisiljeno.
    return megaStatusPoznat;
  }

  const unsigned long pocetniBroj = megaStatusSerijskiBroj;
  Serial.println("STATUS?");

  const unsigned long pocetakMs = millis();
  while ((millis() - pocetakMs) < STATUS_CEKANJE_NA_MEGU_MS) {
    obradiSerijskiUlaz();
    if (megaStatusSerijskiBroj != pocetniBroj) {
      return megaStatusPoznat;
    }
    delay(1);
    yield();
  }

  return megaStatusPoznat;
}

bool osvjeziSustavskePostavkeMegai(bool prisilno) {
  if (!prisilno) {
    return megaSustavskePostavke.poznate;
  }

  const unsigned long pocetniBroj = megaSustavskePostavke.serijskiBroj;
  Serial.println("SETREQ:SUSTAV");

  const unsigned long pocetakMs = millis();
  while ((millis() - pocetakMs) < STATUS_CEKANJE_NA_MEGU_MS) {
    obradiSerijskiUlaz();
    if (megaSustavskePostavke.serijskiBroj != pocetniBroj) {
      return megaSustavskePostavke.poznate;
    }
    delay(1);
    yield();
  }

  return megaSustavskePostavke.poznate;
}

bool osvjeziPostavkeStapicaMegai(bool prisilno) {
  if (!prisilno) {
    return megaPostavkeStapica.poznate;
  }

  const unsigned long pocetniBroj = megaPostavkeStapica.serijskiBroj;
  Serial.println("SETREQ:STAPICI");

  const unsigned long pocetakMs = millis();
  while ((millis() - pocetakMs) < STATUS_CEKANJE_NA_MEGU_MS) {
    obradiSerijskiUlaz();
    if (megaPostavkeStapica.serijskiBroj != pocetniBroj) {
      return megaPostavkeStapica.poznate;
    }
    delay(1);
    yield();
  }

  return megaPostavkeStapica.poznate;
}

bool osvjeziBATPostavkeMegai(bool prisilno) {
  if (!prisilno) {
    return megaBATPostavke.poznate;
  }

  const unsigned long pocetniBroj = megaBATPostavke.serijskiBroj;
  Serial.println("SETREQ:BAT");

  const unsigned long pocetakMs = millis();
  while ((millis() - pocetakMs) < STATUS_CEKANJE_NA_MEGU_MS) {
    obradiSerijskiUlaz();
    if (megaBATPostavke.serijskiBroj != pocetniBroj) {
      return megaBATPostavke.poznate;
    }
    delay(1);
    yield();
  }

  return megaBATPostavke.poznate;
}

bool osvjeziSuncevePostavkeMegai(bool prisilno) {
  if (!prisilno) {
    return megaSuncevePostavke.poznate;
  }

  const unsigned long pocetniBroj = megaSuncevePostavke.serijskiBroj;
  Serial.println("SETREQ:SUNCE");

  const unsigned long pocetakMs = millis();
  while ((millis() - pocetakMs) < STATUS_CEKANJE_NA_MEGU_MS) {
    obradiSerijskiUlaz();
    if (megaSuncevePostavke.serijskiBroj != pocetniBroj) {
      return megaSuncevePostavke.poznate;
    }
    delay(1);
    yield();
  }

  return megaSuncevePostavke.poznate;
}

bool osvjeziMisePostavkeMegai(bool prisilno) {
  if (!prisilno && megaBlagdanskePostavke.poznateMise) {
    return true;
  }

  const unsigned long pocetniBroj = megaBlagdanskePostavke.serijskiBrojMise;
  Serial.println("SETREQ:MISE");

  const unsigned long krajMs = millis() + STATUS_CEKANJE_NA_MEGU_MS;
  while (static_cast<long>(millis() - krajMs) < 0) {
    obradiSerijskiUlaz();
    yield();
    if (megaBlagdanskePostavke.serijskiBrojMise != pocetniBroj) {
      return megaBlagdanskePostavke.poznateMise;
    }
    delay(10);
  }

  obradiSerijskiUlaz();
  return megaBlagdanskePostavke.poznateMise;
}

bool osvjeziNepomicneBlagdaneMegai(bool prisilno) {
  if (!prisilno && megaBlagdanskePostavke.poznatiNepomicni) {
    return true;
  }

  const unsigned long pocetniBroj = megaBlagdanskePostavke.serijskiBrojNepomicni;
  Serial.println("SETREQ:BLAGDANI_NEP");

  const unsigned long krajMs = millis() + STATUS_CEKANJE_NA_MEGU_MS;
  while (static_cast<long>(millis() - krajMs) < 0) {
    obradiSerijskiUlaz();
    yield();
    if (megaBlagdanskePostavke.serijskiBrojNepomicni != pocetniBroj) {
      return megaBlagdanskePostavke.poznatiNepomicni;
    }
    delay(10);
  }

  obradiSerijskiUlaz();
  return megaBlagdanskePostavke.poznatiNepomicni;
}

bool osvjeziPomicneBlagdaneMegai(bool prisilno) {
  if (!prisilno && megaBlagdanskePostavke.poznatiPomicni) {
    return true;
  }

  const unsigned long pocetniBroj = megaBlagdanskePostavke.serijskiBrojPomicni;
  Serial.println("SETREQ:BLAGDANI_POM");

  const unsigned long krajMs = millis() + STATUS_CEKANJE_NA_MEGU_MS;
  while (static_cast<long>(millis() - krajMs) < 0) {
    obradiSerijskiUlaz();
    yield();
    if (megaBlagdanskePostavke.serijskiBrojPomicni != pocetniBroj) {
      return megaBlagdanskePostavke.poznatiPomicni;
    }
    delay(10);
  }

  obradiSerijskiUlaz();
  return megaBlagdanskePostavke.poznatiPomicni;
}

void obradiSerijskiUlaz() {
  static char prijemniBuffer[SERIJSKI_BUFFER_MAX + 1] = {0};
  static size_t prijemnaDuljina = 0;
  size_t obradeniBajtovi = 0;

  while (Serial.available() && obradeniBajtovi < SERIJSKI_BUDZET_BAJTOVA_PO_POZIVU) {
    char znak = static_cast<char>(Serial.read());
    ++obradeniBajtovi;
    if (znak == '\n') {
      prijemniBuffer[prijemnaDuljina] = '\0';
      trimJednolinijskiBuffer(prijemniBuffer);
      char* linija = prijemniBuffer;

      if (linija[0] != '\0') {
        if (strcmp(linija, "ACK:SETUPWIFI") == 0) {
          odgovorSetupWiFiPrimljen = true;
          setupWiFiNeuspjeh = false;
        } else if (strcmp(linija, "ACK:SETCFG") == 0) {
          zadnjiOdgovorSustavskihPostavkiMega = POSTAVKE_ODGOVOR_OK;
        } else if (strcmp(linija, "ERR:SETCFG") == 0) {
          zadnjiOdgovorSustavskihPostavkiMega = POSTAVKE_ODGOVOR_ERR;
        } else if (strcmp(linija, "ACK:CMD_OK") == 0) {
          zadnjiCmdOdgovorMega = CMD_ODGOVOR_OK;
        } else if (strcmp(linija, "ERR:CMD_BUSY") == 0) {
          zadnjiCmdOdgovorMega = CMD_ODGOVOR_BUSY;
        } else if (strcmp(linija, "ERR:CMD") == 0) {
          zadnjiCmdOdgovorMega = CMD_ODGOVOR_ERR;
        } else if (strcmp(linija, "ERR:SETUPWIFI") == 0) {
          odgovorSetupWiFiPrimljen = true;
          setupWiFiNeuspjeh = true;
        } else if (strncmp(linija, "SET:SUSTAV|", 11) == 0) {
          obradiSustavskePostavkeMegai(linija + 11);
        } else if (strncmp(linija, "SET:STAPICI|", 12) == 0) {
          obradiPostavkeStapicaMegai(linija + 12);
        } else if (strncmp(linija, "SET:BAT|", 8) == 0) {
          obradiBATPostavkeMegai(linija + 8);
        } else if (strncmp(linija, "SET:SUNCE|", 10) == 0) {
          obradiSuncevePostavkeMegai(linija + 10);
      } else if (strncmp(linija, "SET:MISE|", 9) == 0) {
        obradiMisePostavkeMegai(linija + 9);
      } else if (strncmp(linija, "SET:BLAGDANI_NEP|", 17) == 0) {
        obradiNepomicneBlagdaneMegai(linija + 17);
      } else if (strncmp(linija, "SET:BLAGDANI_POM|", 17) == 0) {
        obradiPomicneBlagdaneMegai(linija + 17);
      } else if (strncmp(linija, "STATUS:", 7) == 0) {
        obradiStatusMegai(linija + 7);
        } else if (strcmp(linija, "WIFISTATUS?") == 0) {
          prijaviPromjenuWiFiStatusa();
          if (wifiOmogucen && WiFi.status() == WL_CONNECTED) {
            Serial.print("WIFI:LOCAL_IP:");
            Serial.println(WiFi.localIP().toString());
            posaljiWiFiLcdSazetak();
            Serial.print("WIFI:MAC:");
            Serial.println(WiFi.macAddress());
          }
          Serial.println("ACK:WIFISTATUS");
        } else if (strcmp(linija, "SETUPAP:START") == 0) {
          Serial.println("WIFI: zahtjev za setup AP primljen preko Mega tipki");
          pokreniSetupPristupnuTocku();
        } else if (strncmp(linija, "WIFIEN:", 7) == 0) {
          char* payload = linija + 7;
          trimJednolinijskiBuffer(payload);

          if ((payload[0] == '0' || payload[0] == '1') && payload[1] == '\0') {
            primijeniWiFiOmogucenost(payload[0] == '1');
            Serial.println("ACK:WIFIEN");
          } else {
            Serial.println("ERR:WIFIEN");
          }
        } else if (strncmp(linija, "WIFI:", 5) == 0) {
          char* payload = linija + 5;
          bool uspjeh = false;

          char* context = nullptr;
          char* noviSsid = strtok_r(payload, "|", &context);
          char* novaLozinka = strtok_r(nullptr, "|", &context);
          char* dhcpZastavica = strtok_r(nullptr, "|", &context);
          char* novaIp = strtok_r(nullptr, "|", &context);
          char* novaMaska = strtok_r(nullptr, "|", &context);
          char* noviGateway = strtok_r(nullptr, "|", &context);
          char* visak = strtok_r(nullptr, "|", &context);

          if (noviSsid == nullptr || novaLozinka == nullptr || dhcpZastavica == nullptr ||
              novaIp == nullptr || novaMaska == nullptr || noviGateway == nullptr || visak != nullptr) {
            Serial.println("WIFI RX: nedostaje separator | u postavkama");
          } else {
            trimJednolinijskiBuffer(noviSsid);
            trimJednolinijskiBuffer(novaLozinka);
            trimJednolinijskiBuffer(dhcpZastavica);
            trimJednolinijskiBuffer(novaIp);
            trimJednolinijskiBuffer(novaMaska);
            trimJednolinijskiBuffer(noviGateway);

            if (noviSsid[0] != '\0' && novaLozinka[0] != '\0' && dhcpZastavica[0] != '\0') {
              const bool noviDhcp = strcmp(dhcpZastavica, "1") == 0;
              const bool konfiguracijaPromijenjena =
                  (strcmp(wifiSsid, noviSsid) != 0) ||
                  (strcmp(wifiLozinka, novaLozinka) != 0) ||
                  (koristiDhcp != noviDhcp) ||
                  (strcmp(statickaIp, novaIp) != 0) ||
                  (strcmp(mreznaMaska, novaMaska) != 0) ||
                  (strcmp(zadaniGateway, noviGateway) != 0);

              strncpy(wifiSsid, noviSsid, sizeof(wifiSsid) - 1);
              wifiSsid[sizeof(wifiSsid) - 1] = '\0';
              strncpy(wifiLozinka, novaLozinka, sizeof(wifiLozinka) - 1);
              wifiLozinka[sizeof(wifiLozinka) - 1] = '\0';
              koristiDhcp = noviDhcp;
              strncpy(statickaIp, novaIp, sizeof(statickaIp) - 1);
              statickaIp[sizeof(statickaIp) - 1] = '\0';
              strncpy(mreznaMaska, novaMaska, sizeof(mreznaMaska) - 1);
              mreznaMaska[sizeof(mreznaMaska) - 1] = '\0';
              strncpy(zadaniGateway, noviGateway, sizeof(zadaniGateway) - 1);
              zadaniGateway[sizeof(zadaniGateway) - 1] = '\0';
              primljenaWifiKonfiguracija = true;

              Serial.print("WIFI RX: primljen SSID ");
              Serial.print(wifiSsid);
              Serial.print(", DHCP=");
              Serial.println(koristiDhcp ? "DA" : "NE");

              if (konfiguracijaPromijenjena) {
                WiFi.disconnect();
                oznaciWiFiKaoOdspojen();
                wifiPokusajUToku = false;
                wifiPokusajPocetak = 0;
                wifiSljedeciPokusajDozvoljen = 0;
                wifiBrojPokusajaZaredom = 0;
                resetirajNtpStanje();
                if (wifiOmogucen) {
                  Serial.println("WIFI RX: konfiguracija promijenjena, pokrecem novo spajanje");
                  poveziNaWiFi();
                } else {
                  prijaviPromjenuWiFiStatusa();
                  Serial.println("WIFI RX: konfiguracija spremljena, WiFi je trenutno iskljucen");
                }
              } else {
                Serial.println("WIFI RX: konfiguracija je ista, bez novog spajanja");
                if (!wifiOmogucen) {
                  prijaviPromjenuWiFiStatusa();
                }
              }
              uspjeh = true;
            } else {
              Serial.println("WIFI RX: neispravna duljina SSID/lozinke/DHCP oznake");
            }
          }

          Serial.println(uspjeh ? "ACK:WIFI" : "ERR:WIFI");
        } else if (strcmp(linija, "NTPREQ:SYNC") == 0) {
            unsigned long sada = millis();
            osvjeziNTPSat();
            if (WiFi.status() != WL_CONNECTED) {
              Serial.println("NTPLOG: NTP zahtjev odbijen jer WiFi nije spojen");
              Serial.println("ERR:NTPREQ");
            } else if (!jeNtpVrijemeSvjezeZaMegu(sada)) {
              osvjeziNTPSat(true);
              sada = millis();
              if (!jeNtpVrijemeSvjezeZaMegu(sada)) {
                if (!jeNtpVrijemeStabilizirano()) {
                  Serial.println("NTPLOG: NTP zahtjev odbijen jer prvi uzorak jos nije potvrden drugim uzorkom");
                } else {
                  Serial.println("NTPLOG: NTP zahtjev odbijen jer nema dovoljno svjezeg NTP vremena");
                }
                Serial.println("ERR:NTPREQ");
              } else {
                posaljiNTPPremaMegai();
                Serial.println("NTPLOG: NTP zahtjev izvrsen nakon prisilnog osvjezavanja");
                Serial.println("ACK:NTPREQ");
              }
            } else {
              posaljiNTPPremaMegai();
              Serial.println("NTPLOG: NTP zahtjev izvrsen");
              Serial.println("ACK:NTPREQ");
            }
        } else if (strncmp(linija, "NTPCFG:", 7) == 0) {
          obradiNTPSerijskuNaredbu(linija + 7);
        }
      }
      prijemnaDuljina = 0;
      prijemniBuffer[0] = '\0';
    } else if (znak != '\r') {
      if (prijemnaDuljina < SERIJSKI_BUFFER_MAX) {
        prijemniBuffer[prijemnaDuljina++] = znak;
        prijemniBuffer[prijemnaDuljina] = '\0';
      } else {
        Serial.println("SERIJSKI RX: linija preduga, odbacujem");
        prijemnaDuljina = 0;
        prijemniBuffer[0] = '\0';
      }
    }
  }
}

