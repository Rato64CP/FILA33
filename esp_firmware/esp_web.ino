// esp_web.ino - web sucelje, JSON API i OTA nadogradnja ESP32 modula toranjskog sata

void ucitajWebAutentikaciju() {
  WebAuthConfig cfg{};
  EEPROM.get(ESP_EEPROM_ADRESA_WEB, cfg);

  if (cfg.potpis == WEB_AUTH_POTPIS && cfg.lozinka[0] != '\0') {
    cfg.lozinka[WEB_LOZINKA_MAX - 1] = '\0';
    strncpy(webLozinka, cfg.lozinka, sizeof(webLozinka) - 1);
    webLozinka[sizeof(webLozinka) - 1] = '\0';
    Serial.println("WEB AUTH: ucitana spremljena lozinka");
    return;
  }

  strncpy(webLozinka, "cista2906", sizeof(webLozinka) - 1);
  webLozinka[sizeof(webLozinka) - 1] = '\0';
  Serial.println("WEB AUTH: koristim zadanu lozinku");
}

void posaljiApiKomanduMegai(const char* naredba, const char* odgovor) {
  if (naredba == nullptr || odgovor == nullptr) {
    webPosluzitelj.send(500, "text/plain", "API konfiguracija nije valjana");
    return;
  }

  const CmdOdgovorMegai status = posaljiKomanduMegaiIPricekaj(naredba, CMD_CEKANJE_NA_MEGU_MS);
  if (status == CMD_ODGOVOR_OK) {
    webPosluzitelj.send(200, "text/plain", odgovor);
    return;
  }

  if (status == CMD_ODGOVOR_BUSY) {
    webPosluzitelj.send(409,
                        "text/plain",
                        "Naredba sada nije prihvacena. Pricekaj da se smire zvona i inercija pa pokusaj ponovno.");
    return;
  }

  if (status == CMD_ODGOVOR_ERR) {
    webPosluzitelj.send(502, "text/plain", "Mega je odbila naredbu");
    return;
  }

  webPosluzitelj.send(504, "text/plain", "Mega nije odgovorila na naredbu");
}

CmdOdgovorMegai posaljiKomanduMegaiIPricekaj(const char* naredba, unsigned long timeoutMs) {
  if (naredba == nullptr || naredba[0] == '\0') {
    return CMD_ODGOVOR_ERR;
  }

  zadnjiCmdOdgovorMega = CMD_ODGOVOR_CEKA;
  Serial.print("CMD:");
  Serial.println(naredba);

  const unsigned long pocetakMs = millis();
  while ((millis() - pocetakMs) < timeoutMs) {
    obradiSerijskiUlaz();
    if (zadnjiCmdOdgovorMega != CMD_ODGOVOR_CEKA) {
      return zadnjiCmdOdgovorMega;
    }
    delay(1);
    yield();
  }

  return CMD_ODGOVOR_TIMEOUT;
}

bool osigurajWebAutorizaciju() {
  if (webPosluzitelj.authenticate("admin", webLozinka)) {
    return true;
  }

  // Blaga odgoda nakon pogresne prijave usporava skripte za pogadanje
  // lozinke, ali ne remeti uobicajeni rad web dashboarda toranjskog sata.
  const unsigned long pocetakMs = millis();
  while ((millis() - pocetakMs) < WEB_AUTH_NEUSPJEH_ODGODA_MS) {
    delay(1);
    yield();
  }
  webPosluzitelj.requestAuthentication(BASIC_AUTH, "ZVONKO v. 1.0", "Unesite web lozinku");
  return false;
}

void posaljiJsonStatus(bool prisilno) {
  osvjeziStatusMegai(prisilno);

  char ipBuffer[16];
  snprintf(ipBuffer, sizeof(ipBuffer), "%s", WiFi.localIP().toString().c_str());

  char tijelo[440];
  snprintf_P(tijelo,
             sizeof(tijelo),
             PSTR("{\"wifi_ip\":\"%s\",\"wifi_connected\":%s,\"mega_status_known\":%s,\"bell1_active\":%s,\"bell2_active\":%s,\"slavljenje_active\":%s,\"mrtvacko_active\":%s,\"pokojnik_active\":%s,\"pokojnica_active\":%s,\"solar_morning_active\":%s,\"solar_noon_active\":%s,\"solar_evening_active\":%s,\"silent_mode_active\":%s}"),
             ipBuffer,
             (WiFi.status() == WL_CONNECTED) ? "true" : "false",
             megaStatusPoznat ? "true" : "false",
             megaZvono1Aktivno ? "true" : "false",
             megaZvono2Aktivno ? "true" : "false",
             megaSlavljenjeAktivno ? "true" : "false",
             megaMrtvackoAktivno ? "true" : "false",
             (megaPogrebnaSkriptaTip == 1) ? "true" : "false",
             (megaPogrebnaSkriptaTip == 2) ? "true" : "false",
             megaSunceJutroAktivno ? "true" : "false",
             megaSuncePodneAktivno ? "true" : "false",
             megaSunceVecerAktivno ? "true" : "false",
             megaTihiRezimAktivan ? "true" : "false");
  webPosluzitelj.send(200, "application/json", tijelo);
}

PostavkeOdgovorMegai posaljiSustavskePostavkeMegai(bool lcdPozadinskoOsvjetljenje,
                                                   bool logiranje,
                                                   bool rs485,
                                                   bool upsMod,
                                                   bool kocnicaZvona,
                                                   unsigned int inercijaZvona1Sekunde,
                                                   unsigned int inercijaZvona2Sekunde,
                                                   unsigned int impulsCekicaMs,
                                                   unsigned long timeoutMs) {
  zadnjiOdgovorSustavskihPostavkiMega = POSTAVKE_ODGOVOR_CEKA;

  char naredba[112];
  snprintf(naredba,
           sizeof(naredba),
           "SETCFG:SUSTAV|lcd=%d|log=%d|rs=%d|ups=%d|koc=%d|inr1=%u|inr2=%u|imp=%u",
           lcdPozadinskoOsvjetljenje ? 1 : 0,
           logiranje ? 1 : 0,
           rs485 ? 1 : 0,
           upsMod ? 1 : 0,
           kocnicaZvona ? 1 : 0,
           inercijaZvona1Sekunde,
           inercijaZvona2Sekunde,
           impulsCekicaMs);
  Serial.println(naredba);

  const unsigned long pocetakMs = millis();
  while ((millis() - pocetakMs) < timeoutMs) {
    obradiSerijskiUlaz();
    if (zadnjiOdgovorSustavskihPostavkiMega != POSTAVKE_ODGOVOR_CEKA) {
      return zadnjiOdgovorSustavskihPostavkiMega;
    }
    delay(1);
    yield();
  }

  return POSTAVKE_ODGOVOR_TIMEOUT;
}

PostavkeOdgovorMegai posaljiMisePostavkeMegai(bool dnevnaOmogucena,
                                              unsigned int dnevnaSat,
                                              unsigned int dnevnaMinuta,
                                              bool nedjeljnaOmogucena,
                                              unsigned int nedjeljnaSat,
                                              unsigned int nedjeljnaMinuta,
                                              unsigned long timeoutMs) {
  zadnjiOdgovorSustavskihPostavkiMega = POSTAVKE_ODGOVOR_CEKA;

  String naredba = "SETCFG:MISE|rd=";
  naredba += (dnevnaOmogucena ? "1" : "0");
  naredba += ",";
  naredba += String(dnevnaSat);
  naredba += ",";
  naredba += String(dnevnaMinuta);
  naredba += "|nd=";
  naredba += (nedjeljnaOmogucena ? "1" : "0");
  naredba += ",";
  naredba += String(nedjeljnaSat);
  naredba += ",";
  naredba += String(nedjeljnaMinuta);
  Serial.println(naredba);

  const unsigned long pocetakMs = millis();
  while ((millis() - pocetakMs) < timeoutMs) {
    obradiSerijskiUlaz();
    if (zadnjiOdgovorSustavskihPostavkiMega != POSTAVKE_ODGOVOR_CEKA) {
      return zadnjiOdgovorSustavskihPostavkiMega;
    }
    delay(1);
    yield();
  }

  return POSTAVKE_ODGOVOR_TIMEOUT;
}

PostavkeOdgovorMegai posaljiNepomicneBlagdaneMegai(const MegaNepomicniBlagdan* postavke,
                                                   uint8_t brojPostavki,
                                                   unsigned long timeoutMs) {
  zadnjiOdgovorSustavskihPostavkiMega = POSTAVKE_ODGOVOR_CEKA;

  String naredba = "SETCFG:BLAGDANI_NEP";
  for (uint8_t i = 0; i < brojPostavki; ++i) {
    naredba += "|f";
    naredba += String(i);
    naredba += "=";
    naredba += (postavke[i].omogucen ? "1" : "0");
    naredba += ",";
    naredba += String(postavke[i].satMise);
    naredba += ",";
    naredba += String(postavke[i].minutaMise);
  }
  Serial.println(naredba);

  const unsigned long pocetakMs = millis();
  while ((millis() - pocetakMs) < timeoutMs) {
    obradiSerijskiUlaz();
    if (zadnjiOdgovorSustavskihPostavkiMega != POSTAVKE_ODGOVOR_CEKA) {
      return zadnjiOdgovorSustavskihPostavkiMega;
    }
    delay(1);
    yield();
  }

  return POSTAVKE_ODGOVOR_TIMEOUT;
}

PostavkeOdgovorMegai posaljiPomicneBlagdaneMegai(const MegaPomicniBlagdan* postavke,
                                                 uint8_t brojPostavki,
                                                 unsigned long timeoutMs) {
  zadnjiOdgovorSustavskihPostavkiMega = POSTAVKE_ODGOVOR_CEKA;

  String naredba = "SETCFG:BLAGDANI_POM";
  for (uint8_t i = 0; i < brojPostavki; ++i) {
    naredba += "|p";
    naredba += String(i);
    naredba += "=";
    naredba += (postavke[i].omogucen ? "1" : "0");
    naredba += ",";
    naredba += String(postavke[i].satMise);
    naredba += ",";
    naredba += String(postavke[i].minutaMise);
  }
  Serial.println(naredba);

  const unsigned long pocetakMs = millis();
  while ((millis() - pocetakMs) < timeoutMs) {
    obradiSerijskiUlaz();
    if (zadnjiOdgovorSustavskihPostavkiMega != POSTAVKE_ODGOVOR_CEKA) {
      return zadnjiOdgovorSustavskihPostavkiMega;
    }
    delay(1);
    yield();
  }

  return POSTAVKE_ODGOVOR_TIMEOUT;
}

PostavkeOdgovorMegai posaljiPostavkeStapicaMegai(unsigned int trajanjeRadniMin,
                                                 unsigned int trajanjeNedjeljaMin,
                                                 unsigned int trajanjeSlavljenjaMin,
                                                 unsigned int odgodaSlavljenjaSekunde,
                                                 unsigned long timeoutMs) {
  zadnjiOdgovorSustavskihPostavkiMega = POSTAVKE_ODGOVOR_CEKA;

  char naredba[80];
  snprintf(naredba,
           sizeof(naredba),
           "SETCFG:STAPICI|tr=%u|tn=%u|ts=%u|odg=%u",
           trajanjeRadniMin,
           trajanjeNedjeljaMin,
           trajanjeSlavljenjaMin,
           odgodaSlavljenjaSekunde);
  Serial.println(naredba);

  const unsigned long pocetakMs = millis();
  while ((millis() - pocetakMs) < timeoutMs) {
    obradiSerijskiUlaz();
    if (zadnjiOdgovorSustavskihPostavkiMega != POSTAVKE_ODGOVOR_CEKA) {
      return zadnjiOdgovorSustavskihPostavkiMega;
    }
    delay(1);
    yield();
  }

  return POSTAVKE_ODGOVOR_TIMEOUT;
}

PostavkeOdgovorMegai posaljiBATPostavkeMegai(unsigned int satOd,
                                             unsigned int satDo,
                                             unsigned int modOtkucavanja,
                                             unsigned int modSlavljenja,
                                             unsigned int modMrtvackog,
                                             unsigned long timeoutMs) {
  zadnjiOdgovorSustavskihPostavkiMega = POSTAVKE_ODGOVOR_CEKA;

  char naredba[80];
  snprintf(naredba,
           sizeof(naredba),
           "SETCFG:BAT|od=%u|do=%u|otk=%u|sl=%u|mr=%u",
           satOd,
           satDo,
           modOtkucavanja,
           modSlavljenja,
           modMrtvackog);
  Serial.println(naredba);

  const unsigned long pocetakMs = millis();
  while ((millis() - pocetakMs) < timeoutMs) {
    obradiSerijskiUlaz();
    if (zadnjiOdgovorSustavskihPostavkiMega != POSTAVKE_ODGOVOR_CEKA) {
      return zadnjiOdgovorSustavskihPostavkiMega;
    }
    delay(1);
    yield();
  }

  return POSTAVKE_ODGOVOR_TIMEOUT;
}

PostavkeOdgovorMegai posaljiSuncevePostavkeMegai(bool jutroOmoguceno,
                                                 unsigned int jutroZvono,
                                                 int jutroOdgodaMin,
                                                 bool podneOmoguceno,
                                                 unsigned int podneZvono,
                                                 bool vecerOmoguceno,
                                                 unsigned int vecerZvono,
                                                 int vecerOdgodaMin,
                                                 bool nocnaRasvjeta,
                                                 unsigned long timeoutMs) {
  zadnjiOdgovorSustavskihPostavkiMega = POSTAVKE_ODGOVOR_CEKA;

  char naredba[128];
  snprintf(naredba,
           sizeof(naredba),
           "SETCFG:SUNCE|ju=%d|jb=%u|jo=%d|pu=%d|pb=%u|vu=%d|vb=%u|vo=%d|nr=%d",
           jutroOmoguceno ? 1 : 0,
           jutroZvono,
           jutroOdgodaMin,
           podneOmoguceno ? 1 : 0,
           podneZvono,
           vecerOmoguceno ? 1 : 0,
           vecerZvono,
           vecerOdgodaMin,
           nocnaRasvjeta ? 1 : 0);
  Serial.println(naredba);

  const unsigned long pocetakMs = millis();
  while ((millis() - pocetakMs) < timeoutMs) {
    obradiSerijskiUlaz();
    if (zadnjiOdgovorSustavskihPostavkiMega != POSTAVKE_ODGOVOR_CEKA) {
      return zadnjiOdgovorSustavskihPostavkiMega;
    }
    delay(1);
    yield();
  }

  return POSTAVKE_ODGOVOR_TIMEOUT;
}

void posaljiJsonSustavskihPostavki(bool prisilno) {
  osvjeziSustavskePostavkeMegai(prisilno);

  char tijelo[320];
  snprintf_P(tijelo,
             sizeof(tijelo),
             PSTR("{\"known\":%s,\"lcd_backlight\":%s,\"pc_logging\":%s,\"rs485_enabled\":%s,\"ups_mode\":%s,\"bell_brake\":%s,\"inertia1_seconds\":%u,\"inertia2_seconds\":%u,\"hammer_pulse_ms\":%u}"),
             megaSustavskePostavke.poznate ? "true" : "false",
             (megaSustavskePostavke.poznate && megaSustavskePostavke.lcdPozadinskoOsvjetljenje) ? "true" : "false",
             (megaSustavskePostavke.poznate && megaSustavskePostavke.logiranje) ? "true" : "false",
             (megaSustavskePostavke.poznate && megaSustavskePostavke.rs485) ? "true" : "false",
             (megaSustavskePostavke.poznate && megaSustavskePostavke.upsMod) ? "true" : "false",
             (megaSustavskePostavke.poznate && megaSustavskePostavke.kocnicaZvona) ? "true" : "false",
             static_cast<unsigned>(megaSustavskePostavke.poznate ? megaSustavskePostavke.inercijaZvona1Sekunde : 0U),
             static_cast<unsigned>(megaSustavskePostavke.poznate ? megaSustavskePostavke.inercijaZvona2Sekunde : 0U),
             static_cast<unsigned>(megaSustavskePostavke.poznate ? megaSustavskePostavke.impulsCekicaMs : 0U));
  webPosluzitelj.send(200, "application/json", tijelo);
}

void posaljiJsonPostavkiStapica(bool prisilno) {
  osvjeziPostavkeStapicaMegai(prisilno);

  char tijelo[240];
  snprintf_P(tijelo,
             sizeof(tijelo),
             PSTR("{\"known\":%s,\"radni_minutes\":%u,\"nedjelja_minutes\":%u,\"slavljenje_minutes\":%u,\"slavljenje_delay_seconds\":%u}"),
             megaPostavkeStapica.poznate ? "true" : "false",
             static_cast<unsigned>(megaPostavkeStapica.poznate ? megaPostavkeStapica.trajanjeRadniMin : 0U),
             static_cast<unsigned>(megaPostavkeStapica.poznate ? megaPostavkeStapica.trajanjeNedjeljaMin : 0U),
             static_cast<unsigned>(megaPostavkeStapica.poznate ? megaPostavkeStapica.trajanjeSlavljenjaMin : 0U),
             static_cast<unsigned>(megaPostavkeStapica.poznate ? megaPostavkeStapica.odgodaSlavljenjaSekunde : 0U));
  webPosluzitelj.send(200, "application/json", tijelo);
}

void posaljiJsonBATPostavki(bool prisilno) {
  osvjeziBATPostavkeMegai(prisilno);

  char tijelo[240];
  snprintf_P(tijelo,
             sizeof(tijelo),
             PSTR("{\"known\":%s,\"sat_od\":%u,\"sat_do\":%u,\"otkucavanje_mode\":%u,\"slavljenje_mode\":%u,\"mrtvacko_mode\":%u}"),
             megaBATPostavke.poznate ? "true" : "false",
             static_cast<unsigned>(megaBATPostavke.poznate ? megaBATPostavke.satOd : 0U),
             static_cast<unsigned>(megaBATPostavke.poznate ? megaBATPostavke.satDo : 0U),
             static_cast<unsigned>(megaBATPostavke.poznate ? megaBATPostavke.modOtkucavanja : 0U),
             static_cast<unsigned>(megaBATPostavke.poznate ? megaBATPostavke.modSlavljenja : 0U),
             static_cast<unsigned>(megaBATPostavke.poznate ? megaBATPostavke.modMrtvackog : 0U));
  webPosluzitelj.send(200, "application/json", tijelo);
}

void posaljiJsonSuncevihPostavki(bool prisilno) {
  osvjeziSuncevePostavkeMegai(prisilno);

  char tijelo[400];
  snprintf_P(tijelo,
             sizeof(tijelo),
             PSTR("{\"known\":%s,\"jutro_enabled\":%s,\"jutro_bell\":%u,\"jutro_offset_minutes\":%d,\"podne_enabled\":%s,\"podne_bell\":%u,\"vecer_enabled\":%s,\"vecer_bell\":%u,\"vecer_offset_minutes\":%d,\"night_light\":%s}"),
             megaSuncevePostavke.poznate ? "true" : "false",
             (megaSuncevePostavke.poznate && megaSuncevePostavke.jutroOmoguceno) ? "true" : "false",
             static_cast<unsigned>(megaSuncevePostavke.poznate ? megaSuncevePostavke.jutroZvono : 0U),
             static_cast<int>(megaSuncevePostavke.poznate ? megaSuncevePostavke.jutroOdgodaMin : 0),
             (megaSuncevePostavke.poznate && megaSuncevePostavke.podneOmoguceno) ? "true" : "false",
             static_cast<unsigned>(megaSuncevePostavke.poznate ? megaSuncevePostavke.podneZvono : 0U),
             (megaSuncevePostavke.poznate && megaSuncevePostavke.vecerOmoguceno) ? "true" : "false",
             static_cast<unsigned>(megaSuncevePostavke.poznate ? megaSuncevePostavke.vecerZvono : 0U),
             static_cast<int>(megaSuncevePostavke.poznate ? megaSuncevePostavke.vecerOdgodaMin : 0),
             (megaSuncevePostavke.poznate && megaSuncevePostavke.nocnaRasvjeta) ? "true" : "false");
  webPosluzitelj.send(200, "application/json", tijelo);
}

void posaljiJsonBlagdanskihPostavki(bool prisilno) {
  osvjeziMisePostavkeMegai(prisilno);
  osvjeziNepomicneBlagdaneMegai(prisilno);
  osvjeziPomicneBlagdaneMegai(prisilno);

  String tijelo = "{\"known\":";
  tijelo += suSveBlagdanskeSkupinePoznate() ? "true" : "false";
  tijelo += ",\"daily\":{\"enabled\":";
  tijelo += (megaBlagdanskePostavke.poznateMise && megaBlagdanskePostavke.dnevnaMisaOmogucena) ? "true" : "false";
  tijelo += ",\"hour\":";
  tijelo += String(megaBlagdanskePostavke.poznateMise ? megaBlagdanskePostavke.dnevnaMisaSat : 0);
  tijelo += ",\"minute\":";
  tijelo += String(megaBlagdanskePostavke.poznateMise ? megaBlagdanskePostavke.dnevnaMisaMinuta : 0);
  tijelo += "},\"sunday\":{\"enabled\":";
  tijelo += (megaBlagdanskePostavke.poznateMise && megaBlagdanskePostavke.nedjeljnaMisaOmogucena) ? "true" : "false";
  tijelo += ",\"hour\":";
  tijelo += String(megaBlagdanskePostavke.poznateMise ? megaBlagdanskePostavke.nedjeljnaMisaSat : 0);
  tijelo += ",\"minute\":";
  tijelo += String(megaBlagdanskePostavke.poznateMise ? megaBlagdanskePostavke.nedjeljnaMisaMinuta : 0);
  tijelo += "}";
  tijelo += ",\"fixed\":[";
  for (uint8_t i = 0; i < BROJ_NEPOMICNIH_BLAGDANA; ++i) {
    if (i > 0) {
      tijelo += ",";
    }
    tijelo += "{\"enabled\":";
    tijelo += (megaBlagdanskePostavke.poznatiNepomicni && megaBlagdanskePostavke.nepomicni[i].omogucen) ? "true" : "false";
    tijelo += ",\"hour\":";
    tijelo += String(megaBlagdanskePostavke.poznatiNepomicni ? megaBlagdanskePostavke.nepomicni[i].satMise : 0);
    tijelo += ",\"minute\":";
    tijelo += String(megaBlagdanskePostavke.poznatiNepomicni ? megaBlagdanskePostavke.nepomicni[i].minutaMise : 0);
    tijelo += "}";
  }
  tijelo += "],\"movable\":[";
  for (uint8_t i = 0; i < BROJ_POMICNIH_BLAGDANA; ++i) {
    if (i > 0) {
      tijelo += ",";
    }
    tijelo += "{\"enabled\":";
    tijelo += (megaBlagdanskePostavke.poznatiPomicni && megaBlagdanskePostavke.pomicni[i].omogucen) ? "true" : "false";
    tijelo += ",\"hour\":";
    tijelo += String(megaBlagdanskePostavke.poznatiPomicni ? megaBlagdanskePostavke.pomicni[i].satMise : 0);
    tijelo += ",\"minute\":";
    tijelo += String(megaBlagdanskePostavke.poznatiPomicni ? megaBlagdanskePostavke.pomicni[i].minutaMise : 0);
    tijelo += "}";
  }
  tijelo += "]}";

  webPosluzitelj.send(200, "application/json", tijelo);
}

void posaljiHtmlStranicuIzProgMema(PGM_P stranica) {
  if (stranica == nullptr) {
    webPosluzitelj.send(500, "text/plain", "HTML stranica nije dostupna");
    return;
  }

  static const size_t HTML_CHUNK_VELICINA = 384;
  char meduspremnik[HTML_CHUNK_VELICINA + 1];
  const size_t duljina = strlen_P(stranica);

  webPosluzitelj.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
  webPosluzitelj.sendHeader("Pragma", "no-cache");
  webPosluzitelj.sendHeader("Connection", "close");
  webPosluzitelj.setContentLength(duljina);
  webPosluzitelj.send(200, "text/html; charset=utf-8", "");

  size_t pomak = 0;
  while (pomak < duljina) {
    const size_t preostalo = duljina - pomak;
    const size_t trenutniKomad =
        (preostalo > HTML_CHUNK_VELICINA) ? HTML_CHUNK_VELICINA : preostalo;
    memcpy_P(meduspremnik, stranica + pomak, trenutniKomad);
    meduspremnik[trenutniKomad] = '\0';
    webPosluzitelj.sendContent(meduspremnik);
    pomak += trenutniKomad;
    yield();
  }

  webPosluzitelj.sendContent("");
}

static const char WEB_POCETNA_STRANICA[] PROGMEM = R"HTML(
<!doctype html>
<html lang="hr">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ZVONKO v. 1.0</title>
  <style>
    :root {
      color-scheme: light;
      --bg:#e7edf5;
      --panel:#f8fbff;
      --line:#b8c4d3;
      --text:#223246;
      --muted:#617286;
      --blue:#dcecff;
      --blue-line:#4b84c8;
      --blue-strong:#b9d5fb;
      --blue-strong-line:#2f67b1;
      --gray:#edf1f6;
      --gray-line:#95a3b3;
      --danger:#b45167;
      --danger-dark:#983f54;
      --shadow:0 10px 24px rgba(63, 82, 110, 0.12);
    }
    * { box-sizing:border-box; }
    body {
      margin:0;
      font-family: Arial, sans-serif;
      background:linear-gradient(180deg, #dfe7f1, #f4f7fb);
      color:var(--text);
    }
    .wrap {
      max-width:820px;
      margin:0 auto;
      padding:16px 12px 24px;
    }
    .title {
      margin:0 0 14px;
      text-align:center;
      font-size:28px;
      letter-spacing:0.02em;
    }
    .grid {
      display:grid;
      grid-template-columns:1fr 1fr;
      gap:14px;
      margin-bottom:18px;
    }
    .card,
    .section,
    .log-panel {
      background:var(--panel);
      border:1px solid var(--line);
      border-radius:18px;
      box-shadow:var(--shadow);
    }
    .card {
      padding:16px;
      text-align:center;
    }
    .section h3 {
      margin:0 0 12px;
      font-size:24px;
    }
    .toggle-btn {
      width:100%;
      border-radius:16px;
      border:2px solid var(--gray-line);
      background:var(--gray);
      color:#47596f;
      min-height:92px;
      font-size:24px;
      font-weight:700;
      letter-spacing:0.05em;
      cursor:pointer;
    }
    .toggle-btn.active {
      background:var(--blue);
      border-color:var(--blue-line);
      color:#1b4d8f;
    }
    .toggle-btn.primary {
      min-height:108px;
      font-size:28px;
    }
    .toggle-btn.primary.active {
      background:var(--blue-strong);
      border-color:var(--blue-strong-line);
      color:#123f79;
      box-shadow:inset 0 0 0 1px rgba(255,255,255,0.45);
    }
    .toggle-btn.secondary {
      min-height:68px;
      font-size:20px;
      letter-spacing:0.03em;
    }
    .toggle-btn.quiet-mode {
      min-height:74px;
      font-size:21px;
      letter-spacing:0.03em;
    }
    .toggle-btn.quiet-mode.active {
      background:#f1b9c2;
      border-color:var(--danger-dark);
      color:#7e2038;
      box-shadow:inset 0 0 0 1px rgba(255,255,255,0.4);
    }
    .section {
      padding:16px;
      margin-bottom:14px;
    }
    .section-note {
      margin:0 0 12px;
      font-size:13px;
      line-height:1.45;
      color:var(--muted);
    }
    .secondary-grid {
      display:grid;
      grid-template-columns:repeat(3, 1fr);
      gap:12px;
    }
    .script-grid {
      display:grid;
      grid-template-columns:1fr 1fr;
      gap:12px;
    }
    .mini-card {
      padding:0;
    }
    .script-btn {
      min-height:76px;
      font-size:22px;
      letter-spacing:0.03em;
      background:#f7ebe7;
      border-color:#b88777;
      color:#6f3024;
    }
    .script-btn.active {
      background:#f1b9c2;
      border-color:var(--danger-dark);
      color:#7e2038;
      box-shadow:inset 0 0 0 1px rgba(255,255,255,0.4);
    }
    .quiet-mode-wrap {
      margin-top:14px;
    }
    .log-panel {
      padding:14px 16px;
    }
    .service-links {
      margin-top:14px;
      display:flex;
      justify-content:center;
      gap:10px;
      flex-wrap:wrap;
    }
    .service-link {
      display:inline-block;
      padding:10px 14px;
      border-radius:12px;
      border:1px solid var(--line);
      background:#f2f6fb;
      color:var(--text);
      text-decoration:none;
      font-size:14px;
      font-weight:700;
      letter-spacing:0.03em;
    }
    .log-text {
      min-height:24px;
      white-space:pre-wrap;
      line-height:1.45;
      font-size:14px;
    }
    @media (max-width:700px) {
      .grid,
      .secondary-grid {
        grid-template-columns:1fr;
      }
    }
  </style>
</head>
<body>
  <div class="wrap">
    <h1 class="title">ZVONKO</h1>

    <section class="grid">
      <article class="card">
        <button id="toggleBell1" class="toggle-btn primary inactive" onclick="prebaciGlavnuKomandu('bell1')">MU&Scaron;KO</button>
      </article>

      <article class="card">
        <button id="toggleBell2" class="toggle-btn primary inactive" onclick="prebaciGlavnuKomandu('bell2')">&Zcaron;ENSKO</button>
      </article>

      <article class="card">
        <button id="toggleSlavljenje" class="toggle-btn primary inactive" onclick="prebaciGlavnuKomandu('slavljenje')">SLAVI</button>
      </article>

      <article class="card">
        <button id="toggleMrtvacko" class="toggle-btn primary inactive" onclick="prebaciGlavnuKomandu('mrtvacko')">BRECA</button>
      </article>
    </section>

    <section class="section">
      <h3>Pogrebne skripte</h3>
      <p class="section-note">POKOJNIK pokrece musko zvono 2 minute, zatim nakon inercije mrtvacko 10 minuta. POKOJNICA radi isto sa zenskim zvonom.</p>
        <div class="script-grid">
        <article class="mini-card">
          <button id="togglePokojnik" class="toggle-btn script-btn inactive" onclick="pokreniPogrebnuSkriptu('pokojnik', '/api/pokojnik', 'POKOJNIK')">POKOJNIK</button>
        </article>
        <article class="mini-card">
          <button id="togglePokojnica" class="toggle-btn script-btn inactive" onclick="pokreniPogrebnuSkriptu('pokojnica', '/api/pokojnica', 'POKOJNICA')">POKOJNICA</button>
        </article>
      </div>
    </section>

    <section class="section">
      <h3>Dodatne opcije</h3>
      <p class="section-note">Ako su gumbi ukljuceni, zdravomarije ce zvoniti prema suncanom rasporedu, a podne u 12.00 sati.</p>
      <div class="secondary-grid">
        <article class="mini-card">
          <button id="toggleSunceJutro" class="toggle-btn secondary inactive" onclick="prebaciSuncevuKomandu('jutro')">JUTRO</button>
        </article>
        <article class="mini-card">
          <button id="toggleSuncePodne" class="toggle-btn secondary inactive" onclick="prebaciSuncevuKomandu('podne')">PODNE</button>
        </article>
        <article class="mini-card">
          <button id="toggleSunceVecer" class="toggle-btn secondary inactive" onclick="prebaciSuncevuKomandu('vecer')">VECER</button>
        </article>
      </div>
      <div class="quiet-mode-wrap">
        <button id="toggleTihiMod" class="toggle-btn quiet-mode inactive" onclick="prebaciTihiMod()">TIHI MOD</button>
      </div>
    </section>

    <section class="log-panel">
      <div id="odgovor" class="log-text">Dashboard je spreman.</div>
      <div class="service-links">
        <a class="service-link" href="/settings">POSTAVKE</a>
        <a class="service-link" href="/blagdani">BLAGDANI</a>
      </div>
    </section>
  </div>
  <script>
    const glavneKomande = {
      bell1: {
        tipkaId: 'toggleBell1',
        apiOn: '/api/bell1/on',
        apiOff: '/api/bell1/off',
        naziv: 'Musko zvono',
        oznakaTipke: 'MU\u0160KO'
      },
      bell2: {
        tipkaId: 'toggleBell2',
        apiOn: '/api/bell2/on',
        apiOff: '/api/bell2/off',
        naziv: 'Zensko zvono',
        oznakaTipke: '\u017dENSKO'
      },
      slavljenje: {
        tipkaId: 'toggleSlavljenje',
        apiOn: '/api/slavljenje/on',
        apiOff: '/api/slavljenje/off',
        naziv: 'Slavljenje',
        oznakaTipke: 'SLAVI'
      },
      mrtvacko: {
        tipkaId: 'toggleMrtvacko',
        apiOn: '/api/mrtvacko/on',
        apiOff: '/api/mrtvacko/off',
        naziv: 'Mrtvacko',
        oznakaTipke: 'BRECA'
      }
    };

    const sunceveKomande = {
      jutro: {
        tipkaId: 'toggleSunceJutro',
        apiOn: '/api/solar/morning/on',
        apiOff: '/api/solar/morning/off',
        naziv: 'Sunce jutro',
        oznakaTipke: 'JUTRO'
      },
      podne: {
        tipkaId: 'toggleSuncePodne',
        apiOn: '/api/solar/noon/on',
        apiOff: '/api/solar/noon/off',
        naziv: 'Sunce podne',
        oznakaTipke: 'PODNE'
      },
      vecer: {
        tipkaId: 'toggleSunceVecer',
        apiOn: '/api/solar/evening/on',
        apiOff: '/api/solar/evening/off',
        naziv: 'Sunce vecer',
        oznakaTipke: 'VECER'
      }
    };

    let glavnoStanje = {
      bell1: null,
      bell2: null,
      slavljenje: null,
      mrtvacko: null
    };

    let suncevoStanje = {
      jutro: null,
      podne: null,
      vecer: null
    };

    let pogrebneSkripte = {
      pokojnik: null,
      pokojnica: null
    };

    let tihiModAktivan = null;

    function postaviLog(poruka) {
      document.getElementById('odgovor').textContent = poruka;
    }

    function postaviTipkuStanja(kljuc, aktivno, statusPoznat) {
      const meta = glavneKomande[kljuc] || sunceveKomande[kljuc];
      const tipka = document.getElementById(meta.tipkaId);
      if (!tipka) {
        return;
      }

      tipka.textContent = meta.oznakaTipke;

      if (!statusPoznat || aktivno === null) {
        tipka.classList.remove('active');
        tipka.classList.add('inactive');
        tipka.setAttribute('aria-pressed', 'false');
        return;
      }

      tipka.classList.toggle('active', aktivno);
      tipka.classList.toggle('inactive', !aktivno);
      tipka.setAttribute('aria-pressed', aktivno ? 'true' : 'false');
    }

    function postaviTipkuPogrebneSkripte(tipkaId, aktivno, statusPoznat) {
      const tipka = document.getElementById(tipkaId);
      if (!tipka) {
        return;
      }

      if (!statusPoznat || aktivno === null) {
        tipka.classList.remove('active');
        tipka.classList.add('inactive');
        tipka.setAttribute('aria-pressed', 'false');
        return;
      }

      tipka.classList.toggle('active', aktivno);
      tipka.classList.toggle('inactive', !aktivno);
      tipka.setAttribute('aria-pressed', aktivno ? 'true' : 'false');
    }

    async function pozoviApi(putanja, oznaka) {
      postaviLog('Saljem naredbu: ' + oznaka + '...');
      try {
        const odgovor = await fetch(putanja, {
          method: 'GET',
          cache: 'no-store'
        });
        const tekst = await odgovor.text();
        if (odgovor.ok) {
          postaviLog(oznaka + ': ' + tekst);
        } else {
          postaviLog(oznaka + ': GRESKA ' + odgovor.status + ' - ' + tekst);
        }
      } catch (greska) {
        postaviLog(oznaka + ': mreza ili autentikacija nisu uspjeli');
      }
    }

    async function prebaciGlavnuKomandu(kljuc) {
      const meta = glavneKomande[kljuc];
      if (glavnoStanje[kljuc] === null) {
        await osvjeziStatus(true);
      }
      if (glavnoStanje[kljuc] === null) {
        postaviLog(meta.naziv + ': stanje nije dostupno, pokusaj ponovno za trenutak.');
        return;
      }

      const trenutnoAktivno = glavnoStanje[kljuc] === true;
      const ukljucujeSe = !trenutnoAktivno;
      const putanja = ukljucujeSe ? meta.apiOn : meta.apiOff;
      const oznaka = meta.naziv + (ukljucujeSe ? ' ukljuci' : ' iskljuci');

      await pozoviApi(putanja, oznaka);
      await osvjeziStatus(true);
    }

    async function prebaciSuncevuKomandu(kljuc) {
      const meta = sunceveKomande[kljuc];
      if (suncevoStanje[kljuc] === null) {
        await osvjeziStatus(true);
      }
      if (suncevoStanje[kljuc] === null) {
        postaviLog(meta.naziv + ': stanje nije dostupno, pokusaj ponovno za trenutak.');
        return;
      }

      const trenutnoAktivno = suncevoStanje[kljuc] === true;
      const ukljucujeSe = !trenutnoAktivno;
      const putanja = ukljucujeSe ? meta.apiOn : meta.apiOff;
      const oznaka = meta.naziv + (ukljucujeSe ? ' ukljuci' : ' iskljuci');

      await pozoviApi(putanja, oznaka);
      await osvjeziStatus(true);
    }

    async function prebaciTihiMod() {
      if (tihiModAktivan === null) {
        await osvjeziStatus(true);
      }
      if (tihiModAktivan === null) {
        postaviLog('Tihi mod: stanje nije dostupno, pokusaj ponovno za trenutak.');
        return;
      }

      const ukljucujeSe = !tihiModAktivan;
      const putanja = ukljucujeSe ? '/api/quiet/on' : '/api/quiet/off';
      const oznaka = ukljucujeSe ? 'Tihi mod ukljuci' : 'Tihi mod iskljuci';

      await pozoviApi(putanja, oznaka);
      await osvjeziStatus(true);
    }

    async function pokreniPogrebnuSkriptu(kljuc, putanja, oznaka) {
      const drugiKljuc = kljuc === 'pokojnik' ? 'pokojnica' : 'pokojnik';
      if (pogrebneSkripte[kljuc] === null || pogrebneSkripte[drugiKljuc] === null) {
        await osvjeziStatus(true);
      }

      if (pogrebneSkripte[drugiKljuc] === true) {
        postaviLog(oznaka + ': druga pogrebna sekvenca je vec aktivna, najprije je zaustavi.');
        return;
      }

      const oznakaAkcije = pogrebneSkripte[kljuc] === true ? ' zaustavi' : ' pokreni';
      await pozoviApi(putanja, oznaka + oznakaAkcije);
      await osvjeziStatus(true);
    }

    async function osvjeziStatus(prisilno = false) {
      const timeoutMs = prisilno ? 1500 : 700;
      const imaAbortKontroler = typeof AbortController === 'function';
      const kontroler = imaAbortKontroler ? new AbortController() : null;
      const timeoutId = imaAbortKontroler
        ? setTimeout(() => kontroler.abort(), timeoutMs)
        : 0;
      try {
        const putanjaStatusa = prisilno ? '/api/status?force=1' : '/api/status';
        const odgovor = await fetch(putanjaStatusa, {
          cache: 'no-store',
          signal: kontroler ? kontroler.signal : undefined
        });
        if (timeoutId) {
          clearTimeout(timeoutId);
        }
        if (!odgovor.ok) {
          throw new Error('status');
        }
        const podaci = await odgovor.json();

        glavnoStanje.bell1 = podaci.mega_status_known ? !!podaci.bell1_active : null;
        glavnoStanje.bell2 = podaci.mega_status_known ? !!podaci.bell2_active : null;
        glavnoStanje.slavljenje = podaci.mega_status_known ? !!podaci.slavljenje_active : null;
        glavnoStanje.mrtvacko = podaci.mega_status_known ? !!podaci.mrtvacko_active : null;
        pogrebneSkripte.pokojnik = podaci.mega_status_known ? !!podaci.pokojnik_active : null;
        pogrebneSkripte.pokojnica = podaci.mega_status_known ? !!podaci.pokojnica_active : null;
        suncevoStanje.jutro = podaci.mega_status_known ? !!podaci.solar_morning_active : null;
        suncevoStanje.podne = podaci.mega_status_known ? !!podaci.solar_noon_active : null;
        suncevoStanje.vecer = podaci.mega_status_known ? !!podaci.solar_evening_active : null;
        tihiModAktivan = podaci.mega_status_known ? !!podaci.silent_mode_active : null;

        postaviTipkuStanja('bell1', glavnoStanje.bell1, !!podaci.mega_status_known);
        postaviTipkuStanja('bell2', glavnoStanje.bell2, !!podaci.mega_status_known);
        postaviTipkuStanja('slavljenje', glavnoStanje.slavljenje, !!podaci.mega_status_known);
        postaviTipkuStanja('mrtvacko', glavnoStanje.mrtvacko, !!podaci.mega_status_known);
        postaviTipkuPogrebneSkripte('togglePokojnik', pogrebneSkripte.pokojnik, !!podaci.mega_status_known);
        postaviTipkuPogrebneSkripte('togglePokojnica', pogrebneSkripte.pokojnica, !!podaci.mega_status_known);
        postaviTipkuStanja('jutro', suncevoStanje.jutro, !!podaci.mega_status_known);
        postaviTipkuStanja('podne', suncevoStanje.podne, !!podaci.mega_status_known);
        postaviTipkuStanja('vecer', suncevoStanje.vecer, !!podaci.mega_status_known);
        postaviTipkuTihogModa(tihiModAktivan, !!podaci.mega_status_known);
      } catch (greska) {
        if (timeoutId) {
          clearTimeout(timeoutId);
        }
        glavnoStanje.bell1 = null;
        glavnoStanje.bell2 = null;
        glavnoStanje.slavljenje = null;
        glavnoStanje.mrtvacko = null;
        pogrebneSkripte.pokojnik = null;
        pogrebneSkripte.pokojnica = null;
        suncevoStanje.jutro = null;
        suncevoStanje.podne = null;
        suncevoStanje.vecer = null;
        tihiModAktivan = null;

        postaviTipkuStanja('bell1', null, false);
        postaviTipkuStanja('bell2', null, false);
        postaviTipkuStanja('slavljenje', null, false);
        postaviTipkuStanja('mrtvacko', null, false);
        postaviTipkuPogrebneSkripte('togglePokojnik', null, false);
        postaviTipkuPogrebneSkripte('togglePokojnica', null, false);
        postaviTipkuStanja('jutro', null, false);
        postaviTipkuStanja('podne', null, false);
        postaviTipkuStanja('vecer', null, false);
        postaviTipkuTihogModa(null, false);
      }
    }

    function postaviTipkuTihogModa(aktivno, statusPoznat) {
      const tipka = document.getElementById('toggleTihiMod');
      if (!tipka) {
        return;
      }

      tipka.textContent = 'TIHI MOD';

      if (!statusPoznat || aktivno === null) {
        tipka.classList.remove('active');
        tipka.classList.add('inactive');
        tipka.setAttribute('aria-pressed', 'false');
        return;
      }

      tipka.classList.toggle('active', aktivno);
      tipka.classList.toggle('inactive', !aktivno);
      tipka.setAttribute('aria-pressed', aktivno ? 'true' : 'false');
    }

    setTimeout(() => osvjeziStatus(true), 250);
    setInterval(() => osvjeziStatus(false), 1500);
  </script>
</body>
</html>
)HTML";

static const char WEB_POSTAVKE_STRANICA[] PROGMEM = R"HTML(
<!doctype html>
<html lang="hr">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ZVONKO postavke</title>
  <style>
    :root {
      color-scheme: light;
      --bg:#e6edf5;
      --panel:#f9fbff;
      --line:#bcc7d6;
      --text:#223246;
      --muted:#5d6f84;
      --accent:#3f78bd;
      --accent-soft:#dcecff;
      --shadow:0 10px 24px rgba(63,82,110,0.12);
    }
    * { box-sizing:border-box; }
    body {
      margin:0;
      font-family: Arial, sans-serif;
      background:linear-gradient(180deg,#dfe7f1,#f5f8fc);
      color:var(--text);
    }
    .wrap {
      max-width:880px;
      margin:0 auto;
      padding:18px 12px 28px;
    }
    .panel {
      background:var(--panel);
      border:1px solid var(--line);
      border-radius:18px;
      box-shadow:var(--shadow);
      padding:18px;
    }
    h1 {
      margin:0 0 8px;
      font-size:28px;
      text-align:center;
      letter-spacing:0.02em;
    }
    h2 {
      margin:0 0 10px;
      font-size:22px;
    }
    .intro {
      margin:0 0 16px;
      color:var(--muted);
      line-height:1.5;
      text-align:center;
      font-size:14px;
    }
    .section {
      margin-top:18px;
      padding-top:18px;
      border-top:1px solid var(--line);
    }
    .grid {
      display:grid;
      grid-template-columns:1fr 1fr;
      gap:12px;
    }
    .field {
      display:grid;
      gap:8px;
      padding:14px;
      border:1px solid var(--line);
      border-radius:14px;
      background:#fff;
    }
    .field label {
      font-size:14px;
      font-weight:700;
    }
    .field small {
      color:var(--muted);
      line-height:1.4;
    }
    .toggle-row {
      display:flex;
      gap:10px;
      align-items:center;
      justify-content:space-between;
    }
    .toggle-chip {
      padding:10px 14px;
      border-radius:12px;
      border:2px solid #97a8bb;
      background:#edf2f7;
      color:#435567;
      font-weight:700;
      min-width:124px;
      cursor:pointer;
    }
    .toggle-chip.active {
      background:var(--accent-soft);
      border-color:var(--accent);
      color:#184a86;
    }
    input[type=number], select {
      width:100%;
      border:1px solid var(--line);
      border-radius:12px;
      padding:12px 10px;
      font:inherit;
      color:var(--text);
      background:#fff;
    }
    .actions {
      margin-top:16px;
      display:flex;
      gap:12px;
      justify-content:center;
      flex-wrap:wrap;
    }
    .actions button,
    .actions a {
      display:inline-block;
      padding:12px 18px;
      border-radius:12px;
      border:1px solid var(--line);
      font:inherit;
      font-weight:700;
      text-decoration:none;
      cursor:pointer;
    }
    .primary {
      background:var(--accent);
      color:#fff;
      border-color:var(--accent);
    }
    .secondary {
      background:#f2f6fb;
      color:var(--text);
    }
    .log {
      margin-top:16px;
      min-height:24px;
      white-space:pre-wrap;
      line-height:1.45;
      font-size:14px;
      color:var(--muted);
    }
    @media (max-width:700px) {
      .grid {
        grid-template-columns:1fr;
      }
    }
  </style>
</head>
<body>
  <div class="wrap">
    <div class="panel">
      <h1>Postavke toranjskog sata</h1>
      <p class="intro">Vrijeme, datum, kazaljke i okretna ploca ostaju na Megi i LCD meniju. Ovdje se uredjuju samo sigurne servisne i automaticke postavke.</p>

      <div class="section" style="margin-top:0;padding-top:0;border-top:none;">
        <h2>Sustav</h2>
        <div class="grid">
          <div class="field">
            <label for="lcdBacklight">LCD svjetlo</label>
            <div class="toggle-row">
              <small>Pozadinsko osvjetljenje LCD-a toranjskog sata.</small>
              <button id="lcdBacklight" type="button" class="toggle-chip" onclick="prebaciToggle('lcdBacklight')">ISKLJUCENO</button>
            </div>
          </div>
          <div class="field">
            <label for="pcLogging">Logiranje</label>
            <div class="toggle-row">
              <small>Servisni logovi prema dijagnostickom izlazu.</small>
              <button id="pcLogging" type="button" class="toggle-chip" onclick="prebaciToggle('pcLogging')">ISKLJUCENO</button>
            </div>
          </div>
          <div class="field">
            <label for="rs485Enabled">RS485</label>
            <div class="toggle-row">
              <small>Omogucuje vanjsku RS485 komunikaciju tornjskog sata.</small>
              <button id="rs485Enabled" type="button" class="toggle-chip" onclick="prebaciToggle('rs485Enabled')">ISKLJUCENO</button>
            </div>
          </div>
          <div class="field">
            <label for="upsMode">UPS mod</label>
            <div class="toggle-row">
              <small>Ponasanje sustava pri radu preko rezervnog napajanja.</small>
              <button id="upsMode" type="button" class="toggle-chip" onclick="prebaciToggle('upsMode')">ISKLJUCENO</button>
            </div>
          </div>
          <div class="field">
            <label for="bellBrake">Kocnica zvona</label>
            <div class="toggle-row">
              <small>Koordinira zaustavljanje zvona i rad mehanike.</small>
              <button id="bellBrake" type="button" class="toggle-chip" onclick="prebaciToggle('bellBrake')">ISKLJUCENO</button>
            </div>
          </div>
          <div class="field">
            <label for="inertia1">INR1 (sekunde)</label>
            <small>Inercija prvog zvona nakon zaustavljanja motora.</small>
            <input id="inertia1" type="number" min="10" max="180" step="1" inputmode="numeric">
          </div>
          <div class="field">
            <label for="inertia2">INR2 (sekunde)</label>
            <small>Inercija drugog zvona nakon zaustavljanja motora.</small>
            <input id="inertia2" type="number" min="10" max="180" step="1" inputmode="numeric">
          </div>
          <div class="field">
            <label for="hammerPulse">Impuls cekica (ms)</label>
            <small>Trajanje impulsa elektromagnetskih batova u koraku od 10 ms.</small>
            <input id="hammerPulse" type="number" min="10" max="300" step="10" inputmode="numeric">
          </div>
        </div>
        <div class="actions">
          <button class="primary" type="button" onclick="spremiSustav()">Spremi sustav</button>
        </div>
      </div>

      <div class="section">
        <h2>Stapici</h2>
        <div class="grid">
          <div class="field">
            <label for="stapiciRadni">Trajanje radni dan (min)</label>
            <small>Koliko minuta traje zvonjenje stapica radnim danom.</small>
            <select id="stapiciRadni">
              <option value="2">2 minute</option>
              <option value="3">3 minute</option>
              <option value="4">4 minute</option>
            </select>
          </div>
          <div class="field">
            <label for="stapiciNedjelja">Trajanje nedjelja (min)</label>
            <small>Koliko minuta traje zvonjenje stapica nedjeljom.</small>
            <select id="stapiciNedjelja">
              <option value="2">2 minute</option>
              <option value="3">3 minute</option>
              <option value="4">4 minute</option>
            </select>
          </div>
          <div class="field">
            <label for="stapiciSlavljenje">Trajanje slavljenja (min)</label>
            <small>Koliko minuta traje slavljenje kad je aktivno.</small>
            <select id="stapiciSlavljenje">
              <option value="2">2 minute</option>
              <option value="3">3 minute</option>
              <option value="4">4 minute</option>
            </select>
          </div>
          <div class="field">
            <label for="stapiciOdgoda">Odgoda slavljenja (s)</label>
            <small>Pomak slavljenja prije pocetka zvonjenja stapica.</small>
            <select id="stapiciOdgoda">
              <option value="15">15 sekundi</option>
              <option value="30">30 sekundi</option>
              <option value="45">45 sekundi</option>
              <option value="60">60 sekundi</option>
            </select>
          </div>
        </div>
        <div class="actions">
          <button class="primary" type="button" onclick="spremiStapici()">Spremi stapice</button>
        </div>
      </div>

      <div class="section">
        <h2>Tihi sati / BAT</h2>
        <div class="grid">
          <div class="field">
            <label for="batSatOd">BAT od (sat)</label>
            <small>Pocetak tihog razdoblja za otkucavanje i posebne nacine.</small>
            <input id="batSatOd" type="number" min="0" max="23" step="1" inputmode="numeric">
          </div>
          <div class="field">
            <label for="batSatDo">BAT do (sat)</label>
            <small>Zavrsetak tihog razdoblja.</small>
            <input id="batSatDo" type="number" min="0" max="23" step="1" inputmode="numeric">
          </div>
          <div class="field">
            <label for="batOtkucavanjeMode">Mod otkucavanja</label>
            <small>Ponasanje satnih otkucaja unutar BAT razdoblja.</small>
            <select id="batOtkucavanjeMode">
              <option value="0">0</option>
              <option value="1">1</option>
              <option value="2">2</option>
            </select>
          </div>
          <div class="field">
            <label for="batSlavljenjeMode">Mod slavljenja</label>
            <small>Ponasanje slavljenja unutar BAT razdoblja.</small>
            <select id="batSlavljenjeMode">
              <option value="1">1</option>
              <option value="2">2</option>
            </select>
          </div>
          <div class="field">
            <label for="batMrtvackoMode">Mod mrtvackog</label>
            <small>Ponasanje mrtvackog unutar BAT razdoblja.</small>
            <select id="batMrtvackoMode">
              <option value="1">1</option>
              <option value="2">2</option>
            </select>
          </div>
        </div>
        <div class="actions">
          <button class="primary" type="button" onclick="spremiBat()">Spremi BAT</button>
        </div>
      </div>

      <div class="section">
        <h2>Sunce</h2>
        <div class="grid">
          <div class="field">
            <label for="jutroEnabled">Jutro</label>
            <div class="toggle-row">
              <small>Automatika jutarnje zdravomarije prema izlasku sunca.</small>
              <button id="jutroEnabled" type="button" class="toggle-chip" onclick="prebaciToggle('jutroEnabled')">ISKLJUCENO</button>
            </div>
          </div>
          <div class="field">
            <label for="jutroBell">Jutro zvono</label>
            <small>Odabir zvona za jutarnji dogadaj.</small>
            <select id="jutroBell">
              <option value="1">Zvono 1</option>
              <option value="2">Zvono 2</option>
            </select>
          </div>
          <div class="field">
            <label for="jutroOffset">Jutro odgoda (min)</label>
            <small>Pomak jutarnjeg dogadaja u odnosu na sunce.</small>
            <select id="jutroOffset">
              <option value="-30">-30</option>
              <option value="-20">-20</option>
              <option value="-10">-10</option>
              <option value="0">0</option>
              <option value="10">10</option>
              <option value="20">20</option>
              <option value="30">30</option>
            </select>
          </div>
          <div class="field">
            <label for="podneEnabled">Podne</label>
            <div class="toggle-row">
              <small>Automatika podnevne zdravomarije u 12:00.</small>
              <button id="podneEnabled" type="button" class="toggle-chip" onclick="prebaciToggle('podneEnabled')">ISKLJUCENO</button>
            </div>
          </div>
          <div class="field">
            <label for="podneBell">Podne zvono</label>
            <small>Odabir zvona za podnevni dogadaj.</small>
            <select id="podneBell">
              <option value="1">Zvono 1</option>
              <option value="2">Zvono 2</option>
            </select>
          </div>
          <div class="field">
            <label for="vecerEnabled">Vecer</label>
            <div class="toggle-row">
              <small>Automatika vecernje zdravomarije prema zalasku sunca.</small>
              <button id="vecerEnabled" type="button" class="toggle-chip" onclick="prebaciToggle('vecerEnabled')">ISKLJUCENO</button>
            </div>
          </div>
          <div class="field">
            <label for="vecerBell">Vecer zvono</label>
            <small>Odabir zvona za vecernji dogadaj.</small>
            <select id="vecerBell">
              <option value="1">Zvono 1</option>
              <option value="2">Zvono 2</option>
            </select>
          </div>
          <div class="field">
            <label for="vecerOffset">Vecer odgoda (min)</label>
            <small>Pomak vecernjeg dogadaja u odnosu na sunce.</small>
            <select id="vecerOffset">
              <option value="-30">-30</option>
              <option value="-20">-20</option>
              <option value="-10">-10</option>
              <option value="0">0</option>
              <option value="10">10</option>
              <option value="20">20</option>
              <option value="30">30</option>
            </select>
          </div>
          <div class="field">
            <label for="nightLight">Nocna rasvjeta</label>
            <div class="toggle-row">
              <small>Automatsko paljenje nocne rasvjete uz sunceve dogadaje.</small>
              <button id="nightLight" type="button" class="toggle-chip" onclick="prebaciToggle('nightLight')">ISKLJUCENO</button>
            </div>
          </div>
        </div>
        <div class="actions">
          <button class="primary" type="button" onclick="spremiSunce()">Spremi sunce</button>
        </div>
      </div>

      <div class="actions">
        <a class="secondary" href="/blagdani">Blagdani</a>
        <button class="secondary" type="button" onclick="ucitajSvePostavke(true)">Osvjezi sve s Mege</button>
        <a class="secondary" href="/">Natrag na dashboard</a>
      </div>

      <div id="odgovor" class="log">Stranica je spremna za citanje web postavki toranjskog sata.</div>
    </div>
  </div>
  <script>
    const stanje = {
      lcdBacklight: false,
      pcLogging: false,
      rs485Enabled: false,
      upsMode: false,
      bellBrake: false,
      jutroEnabled: false,
      podneEnabled: false,
      vecerEnabled: false,
      nightLight: false
    };

    function postaviLog(poruka) {
      document.getElementById('odgovor').textContent = poruka;
    }

    function osvjeziToggleGumb(id) {
      const gumb = document.getElementById(id);
      const aktivno = !!stanje[id];
      gumb.classList.toggle('active', aktivno);
      gumb.textContent = aktivno ? 'UKLJUCENO' : 'ISKLJUCENO';
      gumb.setAttribute('aria-pressed', aktivno ? 'true' : 'false');
    }

    function prebaciToggle(id) {
      stanje[id] = !stanje[id];
      osvjeziToggleGumb(id);
    }

    function postaviSelect(id, vrijednost) {
      const polje = document.getElementById(id);
      polje.value = String(vrijednost);
    }

    function popuniSustav(podaci) {
      stanje.lcdBacklight = !!podaci.lcd_backlight;
      stanje.pcLogging = !!podaci.pc_logging;
      stanje.rs485Enabled = !!podaci.rs485_enabled;
      stanje.upsMode = !!podaci.ups_mode;
      stanje.bellBrake = !!podaci.bell_brake;
      osvjeziToggleGumb('lcdBacklight');
      osvjeziToggleGumb('pcLogging');
      osvjeziToggleGumb('rs485Enabled');
      osvjeziToggleGumb('upsMode');
      osvjeziToggleGumb('bellBrake');
      document.getElementById('inertia1').value = podaci.inertia1_seconds ?? '';
      document.getElementById('inertia2').value = podaci.inertia2_seconds ?? '';
      document.getElementById('hammerPulse').value = podaci.hammer_pulse_ms ?? '';
    }

    function popuniStapici(podaci) {
      postaviSelect('stapiciRadni', podaci.radni_minutes ?? 2);
      postaviSelect('stapiciNedjelja', podaci.nedjelja_minutes ?? 3);
      postaviSelect('stapiciSlavljenje', podaci.slavljenje_minutes ?? 2);
      postaviSelect('stapiciOdgoda', podaci.slavljenje_delay_seconds ?? 15);
    }

    function popuniBat(podaci) {
      document.getElementById('batSatOd').value = podaci.sat_od ?? '';
      document.getElementById('batSatDo').value = podaci.sat_do ?? '';
      postaviSelect('batOtkucavanjeMode', podaci.otkucavanje_mode ?? 0);
      postaviSelect('batSlavljenjeMode', podaci.slavljenje_mode ?? 1);
      postaviSelect('batMrtvackoMode', podaci.mrtvacko_mode ?? 1);
    }

    function popuniSunce(podaci) {
      stanje.jutroEnabled = !!podaci.jutro_enabled;
      stanje.podneEnabled = !!podaci.podne_enabled;
      stanje.vecerEnabled = !!podaci.vecer_enabled;
      stanje.nightLight = !!podaci.night_light;
      osvjeziToggleGumb('jutroEnabled');
      osvjeziToggleGumb('podneEnabled');
      osvjeziToggleGumb('vecerEnabled');
      osvjeziToggleGumb('nightLight');
      postaviSelect('jutroBell', podaci.jutro_bell ?? 1);
      postaviSelect('jutroOffset', podaci.jutro_offset_minutes ?? 0);
      postaviSelect('podneBell', podaci.podne_bell ?? 1);
      postaviSelect('vecerBell', podaci.vecer_bell ?? 1);
      postaviSelect('vecerOffset', podaci.vecer_offset_minutes ?? 0);
    }

    async function dohvatiJson(putanja) {
      const odgovor = await fetch(putanja, { cache: 'no-store' });
      if (!odgovor.ok) {
        throw new Error('status');
      }
      return odgovor.json();
    }

    async function ucitajSvePostavke(prisilno = false) {
      postaviLog('Dohvacam web postavke s Mege...');
      try {
        const sufiks = prisilno ? '?force=1' : '';
        const [sustav, stapici, bat, sunce] = await Promise.all([
          dohvatiJson('/api/settings/system' + sufiks),
          dohvatiJson('/api/settings/stapici' + sufiks),
          dohvatiJson('/api/settings/bat' + sufiks),
          dohvatiJson('/api/settings/sunce' + sufiks)
        ]);

        if (!sustav.known || !stapici.known || !bat.known || !sunce.known) {
          postaviLog('Mega jos nije vratila sve web postavke. Pokusaj ponovno za trenutak.');
          return;
        }

        popuniSustav(sustav);
        popuniStapici(stapici);
        popuniBat(bat);
        popuniSunce(sunce);
        postaviLog('Sve web postavke su ucitane s Mege.');
      } catch (greska) {
        postaviLog('Ucitanje web postavki nije uspjelo.');
      }
    }

    function procitajBroj(id, min, max, korak) {
      const polje = document.getElementById(id);
      const vrijednost = Number(polje.value);
      if (!Number.isFinite(vrijednost) || vrijednost < min || vrijednost > max || (vrijednost % korak) !== 0) {
        throw new Error(id);
      }
      return vrijednost;
    }

    function procitajOdabraniBroj(id, dopustene) {
      const vrijednost = Number(document.getElementById(id).value);
      if (!dopustene.includes(vrijednost)) {
        throw new Error(id);
      }
      return vrijednost;
    }

    async function spremiSustav() {
      let inertia1;
      let inertia2;
      let hammerPulse;
      try {
        inertia1 = procitajBroj('inertia1', 10, 180, 1);
        inertia2 = procitajBroj('inertia2', 10, 180, 1);
        hammerPulse = procitajBroj('hammerPulse', 10, 300, 10);
      } catch (greska) {
        postaviLog('Provjeri granice: INR1 i INR2 moraju biti 10-180 s, a impuls cekica 10-300 ms u koraku 10 ms.');
        return;
      }

      const tijelo = new URLSearchParams({
        lcd: stanje.lcdBacklight ? '1' : '0',
        log: stanje.pcLogging ? '1' : '0',
        rs: stanje.rs485Enabled ? '1' : '0',
        ups: stanje.upsMode ? '1' : '0',
        koc: stanje.bellBrake ? '1' : '0',
        inr1: String(inertia1),
        inr2: String(inertia2),
        imp: String(hammerPulse)
      });

      postaviLog('Saljem sustavske postavke prema Megi...');
      try {
        const odgovor = await fetch('/api/settings/system', {
          method: 'POST',
          headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
          body: tijelo,
          cache: 'no-store'
        });
        const tekst = await odgovor.text();
        postaviLog(tekst);
        if (odgovor.ok) {
          await ucitajSvePostavke(true);
        }
      } catch (greska) {
        postaviLog('Spremanje sustavskih postavki nije uspjelo.');
      }
    }

    async function spremiStapici() {
      let tr, tn, ts, odg;
      try {
        tr = procitajOdabraniBroj('stapiciRadni', [2, 3, 4]);
        tn = procitajOdabraniBroj('stapiciNedjelja', [2, 3, 4]);
        ts = procitajOdabraniBroj('stapiciSlavljenje', [2, 3, 4]);
        odg = procitajOdabraniBroj('stapiciOdgoda', [15, 30, 45, 60]);
      } catch (greska) {
        postaviLog('Provjeri stapice: trajanja moraju biti 2-4 minute, a odgoda 15/30/45/60 s.');
        return;
      }

      postaviLog('Saljem postavke stapica prema Megi...');
      try {
        const odgovor = await fetch('/api/settings/stapici', {
          method: 'POST',
          headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
          body: new URLSearchParams({
            tr: String(tr),
            tn: String(tn),
            ts: String(ts),
            odg: String(odg)
          }),
          cache: 'no-store'
        });
        const tekst = await odgovor.text();
        postaviLog(tekst);
        if (odgovor.ok) {
          await ucitajSvePostavke(true);
        }
      } catch (greska) {
        postaviLog('Spremanje postavki stapica nije uspjelo.');
      }
    }

    async function spremiBat() {
      let satOd, satDo, otk, sl, mr;
      try {
        satOd = procitajBroj('batSatOd', 0, 23, 1);
        satDo = procitajBroj('batSatDo', 0, 23, 1);
        otk = procitajOdabraniBroj('batOtkucavanjeMode', [0, 1, 2]);
        sl = procitajOdabraniBroj('batSlavljenjeMode', [1, 2]);
        mr = procitajOdabraniBroj('batMrtvackoMode', [1, 2]);
      } catch (greska) {
        postaviLog('Provjeri BAT postavke: sati moraju biti 0-23, OTK 0-2, a S i M 1-2.');
        return;
      }

      postaviLog('Saljem BAT postavke prema Megi...');
      try {
        const odgovor = await fetch('/api/settings/bat', {
          method: 'POST',
          headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
          body: new URLSearchParams({
            od: String(satOd),
            do: String(satDo),
            otk: String(otk),
            sl: String(sl),
            mr: String(mr)
          }),
          cache: 'no-store'
        });
        const tekst = await odgovor.text();
        postaviLog(tekst);
        if (odgovor.ok) {
          await ucitajSvePostavke(true);
        }
      } catch (greska) {
        postaviLog('Spremanje BAT postavki nije uspjelo.');
      }
    }

    async function spremiSunce() {
      let jb, jo, pb, vb, vo;
      try {
        jb = procitajOdabraniBroj('jutroBell', [1, 2]);
        jo = procitajOdabraniBroj('jutroOffset', [-30, -20, -10, 0, 10, 20, 30]);
        pb = procitajOdabraniBroj('podneBell', [1, 2]);
        vb = procitajOdabraniBroj('vecerBell', [1, 2]);
        vo = procitajOdabraniBroj('vecerOffset', [-30, -20, -10, 0, 10, 20, 30]);
      } catch (greska) {
        postaviLog('Provjeri sunceve postavke: zvono mora biti 1 ili 2, a odgode u koraku od 10 min od -30 do +30.');
        return;
      }

      postaviLog('Saljem sunceve postavke prema Megi...');
      try {
        const odgovor = await fetch('/api/settings/sunce', {
          method: 'POST',
          headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
          body: new URLSearchParams({
            ju: stanje.jutroEnabled ? '1' : '0',
            jb: String(jb),
            jo: String(jo),
            pu: stanje.podneEnabled ? '1' : '0',
            pb: String(pb),
            vu: stanje.vecerEnabled ? '1' : '0',
            vb: String(vb),
            vo: String(vo),
            nr: stanje.nightLight ? '1' : '0'
          }),
          cache: 'no-store'
        });
        const tekst = await odgovor.text();
        postaviLog(tekst);
        if (odgovor.ok) {
          await ucitajSvePostavke(true);
        }
      } catch (greska) {
        postaviLog('Spremanje suncevih postavki nije uspjelo.');
      }
    }

    ucitajSvePostavke(true);
  </script>
</body>
</html>
)HTML";

static const char WEB_BLAGDANI_STRANICA[] PROGMEM = R"HTML(
<!doctype html>
<html lang="hr">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ZVONKO blagdani</title>
  <style>
    :root {
      color-scheme: light;
      --bg:#e6edf5;
      --panel:#f9fbff;
      --line:#bcc7d6;
      --text:#223246;
      --muted:#5d6f84;
      --accent:#3f78bd;
      --accent-soft:#dcecff;
      --shadow:0 10px 24px rgba(63,82,110,0.12);
    }
    * { box-sizing:border-box; }
    body {
      margin:0;
      font-family: Arial, sans-serif;
      background:linear-gradient(180deg,#dfe7f1,#f5f8fc);
      color:var(--text);
    }
    .wrap {
      max-width:980px;
      margin:0 auto;
      padding:18px 12px 28px;
    }
    .panel {
      background:var(--panel);
      border:1px solid var(--line);
      border-radius:18px;
      box-shadow:var(--shadow);
      padding:18px;
    }
    h1 {
      margin:0 0 8px;
      font-size:28px;
      text-align:center;
      letter-spacing:0.02em;
    }
    h2 {
      margin:0 0 10px;
      font-size:22px;
    }
    .intro {
      margin:0 0 16px;
      color:var(--muted);
      line-height:1.5;
      text-align:center;
      font-size:14px;
    }
    .section {
      margin-top:18px;
      padding-top:18px;
      border-top:1px solid var(--line);
    }
    .section-note {
      margin:0 0 12px;
      color:var(--muted);
      line-height:1.45;
      font-size:14px;
    }
    .rows {
      display:grid;
      gap:10px;
    }
    .row {
      display:grid;
      grid-template-columns:90px 100px 100px 120px 1fr;
      gap:10px;
      align-items:center;
      padding:12px;
      border:1px solid var(--line);
      border-radius:14px;
      background:#fff;
    }
    .row-pomicni {
      grid-template-columns:90px 150px 120px 1fr;
    }
    .slot {
      font-weight:700;
      color:var(--muted);
    }
    .toggle-chip {
      padding:10px 12px;
      border-radius:12px;
      border:2px solid #97a8bb;
      background:#edf2f7;
      color:#435567;
      font-weight:700;
      cursor:pointer;
      min-width:84px;
    }
    .toggle-chip.active {
      background:var(--accent-soft);
      border-color:var(--accent);
      color:#184a86;
    }
    input[type=number] {
      width:100%;
      border:1px solid var(--line);
      border-radius:12px;
      padding:12px 10px;
      font:inherit;
      color:var(--text);
      background:#fff;
    }
    .hint {
      color:var(--muted);
      font-size:13px;
      line-height:1.35;
    }
    .actions {
      margin-top:18px;
      display:flex;
      gap:12px;
      justify-content:center;
      flex-wrap:wrap;
    }
    .actions button,
    .actions a {
      display:inline-block;
      padding:12px 18px;
      border-radius:12px;
      border:1px solid var(--line);
      font:inherit;
      font-weight:700;
      text-decoration:none;
      cursor:pointer;
    }
    .primary {
      background:var(--accent);
      color:#fff;
      border-color:var(--accent);
    }
    .secondary {
      background:#f2f6fb;
      color:var(--text);
    }
    .log {
      margin-top:16px;
      min-height:24px;
      white-space:pre-wrap;
      line-height:1.45;
      font-size:14px;
      color:var(--muted);
    }
    @media (max-width:840px) {
      .row,
      .row-pomicni {
        grid-template-columns:1fr 1fr;
      }
      .hint {
        grid-column:1 / -1;
      }
    }
  </style>
</head>
<body>
  <div class="wrap">
    <div class="panel">
      <h1>Blagdani toranjskog sata</h1>
<p class="intro">Na ovoj stranici toranjskog sata unose se redovite dnevne i nedjeljne mise, kao i posebni nepomični i pomični blagdani. Dnevna misa pokreće samo muško zvono 30 minuta prije mise, a nedjeljna i blagdanska misa pokreću oba zvona 2 sata i 1 sat prije mise, bez dodatnog slavljenja. Sva ta zvonjenja startaju u 25. sekundi minute, kao i čitanje čavala okretne ploče.</p>

      <div class="section" style="margin-top:0;padding-top:0;border-top:none;">
        <h2>Redovite mise</h2>
        <p class="section-note">Dnevna misa vrijedi za dane koji nisu nedjelja. Nedjeljna misa vrijedi samo nedjeljom.</p>
        <div class="rows">
          <div class="row">
            <div class="slot">Dnevna misa</div>
            <button id="rd_en" type="button" class="toggle-chip" onclick="prebaciToggle('rd_en')">ISKLJUCEN</button>
            <input id="rd_tm" type="text" inputmode="numeric" maxlength="5" placeholder="HH:MM" data-time-field="1">
<div class="hint">Muško zvono 30 min prije mise, uz radno trajanje zvonjenja iz postavki.</div>
          </div>
          <div class="row">
            <div class="slot">Nedjeljna misa</div>
            <button id="nd_en" type="button" class="toggle-chip" onclick="prebaciToggle('nd_en')">ISKLJUCEN</button>
            <input id="nd_tm" type="text" inputmode="numeric" maxlength="5" placeholder="HH:MM" data-time-field="1">
<div class="hint">Oba zvona 2 h i 1 h prije mise, uz nedjeljno trajanje zvonjenja iz postavki.</div>
          </div>
        </div>
      </div>

      <div class="section">
        <h2>Nepomični blagdani</h2>
        <p class="section-note">Primjeri: 13.06. za svetog Antu ili 29.06. za svetog Petra. Sat mise je puni sat 0-23.</p>
        <div id="fixedRows" class="rows"></div>
      </div>

      <div class="section">
        <h2>Pomični blagdani vezani uz Uskrs</h2>
        <p class="section-note">Pomak se upisuje u danima u odnosu na Uskrs. Negativan broj je prije Uskrsa, pozitivan nakon Uskrsa.</p>
        <div id="movableRows" class="rows"></div>
      </div>

      <div class="actions">
        <button class="primary" type="button" onclick="spremiBlagdane()">Spremi mise i blagdane</button>
        <button class="secondary" type="button" onclick="ucitajBlagdane(true)">Osvjezi s Mege</button>
        <a class="secondary" href="/settings">Natrag na postavke</a>
        <a class="secondary" href="/">Pocetna</a>
      </div>

      <div id="holidayLog" class="log"></div>
    </div>
  </div>

  <script>
    const brojNepomicnih = 15;
    const brojPomicnih = 7;
    const naziviNepomicnih = [
      'Nova Godina',
      'Bogojavljenje',
      'Svjecnica',
      'Alojzije Stepinac',
      'Sveti Josip',
      'Blagovijest',
      'Sveti Ante',
      'Sveti Petar',
      'Velika Gospa',
      'Svi Sveti',
      'Dusni dan',
      'Badnjak (polnocka)',
      'Bozic',
      'Sveti Stjepan',
      'Sveti Ivan'
    ];
    const naziviPomicnih = [
      'PEPELNICA',
      'VELIKI CETVRTAK',
      'USKRS',
      'USKRSNI PONEDJELJAK',
      'UZASASCE',
      'TIJELOVO',
      'SRCE ISUSOVO'
    ];

    function osvjeziTekstBlagdanskeStranice() {
      const intro = document.querySelector('.intro');
      if (intro) {
        intro.textContent = 'Na ovoj stranici toranjskog sata unose se redovite dnevne i nedjeljne mise te ukljucenje i vrijeme vec zadanih blagdana. Dnevna misa pokrece samo musko zvono 30 minuta prije mise, a nedjeljna i blagdanska misa pokrecu oba zvona 2 sata i 1 sat prije mise, bez dodatnog slavljenja. Sva ta zvonjenja startaju u 25. sekundi minute, kao i citanje cavala okretne ploce.';
      }

      const sectionTitles = document.querySelectorAll('.section h2');
      if (sectionTitles.length >= 3) {
        sectionTitles[1].textContent = 'Nepomicni blagdani';
        sectionTitles[2].textContent = 'Pomicni blagdani vezani uz Uskrs';
      }

      const sectionNotes = document.querySelectorAll('.section .section-note');
      if (sectionNotes.length >= 3) {
        sectionNotes[1].textContent = 'Datumi su vec zadani u kodu toranjskog sata. Ovdje se za svaki blagdan uredjuje samo ukljucenje i vrijeme mise u formatu HH:MM.';
        sectionNotes[2].textContent = 'Pomicni blagdani su vec vezani uz Uskrs u kodu toranjskog sata. Ovdje se za svaki blagdan uredjuje samo ukljucenje i vrijeme mise u formatu HH:MM.';
      }
    }

    function postaviLog(poruka) {
      const log = document.getElementById('holidayLog');
      if (log) {
        log.textContent = poruka;
      }
    }

    function redNepomicni(indeks) {
      return `
        <div class="row">
          <div class="slot">${naziviNepomicnih[indeks] || ('Blagdan ' + (indeks + 1))}</div>
          <button id="f${indeks}_en" type="button" class="toggle-chip" onclick="prebaciToggle('f${indeks}_en')">ISKLJUCEN</button>
          <input id="f${indeks}_tm" type="text" inputmode="numeric" maxlength="5" placeholder="HH:MM" data-time-field="1">
          <div class="hint">Vrijeme mise HH:MM</div>
        </div>`;
    }

    function redPomicni(indeks) {
      return `
        <div class="row row-pomicni">
          <div class="slot">${naziviPomicnih[indeks] || ('Pomicni ' + (indeks + 1))}</div>
          <button id="p${indeks}_en" type="button" class="toggle-chip" onclick="prebaciToggle('p${indeks}_en')">ISKLJUCEN</button>
          <input id="p${indeks}_tm" type="text" inputmode="numeric" maxlength="5" placeholder="HH:MM" data-time-field="1">
          <div class="hint">Vrijeme mise HH:MM</div>
        </div>`;
    }

    function iscrtajRetke() {
      const fixed = document.getElementById('fixedRows');
      const movable = document.getElementById('movableRows');
      let fixedHtml = '';
      let movableHtml = '';
      for (let i = 0; i < brojNepomicnih; i += 1) {
        fixedHtml += redNepomicni(i);
      }
      for (let i = 0; i < brojPomicnih; i += 1) {
        movableHtml += redPomicni(i);
      }
      fixed.innerHTML = fixedHtml;
      movable.innerHTML = movableHtml;
      inicijalizirajPoljaVremena();
    }

    function postaviToggle(id, aktivno) {
      const tipka = document.getElementById(id);
      if (!tipka) {
        return;
      }
      tipka.dataset.active = aktivno ? '1' : '0';
      tipka.textContent = aktivno ? 'UKLJUCEN' : 'ISKLJUCEN';
      tipka.classList.toggle('active', aktivno);
    }

    function prebaciToggle(id) {
      const tipka = document.getElementById(id);
      const aktivno = tipka && tipka.dataset.active === '1';
      postaviToggle(id, !aktivno);
    }

    function normalizirajUnosVremena(vrijednost) {
      const znamenke = String(vrijednost || '').replace(/\D/g, '').slice(0, 4);
      if (znamenke.length <= 2) {
        return znamenke;
      }
      return znamenke.slice(0, 2) + ':' + znamenke.slice(2);
    }

    function obradiUnosVremenaDogadaj(event) {
      const polje = event && event.target;
      if (!polje) {
        return;
      }
      const prije = String(polje.value || '');
      const poslije = normalizirajUnosVremena(prije);
      if (prije !== poslije) {
        polje.value = poslije;
      }
    }

    function inicijalizirajPoljaVremena() {
      document.querySelectorAll('input[data-time-field="1"]').forEach((polje) => {
        if (polje.dataset.timeInit === '1') {
          return;
        }
        polje.dataset.timeInit = '1';
        polje.addEventListener('input', obradiUnosVremenaDogadaj);
        polje.addEventListener('blur', obradiUnosVremenaDogadaj);
      });
    }

    function procitajVrijeme(id) {
      const polje = document.getElementById(id);
      if (!polje) {
        throw new Error('Polje ' + id + ' nije pronadjeno.');
      }
      const vrijednost = normalizirajUnosVremena(String(polje.value || '').trim());
      polje.value = vrijednost;
      if (vrijednost.length === 0) {
        return null;
      }
      if (!/^\d{1,2}:\d{2}$/.test(vrijednost)) {
        throw new Error('Polje ' + id + ' mora biti u formatu HH:MM.');
      }
      const [satTekst, minutaTekst] = vrijednost.split(':');
      const sat = Number(satTekst);
      const minuta = Number(minutaTekst);
      if (!Number.isInteger(sat) || !Number.isInteger(minuta) || sat < 0 || sat > 23 || minuta < 0 || minuta > 59) {
        throw new Error('Polje ' + id + ' mora biti valjano vrijeme HH:MM.');
      }
      return { sat, minuta };
    }

    function formatVrijeme(sat, minuta, zadano = '10:00') {
      if (!Number.isInteger(sat) || sat < 0 || sat > 23 || !Number.isInteger(minuta) || minuta < 0 || minuta > 59) {
        return zadano;
      }
      return String(sat).padStart(2, '0') + ':' + String(minuta).padStart(2, '0');
    }

    function ucitajRedoviteMiseUPayload(params) {
      const dnevna = procitajVrijeme('rd_tm');
      const dnevnaAktivna = !!dnevna && document.getElementById('rd_en').dataset.active === '1';
      params.append('rd_en', dnevnaAktivna ? '1' : '0');
      params.append('rd_tm', dnevna ? formatVrijeme(dnevna.sat, dnevna.minuta) : '');

      const nedjeljna = procitajVrijeme('nd_tm');
      const nedjeljnaAktivna = !!nedjeljna && document.getElementById('nd_en').dataset.active === '1';
      params.append('nd_en', nedjeljnaAktivna ? '1' : '0');
      params.append('nd_tm', nedjeljna ? formatVrijeme(nedjeljna.sat, nedjeljna.minuta) : '');
    }

    function ucitajNepomicneUPayload(params) {
      for (let i = 0; i < brojNepomicnih; i += 1) {
        const vrijeme = procitajVrijeme('f' + i + '_tm');
        const aktivno = !!vrijeme && document.getElementById('f' + i + '_en').dataset.active === '1';
        params.append('f' + i + '_en', aktivno ? '1' : '0');
        params.append('f' + i + '_tm', vrijeme ? formatVrijeme(vrijeme.sat, vrijeme.minuta) : '');
      }
    }

    function ucitajPomicneUPayload(params) {
      for (let i = 0; i < brojPomicnih; i += 1) {
        const vrijeme = procitajVrijeme('p' + i + '_tm');
        const aktivno = !!vrijeme && document.getElementById('p' + i + '_en').dataset.active === '1';
        params.append('p' + i + '_en', aktivno ? '1' : '0');
        params.append('p' + i + '_tm', vrijeme ? formatVrijeme(vrijeme.sat, vrijeme.minuta) : '');
      }
    }

    function popuniBlagdane(podaci) {
      const dnevna = podaci.daily || {};
      postaviToggle('rd_en', !!dnevna.enabled);
      document.getElementById('rd_tm').value =
        dnevna.enabled ? formatVrijeme(dnevna.hour, dnevna.minute, '19:00') : '';

      const nedjeljna = podaci.sunday || {};
      postaviToggle('nd_en', !!nedjeljna.enabled);
      document.getElementById('nd_tm').value =
        nedjeljna.enabled ? formatVrijeme(nedjeljna.hour, nedjeljna.minute, '10:00') : '';

      for (let i = 0; i < brojNepomicnih; i += 1) {
        const red = (podaci.fixed && podaci.fixed[i]) || {};
        postaviToggle('f' + i + '_en', !!red.enabled);
        document.getElementById('f' + i + '_tm').value =
          red.enabled ? formatVrijeme(red.hour, red.minute) : '';
      }
      for (let i = 0; i < brojPomicnih; i += 1) {
        const red = (podaci.movable && podaci.movable[i]) || {};
        postaviToggle('p' + i + '_en', !!red.enabled);
        document.getElementById('p' + i + '_tm').value =
          red.enabled ? formatVrijeme(red.hour, red.minute) : '';
      }
    }

    async function ucitajBlagdane(prisilno = false) {
      postaviLog('Ucitavam mise i blagdanske postavke s Mege...');
      try {
        const odgovor = await fetch('/api/settings/blagdani' + (prisilno ? '?force=1' : ''), { cache: 'no-store' });
        if (!odgovor.ok) {
          throw new Error('HTTP ' + odgovor.status);
        }
        const podaci = await odgovor.json();
        if (!podaci.known) {
          postaviLog('Mega jos nije vratila misne i blagdanske postavke. Pokusaj ponovno za trenutak.');
          return;
        }
        popuniBlagdane(podaci);
        postaviLog('Misne i blagdanske postavke ucitane s Mege.');
      } catch (greska) {
        postaviLog('Ne mogu ucitati misne i blagdanske postavke: ' + greska.message);
      }
    }

    async function spremiBlagdane() {
      try {
        const params = new URLSearchParams();
        ucitajRedoviteMiseUPayload(params);
        ucitajNepomicneUPayload(params);
        ucitajPomicneUPayload(params);

        postaviLog('Spremam misne i blagdanske postavke na Megu...');
        const odgovor = await fetch('/api/settings/blagdani', {
          method: 'POST',
          headers: {
            'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8'
          },
          body: params.toString()
        });

        const tekst = await odgovor.text();
        if (!odgovor.ok) {
          throw new Error(tekst || ('HTTP ' + odgovor.status));
        }

        postaviLog(tekst);
        await ucitajBlagdane(true);
      } catch (greska) {
        postaviLog('Misne i blagdanske postavke nisu spremljene: ' + greska.message);
      }
    }

    osvjeziTekstBlagdanskeStranice();
    iscrtajRetke();
    ucitajBlagdane(true);
  </script>
</body>
</html>
)HTML";

static const char WEB_SETUP_STRANICA[] PROGMEM = R"HTML(
<!doctype html>
<html lang="hr">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ZVONKO setup WiFi</title>
  <style>
    :root { color-scheme: light; --bg:#f3efe6; --panel:#fffaf1; --line:#c8baa1; --text:#2c2418; --soft:#e7dcc8; }
    body { margin:0; font-family: Georgia, "Times New Roman", serif; background:linear-gradient(180deg,#efe6d3,#f7f3ea); color:var(--text); }
    .wrap { max-width:620px; margin:0 auto; padding:20px; }
    .panel { background:var(--panel); border:1px solid var(--line); border-radius:14px; padding:16px; box-shadow:0 10px 24px rgba(77,52,24,0.08); }
    label { display:grid; gap:6px; margin-bottom:12px; color:#5f4a32; }
    input, button { border:1px solid var(--line); border-radius:10px; padding:12px 10px; background:#fff; color:var(--text); width:100%; box-sizing:border-box; }
    button { font-weight:700; cursor:pointer; }
    button:hover { background:var(--soft); }
    .muted { color:#7a6a56; font-size:14px; }
    .log { white-space:pre-wrap; min-height:24px; color:#5f4a32; }
  </style>
</head>
<body>
  <div class="wrap">
    <div class="panel">
      <h1>ZVONKO setup WiFi</h1>
      <p class="muted">Ova stranica sluzi za kratkotrajno postavljanje nove WiFi mreze toranjskog sata preko setup mreze <strong>ZVONKO_setup</strong>.</p>
      <label>SSID nove mreze
        <input id="ssid" type="text" maxlength="32" placeholder="Naziv WiFi mreze">
      </label>
      <label>Lozinka nove mreze
        <input id="lozinka" type="password" maxlength="32" placeholder="Lozinka WiFi mreze">
      </label>
      <button onclick="spremiWiFi()">Spremi i spoji</button>
      <div id="odgovor" class="log" style="margin-top:12px;">Setup mreza je spremna za unos nove WiFi konfiguracije.</div>
    </div>
  </div>
  <script>
    async function spremiWiFi() {
      const ssid = document.getElementById('ssid').value.trim();
      const lozinka = document.getElementById('lozinka').value;
      if (!ssid || ssid.includes('|') || ssid.includes('\n')) {
        document.getElementById('odgovor').textContent = 'SSID mora biti upisan i ne smije sadrzavati znak |.';
        return;
      }
      if (!lozinka || lozinka.includes('|') || lozinka.includes('\n')) {
        document.getElementById('odgovor').textContent = 'Lozinka mora biti upisana i ne smije sadrzavati znak |.';
        return;
      }

      const body = new URLSearchParams({ ssid, lozinka });
      const r = await fetch('/setup', {
        method: 'POST',
        headers: {'Content-Type':'application/x-www-form-urlencoded'},
        body
      });
      const t = await r.text();
      document.getElementById('odgovor').textContent = t;
    }
  </script>
</body>
</html>
)HTML";

static const char WEB_OTA_STRANICA[] PROGMEM = R"HTML(
<!doctype html>
<html lang="hr">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ZVONKO OTA</title>
  <style>
    :root { color-scheme: light; --bg:#e8eef6; --panel:#f9fbff; --line:#bcc7d6; --text:#223246; --accent:#3f78bd; }
    * { box-sizing:border-box; }
    body { margin:0; font-family: Arial, sans-serif; background:linear-gradient(180deg,#dfe7f1,#f5f8fc); color:var(--text); }
    .wrap { max-width:620px; margin:0 auto; padding:20px 14px; }
    .panel { background:var(--panel); border:1px solid var(--line); border-radius:18px; padding:18px; box-shadow:0 10px 24px rgba(63,82,110,0.12); }
    h1 { margin:0 0 10px; font-size:28px; }
    p { line-height:1.55; }
    input[type=file], button { width:100%; padding:12px; border-radius:12px; border:1px solid var(--line); font:inherit; }
    button { margin-top:12px; background:var(--accent); color:#fff; font-weight:700; cursor:pointer; }
    .log { margin-top:14px; min-height:24px; white-space:pre-wrap; }
  </style>
</head>
<body>
  <div class="wrap">
    <div class="panel">
      <h1>OTA nadogradnja ESP modula</h1>
      <p>Odaberi novu <code>.bin</code> datoteku za mrezni modul toranjskog sata. Tijekom upisa firmwarea nemoj gasiti napajanje ni prekidati WiFi vezu.</p>
      <form id="otaForm">
        <input id="firmware" name="update" type="file" accept=".bin,application/octet-stream" required>
        <button type="submit">Pokreni OTA nadogradnju</button>
      </form>
      <div id="odgovor" class="log">Stranica je spremna za upload novog firmwarea.</div>
    </div>
  </div>
  <script>
    const forma = document.getElementById('otaForm');
    const odgovor = document.getElementById('odgovor');
    forma.addEventListener('submit', async (dogadaj) => {
      dogadaj.preventDefault();
      const datoteka = document.getElementById('firmware').files[0];
      if (!datoteka) {
        odgovor.textContent = 'Odaberi firmware datoteku prije slanja.';
        return;
      }

      const podaci = new FormData();
      podaci.append('update', datoteka);
      odgovor.textContent = 'Upload je pokrenut, pricekaj zavrsetak...';

      try {
        const r = await fetch('/update', {
          method: 'POST',
          body: podaci,
          cache: 'no-store'
        });
        const t = await r.text();
        odgovor.textContent = t;
      } catch (greska) {
        odgovor.textContent = 'OTA upload nije uspio. Provjeri WiFi vezu i pokusaj ponovno.';
      }
    });
  </script>
</body>
</html>
)HTML";



void zakaziRestartNakonOta() {
  otaRestartZakazan = true;
  otaRestartZakazanMs = millis();
}

void obradiZakazaniRestartNakonOta() {
  if ((millis() - otaRestartZakazanMs) < OTA_RESTART_ODGODA_MS) {
    return;
  }

  Serial.println("OTA: restart ESP modula nakon uspjesne nadogradnje");
  delay(50);
  ESP.restart();
}

void obradiOtaUpload() {
  HTTPUpload& upload = webPosluzitelj.upload();

  switch (upload.status) {
    case UPLOAD_FILE_START: {
      otaAzuriranjeUTijeku = true;
      otaRestartZakazan = false;
      otaUspjesanZadnjiPut = false;
      resetirajNtpStanje();
      Serial.printf("OTA: pocetak nadogradnje %s\n", upload.filename.c_str());

      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
        Update.printError(Serial);
      }
      break;
    }

    case UPLOAD_FILE_WRITE:
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        Update.printError(Serial);
      }
      yield();
      break;

    case UPLOAD_FILE_END:
      if (Update.end(true)) {
        otaUspjesanZadnjiPut = true;
        Serial.printf("OTA: nadogradnja zavrsena, %u bajtova\n", upload.totalSize);
      } else {
        Update.printError(Serial);
      }
      otaAzuriranjeUTijeku = false;
      break;

    case UPLOAD_FILE_ABORTED:
      Update.abort();
      otaAzuriranjeUTijeku = false;
      otaUspjesanZadnjiPut = false;
      Serial.println("OTA: upload prekinut");
      break;

    default:
      break;
  }
}

void konfigurirajWebPosluzitelj() {
  Serial.println("WEB: Registriram / rutu");
  webPosluzitelj.on("/", []() {
    if (setupApAktivan) {
      posaljiHtmlStranicuIzProgMema(WEB_SETUP_STRANICA);
      return;
    }
    if (!osigurajWebAutorizaciju()) {
      return;
    }
    posaljiHtmlStranicuIzProgMema(WEB_POCETNA_STRANICA);
  });

  Serial.println("WEB: Registriram /settings rutu");
  webPosluzitelj.on("/settings", HTTP_GET, []() {
    if (setupApAktivan) {
      posaljiHtmlStranicuIzProgMema(WEB_SETUP_STRANICA);
      return;
    }
    if (!osigurajWebAutorizaciju()) {
      return;
    }
    posaljiHtmlStranicuIzProgMema(WEB_POSTAVKE_STRANICA);
  });

  Serial.println("WEB: Registriram /blagdani rutu");
  webPosluzitelj.on("/blagdani", HTTP_GET, []() {
    if (setupApAktivan) {
      posaljiHtmlStranicuIzProgMema(WEB_SETUP_STRANICA);
      return;
    }
    if (!osigurajWebAutorizaciju()) {
      return;
    }
    posaljiHtmlStranicuIzProgMema(WEB_BLAGDANI_STRANICA);
  });

  Serial.println("WEB: Registriram /setup rutu");
  webPosluzitelj.on("/setup", HTTP_GET, []() {
    if (!setupApAktivan) {
      webPosluzitelj.send(404, "text/plain", "Setup WiFi mreza trenutno nije aktivna");
      return;
    }
    posaljiHtmlStranicuIzProgMema(WEB_SETUP_STRANICA);
  });
  webPosluzitelj.on("/setup", HTTP_POST, []() {
    if (!setupApAktivan) {
      webPosluzitelj.send(409, "text/plain", "Setup WiFi mreza nije aktivna");
      return;
    }
    if (!webPosluzitelj.hasArg("ssid") || !webPosluzitelj.hasArg("lozinka")) {
      webPosluzitelj.send(400, "text/plain", "Nedostaje SSID ili lozinka nove WiFi mreze");
      return;
    }

    String ssid = ocistiJednolinijskiTekst(webPosluzitelj.arg("ssid"), 32);
    String lozinka = ocistiJednolinijskiTekst(webPosluzitelj.arg("lozinka"), 32);
    ssid.trim();

    if (ssid.length() == 0 || lozinka.length() == 0 ||
        ssid.indexOf('|') >= 0 || lozinka.indexOf('|') >= 0) {
      webPosluzitelj.send(422, "text/plain", "SSID i lozinka moraju biti valjani");
      return;
    }

    String odgovor = "";
    if (!posaljiSetupWiFiMegai(ssid, lozinka, odgovor, 2500)) {
      webPosluzitelj.send(422, "text/plain", odgovor);
      return;
    }

    webPosluzitelj.send(200, "text/plain", odgovor);
  });

  Serial.println("WEB: Registriram /update rutu");
  webPosluzitelj.on("/update", HTTP_GET, []() {
    if (!osigurajWebAutorizaciju()) {
      return;
    }
    posaljiHtmlStranicuIzProgMema(WEB_OTA_STRANICA);
  });
  webPosluzitelj.on("/update", HTTP_POST, []() {
    if (!osigurajWebAutorizaciju()) {
      return;
    }

    if (!otaUspjesanZadnjiPut) {
      webPosluzitelj.send(500,
                          "text/plain",
                          "OTA nadogradnja nije uspjela. Provjeri serijski log ESP modula.");
      return;
    }

    zakaziRestartNakonOta();
    webPosluzitelj.send(200,
                        "text/plain",
                        "OTA nadogradnja je uspjela. ESP modul ce se uskoro restartati.");
  }, []() {
    obradiOtaUpload();
  });

  Serial.println("WEB: Registriram /api/status rutu");
  webPosluzitelj.on("/api/status", []() {
    if (!osigurajWebAutorizaciju()) {
      return;
    }
    const bool prisilno =
        webPosluzitelj.hasArg("force") &&
        webPosluzitelj.arg("force") == "1";
    posaljiJsonStatus(prisilno);
  });

  Serial.println("WEB: Registriram /api/settings/system rutu");
  webPosluzitelj.on("/api/settings/system", HTTP_GET, []() {
    if (!osigurajWebAutorizaciju()) {
      return;
    }
    const bool prisilno =
        webPosluzitelj.hasArg("force") &&
        webPosluzitelj.arg("force") == "1";
    posaljiJsonSustavskihPostavki(prisilno);
  });
  webPosluzitelj.on("/api/settings/system", HTTP_POST, []() {
    if (!osigurajWebAutorizaciju()) {
      return;
    }

    const char* obaveznaPolja[] = {"lcd", "log", "rs", "ups", "koc", "inr1", "inr2", "imp"};
    for (size_t i = 0; i < (sizeof(obaveznaPolja) / sizeof(obaveznaPolja[0])); ++i) {
      if (!webPosluzitelj.hasArg(obaveznaPolja[i])) {
        webPosluzitelj.send(400, "text/plain", "Nedostaje jedno ili vise polja sustavskih postavki");
        return;
      }
    }

    const String lcdArg = ocistiJednolinijskiTekst(webPosluzitelj.arg("lcd"), 1);
    const String logArg = ocistiJednolinijskiTekst(webPosluzitelj.arg("log"), 1);
    const String rsArg = ocistiJednolinijskiTekst(webPosluzitelj.arg("rs"), 1);
    const String upsArg = ocistiJednolinijskiTekst(webPosluzitelj.arg("ups"), 1);
    const String kocArg = ocistiJednolinijskiTekst(webPosluzitelj.arg("koc"), 1);
    const String inr1Arg = ocistiJednolinijskiTekst(webPosluzitelj.arg("inr1"), 3);
    const String inr2Arg = ocistiJednolinijskiTekst(webPosluzitelj.arg("inr2"), 3);
    const String impArg = ocistiJednolinijskiTekst(webPosluzitelj.arg("imp"), 3);

    if (!((lcdArg == "0" || lcdArg == "1") &&
          (logArg == "0" || logArg == "1") &&
          (rsArg == "0" || rsArg == "1") &&
          (upsArg == "0" || upsArg == "1") &&
          (kocArg == "0" || kocArg == "1") &&
          jeDecimalniBrojString(inr1Arg) &&
          jeDecimalniBrojString(inr2Arg) &&
          jeDecimalniBrojString(impArg))) {
      webPosluzitelj.send(422, "text/plain", "Toggle polja moraju biti 0 ili 1, a brojcana polja cijeli brojevi");
      return;
    }

    const long inr1 = inr1Arg.toInt();
    const long inr2 = inr2Arg.toInt();
    const long impuls = impArg.toInt();
    if (inr1 < 10L || inr1 > 180L || inr2 < 10L || inr2 > 180L ||
        impuls < 10L || impuls > 300L || (impuls % 10L) != 0L) {
      webPosluzitelj.send(422,
                          "text/plain",
                          "INR1 i INR2 moraju biti 10-180 s, a impuls cekica 10-300 ms u koraku 10 ms");
      return;
    }

    const PostavkeOdgovorMegai status = posaljiSustavskePostavkeMegai(
        lcdArg == "1",
        logArg == "1",
        rsArg == "1",
        upsArg == "1",
        kocArg == "1",
        static_cast<unsigned int>(inr1),
        static_cast<unsigned int>(inr2),
        static_cast<unsigned int>(impuls),
        CMD_CEKANJE_NA_MEGU_MS);

    if (status == POSTAVKE_ODGOVOR_OK) {
      osvjeziSustavskePostavkeMegai(true);
      webPosluzitelj.send(200, "text/plain", "Sustavske postavke su spremljene na Megi.");
      return;
    }

    if (status == POSTAVKE_ODGOVOR_ERR) {
      webPosluzitelj.send(422, "text/plain", "Mega je odbila sustavske postavke.");
      return;
    }

    webPosluzitelj.send(504, "text/plain", "Mega nije potvrdila spremanje sustavskih postavki.");
  });

  Serial.println("WEB: Registriram /api/settings/stapici rutu");
  webPosluzitelj.on("/api/settings/stapici", HTTP_GET, []() {
    if (!osigurajWebAutorizaciju()) {
      return;
    }
    const bool prisilno =
        webPosluzitelj.hasArg("force") &&
        webPosluzitelj.arg("force") == "1";
    posaljiJsonPostavkiStapica(prisilno);
  });
  webPosluzitelj.on("/api/settings/stapici", HTTP_POST, []() {
    if (!osigurajWebAutorizaciju()) {
      return;
    }

    const char* obaveznaPolja[] = {"tr", "tn", "ts", "odg"};
    for (size_t i = 0; i < (sizeof(obaveznaPolja) / sizeof(obaveznaPolja[0])); ++i) {
      if (!webPosluzitelj.hasArg(obaveznaPolja[i])) {
        webPosluzitelj.send(400, "text/plain", "Nedostaje jedno ili vise polja postavki stapica");
        return;
      }
    }

    const String trArg = ocistiJednolinijskiTekst(webPosluzitelj.arg("tr"), 1);
    const String tnArg = ocistiJednolinijskiTekst(webPosluzitelj.arg("tn"), 1);
    const String tsArg = ocistiJednolinijskiTekst(webPosluzitelj.arg("ts"), 1);
    const String odgArg = ocistiJednolinijskiTekst(webPosluzitelj.arg("odg"), 2);

    if (!(jeDecimalniBrojString(trArg) &&
          jeDecimalniBrojString(tnArg) &&
          jeDecimalniBrojString(tsArg) &&
          jeDecimalniBrojString(odgArg))) {
      webPosluzitelj.send(422, "text/plain", "Postavke stapica moraju biti cijeli brojevi");
      return;
    }

    const long tr = trArg.toInt();
    const long tn = tnArg.toInt();
    const long ts = tsArg.toInt();
    const long odg = odgArg.toInt();
    if (!((tr >= 2L && tr <= 4L) &&
          (tn >= 2L && tn <= 4L) &&
          (ts >= 2L && ts <= 4L) &&
          (odg == 15L || odg == 30L || odg == 45L || odg == 60L))) {
      webPosluzitelj.send(422, "text/plain", "Stapici podrzavaju trajanja 2-4 minute i odgodu 15/30/45/60 sekundi");
      return;
    }

    const PostavkeOdgovorMegai status = posaljiPostavkeStapicaMegai(
        static_cast<unsigned int>(tr),
        static_cast<unsigned int>(tn),
        static_cast<unsigned int>(ts),
        static_cast<unsigned int>(odg),
        CMD_CEKANJE_NA_MEGU_MS);

    if (status == POSTAVKE_ODGOVOR_OK) {
      osvjeziPostavkeStapicaMegai(true);
      webPosluzitelj.send(200, "text/plain", "Postavke stapica su spremljene na Megi.");
      return;
    }

    if (status == POSTAVKE_ODGOVOR_ERR) {
      webPosluzitelj.send(422, "text/plain", "Mega je odbila postavke stapica.");
      return;
    }

    webPosluzitelj.send(504, "text/plain", "Mega nije potvrdila spremanje postavki stapica.");
  });

  Serial.println("WEB: Registriram /api/settings/bat rutu");
  webPosluzitelj.on("/api/settings/bat", HTTP_GET, []() {
    if (!osigurajWebAutorizaciju()) {
      return;
    }
    const bool prisilno =
        webPosluzitelj.hasArg("force") &&
        webPosluzitelj.arg("force") == "1";
    posaljiJsonBATPostavki(prisilno);
  });
  webPosluzitelj.on("/api/settings/bat", HTTP_POST, []() {
    if (!osigurajWebAutorizaciju()) {
      return;
    }

    const char* obaveznaPolja[] = {"od", "do", "otk", "sl", "mr"};
    for (size_t i = 0; i < (sizeof(obaveznaPolja) / sizeof(obaveznaPolja[0])); ++i) {
      if (!webPosluzitelj.hasArg(obaveznaPolja[i])) {
        webPosluzitelj.send(400, "text/plain", "Nedostaje jedno ili vise polja BAT postavki");
        return;
      }
    }

    const String odArg = ocistiJednolinijskiTekst(webPosluzitelj.arg("od"), 2);
    const String doArg = ocistiJednolinijskiTekst(webPosluzitelj.arg("do"), 2);
    const String otkArg = ocistiJednolinijskiTekst(webPosluzitelj.arg("otk"), 1);
    const String slArg = ocistiJednolinijskiTekst(webPosluzitelj.arg("sl"), 1);
    const String mrArg = ocistiJednolinijskiTekst(webPosluzitelj.arg("mr"), 1);

    if (!(jeDecimalniBrojString(odArg) &&
          jeDecimalniBrojString(doArg) &&
          jeDecimalniBrojString(otkArg) &&
          jeDecimalniBrojString(slArg) &&
          jeDecimalniBrojString(mrArg))) {
      webPosluzitelj.send(422, "text/plain", "BAT postavke moraju biti cijeli brojevi");
      return;
    }

    const long satOd = odArg.toInt();
    const long satDo = doArg.toInt();
    const long otk = otkArg.toInt();
    const long sl = slArg.toInt();
    const long mr = mrArg.toInt();
    if (satOd < 0L || satOd > 23L || satDo < 0L || satDo > 23L ||
        otk < 0L || otk > 2L ||
        (sl != 1L && sl != 2L) ||
        (mr != 1L && mr != 2L)) {
      webPosluzitelj.send(422, "text/plain", "BAT podrzava sate 0-23, OTK 0-2 te modove S i M 1-2");
      return;
    }

    const PostavkeOdgovorMegai status = posaljiBATPostavkeMegai(
        static_cast<unsigned int>(satOd),
        static_cast<unsigned int>(satDo),
        static_cast<unsigned int>(otk),
        static_cast<unsigned int>(sl),
        static_cast<unsigned int>(mr),
        CMD_CEKANJE_NA_MEGU_MS);

    if (status == POSTAVKE_ODGOVOR_OK) {
      osvjeziBATPostavkeMegai(true);
      webPosluzitelj.send(200, "text/plain", "BAT postavke su spremljene na Megi.");
      return;
    }

    if (status == POSTAVKE_ODGOVOR_ERR) {
      webPosluzitelj.send(422, "text/plain", "Mega je odbila BAT postavke.");
      return;
    }

    webPosluzitelj.send(504, "text/plain", "Mega nije potvrdila spremanje BAT postavki.");
  });

  Serial.println("WEB: Registriram /api/settings/sunce rutu");
  webPosluzitelj.on("/api/settings/sunce", HTTP_GET, []() {
    if (!osigurajWebAutorizaciju()) {
      return;
    }
    const bool prisilno =
        webPosluzitelj.hasArg("force") &&
        webPosluzitelj.arg("force") == "1";
    posaljiJsonSuncevihPostavki(prisilno);
  });
  webPosluzitelj.on("/api/settings/sunce", HTTP_POST, []() {
    if (!osigurajWebAutorizaciju()) {
      return;
    }

    const char* obaveznaPolja[] = {"ju", "jb", "jo", "pu", "pb", "vu", "vb", "vo", "nr"};
    for (size_t i = 0; i < (sizeof(obaveznaPolja) / sizeof(obaveznaPolja[0])); ++i) {
      if (!webPosluzitelj.hasArg(obaveznaPolja[i])) {
        webPosluzitelj.send(400, "text/plain", "Nedostaje jedno ili vise polja suncevih postavki");
        return;
      }
    }

    const String juArg = ocistiJednolinijskiTekst(webPosluzitelj.arg("ju"), 1);
    const String jbArg = ocistiJednolinijskiTekst(webPosluzitelj.arg("jb"), 1);
    const String joArg = ocistiJednolinijskiTekst(webPosluzitelj.arg("jo"), 3);
    const String puArg = ocistiJednolinijskiTekst(webPosluzitelj.arg("pu"), 1);
    const String pbArg = ocistiJednolinijskiTekst(webPosluzitelj.arg("pb"), 1);
    const String vuArg = ocistiJednolinijskiTekst(webPosluzitelj.arg("vu"), 1);
    const String vbArg = ocistiJednolinijskiTekst(webPosluzitelj.arg("vb"), 1);
    const String voArg = ocistiJednolinijskiTekst(webPosluzitelj.arg("vo"), 3);
    const String nrArg = ocistiJednolinijskiTekst(webPosluzitelj.arg("nr"), 1);

    const bool boolPoljaValjana =
        ((juArg == "0" || juArg == "1") &&
         (puArg == "0" || puArg == "1") &&
         (vuArg == "0" || vuArg == "1") &&
         (nrArg == "0" || nrArg == "1"));
    const bool brojPoljaValjana =
        jeDecimalniBrojString(jbArg) &&
        jeDecimalniBrojString(pbArg) &&
        jeDecimalniBrojString(vbArg) &&
        jeDecimalniBrojString(joArg.startsWith("-") ? joArg.substring(1) : joArg) &&
        jeDecimalniBrojString(voArg.startsWith("-") ? voArg.substring(1) : voArg);

    if (!(boolPoljaValjana && brojPoljaValjana)) {
      webPosluzitelj.send(422, "text/plain", "Sunceve postavke moraju imati valjane toggle i brojcane vrijednosti");
      return;
    }

    const long jb = jbArg.toInt();
    const long jo = joArg.toInt();
    const long pb = pbArg.toInt();
    const long vb = vbArg.toInt();
    const long vo = voArg.toInt();
    const bool odgodaJutroValjana =
        (jo == -30L || jo == -20L || jo == -10L || jo == 0L || jo == 10L || jo == 20L || jo == 30L);
    const bool odgodaVecerValjana =
        (vo == -30L || vo == -20L || vo == -10L || vo == 0L || vo == 10L || vo == 20L || vo == 30L);

    if (!((jb == 1L || jb == 2L) &&
          (pb == 1L || pb == 2L) &&
          (vb == 1L || vb == 2L) &&
          odgodaJutroValjana &&
          odgodaVecerValjana)) {
      webPosluzitelj.send(422, "text/plain", "Sunce podrzava zvono 1 ili 2 te odgode od -30 do +30 minuta u koraku 10");
      return;
    }

    const PostavkeOdgovorMegai status = posaljiSuncevePostavkeMegai(
        juArg == "1",
        static_cast<unsigned int>(jb),
        static_cast<int>(jo),
        puArg == "1",
        static_cast<unsigned int>(pb),
        vuArg == "1",
        static_cast<unsigned int>(vb),
        static_cast<int>(vo),
        nrArg == "1",
        CMD_CEKANJE_NA_MEGU_MS);

    if (status == POSTAVKE_ODGOVOR_OK) {
      osvjeziSuncevePostavkeMegai(true);
      webPosluzitelj.send(200, "text/plain", "Sunceve postavke su spremljene na Megi.");
      return;
    }

    if (status == POSTAVKE_ODGOVOR_ERR) {
      webPosluzitelj.send(422, "text/plain", "Mega je odbila sunceve postavke.");
      return;
    }

    webPosluzitelj.send(504, "text/plain", "Mega nije potvrdila spremanje suncevih postavki.");
  });

  Serial.println("WEB: Registriram /api/settings/blagdani rutu");
  webPosluzitelj.on("/api/settings/blagdani", HTTP_GET, []() {
    if (!osigurajWebAutorizaciju()) {
      return;
    }
    const bool prisilno =
        webPosluzitelj.hasArg("force") &&
        webPosluzitelj.arg("force") == "1";
    posaljiJsonBlagdanskihPostavki(prisilno);
  });
  webPosluzitelj.on("/api/settings/blagdani", HTTP_POST, []() {
    if (!osigurajWebAutorizaciju()) {
      return;
    }

    auto procitajVrijemeHHMM = [](const String& ulaz, uint8_t& sat, uint8_t& minuta, bool& prazno) -> bool {
      const String ocisceno = ocistiJednolinijskiTekst(ulaz, 8);
      prazno = (ocisceno.length() == 0);
      if (prazno) {
        sat = 0;
        minuta = 0;
        return true;
      }

      const int pozicijaDvotocke = ocisceno.indexOf(':');
      if (pozicijaDvotocke <= 0 || pozicijaDvotocke >= (ocisceno.length() - 1)) {
        return false;
      }

      const String satTekst = ocisceno.substring(0, pozicijaDvotocke);
      const String minutaTekst = ocisceno.substring(pozicijaDvotocke + 1);
      if (satTekst.length() > 2 || minutaTekst.length() != 2) {
        return false;
      }
      if (!jeDecimalniBrojString(satTekst) || !jeDecimalniBrojString(minutaTekst)) {
        return false;
      }

      const long satVrijednost = satTekst.toInt();
      const long minutaVrijednost = minutaTekst.toInt();
      if (satVrijednost < 0L || satVrijednost > 23L ||
          minutaVrijednost < 0L || minutaVrijednost > 59L) {
        return false;
      }

      sat = static_cast<uint8_t>(satVrijednost);
      minuta = static_cast<uint8_t>(minutaVrijednost);
      return true;
    };

    MegaBlagdanskePostavke novePostavke = {};
    const String rdEnArg = ocistiJednolinijskiTekst(webPosluzitelj.arg("rd_en"), 1);
    const String rdTmArg = webPosluzitelj.arg("rd_tm");
    if (!(rdEnArg == "0" || rdEnArg == "1")) {
      webPosluzitelj.send(422, "text/plain", "Dnevna misa mora imati valjan ON/OFF status");
      return;
    }
    uint8_t rdSat = 0;
    uint8_t rdMinuta = 0;
    bool rdPrazno = false;
    if (!procitajVrijemeHHMM(rdTmArg, rdSat, rdMinuta, rdPrazno)) {
      webPosluzitelj.send(422, "text/plain", "Dnevna misa mora imati valjano vrijeme mise u formatu HH:MM");
      return;
    }
    novePostavke.dnevnaMisaOmogucena = (rdEnArg == "1") && !rdPrazno;
    novePostavke.dnevnaMisaSat = rdSat;
    novePostavke.dnevnaMisaMinuta = rdMinuta;

    const String ndEnArg = ocistiJednolinijskiTekst(webPosluzitelj.arg("nd_en"), 1);
    const String ndTmArg = webPosluzitelj.arg("nd_tm");
    if (!(ndEnArg == "0" || ndEnArg == "1")) {
      webPosluzitelj.send(422, "text/plain", "Nedjeljna misa mora imati valjan ON/OFF status");
      return;
    }
    uint8_t ndSat = 0;
    uint8_t ndMinuta = 0;
    bool ndPrazno = false;
    if (!procitajVrijemeHHMM(ndTmArg, ndSat, ndMinuta, ndPrazno)) {
      webPosluzitelj.send(422, "text/plain", "Nedjeljna misa mora imati valjano vrijeme mise u formatu HH:MM");
      return;
    }
    novePostavke.nedjeljnaMisaOmogucena = (ndEnArg == "1") && !ndPrazno;
    novePostavke.nedjeljnaMisaSat = ndSat;
    novePostavke.nedjeljnaMisaMinuta = ndMinuta;

    for (uint8_t i = 0; i < BROJ_NEPOMICNIH_BLAGDANA; ++i) {
      const String prefiks = "f" + String(i) + "_";
      const String enArg = ocistiJednolinijskiTekst(webPosluzitelj.arg(prefiks + "en"), 1);
      const String tmArg = webPosluzitelj.arg(prefiks + "tm");

      if (!webPosluzitelj.hasArg(prefiks + "en") ||
          !webPosluzitelj.hasArg(prefiks + "tm")) {
        webPosluzitelj.send(400, "text/plain", "Nedostaje jedno ili vise polja nepomicnih blagdana");
        return;
      }

      uint8_t sat = 0;
      uint8_t minuta = 0;
      bool prazno = false;
      if (!((enArg == "0" || enArg == "1") &&
            procitajVrijemeHHMM(tmArg, sat, minuta, prazno))) {
        webPosluzitelj.send(422, "text/plain", "Nepomicni blagdani moraju imati valjano vrijeme mise u formatu HH:MM");
        return;
      }

      novePostavke.nepomicni[i].omogucen = (enArg == "1") && !prazno;
      novePostavke.nepomicni[i].satMise = sat;
      novePostavke.nepomicni[i].minutaMise = minuta;
    }

    for (uint8_t i = 0; i < BROJ_POMICNIH_BLAGDANA; ++i) {
      const String prefiks = "p" + String(i) + "_";
      const String enArg = ocistiJednolinijskiTekst(webPosluzitelj.arg(prefiks + "en"), 1);
      const String tmArg = webPosluzitelj.arg(prefiks + "tm");

      if (!webPosluzitelj.hasArg(prefiks + "en") ||
          !webPosluzitelj.hasArg(prefiks + "tm")) {
        webPosluzitelj.send(400, "text/plain", "Nedostaje jedno ili vise polja pomicnih blagdana");
        return;
      }

      uint8_t sat = 0;
      uint8_t minuta = 0;
      bool prazno = false;
      if (!((enArg == "0" || enArg == "1") &&
            procitajVrijemeHHMM(tmArg, sat, minuta, prazno))) {
        webPosluzitelj.send(422, "text/plain", "Pomicni blagdani moraju imati valjano vrijeme mise u formatu HH:MM");
        return;
      }

      novePostavke.pomicni[i].omogucen = (enArg == "1") && !prazno;
      novePostavke.pomicni[i].satMise = sat;
      novePostavke.pomicni[i].minutaMise = minuta;
    }

    PostavkeOdgovorMegai status = posaljiMisePostavkeMegai(
        novePostavke.dnevnaMisaOmogucena,
        novePostavke.dnevnaMisaSat,
        novePostavke.dnevnaMisaMinuta,
        novePostavke.nedjeljnaMisaOmogucena,
        novePostavke.nedjeljnaMisaSat,
        novePostavke.nedjeljnaMisaMinuta,
        CMD_CEKANJE_NA_MEGU_MS);
    if (status != POSTAVKE_ODGOVOR_OK) {
      webPosluzitelj.send(status == POSTAVKE_ODGOVOR_ERR ? 422 : 504,
                          "text/plain",
                          status == POSTAVKE_ODGOVOR_ERR
                              ? "Mega je odbila misne postavke."
                              : "Mega nije potvrdila spremanje misnih postavki.");
      return;
    }

    status = posaljiNepomicneBlagdaneMegai(
        novePostavke.nepomicni,
        BROJ_NEPOMICNIH_BLAGDANA,
        CMD_CEKANJE_NA_MEGU_MS);
    if (status != POSTAVKE_ODGOVOR_OK) {
      webPosluzitelj.send(status == POSTAVKE_ODGOVOR_ERR ? 422 : 504,
                          "text/plain",
                          status == POSTAVKE_ODGOVOR_ERR
                              ? "Mega je odbila nepomicne blagdane."
                              : "Mega nije potvrdila spremanje nepomicnih blagdana.");
      return;
    }

    status = posaljiPomicneBlagdaneMegai(
        novePostavke.pomicni,
        BROJ_POMICNIH_BLAGDANA,
        CMD_CEKANJE_NA_MEGU_MS);
    if (status != POSTAVKE_ODGOVOR_OK) {
      webPosluzitelj.send(status == POSTAVKE_ODGOVOR_ERR ? 422 : 504,
                          "text/plain",
                          status == POSTAVKE_ODGOVOR_ERR
                              ? "Mega je odbila pomicne blagdane."
                              : "Mega nije potvrdila spremanje pomicnih blagdana.");
      return;
    }

    osvjeziMisePostavkeMegai(true);
    osvjeziNepomicneBlagdaneMegai(true);
    osvjeziPomicneBlagdaneMegai(true);
    webPosluzitelj.send(200, "text/plain", "Misne i blagdanske postavke su spremljene na Megi.");
  });

  Serial.println("WEB: Registriram API rute");
  webPosluzitelj.on("/api/bell1/on", HTTP_GET, []() {
    if (!osigurajWebAutorizaciju()) {
      return;
    }
    posaljiApiKomanduMegai("ZVONO1_ON", "BELL1 ukljucen");
  });
  webPosluzitelj.on("/api/bell1/off", HTTP_GET, []() {
    if (!osigurajWebAutorizaciju()) {
      return;
    }
    posaljiApiKomanduMegai("ZVONO1_OFF", "BELL1 iskljucen");
  });
  webPosluzitelj.on("/api/bell2/on", HTTP_GET, []() {
    if (!osigurajWebAutorizaciju()) {
      return;
    }
    posaljiApiKomanduMegai("ZVONO2_ON", "BELL2 ukljucen");
  });
  webPosluzitelj.on("/api/bell2/off", HTTP_GET, []() {
    if (!osigurajWebAutorizaciju()) {
      return;
    }
    posaljiApiKomanduMegai("ZVONO2_OFF", "BELL2 iskljucen");
  });
  webPosluzitelj.on("/api/slavljenje/on", HTTP_GET, []() {
    if (!osigurajWebAutorizaciju()) {
      return;
    }
    posaljiApiKomanduMegai("SLAVLJENJE_ON", "SLAVLJENJE ukljuceno");
  });
  webPosluzitelj.on("/api/slavljenje/off", HTTP_GET, []() {
    if (!osigurajWebAutorizaciju()) {
      return;
    }
    posaljiApiKomanduMegai("SLAVLJENJE_OFF", "SLAVLJENJE iskljuceno");
  });
  webPosluzitelj.on("/api/mrtvacko/on", HTTP_GET, []() {
    if (!osigurajWebAutorizaciju()) {
      return;
    }
    posaljiApiKomanduMegai("MRTVACKO_ON", "MRTVACKO ukljuceno");
  });
  webPosluzitelj.on("/api/mrtvacko/off", HTTP_GET, []() {
    if (!osigurajWebAutorizaciju()) {
      return;
    }
    posaljiApiKomanduMegai("MRTVACKO_OFF", "MRTVACKO iskljuceno");
  });
  webPosluzitelj.on("/api/pokojnik", HTTP_GET, []() {
    if (!osigurajWebAutorizaciju()) {
      return;
    }
    posaljiApiKomanduMegai("POKOJNIK", "POKOJNIK zahtjev prihvacen");
  });
  webPosluzitelj.on("/api/pokojnica", HTTP_GET, []() {
    if (!osigurajWebAutorizaciju()) {
      return;
    }
    posaljiApiKomanduMegai("POKOJNICA", "POKOJNICA zahtjev prihvacen");
  });
  webPosluzitelj.on("/api/solar/morning/on", HTTP_GET, []() {
    if (!osigurajWebAutorizaciju()) {
      return;
    }
    posaljiApiKomanduMegai("SUNCE_JUTRO_ON", "Jutarnja sunceva automatika ukljucena");
  });
  webPosluzitelj.on("/api/solar/morning/off", HTTP_GET, []() {
    if (!osigurajWebAutorizaciju()) {
      return;
    }
    posaljiApiKomanduMegai("SUNCE_JUTRO_OFF", "Jutarnja sunceva automatika iskljucena");
  });
  webPosluzitelj.on("/api/solar/noon/on", HTTP_GET, []() {
    if (!osigurajWebAutorizaciju()) {
      return;
    }
    posaljiApiKomanduMegai("SUNCE_PODNE_ON", "Podnevna sunceva automatika ukljucena");
  });
  webPosluzitelj.on("/api/solar/noon/off", HTTP_GET, []() {
    if (!osigurajWebAutorizaciju()) {
      return;
    }
    posaljiApiKomanduMegai("SUNCE_PODNE_OFF", "Podnevna sunceva automatika iskljucena");
  });
  webPosluzitelj.on("/api/solar/evening/on", HTTP_GET, []() {
    if (!osigurajWebAutorizaciju()) {
      return;
    }
    posaljiApiKomanduMegai("SUNCE_VECER_ON", "Vecernja sunceva automatika ukljucena");
  });
  webPosluzitelj.on("/api/solar/evening/off", HTTP_GET, []() {
    if (!osigurajWebAutorizaciju()) {
      return;
    }
    posaljiApiKomanduMegai("SUNCE_VECER_OFF", "Vecernja sunceva automatika iskljucena");
  });
  webPosluzitelj.on("/api/quiet/on", HTTP_GET, []() {
    if (!osigurajWebAutorizaciju()) {
      return;
    }
    posaljiApiKomanduMegai("TIHI_ON", "Tihi mod ukljucen");
  });
  webPosluzitelj.on("/api/quiet/off", HTTP_GET, []() {
    if (!osigurajWebAutorizaciju()) {
      return;
    }
    posaljiApiKomanduMegai("TIHI_OFF", "Tihi mod iskljucen");
  });

  webPosluzitelj.onNotFound([]() {
    Serial.println("WEB: 404 - ruta ne postoji");
    webPosluzitelj.send(404, "text/plain", "Ruta ne postoji");
  });

  webPosluzitelj.begin();
  Serial.println("WEB: posluzitelj pokrenut na portu 80");
}

