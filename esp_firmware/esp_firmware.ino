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
#include <EEPROM.h>
#include <pgmspace.h>
// Konfiguracija WiFi mreze za toranjski sat.
// Mega moze ove vrijednosti prepisati preko serijske veze naredbom WIFI:...
char wifiSsid[33] = "SVETI PETAR";
char wifiLozinka[33] = "cista2906";
bool koristiDhcp = true;
char statickaIp[16] = "192.168.1.200";
char mreznaMaska[16] = "255.255.255.0";
char zadaniGateway[16] = "192.168.1.1";
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

// Parametri UDP NTP sloja za sinkronizaciju toranjskog sata.
char ntpPosluzitelj[40] = "pool.ntp.org";
static const unsigned long NTP_INTERVAL_MS = 60000UL;
static const unsigned long NTP_TIMEOUT_MS = 5000UL;
static const unsigned long NTP_PONOVNI_POKUSAJ_BEZ_VREMENA_MS = 10000UL;
static const unsigned long NTP_MAKSIMALNA_STAROST_ODGOVORA_MS = 3UL * NTP_INTERVAL_MS;
static const uint16_t NTP_LOKALNI_PORT = 2390U;
static const uint16_t NTP_UDP_PORT = 123U;
static const size_t NTP_VELICINA_PAKETA = 48U;
static const uint32_t NTP_UNIX_EPOCH_OFFSET = 2208988800UL;
static const uint32_t NTP_REFERENTNI_MIN_UNIX = 1700000000UL;
static const uint32_t NTP_REFERENTNI_MAX_UNIX = 4102444799UL;
static const unsigned long WIFI_WATCHDOG_NTP_ZASTOJ_MS = 7200000UL;
static const size_t SERIJSKI_BUFFER_MAX = 1280;
static const size_t SERIJSKI_BUDZET_BAJTOVA_PO_POZIVU = 192;
static const size_t WEB_LOZINKA_MAX = 33;
static const size_t ESP_EEPROM_VELICINA = 512;
static const uint16_t WEB_AUTH_POTPIS = 0x5741;
static const int ESP_EEPROM_ADRESA_WEB = 0;
static const unsigned long CMD_CEKANJE_NA_MEGU_MS = 1500UL;
static const unsigned long WEB_AUTH_NEUSPJEH_ODGODA_MS = 750UL;

WiFiUDP ntpUDP;

ToranjWebServer webPosluzitelj(80);

char webLozinka[WEB_LOZINKA_MAX] = "cista2906";

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
bool ntpUdpPokrenut = false;
bool ntpZahtjevUTijeku = false;
unsigned long ntpZadnjiUspjehMs = 0;
unsigned long ntpZadnjiPokusajMs = 0;
unsigned long ntpZahtjevPoslanMs = 0;
unsigned long ntpBazniMillis = 0;
uint64_t ntpBazniUtcMs = 0;
unsigned long wifiSpojenOdMs = 0;
bool wifiSpojenOdPoznat = false;
bool megaStatusPoznat = false;
bool megaZvono1Aktivno = false;
bool megaZvono2Aktivno = false;
bool megaSlavljenjeAktivno = false;
bool megaMrtvackoAktivno = false;
unsigned long megaStatusZadnjeOsvjezavanjeMs = 0;
unsigned long megaStatusSerijskiBroj = 0;

// Upravljanje nenametljivim pokusajima WiFi spajanja.
bool wifiPokusajUToku = false;
unsigned long wifiPokusajPocetak = 0;
unsigned long wifiSljedeciPokusajDozvoljen = 0;
int wifiBrojPokusajaZaredom = 0;
static const unsigned long WIFI_POKUSAJ_TIMEOUT_MS = 20000;
static const unsigned long WIFI_ODGODA_NAKON_PRVOG_MS = 10000;
static const unsigned long WIFI_ODGODA_NAKON_DRUGOG_MS = 30000;
static const unsigned long WIFI_ODGODA_NAKON_TRECEG_MS = 60000;
static const unsigned long STATUS_CEKANJE_NA_MEGU_MS = 350UL;
static const unsigned long STATUS_MAKSIMALNA_STAROST_MS = 1500UL;
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
bool osvjeziNTPSat(bool prisilno = false);
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
struct RastavljenoVrijeme {
  uint16_t godina;
  uint8_t mjesec;
  uint8_t dan;
  uint8_t sat;
  uint8_t minuta;
  uint8_t sekunda;
};
bool razloziUnixVrijemeUTC(uint32_t epochSekunde, RastavljenoVrijeme* izlaz);
bool jeLjetnoVrijemeEU(uint32_t utcEpoch);
uint32_t konvertirajUTCuLokalnoVrijeme(uint32_t utcEpoch);
void formatirajIsoDatumIVrijeme(const RastavljenoVrijeme& vrijeme, char* izlaz, size_t velicinaIzlaza);
bool osigurajNtpUdp();
void ocistiZaostaleNtpUdpPakete();
bool posaljiNtpUpit();
bool obradiNtpOdgovor();
void obradiNtpTimeout();
bool trebaPokrenutiNtpOsvjezavanje(unsigned long sadaMs, bool prisilno);
void postaviNtpBaznoVrijemeUtcMs(uint64_t utcMs);
bool jeNtpVrijemePostavljeno();
uint64_t dohvatiNtpUtcMs();
uint32_t dohvatiNtpUnixVrijeme();
uint32_t dohvatiReferentniUnixEpochZaNtp();
int64_t apsolutnaRazlikaInt64(int64_t vrijednost);
int64_t odrediUnixEpochIzNtpSekundi(uint32_t ntpSekunde, uint32_t referentniUnixEpoch);
void obradiNTPSerijskuNaredbu(const char* payload);
String ocistiJednolinijskiTekst(const String &ulaz, size_t maxDuljina);
void primijeniWiFiOmogucenost(bool omogucen);
bool obradiStatusMegai(const char* payload);
bool osvjeziStatusMegai(bool prisilno);
void posaljiJsonStatus(bool prisilno = false);
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
  ntpZadnjiUspjehMs = 0;
  ntpZadnjiPokusajMs = 0;
  ntpZahtjevPoslanMs = 0;
  ntpBazniMillis = 0;
  ntpBazniUtcMs = 0;
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

  uint32_t utcEpoch = dohvatiNtpUnixVrijeme();
  uint32_t lokalniEpoch = konvertirajUTCuLokalnoVrijeme(utcEpoch);
  bool dstAktivan = jeLjetnoVrijemeEU(utcEpoch);
  RastavljenoVrijeme lokalnoVrijeme{};
  if (!razloziUnixVrijemeUTC(lokalniEpoch, &lokalnoVrijeme)) {
    Serial.println("NTPLOG: konverzija lokalnog vremena nije uspjela, preskacem slanje");
    return;
  }

  char isoBuffer[25];
  formatirajIsoDatumIVrijeme(lokalnoVrijeme, isoBuffer, sizeof(isoBuffer));

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

bool obradiStatusMegai(const char* payload) {
  bool zvono1Aktivno = false;
  bool zvono2Aktivno = false;
  bool slavljenjeAktivno = false;
  bool mrtvackoAktivno = false;

  if (!procitajStatusZastavicu(payload, "b1=", &zvono1Aktivno) ||
      !procitajStatusZastavicu(payload, "b2=", &zvono2Aktivno) ||
      !procitajStatusZastavicu(payload, "sl=", &slavljenjeAktivno) ||
      !procitajStatusZastavicu(payload, "mr=", &mrtvackoAktivno)) {
    return false;
  }

  megaZvono1Aktivno = zvono1Aktivno;
  megaZvono2Aktivno = zvono2Aktivno;
  megaSlavljenjeAktivno = slavljenjeAktivno;
  megaMrtvackoAktivno = mrtvackoAktivno;
  megaStatusPoznat = true;
  megaStatusZadnjeOsvjezavanjeMs = millis();
  ++megaStatusSerijskiBroj;
  return true;
}

bool osvjeziStatusMegai(bool prisilno) {
  const unsigned long sadaMs = millis();
  if (!prisilno && megaStatusPoznat &&
      (sadaMs - megaStatusZadnjeOsvjezavanjeMs) <= STATUS_MAKSIMALNA_STAROST_MS) {
    return true;
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
        } else if (strcmp(linija, "ACK:CMD_OK") == 0) {
          zadnjiCmdOdgovorMega = CMD_ODGOVOR_OK;
        } else if (strcmp(linija, "ERR:CMD_BUSY") == 0) {
          zadnjiCmdOdgovorMega = CMD_ODGOVOR_BUSY;
        } else if (strcmp(linija, "ERR:CMD") == 0) {
          zadnjiCmdOdgovorMega = CMD_ODGOVOR_ERR;
        } else if (strcmp(linija, "ERR:SETUPWIFI") == 0) {
          odgovorSetupWiFiPrimljen = true;
          setupWiFiNeuspjeh = true;
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
                Serial.println("NTPLOG: NTP zahtjev odbijen jer nema dovoljno svjezeg NTP vremena");
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

bool razloziUnixVrijemeUTC(uint32_t epochSekunde, RastavljenoVrijeme* izlaz) {
  if (izlaz == nullptr) {
    return false;
  }

  static const uint8_t daniUMjesecu[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

  uint32_t brojDana = epochSekunde / 86400UL;
  uint32_t sekundeUDanu = epochSekunde % 86400UL;

  izlaz->sat = static_cast<uint8_t>(sekundeUDanu / 3600UL);
  sekundeUDanu %= 3600UL;
  izlaz->minuta = static_cast<uint8_t>(sekundeUDanu / 60UL);
  izlaz->sekunda = static_cast<uint8_t>(sekundeUDanu % 60UL);

  uint16_t godina = 1970;
  while (true) {
    const uint16_t brojDanaUGodini = jePrijestupnaGodina(godina) ? 366U : 365U;
    if (brojDana < brojDanaUGodini) {
      break;
    }
    brojDana -= brojDanaUGodini;
    ++godina;
  }

  uint8_t mjesec = 1;
  while (mjesec <= 12) {
    uint8_t brojDanaUMjesecu = daniUMjesecu[mjesec - 1];
    if (mjesec == 2 && jePrijestupnaGodina(godina)) {
      brojDanaUMjesecu = 29;
    }
    if (brojDana < brojDanaUMjesecu) {
      break;
    }
    brojDana -= brojDanaUMjesecu;
    ++mjesec;
  }

  if (mjesec > 12) {
    return false;
  }

  izlaz->godina = godina;
  izlaz->mjesec = mjesec;
  izlaz->dan = static_cast<uint8_t>(brojDana + 1U);
  return true;
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

bool osigurajNtpUdp() {
  if (ntpUdpPokrenut) {
    return true;
  }

  ntpUdpPokrenut = ntpUDP.begin(NTP_LOKALNI_PORT) == 1;
  if (!ntpUdpPokrenut) {
    Serial.println("NTPLOG: ne mogu pokrenuti UDP port za NTP");
  }
  return ntpUdpPokrenut;
}

void ocistiZaostaleNtpUdpPakete() {
  // Prije novog mjerenja RTT-a odbacujemo stare UDP odgovore
  // kako kasni paket ne bi bio pripisan novom NTP zahtjevu toranjskog sata.
  uint8_t odlozeniPaketi = 0;
  while (odlozeniPaketi < 4U) {
    const int velicinaPaketa = ntpUDP.parsePacket();
    if (velicinaPaketa <= 0) {
      break;
    }

    uint8_t meduspremnik[64];
    while (ntpUDP.available() > 0) {
      ntpUDP.read(meduspremnik, sizeof(meduspremnik));
    }

    ++odlozeniPaketi;
  }

  if (odlozeniPaketi > 0U) {
    Serial.print("NTPLOG: odbaceni zaostali UDP paketi prije novog upita, komada=");
    Serial.println(odlozeniPaketi);
  }
}

bool posaljiNtpUpit() {
  if (!osigurajNtpUdp()) {
    return false;
  }

  IPAddress adresaPosluzitelja;
  if (!WiFi.hostByName(ntpPosluzitelj, adresaPosluzitelja)) {
    Serial.print("NTPLOG: ne mogu razrijesiti NTP server ");
    Serial.println(ntpPosluzitelj);
    ntpZadnjiPokusajMs = millis();
    return false;
  }

  ocistiZaostaleNtpUdpPakete();

  uint8_t paket[NTP_VELICINA_PAKETA] = {0};
  paket[0] = 0x23;
  paket[1] = 0;
  paket[2] = 6;
  paket[3] = 0xEC;
  paket[12] = 49;
  paket[13] = 0x4E;
  paket[14] = 49;
  paket[15] = 52;

  ntpUDP.beginPacket(adresaPosluzitelja, NTP_UDP_PORT);
  ntpUDP.write(paket, sizeof(paket));
  if (!ntpUDP.endPacket()) {
    Serial.print("NTPLOG: ne mogu poslati UDP NTP upit na ");
    Serial.println(ntpPosluzitelj);
    ntpZadnjiPokusajMs = millis();
    return false;
  }

  ntpZahtjevUTijeku = true;
  ntpZahtjevPoslanMs = millis();
  ntpZadnjiPokusajMs = ntpZahtjevPoslanMs;
  return true;
}

bool obradiNtpOdgovor() {
  if (!ntpUdpPokrenut) {
    return false;
  }

  const int velicinaPaketa = ntpUDP.parsePacket();
  if (velicinaPaketa <= 0) {
    return false;
  }

  uint8_t paket[NTP_VELICINA_PAKETA] = {0};
  const int procitano = ntpUDP.read(paket, sizeof(paket));

  if (!ntpZahtjevUTijeku) {
    Serial.println("NTPLOG: odbacen je UDP NTP odgovor bez aktivnog zahtjeva");
    return false;
  }

  ntpZahtjevUTijeku = false;

  if (procitano < static_cast<int>(NTP_VELICINA_PAKETA)) {
    Serial.println("NTPLOG: primljen je prekratak UDP NTP odgovor");
    return false;
  }

  const uint32_t ntpSekunde =
      (static_cast<uint32_t>(paket[40]) << 24) |
      (static_cast<uint32_t>(paket[41]) << 16) |
      (static_cast<uint32_t>(paket[42]) << 8) |
      static_cast<uint32_t>(paket[43]);
  const uint32_t ntpRazlomak =
      (static_cast<uint32_t>(paket[44]) << 24) |
      (static_cast<uint32_t>(paket[45]) << 16) |
      (static_cast<uint32_t>(paket[46]) << 8) |
      static_cast<uint32_t>(paket[47]);
  const uint8_t ntpMod = paket[0] & 0x07U;

  if ((ntpMod != 4U && ntpMod != 5U) || paket[1] == 0U || ntpSekunde == 0UL) {
    Serial.println("NTPLOG: primljen je nevaljan UDP NTP odgovor");
    return false;
  }

  const int64_t unixEpoch =
      odrediUnixEpochIzNtpSekundi(ntpSekunde, dohvatiReferentniUnixEpochZaNtp());
  if (unixEpoch < static_cast<int64_t>(NTP_REFERENTNI_MIN_UNIX) ||
      unixEpoch > static_cast<int64_t>(NTP_REFERENTNI_MAX_UNIX)) {
    Serial.println("NTPLOG: primljen je NTP odgovor izvan poduprtog raspona godina");
    return false;
  }

  const uint64_t razlomakMs =
      (static_cast<uint64_t>(ntpRazlomak) * 1000ULL) >> 32;
  const unsigned long roundTripMs = millis() - ntpZahtjevPoslanMs;
  const uint64_t utcMs =
      static_cast<uint64_t>(unixEpoch) * 1000ULL + razlomakMs +
      static_cast<uint64_t>(roundTripMs / 2UL);

  postaviNtpBaznoVrijemeUtcMs(utcMs);
  ntpIkadPostavljen = true;
  ntpZadnjiUspjehMs = millis();

  Serial.print("NTPLOG: osvjezeno, epoch=");
  Serial.println(dohvatiNtpUnixVrijeme());
  return true;
}

void obradiNtpTimeout() {
  if (!ntpZahtjevUTijeku) {
    return;
  }

  if ((millis() - ntpZahtjevPoslanMs) < NTP_TIMEOUT_MS) {
    return;
  }

  ntpZahtjevUTijeku = false;
  Serial.println("NTPLOG: istekao je timeout za UDP NTP upit");
}

bool trebaPokrenutiNtpOsvjezavanje(unsigned long sadaMs, bool prisilno) {
  if (ntpZahtjevUTijeku) {
    return false;
  }

  if (prisilno) {
    return true;
  }

  if (!jeNtpVrijemePostavljeno()) {
    return ntpZadnjiPokusajMs == 0UL ||
           (sadaMs - ntpZadnjiPokusajMs) >= NTP_PONOVNI_POKUSAJ_BEZ_VREMENA_MS;
  }

  return ntpZadnjiUspjehMs == 0UL ||
         (sadaMs - ntpZadnjiUspjehMs) >= NTP_INTERVAL_MS;
}

void postaviNtpBaznoVrijemeUtcMs(uint64_t utcMs) {
  ntpBazniUtcMs = utcMs;
  ntpBazniMillis = millis();
}

bool jeNtpVrijemePostavljeno() {
  return ntpIkadPostavljen;
}

uint64_t dohvatiNtpUtcMs() {
  if (!jeNtpVrijemePostavljeno()) {
    return 0ULL;
  }

  const unsigned long protekloMs = millis() - ntpBazniMillis;
  return ntpBazniUtcMs + static_cast<uint64_t>(protekloMs);
}

uint32_t dohvatiNtpUnixVrijeme() {
  const uint64_t utcMs = dohvatiNtpUtcMs();
  return static_cast<uint32_t>(utcMs / 1000ULL);
}

uint32_t dohvatiReferentniUnixEpochZaNtp() {
  if (jeNtpVrijemePostavljeno()) {
    return dohvatiNtpUnixVrijeme();
  }
  return NTP_REFERENTNI_MIN_UNIX;
}

int64_t apsolutnaRazlikaInt64(int64_t vrijednost) {
  return (vrijednost < 0) ? -vrijednost : vrijednost;
}

int64_t odrediUnixEpochIzNtpSekundi(uint32_t ntpSekunde, uint32_t referentniUnixEpoch) {
  const int64_t ntpSekunde64 = static_cast<int64_t>(ntpSekunde);
  const int64_t eraRaspon = (1LL << 32);
  int64_t najboljiEpoch =
      ntpSekunde64 - static_cast<int64_t>(NTP_UNIX_EPOCH_OFFSET);
  int64_t najboljaRazlika =
      apsolutnaRazlikaInt64(najboljiEpoch - static_cast<int64_t>(referentniUnixEpoch));

  for (int eraPomak = -1; eraPomak <= 1; ++eraPomak) {
    const int64_t kandidatEpoch =
        ntpSekunde64 +
        static_cast<int64_t>(eraPomak) * eraRaspon -
        static_cast<int64_t>(NTP_UNIX_EPOCH_OFFSET);
    const int64_t kandidatRazlika =
        apsolutnaRazlikaInt64(kandidatEpoch - static_cast<int64_t>(referentniUnixEpoch));
    if (kandidatRazlika < najboljaRazlika) {
      najboljiEpoch = kandidatEpoch;
      najboljaRazlika = kandidatRazlika;
    }
  }

  return najboljiEpoch;
}

bool jeLjetnoVrijemeEU(uint32_t utcEpoch) {
  RastavljenoVrijeme utcVrijeme{};
  if (!razloziUnixVrijemeUTC(utcEpoch, &utcVrijeme)) {
    return false;
  }

  int godina = utcVrijeme.godina;
  int mjesec = utcVrijeme.mjesec;
  int dan = utcVrijeme.dan;
  int sati = utcVrijeme.sat;

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

uint32_t konvertirajUTCuLokalnoVrijeme(uint32_t utcEpoch) {
  static const uint32_t CET_OFFSET_SEKUNDI = 3600UL;
  static const uint32_t CEST_DODATAK_SEKUNDI = 3600UL;

  uint32_t ukupniOffset = CET_OFFSET_SEKUNDI;
  if (jeLjetnoVrijemeEU(utcEpoch)) {
    ukupniOffset += CEST_DODATAK_SEKUNDI;
  }
  return utcEpoch + ukupniOffset;
}

void formatirajIsoDatumIVrijeme(const RastavljenoVrijeme& vrijeme, char* izlaz, size_t velicinaIzlaza) {
  if (izlaz == nullptr || velicinaIzlaza == 0U) {
    return;
  }

  snprintf(izlaz,
           velicinaIzlaza,
           "%04u-%02u-%02uT%02u:%02u:%02u",
           static_cast<unsigned>(vrijeme.godina),
           static_cast<unsigned>(vrijeme.mjesec),
           static_cast<unsigned>(vrijeme.dan),
           static_cast<unsigned>(vrijeme.sat),
           static_cast<unsigned>(vrijeme.minuta),
           static_cast<unsigned>(vrijeme.sekunda));
}

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

  char tijelo[192];
  snprintf_P(tijelo,
             sizeof(tijelo),
             PSTR("{\"wifi_ip\":\"%s\",\"wifi_connected\":%s,\"mega_status_known\":%s,\"bell1_active\":%s,\"bell2_active\":%s,\"slavljenje_active\":%s,\"mrtvacko_active\":%s}"),
             ipBuffer,
             (WiFi.status() == WL_CONNECTED) ? "true" : "false",
             megaStatusPoznat ? "true" : "false",
             megaZvono1Aktivno ? "true" : "false",
             megaZvono2Aktivno ? "true" : "false",
             megaSlavljenjeAktivno ? "true" : "false",
             megaMrtvackoAktivno ? "true" : "false");
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
      --bg:#dde6f1;
      --bg2:#f4f7fb;
      --panel:rgba(255,255,255,0.84);
      --line:rgba(130,149,174,0.35);
      --text:#253246;
      --muted:#617286;
      --hero:#fdfefe;
      --ok:#b94e63;
      --ok-soft:#f8dbe2;
      --warn:#6f7f93;
      --warn-soft:#e7edf4;
      --off:#eef2f7;
      --off-line:#8a98a8;
      --shadow:0 18px 42px rgba(75, 95, 122, 0.14);
    }
    * { box-sizing:border-box; }
    body {
      margin:0;
      font-family: "Trebuchet MS", Verdana, sans-serif;
      background:
        radial-gradient(circle at top left, rgba(255,255,255,0.82), transparent 28%),
        linear-gradient(180deg, var(--bg), var(--bg2));
      color:var(--text);
    }
    body::before {
      content:"";
      position:fixed;
      inset:0;
      background:
        radial-gradient(circle at 16% 18%, rgba(255,255,255,0.9), transparent 22%),
        radial-gradient(circle at 84% 30%, rgba(210,223,239,0.7), transparent 24%),
        linear-gradient(180deg, rgba(255,255,255,0.06), rgba(255,255,255,0.16));
      pointer-events:none;
    }
    .wrap {
      position:relative;
      max-width:1080px;
      margin:0 auto;
      padding:22px 18px 34px;
    }
    .hero {
      background:linear-gradient(135deg, rgba(255,255,255,0.95), rgba(245,248,252,0.92));
      border:1px solid var(--line);
      border-radius:22px;
      padding:22px;
      box-shadow:var(--shadow);
      margin-bottom:16px;
    }
    h1, h2, h3 { margin:0; }
    h1 {
      font-size:clamp(28px, 4vw, 40px);
      letter-spacing:0.02em;
      font-family: Georgia, "Times New Roman", serif;
    }
    .subtitle {
      margin-top:10px;
      max-width:720px;
      color:var(--muted);
      line-height:1.6;
      font-size:15px;
    }
    .secondary-card button,
    .toggle-btn {
      appearance:none;
      border:none;
      border-radius:12px;
      padding:12px 16px;
      font:inherit;
      font-weight:700;
      cursor:pointer;
      transition:transform 120ms ease, box-shadow 120ms ease, background 120ms ease;
    }
    .secondary-card button:hover,
    .toggle-btn:hover {
      transform:translateY(-1px);
      box-shadow:0 10px 20px rgba(67, 48, 24, 0.12);
    }
    .primary-grid {
      display:grid;
      grid-template-columns:repeat(2, minmax(0, 1fr));
      gap:16px;
      margin-bottom:18px;
    }
    .main-card {
      background:linear-gradient(180deg, rgba(255,255,255,0.92), rgba(246,249,252,0.88));
      border:1px solid var(--line);
      border-radius:24px;
      padding:20px;
      box-shadow:var(--shadow);
      display:flex;
      flex-direction:column;
      gap:16px;
      min-height:248px;
      position:relative;
      overflow:hidden;
    }
    .main-card::before {
      content:"";
      position:absolute;
      inset:auto -18px -26px auto;
      width:118px;
      height:118px;
      border-radius:50%;
      background:radial-gradient(circle, rgba(217,229,243,0.85), transparent 72%);
      pointer-events:none;
    }
    .main-card-header { position:relative; z-index:1; }
    .eyebrow {
      font-size:11px;
      text-transform:uppercase;
      letter-spacing:0.08em;
      color:var(--muted);
      margin-bottom:8px;
    }
    .main-card h2 {
      font-size:30px;
      line-height:1.05;
      margin-bottom:6px;
      font-family: Georgia, "Times New Roman", serif;
    }
    .toggle-wrap {
      display:grid;
      margin-top:auto;
      position:relative;
      z-index:1;
    }
    .toggle-btn {
      min-height:108px;
      border-radius:20px;
      font-size:20px;
      letter-spacing:0.06em;
      text-transform:uppercase;
      box-shadow:
        inset 0 1px 0 rgba(255,255,255,0.85),
        0 8px 18px rgba(70, 88, 112, 0.12);
    }
    .toggle-btn.active {
      background:linear-gradient(180deg, #f3f8ff, #d6e8ff);
      color:#1f4f8f;
      border:2px solid rgba(63,117,191,0.62);
    }
    .toggle-btn.inactive {
      background:linear-gradient(180deg, #fafbfd, #e7edf4);
      color:#4c5e74;
      border:2px solid rgba(138,152,168,0.5);
    }
    .secondary-section {
      background:rgba(255,255,255,0.62);
      border:1px solid var(--line);
      border-radius:22px;
      padding:18px;
      box-shadow:var(--shadow);
      margin-bottom:16px;
    }
    .secondary-head {
      margin-bottom:14px;
    }
    .secondary-head h3 {
      font-size:22px;
      font-family: Georgia, "Times New Roman", serif;
      margin-bottom:6px;
    }
    .secondary-head p {
      margin:0;
      color:var(--muted);
      line-height:1.5;
      font-size:14px;
    }
    .secondary-grid {
      display:grid;
      grid-template-columns:repeat(auto-fit, minmax(210px, 1fr));
      gap:14px;
    }
    .secondary-card {
      background:var(--panel);
      border:1px solid var(--line);
      border-radius:18px;
      padding:16px;
      box-shadow:0 12px 28px rgba(75, 95, 122, 0.10);
      display:flex;
      flex-direction:column;
      gap:12px;
    }
    .secondary-card h2 {
      font-size:24px;
      line-height:1.1;
      font-family: Georgia, "Times New Roman", serif;
      margin-bottom:6px;
    }
    .secondary-card p {
      margin:0;
      color:var(--muted);
      line-height:1.5;
      font-size:14px;
    }
    .btn-row {
      display:grid;
      grid-template-columns:1fr 1fr;
      gap:10px;
      margin-top:auto;
    }
    .btn-primary {
      background:linear-gradient(180deg, #be5a70, #a64558);
      color:#fffafc;
    }
    .btn-secondary {
      background:#fff;
      color:var(--text);
      border:1px solid var(--line);
    }
    .log-panel {
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
    @media (max-width:820px) {
      .primary-grid { grid-template-columns:1fr; }
    }
    @media (max-width:700px) {
      .wrap { padding:14px 12px 24px; }
      .hero { padding:18px; }
      .btn-row { grid-template-columns:1fr; }
      .toggle-btn { min-height:92px; }
      .main-card { min-height:auto; }
    }
  </style>
</head>
<body>
  <div class="wrap">
    <section class="hero">
      <h1>Dashboard toranjskog sata</h1>
      <p class="subtitle">
        Glavne cetiri komande su odmah ispod, a dodatne opcije ostaju odvojene u donjem dijelu.
      </p>
    </section>

    <section class="primary-grid">
      <article class="main-card">
        <div class="main-card-header">
          <div class="eyebrow">Gornji red</div>
          <h2>Musko zvono</h2>
        </div>
        <div class="toggle-wrap">
          <button id="toggleBell1" class="toggle-btn inactive" onclick="prebaciGlavnuKomandu('bell1')">MU&Scaron;KO</button>
        </div>
      </article>

      <article class="main-card">
        <div class="main-card-header">
          <div class="eyebrow">Gornji red</div>
          <h2>Zensko zvono</h2>
        </div>
        <div class="toggle-wrap">
          <button id="toggleBell2" class="toggle-btn inactive" onclick="prebaciGlavnuKomandu('bell2')">&Zcaron;ENSKO</button>
        </div>
      </article>

      <article class="main-card">
        <div class="main-card-header">
          <div class="eyebrow">Donji red</div>
          <h2>Slavljenje</h2>
        </div>
        <div class="toggle-wrap">
          <button id="toggleSlavljenje" class="toggle-btn inactive" onclick="prebaciGlavnuKomandu('slavljenje')">SLAVI</button>
        </div>
      </article>

      <article class="main-card">
        <div class="main-card-header">
          <div class="eyebrow">Donji red</div>
          <h2>Mrtvacko</h2>
        </div>
        <div class="toggle-wrap">
          <button id="toggleMrtvacko" class="toggle-btn inactive" onclick="prebaciGlavnuKomandu('mrtvacko')">BRECA</button>
        </div>
      </article>
    </section>

    <section class="secondary-section">
      <div class="secondary-head">
        <h3>Dodatne opcije</h3>
        <p>Sunceve automatike i pomocne kontrole ostaju dostupne ispod glavne 2x2 matrice tipki toranjskog sata.</p>
      </div>
      <div class="secondary-grid">
        <article class="secondary-card">
          <div class="eyebrow">Sunceva automatika</div>
          <h2>Jutro</h2>
          <div class="btn-row">
            <button class="btn-primary" onclick="pozoviApi('/api/solar/morning/on', 'Sunce jutro ukljuci')">Ukljuci</button>
            <button class="btn-secondary" onclick="pozoviApi('/api/solar/morning/off', 'Sunce jutro iskljuci')">Iskljuci</button>
          </div>
        </article>

        <article class="secondary-card">
          <div class="eyebrow">Sunceva automatika</div>
          <h2>Podne</h2>
          <div class="btn-row">
            <button class="btn-primary" onclick="pozoviApi('/api/solar/noon/on', 'Sunce podne ukljuci')">Ukljuci</button>
            <button class="btn-secondary" onclick="pozoviApi('/api/solar/noon/off', 'Sunce podne iskljuci')">Iskljuci</button>
          </div>
        </article>

        <article class="secondary-card">
          <div class="eyebrow">Sunceva automatika</div>
          <h2>Vecer</h2>
          <div class="btn-row">
            <button class="btn-primary" onclick="pozoviApi('/api/solar/evening/on', 'Sunce vecer ukljuci')">Ukljuci</button>
            <button class="btn-secondary" onclick="pozoviApi('/api/solar/evening/off', 'Sunce vecer iskljuci')">Iskljuci</button>
          </div>
        </article>
      </div>
    </section>

    <section class="log-panel">
      <div id="odgovor" class="log-text">Dashboard je spreman. Odaberi karticu i posalji naredbu prema Megi.</div>
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

    let glavnoStanje = {
      bell1: null,
      bell2: null,
      slavljenje: null,
      mrtvacko: null
    };

    function postaviLog(poruka) {
      document.getElementById('odgovor').textContent = poruka;
    }

    function postaviTipkuStanja(kljuc, aktivno, statusPoznat) {
      const meta = glavneKomande[kljuc];
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

    async function osvjeziStatus(prisilno = false) {
      try {
        const putanjaStatusa = prisilno ? '/api/status?force=1' : '/api/status';
        const odgovor = await fetch(putanjaStatusa, { cache: 'no-store' });
        if (!odgovor.ok) {
          throw new Error('status');
        }
        const podaci = await odgovor.json();

        glavnoStanje.bell1 = podaci.mega_status_known ? !!podaci.bell1_active : null;
        glavnoStanje.bell2 = podaci.mega_status_known ? !!podaci.bell2_active : null;
        glavnoStanje.slavljenje = podaci.mega_status_known ? !!podaci.slavljenje_active : null;
        glavnoStanje.mrtvacko = podaci.mega_status_known ? !!podaci.mrtvacko_active : null;

        postaviTipkuStanja('bell1', glavnoStanje.bell1, !!podaci.mega_status_known);
        postaviTipkuStanja('bell2', glavnoStanje.bell2, !!podaci.mega_status_known);
        postaviTipkuStanja('slavljenje', glavnoStanje.slavljenje, !!podaci.mega_status_known);
        postaviTipkuStanja('mrtvacko', glavnoStanje.mrtvacko, !!podaci.mega_status_known);
      } catch (greska) {
        glavnoStanje.bell1 = null;
        glavnoStanje.bell2 = null;
        glavnoStanje.slavljenje = null;
        glavnoStanje.mrtvacko = null;

        postaviTipkuStanja('bell1', null, false);
        postaviTipkuStanja('bell2', null, false);
        postaviTipkuStanja('slavljenje', null, false);
        postaviTipkuStanja('mrtvacko', null, false);
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

