#if defined(ESP8266)
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <Updater.h>
using ToranjWebServer = ESP8266WebServer;
#elif defined(ESP32)
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
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
static const uint64_t NTP_MAKS_DOPUSTENO_ODSTUPANJE_PRVA_DVA_UZORKA_MS = 2000ULL;
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
bool ntpPrviUzorakTrebaPotvrdu = true;
bool ntpPrviUzorakZapamcen = false;
unsigned long ntpZadnjiUspjehMs = 0;
unsigned long ntpZadnjiPokusajMs = 0;
unsigned long ntpZahtjevPoslanMs = 0;
unsigned long ntpBazniMillis = 0;
uint64_t ntpBazniUtcMs = 0;
uint64_t ntpPrviUzorakUtcMs = 0ULL;
unsigned long wifiSpojenOdMs = 0;
bool wifiSpojenOdPoznat = false;
bool megaStatusPoznat = false;
bool megaZvono1Aktivno = false;
bool megaZvono2Aktivno = false;
bool megaSlavljenjeAktivno = false;
bool megaMrtvackoAktivno = false;
bool megaSunceJutroAktivno = false;
bool megaSuncePodneAktivno = false;
bool megaSunceVecerAktivno = false;
bool megaTihiRezimAktivan = false;
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
bool otaAzuriranjeUTijeku = false;
bool otaRestartZakazan = false;
unsigned long otaRestartZakazanMs = 0;
bool otaUspjesanZadnjiPut = false;

static const unsigned long OTA_RESTART_ODGODA_MS = 1200UL;

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
void obradiOtaUpload();
void zakaziRestartNakonOta();
void obradiZakazaniRestartNakonOta();
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
  ntpPrviUzorakTrebaPotvrdu = true;
  ntpPrviUzorakZapamcen = false;
  ntpZadnjiUspjehMs = 0;
  ntpZadnjiPokusajMs = 0;
  ntpZahtjevPoslanMs = 0;
  ntpBazniMillis = 0;
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
    Serial.println("NTPLOG: prvi NTP uzorak nakon restarta spremljen, cekam potvrdu drugim uzorkom");
    return false;
  }

  uint64_t razlikaMs = 0ULL;
  if (utcMs >= ntpPrviUzorakUtcMs) {
    razlikaMs = utcMs - ntpPrviUzorakUtcMs;
  } else {
    razlikaMs = ntpPrviUzorakUtcMs - utcMs;
  }

  if (razlikaMs > NTP_MAKS_DOPUSTENO_ODSTUPANJE_PRVA_DVA_UZORKA_MS) {
    ntpPrviUzorakUtcMs = utcMs;
    Serial.print("NTPLOG: drugi NTP uzorak previse odstupa od prvog, ponavljam potvrdu, razlika_ms=");
    Serial.println(static_cast<unsigned long>(razlikaMs));
    return false;
  }

  ntpPrviUzorakTrebaPotvrdu = false;
  ntpPrviUzorakZapamcen = false;
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
  bool sunceJutroAktivno = false;
  bool suncePodneAktivno = false;
  bool sunceVecerAktivno = false;
  bool tihiRezimAktivan = false;

  if (!procitajStatusZastavicu(payload, "b1=", &zvono1Aktivno) ||
      !procitajStatusZastavicu(payload, "b2=", &zvono2Aktivno) ||
      !procitajStatusZastavicu(payload, "sl=", &slavljenjeAktivno) ||
      !procitajStatusZastavicu(payload, "mr=", &mrtvackoAktivno) ||
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
  megaSunceJutroAktivno = sunceJutroAktivno;
  megaSuncePodneAktivno = suncePodneAktivno;
  megaSunceVecerAktivno = sunceVecerAktivno;
  megaTihiRezimAktivan = tihiRezimAktivan;
  megaStatusPoznat = true;
  megaStatusZadnjeOsvjezavanjeMs = millis();
  ++megaStatusSerijskiBroj;
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

  if (!prihvatiNtpUzorakZaToranjskiSat(utcMs)) {
    return false;
  }

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

  char tijelo[352];
  snprintf_P(tijelo,
             sizeof(tijelo),
             PSTR("{\"wifi_ip\":\"%s\",\"wifi_connected\":%s,\"mega_status_known\":%s,\"bell1_active\":%s,\"bell2_active\":%s,\"slavljenje_active\":%s,\"mrtvacko_active\":%s,\"solar_morning_active\":%s,\"solar_noon_active\":%s,\"solar_evening_active\":%s,\"silent_mode_active\":%s}"),
             ipBuffer,
             (WiFi.status() == WL_CONNECTED) ? "true" : "false",
             megaStatusPoznat ? "true" : "false",
             megaZvono1Aktivno ? "true" : "false",
             megaZvono2Aktivno ? "true" : "false",
             megaSlavljenjeAktivno ? "true" : "false",
             megaMrtvackoAktivno ? "true" : "false",
             megaSunceJutroAktivno ? "true" : "false",
             megaSuncePodneAktivno ? "true" : "false",
             megaSunceVecerAktivno ? "true" : "false",
             megaTihiRezimAktivan ? "true" : "false");
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
    .mini-card {
      padding:0;
    }
    .quiet-mode-wrap {
      margin-top:14px;
    }
    .log-panel {
      padding:14px 16px;
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
        suncevoStanje.jutro = podaci.mega_status_known ? !!podaci.solar_morning_active : null;
        suncevoStanje.podne = podaci.mega_status_known ? !!podaci.solar_noon_active : null;
        suncevoStanje.vecer = podaci.mega_status_known ? !!podaci.solar_evening_active : null;
        tihiModAktivan = podaci.mega_status_known ? !!podaci.silent_mode_active : null;

        postaviTipkuStanja('bell1', glavnoStanje.bell1, !!podaci.mega_status_known);
        postaviTipkuStanja('bell2', glavnoStanje.bell2, !!podaci.mega_status_known);
        postaviTipkuStanja('slavljenje', glavnoStanje.slavljenje, !!podaci.mega_status_known);
        postaviTipkuStanja('mrtvacko', glavnoStanje.mrtvacko, !!podaci.mega_status_known);
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
        suncevoStanje.jutro = null;
        suncevoStanje.podne = null;
        suncevoStanje.vecer = null;
        tihiModAktivan = null;

        postaviTipkuStanja('bell1', null, false);
        postaviTipkuStanja('bell2', null, false);
        postaviTipkuStanja('slavljenje', null, false);
        postaviTipkuStanja('mrtvacko', null, false);
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

#if defined(ESP8266)
      const uint32_t maksimalnaVelicina =
          (ESP.getFreeSketchSpace() - 0x1000U) & 0xFFFFF000U;
      if (!Update.begin(maksimalnaVelicina)) {
        Update.printError(Serial);
      }
#elif defined(ESP32)
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
        Update.printError(Serial);
      }
#endif
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
#if defined(ESP32)
      Update.abort();
#endif
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

