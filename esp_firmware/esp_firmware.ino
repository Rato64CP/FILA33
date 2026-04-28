#if defined(ESP8266)
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
using ToranjWebServer = ESP8266WebServer;
#elif defined(ESP32)
#include <WiFi.h>
#include <WebServer.h>
using ToranjWebServer = WebServer;
#else
#error "Ovaj firmware podrzava samo ESP8266 ili ESP32."
#endif

#include <WiFiUdp.h>
#include <NTPClient.h>
#include <EEPROM.h>
#include <pgmspace.h>
#include <time.h>

// Konfiguracija WiFi mreze za toranjski sat.
// Mega moze ove vrijednosti prepisati preko serijske veze naredbom WIFI:...
String wifiSsid = "SVETI PETAR";
String wifiLozinka = "cista2906";
bool koristiDhcp = true;
String statickaIp = "192.168.1.200";
String mreznaMaska = "255.255.255.0";
String zadaniGateway = "192.168.1.1";
bool primljenaWifiKonfiguracija = false;
bool wifiOmogucen = true;

// Vrijednosti za privremenu setup mrezu toranjskog sata.
// Pinovi ovise o odabranom ESP modulu.
static const char WIFI_SETUP_AP_SSID[] = "ZVONKO_setup";
static const char WIFI_SETUP_AP_LOZINKA[] = "zvonko10";
#if defined(ESP8266)
static const uint8_t WIFI_SETUP_PIN = 14;       // GPIO14 / NodeMCU D5
static const uint8_t WIFI_STATUS_LED_PIN = 12;  // GPIO12 / NodeMCU D6
#elif defined(ESP32)
static const uint8_t WIFI_SETUP_PIN = 27;       // Predlozeni tipkalo prema GND
static const uint8_t WIFI_STATUS_LED_PIN = 26;  // Predlozena status LED
static const int ESP_MEGA_RX_PIN = 16;          // ESP32 RX prema Mega TX1 preko djelitelja
static const int ESP_MEGA_TX_PIN = 17;          // ESP32 TX prema Mega RX1
#endif
static const unsigned long WIFI_SETUP_DRZANJE_MS = 4000UL;
static const unsigned long WIFI_SETUP_TRAJANJE_MS = 300000UL;
static const unsigned long WIFI_SETUP_GASENJE_NAKON_SPREMANJA_MS = 15000UL;
static const unsigned long WIFI_STATUS_LED_BLINK_MS = 400UL;

// Parametri NTP klijenta za sinkronizaciju toranjskog sata.
char ntpPosluzitelj[40] = "pool.ntp.org";
static const long NTP_OFFSET_SEKUNDI = 0;
static const unsigned long NTP_INTERVAL_MS = 60000;
static const unsigned long WIFI_WATCHDOG_NTP_ZASTOJ_MS = 7200000UL;
static const size_t SERIJSKI_BUFFER_MAX = 1280;
static const size_t SERIJSKI_BUDZET_BAJTOVA_PO_POZIVU = 192;
static const size_t WEB_LOZINKA_MAX = 33;
static const size_t ESP_EEPROM_VELICINA = 512;
static const uint16_t WEB_AUTH_POTPIS = 0x5741;
static const int ESP_EEPROM_ADRESA_WEB = 0;
static const unsigned long CMD_CEKANJE_NA_MEGU_MS = 1500UL;

WiFiUDP ntpUDP;
NTPClient ntpKlijent(ntpUDP, ntpPosluzitelj, NTP_OFFSET_SEKUNDI, NTP_INTERVAL_MS);

ToranjWebServer webPosluzitelj(80);

String webLozinka = "cista2906";

struct WebAuthConfig {
  uint16_t potpis;
  char lozinka[WEB_LOZINKA_MAX];
};

enum CmdOdgovorMegai {
  CMD_ODGOVOR_CEKA = 0,
  CMD_ODGOVOR_OK,
  CMD_ODGOVOR_BUSY,
  CMD_ODGOVOR_ERR,
  CMD_ODGOVOR_TIMEOUT
};

// Statusne zastavice za faze rada.
bool ntpIkadPostavljen = false;
unsigned long ntpZadnjiUspjehMs = 0;
unsigned long wifiSpojenOdMs = 0;
bool wifiSpojenOdPoznat = false;

// Upravljanje nenametljivim pokusajima WiFi spajanja.
bool wifiPokusajUToku = false;
unsigned long wifiPokusajPocetak = 0;
unsigned long wifiSljedeciPokusajDozvoljen = 0;
int wifiBrojPokusajaZaredom = 0;
static const unsigned long WIFI_POKUSAJ_TIMEOUT_MS = 20000;
static const unsigned long WIFI_ODGODA_NAKON_PRVOG_MS = 10000;
static const unsigned long WIFI_ODGODA_NAKON_DRUGOG_MS = 30000;
static const unsigned long WIFI_ODGODA_NAKON_TRECEG_MS = 60000;
wl_status_t zadnjiPrijavljeniWiFiStatus = WL_IDLE_STATUS;
bool odgovorSetupWiFiPrimljen = false;
bool setupWiFiNeuspjeh = false;
bool setupApAktivan = false;
bool setupTipkaBilaPritisnuta = false;
unsigned long setupTipkaPocetakMs = 0;
unsigned long setupApPokrenutMs = 0;
unsigned long setupApZakazanoGasenjeMs = 0;
CmdOdgovorMegai zadnjiCmdOdgovorMega = CMD_ODGOVOR_CEKA;

void poveziNaWiFi();
bool postaviStatickuKonfiguraciju();
void osvjeziNTPSat();
void odrzavajWiFiWatchdogZaNTP();
void posaljiNTPPremaMegai();
void obradiSerijskiUlaz();
void pokupiWifiKonfiguracijuIzSerijske(unsigned long millisTimeout = 3000);
void konfigurirajWebPosluzitelj();
void prijaviPromjenuWiFiStatusa();
unsigned long dohvatiWiFiOdgoduNovogPokusaja();
bool jePrijestupnaGodina(int godina);
int danUTjednu(int godina, int mjesec, int dan);
int zadnjaNedjeljaUMjesecu(int godina, int mjesec);
bool jeLjetnoVrijemeEU(time_t utcEpoch);
time_t konvertirajUTCuLokalnoVrijeme(time_t utcEpoch);
void obradiNTPSerijskuNaredbu(const char* payload);
String ocistiJednolinijskiTekst(const String &ulaz, size_t maxDuljina);
void primijeniWiFiOmogucenost(bool omogucen);
void posaljiJsonStatus();
void ucitajWebAutentikaciju();
bool osigurajWebAutorizaciju();
void posaljiApiKomanduMegai(const char* naredba, const char* odgovor);
CmdOdgovorMegai posaljiKomanduMegaiIPricekaj(const char* naredba, unsigned long timeoutMs);
bool posaljiSetupWiFiMegai(const String &ssid, const String &lozinka, String &odgovor, unsigned long timeoutMs);
void pokreniSetupPristupnuTocku();
void zaustaviSetupPristupnuTocku(bool zbogTimeouta);
void odrzavajSetupTipku();
void odrzavajSetupPristupnuTocku();
void osvjeziWiFiStatusLedicu();
void posaljiHtmlStranicuIzProgMema(PGM_P stranica);

static void resetirajNtpStanje() {
  ntpIkadPostavljen = false;
  ntpZadnjiUspjehMs = 0;
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
#if defined(ESP8266)
  Serial.begin(9600);
#elif defined(ESP32)
  // Na ESP32 runtime preusmjeravamo Serial na zasebne UART pinove prema Megi.
  // Time USB ostaje koristan za upload, a komunikacija toranjskog sata ide preko 16/17.
  Serial.begin(9600, SERIAL_8N1, ESP_MEGA_RX_PIN, ESP_MEGA_TX_PIN);
#endif
  delay(200);
}

static void postaviDhcpKonfiguraciju() {
#if defined(ESP8266)
  WiFi.config(0U, 0U, 0U);
#elif defined(ESP32)
  const IPAddress praznaAdresa(0U, 0U, 0U, 0U);
  WiFi.config(praznaAdresa, praznaAdresa, praznaAdresa);
#endif
}

void setup() {
  inicijalizirajSerijskiPortPremaMegi();
  pinMode(WIFI_SETUP_PIN, INPUT_PULLUP);
  pinMode(WIFI_STATUS_LED_PIN, OUTPUT);
  digitalWrite(WIFI_STATUS_LED_PIN, LOW);
  EEPROM.begin(ESP_EEPROM_VELICINA);
  webLozinka.reserve(WEB_LOZINKA_MAX - 1);
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

  Serial.println("FAZA: Pokretanje NTP klijenta");
  ntpKlijent.begin();

  Serial.println("FAZA: Konfiguracija web posluzitelja");
  konfigurirajWebPosluzitelj();

  Serial.println("CFGREQ");
  Serial.println("FAZA: INIT zavrsen, ulazak u loop()");
}

void loop() {
  obradiSerijskiUlaz();
  webPosluzitelj.handleClient();
  yield();
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

    WiFi.begin(wifiSsid.c_str(), wifiLozinka.c_str());
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

void osvjeziNTPSat() {
  static unsigned long zadnjiLog = 0;
  unsigned long sada = millis();

  bool promjena = ntpKlijent.update();

  if (promjena && ntpKlijent.isTimeSet()) {
    ntpZadnjiUspjehMs = sada;
    if (!ntpIkadPostavljen) {
      Serial.print("NTPLOG: Prvi put postavljeno vrijeme, epoch=");
      Serial.println(ntpKlijent.getEpochTime());
      ntpIkadPostavljen = true;
    }
  } else if (!ntpKlijent.isTimeSet() && (sada - zadnjiLog > 10000)) {
    Serial.println("NTPLOG: jos nije postavljeno vrijeme, cekam...");
    zadnjiLog = sada;
  }
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
  time_t utcEpoch = ntpKlijent.getEpochTime();
  time_t lokalniEpoch = konvertirajUTCuLokalnoVrijeme(utcEpoch);
  bool dstAktivan = jeLjetnoVrijemeEU(utcEpoch);
  struct tm lokalniTm;
  if (gmtime_r(&lokalniEpoch, &lokalniTm) == nullptr) {
    Serial.println("NTPLOG: konverzija lokalnog vremena nije uspjela, preskacem slanje");
    return;
  }

  char isoBuffer[25];
  strftime(isoBuffer, sizeof(isoBuffer), "%Y-%m-%dT%H:%M:%S", &lokalniTm);

  Serial.print("SLANJE: NTP linija prema Megi: ");
  Serial.println(isoBuffer);

  Serial.print("NTP:");
  Serial.print(isoBuffer);
  Serial.print(";DST=");
  Serial.println(dstAktivan ? '1' : '0');
}

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

      wifiSsid = ssid;
      wifiLozinka = lozinka;
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
        if (strncmp(linija, "STATUS:", 7) == 0) {
          // Status toranjskog sata se vise ne cachea na ESP-u.
        } else if (strcmp(linija, "ACK:SETUPWIFI") == 0) {
          odgovorSetupWiFiPrimljen = true;
          setupWiFiNeuspjeh = false;
        } else if (strcmp(linija, "ACK:CMD_OK") == 0) {
          zadnjiCmdOdgovorMega = CMD_ODGOVOR_OK;
        } else if (strcmp(linija, "ERR:CMD_BUSY") == 0) {
          zadnjiCmdOdgovorMega = CMD_ODGOVOR_BUSY;
        } else if (strcmp(linija, "ERR:CMD") == 0) {
          zadnjiCmdOdgovorMega = CMD_ODGOVOR_ERR;
        } else if (strcmp(linija, "ERR:SETUPWIFI") == 0) {
          odgovorSetupWiFiPrimljen = true;
          setupWiFiNeuspjeh = true;
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
                  (wifiSsid != noviSsid) ||
                  (wifiLozinka != novaLozinka) ||
                  (koristiDhcp != noviDhcp) ||
                  (statickaIp != novaIp) ||
                  (mreznaMaska != novaMaska) ||
                  (zadaniGateway != noviGateway);

              wifiSsid = noviSsid;
              wifiLozinka = novaLozinka;
              koristiDhcp = noviDhcp;
              statickaIp = novaIp;
              mreznaMaska = novaMaska;
              zadaniGateway = noviGateway;
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
            osvjeziNTPSat();
            if (WiFi.status() != WL_CONNECTED) {
              Serial.println("NTPLOG: NTP zahtjev odbijen jer WiFi nije spojen");
              Serial.println("ERR:NTPREQ");
            } else if (ntpZadnjiUspjehMs == 0UL) {
              Serial.println("NTPLOG: NTP zahtjev odbijen jer nema svjezeg NTP vremena nakon zadnjeg spajanja");
              Serial.println("ERR:NTPREQ");
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

bool jePrijestupnaGodina(int godina) {
  if ((godina % 4) != 0) return false;
  if ((godina % 100) != 0) return true;
  return (godina % 400) == 0;
}

int zadnjaNedjeljaUMjesecu(int godina, int mjesec) {
  static const int daniUMjesecu[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  int brojDana = daniUMjesecu[mjesec - 1];
  if (mjesec == 2 && jePrijestupnaGodina(godina)) {
    brojDana = 29;
  }

  int pomakDoNedjelje = danUTjednu(godina, mjesec, brojDana);
  return brojDana - pomakDoNedjelje;
}

int danUTjednu(int godina, int mjesec, int dan) {
  static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  int g = godina;
  if (mjesec < 3) {
    g -= 1;
  }
  return (g + g / 4 - g / 100 + g / 400 + t[mjesec - 1] + dan) % 7;
}

bool jeLjetnoVrijemeEU(time_t utcEpoch) {
  struct tm utcTm;
  if (gmtime_r(&utcEpoch, &utcTm) == nullptr) {
    return false;
  }

  int godina = utcTm.tm_year + 1900;
  int mjesec = utcTm.tm_mon + 1;
  int dan = utcTm.tm_mday;
  int sati = utcTm.tm_hour;

  if (mjesec < 3 || mjesec > 10) {
    return false;
  }
  if (mjesec > 3 && mjesec < 10) {
    return true;
  }

  int prijelazniDan = zadnjaNedjeljaUMjesecu(godina, mjesec);
  if (mjesec == 3) {
    if (dan > prijelazniDan) return true;
    if (dan < prijelazniDan) return false;
    return sati >= 1;
  }

  if (dan < prijelazniDan) return true;
  if (dan > prijelazniDan) return false;
  return sati < 1;
}

time_t konvertirajUTCuLokalnoVrijeme(time_t utcEpoch) {
  static const long CET_OFFSET_SEKUNDI = 3600;
  static const long CEST_DODATAK_SEKUNDI = 3600;

  long ukupniOffset = CET_OFFSET_SEKUNDI;
  if (jeLjetnoVrijemeEU(utcEpoch)) {
    ukupniOffset += CEST_DODATAK_SEKUNDI;
  }
  return utcEpoch + ukupniOffset;
}

void ucitajWebAutentikaciju() {
  WebAuthConfig cfg{};
  EEPROM.get(ESP_EEPROM_ADRESA_WEB, cfg);

  if (cfg.potpis == WEB_AUTH_POTPIS && cfg.lozinka[0] != '\0') {
    cfg.lozinka[WEB_LOZINKA_MAX - 1] = '\0';
    webLozinka = String(cfg.lozinka);
    Serial.println("WEB AUTH: ucitana spremljena lozinka");
    return;
  }

  webLozinka = "cista2906";
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
    webPosluzitelj.send(409, "text/plain", "Mega je zauzeta i nije prihvatila naredbu");
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
  if (webPosluzitelj.authenticate("admin", webLozinka.c_str())) {
    return true;
  }

  webPosluzitelj.requestAuthentication(BASIC_AUTH, "ZVONKO v. 1.0", "Unesite web lozinku");
  return false;
}

void posaljiJsonStatus() {
  char ipBuffer[16];
  snprintf(ipBuffer, sizeof(ipBuffer), "%s", WiFi.localIP().toString().c_str());

  char tijelo[80];
  snprintf_P(tijelo,
             sizeof(tijelo),
             PSTR("{\"wifi_ip\":\"%s\",\"wifi_connected\":%s}"),
             ipBuffer,
             (WiFi.status() == WL_CONNECTED) ? "true" : "false");
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

  webPosluzitelj.setContentLength(CONTENT_LENGTH_UNKNOWN);
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
      --bg:#ece3d1;
      --bg2:#f8f2e8;
      --panel:#fffaf1;
      --line:#c5b394;
      --text:#2c2418;
      --muted:#74624c;
      --accent:#7c5b2f;
      --accent-soft:#efe1c7;
      --ok:#365b34;
      --ok-soft:#dfead8;
      --warn:#8a3a24;
      --warn-soft:#f5ddd4;
      --shadow:0 16px 36px rgba(67, 48, 24, 0.10);
    }
    * { box-sizing:border-box; }
    body {
      margin:0;
      font-family: Georgia, "Times New Roman", serif;
      background:
        radial-gradient(circle at top left, rgba(255,255,255,0.55), transparent 28%),
        linear-gradient(180deg, var(--bg), var(--bg2));
      color:var(--text);
    }
    .wrap { max-width:1180px; margin:0 auto; padding:22px 18px 34px; }
    .hero {
      background:linear-gradient(135deg, rgba(255,250,241,0.98), rgba(246,235,212,0.96));
      border:1px solid var(--line);
      border-radius:22px;
      padding:22px;
      box-shadow:var(--shadow);
      margin-bottom:16px;
    }
    .hero-top {
      display:flex;
      justify-content:space-between;
      gap:16px;
      align-items:flex-start;
      flex-wrap:wrap;
    }
    h1, h2, h3 { margin:0; }
    h1 { font-size:clamp(28px, 4vw, 40px); letter-spacing:0.02em; }
    .subtitle {
      margin-top:10px;
      max-width:720px;
      color:var(--muted);
      line-height:1.6;
      font-size:15px;
    }
    .hero-actions {
      display:flex;
      flex-wrap:wrap;
      gap:10px;
      margin-top:18px;
    }
    .link-btn,
    .card button {
      appearance:none;
      border:none;
      border-radius:12px;
      padding:12px 16px;
      font:inherit;
      font-weight:700;
      cursor:pointer;
      transition:transform 120ms ease, box-shadow 120ms ease, background 120ms ease;
    }
    .link-btn {
      display:inline-flex;
      align-items:center;
      justify-content:center;
      text-decoration:none;
      color:var(--text);
      background:#fff;
      border:1px solid var(--line);
      min-width:150px;
    }
    .link-btn:hover,
    .card button:hover {
      transform:translateY(-1px);
      box-shadow:0 10px 20px rgba(67, 48, 24, 0.12);
    }
    .status-box {
      min-width:260px;
      background:rgba(255,255,255,0.72);
      border:1px solid var(--line);
      border-radius:18px;
      padding:14px 16px;
    }
    .status-grid {
      display:grid;
      grid-template-columns:repeat(2, minmax(0, 1fr));
      gap:10px;
      margin-top:10px;
    }
    .status-item {
      background:rgba(255,250,241,0.85);
      border:1px solid rgba(197,179,148,0.65);
      border-radius:12px;
      padding:10px 12px;
    }
    .status-label {
      font-size:11px;
      text-transform:uppercase;
      letter-spacing:0.08em;
      color:var(--muted);
      margin-bottom:4px;
    }
    .status-value {
      font-size:15px;
      font-weight:700;
      word-break:break-word;
    }
    .status-badge {
      display:inline-flex;
      align-items:center;
      gap:8px;
      border-radius:999px;
      padding:8px 12px;
      font-size:13px;
      font-weight:700;
      background:var(--warn-soft);
      color:var(--warn);
    }
    .status-badge.ok {
      background:var(--ok-soft);
      color:var(--ok);
    }
    .dashboard {
      display:grid;
      grid-template-columns:repeat(auto-fit, minmax(240px, 1fr));
      gap:14px;
    }
    .card {
      background:var(--panel);
      border:1px solid var(--line);
      border-radius:20px;
      padding:18px;
      box-shadow:var(--shadow);
      display:flex;
      flex-direction:column;
      gap:14px;
      min-height:220px;
      position:relative;
      overflow:hidden;
    }
    .card::before {
      content:"";
      position:absolute;
      inset:auto -30px -40px auto;
      width:120px;
      height:120px;
      border-radius:50%;
      background:radial-gradient(circle, rgba(239,225,199,0.7), transparent 72%);
      pointer-events:none;
    }
    .card-header { position:relative; z-index:1; }
    .eyebrow {
      font-size:11px;
      text-transform:uppercase;
      letter-spacing:0.08em;
      color:var(--muted);
      margin-bottom:8px;
    }
    .card h2 {
      font-size:26px;
      line-height:1.05;
      margin-bottom:6px;
    }
    .card p {
      margin:0;
      color:var(--muted);
      line-height:1.5;
      font-size:14px;
      position:relative;
      z-index:1;
    }
    .btn-row {
      display:grid;
      grid-template-columns:1fr 1fr;
      gap:10px;
      margin-top:auto;
      position:relative;
      z-index:1;
    }
    .btn-primary { background:var(--accent); color:#fffaf1; }
    .btn-secondary {
      background:#fff;
      color:var(--text);
      border:1px solid var(--line);
    }
    .log-panel {
      margin-top:16px;
      background:#2d2419;
      color:#f7efe0;
      border-radius:18px;
      padding:16px 18px;
      box-shadow:var(--shadow);
    }
    .log-panel h3 { font-size:16px; margin-bottom:10px; }
    .log-text {
      min-height:52px;
      white-space:pre-wrap;
      line-height:1.5;
      font-size:14px;
      color:#eadfcb;
    }
    .muted-small {
      margin-top:10px;
      color:#bfae92;
      font-size:12px;
    }
    @media (max-width:700px) {
      .wrap { padding:14px 12px 24px; }
      .hero { padding:18px; }
      .status-grid { grid-template-columns:1fr; }
      .btn-row { grid-template-columns:1fr; }
      .card { min-height:auto; }
    }
  </style>
</head>
<body>
  <div class="wrap">
    <section class="hero">
      <div class="hero-top">
        <div>
          <h1>Dashboard toranjskog sata</h1>
          <p class="subtitle">
            Ovaj ESP vodi mrezu toranjskog sata i prosljedjuje servisne naredbe prema Arduino Megi.
            Kartice ispod salju API zahtjeve za zvona, slavljenje, mrtvacko i suncevu automatiku.
          </p>
          <div class="hero-actions">
            <a class="link-btn" href="/setup">Setup WiFi</a>
            <a class="link-btn" href="/status" target="_blank" rel="noopener">JSON status</a>
          </div>
        </div>
        <div class="status-box">
          <div id="wifiBadge" class="status-badge">WiFi: provjera...</div>
          <div class="status-grid">
            <div class="status-item">
              <div class="status-label">IP adresa</div>
              <div id="wifiIp" class="status-value">--</div>
            </div>
            <div class="status-item">
              <div class="status-label">Autorizacija</div>
              <div class="status-value">Basic Auth</div>
            </div>
          </div>
        </div>
      </div>
    </section>

    <section class="dashboard">
      <article class="card">
        <div class="card-header">
          <div class="eyebrow">Rucna komanda</div>
          <h2>Zvono 1</h2>
          <p>Direktno ukljucenje ili gasenje prvog zvona toranjskog sata preko API puta prema Megi.</p>
        </div>
        <div class="btn-row">
          <button class="btn-primary" onclick="pozoviApi('/api/bell1/on', 'Zvono 1 ukljuci')">Ukljuci</button>
          <button class="btn-secondary" onclick="pozoviApi('/api/bell1/off', 'Zvono 1 iskljuci')">Iskljuci</button>
        </div>
      </article>

      <article class="card">
        <div class="card-header">
          <div class="eyebrow">Rucna komanda</div>
          <h2>Zvono 2</h2>
          <p>Druga servisna kartica za zasebno upravljanje drugim zvonom bez ulaska u lokalni meni sata.</p>
        </div>
        <div class="btn-row">
          <button class="btn-primary" onclick="pozoviApi('/api/bell2/on', 'Zvono 2 ukljuci')">Ukljuci</button>
          <button class="btn-secondary" onclick="pozoviApi('/api/bell2/off', 'Zvono 2 iskljuci')">Iskljuci</button>
        </div>
      </article>

      <article class="card">
        <div class="card-header">
          <div class="eyebrow">Posebni nacin</div>
          <h2>Slavljenje</h2>
          <p>Rucno pokretanje ili prekid slavljenja za servis, probu ili provjeru reakcije cekica i zvona.</p>
        </div>
        <div class="btn-row">
          <button class="btn-primary" onclick="pozoviApi('/api/slavljenje/on', 'Slavljenje ukljuci')">Ukljuci</button>
          <button class="btn-secondary" onclick="pozoviApi('/api/slavljenje/off', 'Slavljenje iskljuci')">Iskljuci</button>
        </div>
      </article>

      <article class="card">
        <div class="card-header">
          <div class="eyebrow">Posebni nacin</div>
          <h2>Mrtvacko</h2>
          <p>Servisna kartica za ukljucenje ili zaustavljanje mrtvackog nacina rada s Megine strane.</p>
        </div>
        <div class="btn-row">
          <button class="btn-primary" onclick="pozoviApi('/api/mrtvacko/on', 'Mrtvacko ukljuci')">Ukljuci</button>
          <button class="btn-secondary" onclick="pozoviApi('/api/mrtvacko/off', 'Mrtvacko iskljuci')">Iskljuci</button>
        </div>
      </article>

      <article class="card">
        <div class="card-header">
          <div class="eyebrow">Sunceva automatika</div>
          <h2>Jutro</h2>
          <p>Rucno dopusti ili zabrani jutarnji suncev dogadjaj bez mijenjanja glavnih postavki na LCD-u.</p>
        </div>
        <div class="btn-row">
          <button class="btn-primary" onclick="pozoviApi('/api/solar/morning/on', 'Sunce jutro ukljuci')">Ukljuci</button>
          <button class="btn-secondary" onclick="pozoviApi('/api/solar/morning/off', 'Sunce jutro iskljuci')">Iskljuci</button>
        </div>
      </article>

      <article class="card">
        <div class="card-header">
          <div class="eyebrow">Sunceva automatika</div>
          <h2>Podne</h2>
          <p>Kontrola podnevnog suncevog zvonjenja preko jedne velike kartice, bez skrivene tekstualne rute.</p>
        </div>
        <div class="btn-row">
          <button class="btn-primary" onclick="pozoviApi('/api/solar/noon/on', 'Sunce podne ukljuci')">Ukljuci</button>
          <button class="btn-secondary" onclick="pozoviApi('/api/solar/noon/off', 'Sunce podne iskljuci')">Iskljuci</button>
        </div>
      </article>

      <article class="card">
        <div class="card-header">
          <div class="eyebrow">Sunceva automatika</div>
          <h2>Vecer</h2>
          <p>Rucna servisna kartica za vecernju suncevu automatiku i brzu provjeru odgovora Megine logike.</p>
        </div>
        <div class="btn-row">
          <button class="btn-primary" onclick="pozoviApi('/api/solar/evening/on', 'Sunce vecer ukljuci')">Ukljuci</button>
          <button class="btn-secondary" onclick="pozoviApi('/api/solar/evening/off', 'Sunce vecer iskljuci')">Iskljuci</button>
        </div>
      </article>
    </section>

    <section class="log-panel">
      <h3>Zadnja web akcija</h3>
      <div id="odgovor" class="log-text">Dashboard je spreman. Odaberi karticu i posalji naredbu prema Megi.</div>
      <div class="muted-small">Napomena: API i dashboard koriste istu Basic Auth prijavu. Ako je Mega zauzeta, odgovor ce biti prikazan ovdje.</div>
    </section>
  </div>
  <script>
    function postaviLog(poruka) {
      document.getElementById('odgovor').textContent = poruka;
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

    async function osvjeziStatus() {
      try {
        const odgovor = await fetch('/status', { cache: 'no-store' });
        if (!odgovor.ok) {
          throw new Error('status');
        }
        const podaci = await odgovor.json();
        const badge = document.getElementById('wifiBadge');
        const ip = document.getElementById('wifiIp');
        ip.textContent = podaci.wifi_ip || '--';
        if (podaci.wifi_connected) {
          badge.textContent = 'WiFi: spojen';
          badge.classList.add('ok');
        } else {
          badge.textContent = 'WiFi: nije spojen';
          badge.classList.remove('ok');
        }
      } catch (greska) {
        const badge = document.getElementById('wifiBadge');
        document.getElementById('wifiIp').textContent = '--';
        badge.textContent = 'WiFi: status nedostupan';
        badge.classList.remove('ok');
      }
    }

    osvjeziStatus();
    setInterval(osvjeziStatus, 10000);
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


void obradiNTPSerijskuNaredbu(const char* payload) {
  char server[sizeof(ntpPosluzitelj)];
  kopirajOcisceniBuffer(payload, server, sizeof(server));

  if (server[0] == '\0') {
    Serial.println("NTPLOG: prazan NTP server, zadrzavam postojeci");
    Serial.println("ERR:NTPCFG");
    return;
  }

  if (strcmp(server, ntpPosluzitelj) == 0) {
    Serial.print("NTPLOG: NTP server je nepromijenjen ");
    Serial.println(ntpPosluzitelj);
    Serial.println("ACK:NTPCFG");
    return;
  }

  strncpy(ntpPosluzitelj, server, sizeof(ntpPosluzitelj) - 1);
  ntpPosluzitelj[sizeof(ntpPosluzitelj) - 1] = '\0';
  ntpKlijent.setPoolServerName(ntpPosluzitelj);
  resetirajNtpStanje();

  Serial.print("NTPLOG: postavljen novi NTP server ");
  Serial.println(ntpPosluzitelj);
  Serial.println("ACK:NTPCFG");
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

  Serial.println("WEB: Registriram /status rutu");
  webPosluzitelj.on("/status", []() {
    if (!osigurajWebAutorizaciju()) {
      return;
    }
    posaljiJsonStatus();
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

  webPosluzitelj.onNotFound([]() {
    Serial.println("WEB: 404 - ruta ne postoji");
    webPosluzitelj.send(404, "text/plain", "Ruta ne postoji");
  });

  webPosluzitelj.begin();
  Serial.println("WEB: posluzitelj pokrenut na portu 80");
}

