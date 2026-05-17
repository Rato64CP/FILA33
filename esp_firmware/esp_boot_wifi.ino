// esp_boot_wifi.ino - boot, WiFi i runtime petlja ESP32 mreznog mosta toranjskog sata

static void resetirajNtpUdpSloj() {
  if (ntpUdpPokrenut) {
    ntpUDP.stop();
    ntpUdpPokrenut = false;
  }
}

static void resetirajNtpStanje() {
  resetirajNtpUdpSloj();
  ntpIkadPostavljen = false;
  ntpZahtjevUTijeku = false;
  ntpPrviUzorakTrebaPotvrdu = true;
  ntpPrviUzorakZapamcen = false;
  ntpZadnjiUspjehMs = 0;
  ntpZadnjiPokusajMs = 0;
  ntpZahtjevPoslanMs = 0;
  ntpBazniMillis = 0;
  ntpPrviUzorakPrimljenMs = 0;
  ntpBazniUtcMs = 0;
  ntpPrviUzorakUtcMs = 0ULL;
}

static void oznaciWiFiKaoOdspojen() {
  wifiSpojenOdMs = 0;
  wifiSpojenOdPoznat = false;
}

static void oznaciWiFiKaoSpojen(unsigned long sadaMs) {
  if (!wifiSpojenOdPoznat) {
    wifiSpojenOdMs = sadaMs;
    wifiSpojenOdPoznat = true;
  }
}

static void inicijalizirajSerijskiPortPremaMegi() {
  // Na ESP32 runtime preusmjeravamo Serial na zasebne UART pinove prema Megi.
  // Time USB ostaje koristan za upload, a komunikacija toranjskog sata ide preko 16/17.
  Serial.begin(9600, SERIAL_8N1, ESP_MEGA_RX_PIN, ESP_MEGA_TX_PIN);
  delay(200);
}

static void postaviDhcpKonfiguraciju() {
  const IPAddress praznaAdresa(0U, 0U, 0U, 0U);
  WiFi.config(praznaAdresa, praznaAdresa, praznaAdresa);
}

void setup() {
  inicijalizirajSerijskiPortPremaMegi();
  pinMode(WIFI_SETUP_PIN, INPUT_PULLUP);
  pinMode(WIFI_STATUS_LED_PIN, OUTPUT);
  digitalWrite(WIFI_STATUS_LED_PIN, LOW);
  EEPROM.begin(ESP_EEPROM_VELICINA);
  ucitajWebAutentikaciju();

  Serial.println("ESP BOOT");
  Serial.println("CFGREQ");
  Serial.println("FAZA: Povezivanje na WiFi");

  pokupiWifiKonfiguracijuIzSerijske();
  if (wifiOmogucen && (!primljenaWifiKonfiguracija || WiFi.status() != WL_CONNECTED)) {
    poveziNaWiFi();
  } else if (!wifiOmogucen) {
    prijaviPromjenuWiFiStatusa();
  }

  Serial.println("FAZA: Priprema NTP UDP sloja");

  Serial.println("FAZA: Konfiguracija web posluzitelja");
  konfigurirajWebPosluzitelj();

  Serial.println("CFGREQ");
  Serial.println("FAZA: INIT zavrsen, ulazak u loop()");
}

void loop() {
  if (otaRestartZakazan) {
    obradiZakazaniRestartNakonOta();
  }

  if (otaAzuriranjeUTijeku) {
    webPosluzitelj.handleClient();
    yield();
    return;
  }

  obradiSerijskiUlaz();
  odrzavajSetupTipku();
  odrzavajSetupPristupnuTocku();
  osvjeziWiFiStatusLedicu();

  if (wifiOmogucen) {
    if (WiFi.status() != WL_CONNECTED) {
      poveziNaWiFi();
    }

    osvjeziNTPSat();
    odrzavajWiFiWatchdogZaNTP();
  }

  webPosluzitelj.handleClient();
  yield();
}

void posaljiWiFiLcdSazetak() {
  if (!wifiOmogucen || WiFi.status() != WL_CONNECTED) {
    return;
  }

  IPAddress ip = WiFi.localIP();
  long rssiDbm = WiFi.RSSI();
  if (rssiDbm < -99) {
    rssiDbm = -99;
  }
  if (rssiDbm > 0) {
    rssiDbm = 0;
  }

  char sazetak[17];
  snprintf(sazetak,
           sizeof(sazetak),
           ".%u.%u RSSI%ld",
           static_cast<unsigned>(ip[2]),
           static_cast<unsigned>(ip[3]),
           rssiDbm);
  Serial.print("WIFI:LCD:");
  Serial.println(sazetak);
}

void prijaviPromjenuWiFiStatusa() {
  wl_status_t trenutniStatus = wifiOmogucen ? WiFi.status() : WL_DISCONNECTED;
  if (trenutniStatus == zadnjiPrijavljeniWiFiStatus) {
    return;
  }

  zadnjiPrijavljeniWiFiStatus = trenutniStatus;
  if (trenutniStatus == WL_CONNECTED) {
    Serial.println("WIFI:CONNECTED");
    Serial.print("WIFI:LOCAL_IP:");
    Serial.println(WiFi.localIP().toString());
    posaljiWiFiLcdSazetak();
    Serial.print("WIFI:MAC:");
    Serial.println(WiFi.macAddress());
  } else {
    resetirajNtpStanje();
    Serial.println("WIFI:DISCONNECTED");
  }
}

unsigned long dohvatiWiFiOdgoduNovogPokusaja() {
  if (wifiBrojPokusajaZaredom <= 1) {
    return WIFI_ODGODA_NAKON_PRVOG_MS;
  }

  if (wifiBrojPokusajaZaredom == 2) {
    return WIFI_ODGODA_NAKON_DRUGOG_MS;
  }

  return WIFI_ODGODA_NAKON_TRECEG_MS;
}

void primijeniWiFiOmogucenost(bool omogucen) {
  if (wifiOmogucen == omogucen) {
    if (wifiOmogucen && WiFi.status() != WL_CONNECTED) {
      poveziNaWiFi();
    }
    return;
  }

  wifiOmogucen = omogucen;
  wifiPokusajUToku = false;
  wifiPokusajPocetak = 0;
  wifiSljedeciPokusajDozvoljen = 0;
  wifiBrojPokusajaZaredom = 0;
  resetirajNtpStanje();
  oznaciWiFiKaoOdspojen();

  if (!wifiOmogucen) {
    WiFi.disconnect();
    delay(1);
    WiFi.mode(WIFI_OFF);
    prijaviPromjenuWiFiStatusa();
    Serial.println("WIFI RX: WiFi onemogucen");
    return;
  }

  WiFi.mode(WIFI_STA);
  prijaviPromjenuWiFiStatusa();
  Serial.println("WIFI RX: WiFi omogucen");
  poveziNaWiFi();
}

void poveziNaWiFi() {
  unsigned long sada = millis();

  if (!wifiOmogucen) {
    wifiPokusajUToku = false;
    oznaciWiFiKaoOdspojen();
    prijaviPromjenuWiFiStatusa();
    return;
  }

  if (WiFi.status() == WL_CONNECTED) {
    oznaciWiFiKaoSpojen(sada);
    if (wifiPokusajUToku) {
      Serial.println();
      Serial.print("WIFI: Spojen, IP: ");
      Serial.println(WiFi.localIP());
    }
    wifiPokusajUToku = false;
    wifiBrojPokusajaZaredom = 0;
    prijaviPromjenuWiFiStatusa();
    return;
  }

  oznaciWiFiKaoOdspojen();
  prijaviPromjenuWiFiStatusa();

  if (!wifiPokusajUToku && sada >= wifiSljedeciPokusajDozvoljen) {
    WiFi.mode(setupApAktivan ? WIFI_AP_STA : WIFI_STA);

    if (koristiDhcp) {
      postaviDhcpKonfiguraciju();
      Serial.println("WIFI: DHCP konfiguracija aktivna");
    } else if (!postaviStatickuKonfiguraciju()) {
      Serial.println("WIFI: Staticka konfiguracija neispravna, prelazim na DHCP");
      koristiDhcp = true;
      postaviDhcpKonfiguraciju();
    }

    Serial.print("WIFI: Spajam se na SSID: ");
    Serial.println(wifiSsid);

    WiFi.begin(wifiSsid, wifiLozinka);
    wifiPokusajUToku = true;
    wifiPokusajPocetak = sada;
    wifiBrojPokusajaZaredom++;
  }

  if (wifiPokusajUToku && (sada - wifiPokusajPocetak >= WIFI_POKUSAJ_TIMEOUT_MS)) {
    Serial.println();
    Serial.println("WIFI: Timeout pokusaja, odspajam i cekam prije novog pokusaja");
    WiFi.disconnect();
    oznaciWiFiKaoOdspojen();
    resetirajNtpStanje();
    wifiPokusajUToku = false;
    prijaviPromjenuWiFiStatusa();

    unsigned long odgoda = dohvatiWiFiOdgoduNovogPokusaja();
    wifiSljedeciPokusajDozvoljen = sada + odgoda;
    Serial.print("WIFI: Novi pokusaj za ");
    Serial.print(odgoda / 1000UL);
    Serial.println(" s");
  }
}

bool postaviStatickuKonfiguraciju() {
  IPAddress ip;
  IPAddress maska;
  IPAddress gateway;

  if (!ip.fromString(statickaIp) || !maska.fromString(mreznaMaska) || !gateway.fromString(zadaniGateway)) {
    Serial.println("WIFI: Neispravan format staticke IP konfiguracije");
    return false;
  }

  bool uspjeh = WiFi.config(ip, gateway, maska);
  if (!uspjeh) {
    Serial.println("WIFI: ESP nije prihvatio staticku konfiguraciju");
  }
  return uspjeh;
}

void pokupiWifiKonfiguracijuIzSerijske(unsigned long millisTimeout) {
  unsigned long pocetak = millis();
  while (millis() - pocetak < millisTimeout) {
    obradiSerijskiUlaz();
    if (primljenaWifiKonfiguracija) {
      Serial.println("WIFI RX: konfiguracija primljena prije spajanja");
      return;
    }
    delay(10);
  }
}

static bool jeNtpVrijemeSvjezeZaMegu(unsigned long sadaMs) {
  return ntpZadnjiUspjehMs != 0UL &&
         (sadaMs - ntpZadnjiUspjehMs) <= NTP_MAKSIMALNA_STAROST_ODGOVORA_MS;
}

static bool jeNtpVrijemeStabilizirano() {
  return !ntpPrviUzorakTrebaPotvrdu || ntpIkadPostavljen;
}

static bool prihvatiNtpUzorakZaToranjskiSat(uint64_t utcMs) {
  if (!ntpPrviUzorakTrebaPotvrdu) {
    postaviNtpBaznoVrijemeUtcMs(utcMs);
    ntpIkadPostavljen = true;
    ntpZadnjiUspjehMs = millis();
    return true;
  }

  if (!ntpPrviUzorakZapamcen) {
    ntpPrviUzorakZapamcen = true;
    ntpPrviUzorakUtcMs = utcMs;
    ntpPrviUzorakPrimljenMs = millis();
    Serial.println("NTPLOG: prvi NTP uzorak nakon restarta spremljen, cekam potvrdu drugim uzorkom");
    return false;
  }

  const unsigned long sadaMs = millis();
  const unsigned long ocekivaniProtekMs =
      (ntpPrviUzorakPrimljenMs != 0UL) ? (sadaMs - ntpPrviUzorakPrimljenMs) : 0UL;
  uint64_t razlikaMs = 0ULL;
  if (utcMs >= ntpPrviUzorakUtcMs) {
    razlikaMs = utcMs - ntpPrviUzorakUtcMs;
  } else {
    razlikaMs = ntpPrviUzorakUtcMs - utcMs;
  }

  uint64_t odstupanjeOdOcekivanogMs = 0ULL;
  if (razlikaMs >= static_cast<uint64_t>(ocekivaniProtekMs)) {
    odstupanjeOdOcekivanogMs = razlikaMs - static_cast<uint64_t>(ocekivaniProtekMs);
  } else {
    odstupanjeOdOcekivanogMs = static_cast<uint64_t>(ocekivaniProtekMs) - razlikaMs;
  }

  if (odstupanjeOdOcekivanogMs > NTP_MAKS_DOPUSTENO_ODSTUPANJE_PRVA_DVA_UZORKA_MS) {
    ntpPrviUzorakUtcMs = utcMs;
    ntpPrviUzorakPrimljenMs = sadaMs;
    Serial.print("NTPLOG: drugi NTP uzorak previse odstupa od ocekivanog protoka, ponavljam potvrdu, odstupanje_ms=");
    Serial.println(static_cast<unsigned long>(odstupanjeOdOcekivanogMs));
    return false;
  }

  ntpPrviUzorakTrebaPotvrdu = false;
  ntpPrviUzorakZapamcen = false;
  ntpPrviUzorakPrimljenMs = 0;
  ntpPrviUzorakUtcMs = 0ULL;
  postaviNtpBaznoVrijemeUtcMs(utcMs);
  ntpIkadPostavljen = true;
  ntpZadnjiUspjehMs = millis();
  Serial.println("NTPLOG: prvi NTP uzorak potvrden drugim uzorkom");
  return true;
}

bool osvjeziNTPSat(bool prisilno) {
  static unsigned long zadnjiLog = 0;
  unsigned long sada = millis();

  if (WiFi.status() != WL_CONNECTED) {
    ntpZahtjevUTijeku = false;
    return jeNtpVrijemePostavljeno();
  }

  bool promjena = obradiNtpOdgovor();
  obradiNtpTimeout();

  sada = millis();
  if (trebaPokrenutiNtpOsvjezavanje(sada, prisilno)) {
    posaljiNtpUpit();
  }

  if (prisilno && ntpZahtjevUTijeku) {
    const unsigned long pocetakCekanja = millis();
    while (ntpZahtjevUTijeku &&
           (millis() - pocetakCekanja) <= (NTP_TIMEOUT_MS + 250UL)) {
      if (obradiNtpOdgovor()) {
        promjena = true;
        break;
      }
      obradiNtpTimeout();
      yield();
      delay(1);
    }
    obradiNtpTimeout();
    sada = millis();
  }

  if (prisilno && !jeNtpVrijemePostavljeno() &&
      ntpPrviUzorakTrebaPotvrdu && ntpPrviUzorakZapamcen && !ntpZahtjevUTijeku) {
    Serial.println("NTPLOG: prvi uzorak je spremljen, odmah trazim drugi radi stabilizacije");
    if (posaljiNtpUpit()) {
      const unsigned long pocetakDrugogCekanja = millis();
      while (ntpZahtjevUTijeku &&
             (millis() - pocetakDrugogCekanja) <= (NTP_TIMEOUT_MS + 250UL)) {
        if (obradiNtpOdgovor()) {
          promjena = true;
          break;
        }
        obradiNtpTimeout();
        yield();
        delay(1);
      }
      obradiNtpTimeout();
      sada = millis();
    }
  }

  if (!promjena && !jeNtpVrijemePostavljeno() &&
      (sada - zadnjiLog > NTP_PONOVNI_POKUSAJ_BEZ_VREMENA_MS)) {
    Serial.println("NTPLOG: jos nije postavljeno vrijeme, cekam...");
    zadnjiLog = sada;
  }

  return jeNtpVrijemePostavljeno();
}

void odrzavajWiFiWatchdogZaNTP() {
  if (!wifiOmogucen || setupApAktivan || WiFi.status() != WL_CONNECTED) {
    return;
  }

  const unsigned long sada = millis();
  oznaciWiFiKaoSpojen(sada);

  const unsigned long referentnoMs =
      (ntpZadnjiUspjehMs != 0UL) ? ntpZadnjiUspjehMs : wifiSpojenOdMs;

  if ((sada - referentnoMs) < WIFI_WATCHDOG_NTP_ZASTOJ_MS) {
    return;
  }

  Serial.println("WIFI WATCHDOG: NTP nije uspio 2 sata, resetiram WiFi vezu");
  WiFi.disconnect();
  oznaciWiFiKaoOdspojen();
  resetirajNtpStanje();
  wifiPokusajUToku = false;
  wifiPokusajPocetak = 0;
  wifiSljedeciPokusajDozvoljen = 0;
  wifiBrojPokusajaZaredom = 0;
  prijaviPromjenuWiFiStatusa();
}

void posaljiNTPPremaMegai() {
  if (!jeNtpVrijemePostavljeno()) {
    Serial.println("NTPLOG: nema spremljenog NTP vremena za slanje Megi");
    return;
  }

  const uint64_t utcMs = dohvatiNtpUtcMs();
  const uint32_t utcEpoch = static_cast<uint32_t>(utcMs / 1000ULL);
  const uint16_t lokalneMilisekunde = static_cast<uint16_t>(utcMs % 1000ULL);
  uint32_t lokalniEpoch = konvertirajUTCuLokalnoVrijeme(utcEpoch);
  bool dstAktivan = jeLjetnoVrijemeEU(utcEpoch);
  RastavljenoVrijeme lokalnoVrijeme{};
  if (!razloziUnixVrijemeUTC(lokalniEpoch, &lokalnoVrijeme)) {
    Serial.println("NTPLOG: konverzija lokalnog vremena nije uspjela, preskacem slanje");
    return;
  }

  char isoBuffer[29];
  char osnovniIsoBuffer[25];
  formatirajIsoDatumIVrijeme(lokalnoVrijeme, osnovniIsoBuffer, sizeof(osnovniIsoBuffer));
  snprintf(isoBuffer,
           sizeof(isoBuffer),
           "%s.%03u",
           osnovniIsoBuffer,
           static_cast<unsigned>(lokalneMilisekunde));

  Serial.print("SLANJE: NTP linija prema Megi: ");
  Serial.println(isoBuffer);

  Serial.print("NTP:");
  Serial.print(isoBuffer);
  Serial.print(";DST=");
  Serial.println(dstAktivan ? '1' : '0');
}


void pokreniSetupPristupnuTocku() {
  if (setupApAktivan) {
    setupApPokrenutMs = millis();
    setupApZakazanoGasenjeMs = 0;
    return;
  }

  WiFi.mode(wifiOmogucen ? WIFI_AP_STA : WIFI_AP);
  if (!WiFi.softAP(WIFI_SETUP_AP_SSID, WIFI_SETUP_AP_LOZINKA)) {
    Serial.println("WIFI: Setup AP nije uspio");
    return;
  }

  setupApAktivan = true;
  setupApPokrenutMs = millis();
  setupApZakazanoGasenjeMs = 0;

  Serial.print("WIFI: Setup AP aktivan SSID=");
  Serial.print(WIFI_SETUP_AP_SSID);
  Serial.print(" IP=");
  Serial.println(WiFi.softAPIP().toString());
}

void zaustaviSetupPristupnuTocku(bool zbogTimeouta) {
  if (!setupApAktivan) {
    return;
  }

  WiFi.softAPdisconnect(true);
  setupApAktivan = false;
  setupApPokrenutMs = 0;
  setupApZakazanoGasenjeMs = 0;
  WiFi.mode(wifiOmogucen ? WIFI_STA : WIFI_OFF);

  Serial.println(zbogTimeouta
                     ? "WIFI: Setup AP ugasen nakon isteka vremena"
                     : "WIFI: Setup AP ugasen nakon spremanja postavki");
}

void odrzavajSetupTipku() {
  const bool pritisnuta = digitalRead(WIFI_SETUP_PIN) == LOW;

  if (!pritisnuta) {
    setupTipkaBilaPritisnuta = false;
    setupTipkaPocetakMs = 0;
    return;
  }

  if (!setupTipkaBilaPritisnuta) {
    setupTipkaBilaPritisnuta = true;
    setupTipkaPocetakMs = millis();
    return;
  }

  if (setupTipkaPocetakMs != 0 &&
      (millis() - setupTipkaPocetakMs) >= WIFI_SETUP_DRZANJE_MS) {
    pokreniSetupPristupnuTocku();
    setupTipkaPocetakMs = 0;
  }
}

void odrzavajSetupPristupnuTocku() {
  if (!setupApAktivan) {
    return;
  }

  const unsigned long sada = millis();
  if (setupApZakazanoGasenjeMs != 0 && static_cast<long>(sada - setupApZakazanoGasenjeMs) >= 0) {
    zaustaviSetupPristupnuTocku(false);
    return;
  }

  if ((sada - setupApPokrenutMs) >= WIFI_SETUP_TRAJANJE_MS) {
    zaustaviSetupPristupnuTocku(true);
  }
}

void osvjeziWiFiStatusLedicu() {
  bool ukljuciLedicu = false;

  if (setupApAktivan) {
    ukljuciLedicu = ((millis() / WIFI_STATUS_LED_BLINK_MS) % 2UL) == 0;
  } else if (wifiOmogucen && WiFi.status() == WL_CONNECTED) {
    ukljuciLedicu = false;
  } else {
    ukljuciLedicu = true;
  }

  digitalWrite(WIFI_STATUS_LED_PIN, ukljuciLedicu ? HIGH : LOW);
}
