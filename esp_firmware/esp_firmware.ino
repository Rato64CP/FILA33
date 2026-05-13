#if defined(ESP32)
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
using ToranjWebServer = WebServer;
#else
#error "Ovaj firmware podrzava samo ESP32."
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

// Vrijednosti za privremenu setup mrezu toranjskog sata na vanjskom ESP32 modulu.
static const char WIFI_SETUP_AP_SSID[] = "ZVONKO_setup";
static const char WIFI_SETUP_AP_LOZINKA[] = "zvonko10";
static const uint8_t WIFI_SETUP_PIN = 27;       // Predlozeni tipkalo prema GND
static const uint8_t WIFI_STATUS_LED_PIN = 26;  // Predlozena status LED
static const int ESP_MEGA_RX_PIN = 16;          // ESP32 RX prema Mega TX3 preko djelitelja
static const int ESP_MEGA_TX_PIN = 17;          // ESP32 TX prema Mega RX3
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

enum PostavkeOdgovorMegai {
  POSTAVKE_ODGOVOR_CEKA = 0,
  POSTAVKE_ODGOVOR_OK,
  POSTAVKE_ODGOVOR_ERR,
  POSTAVKE_ODGOVOR_TIMEOUT
};

struct MegaSustavskePostavke {
  bool poznate;
  bool lcdPozadinskoOsvjetljenje;
  bool logiranje;
  bool rs485;
  bool upsMod;
  bool kocnicaZvona;
  uint8_t inercijaZvona1Sekunde;
  uint8_t inercijaZvona2Sekunde;
  unsigned int impulsCekicaMs;
  unsigned long serijskiBroj;
};

struct MegaPostavkeStapica {
  bool poznate;
  uint8_t trajanjeRadniMin;
  uint8_t trajanjeNedjeljaMin;
  uint8_t trajanjeSlavljenjaMin;
  uint8_t odgodaSlavljenjaSekunde;
  unsigned long serijskiBroj;
};

struct MegaBATPostavke {
  bool poznate;
  uint8_t satOd;
  uint8_t satDo;
  uint8_t modOtkucavanja;
  uint8_t modSlavljenja;
  uint8_t modMrtvackog;
  unsigned long serijskiBroj;
};

struct MegaSuncevePostavke {
  bool poznate;
  bool jutroOmoguceno;
  uint8_t jutroZvono;
  int8_t jutroOdgodaMin;
  bool podneOmoguceno;
  uint8_t podneZvono;
  bool vecerOmoguceno;
  uint8_t vecerZvono;
  int8_t vecerOdgodaMin;
  bool nocnaRasvjeta;
  unsigned long serijskiBroj;
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
unsigned long ntpPrviUzorakPrimljenMs = 0;
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
PostavkeOdgovorMegai zadnjiOdgovorSustavskihPostavkiMega = POSTAVKE_ODGOVOR_CEKA;
MegaSustavskePostavke megaSustavskePostavke = {
  false,
  false,
  false,
  false,
  false,
  false,
  0,
  0,
  0U,
  0UL,
  0UL
};
MegaPostavkeStapica megaPostavkeStapica = {
  false,
  0U,
  0U,
  0U,
  0U,
  0UL,
  0UL
};
MegaBATPostavke megaBATPostavke = {
  false,
  0U,
  0U,
  0U,
  0U,
  0U,
  0UL,
  0UL
};
MegaSuncevePostavke megaSuncevePostavke = {
  false,
  false,
  0U,
  0,
  false,
  0U,
  false,
  0U,
  0,
  false,
  0UL,
  0UL
};

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
bool obradiSustavskePostavkeMegai(char* payload);
bool osvjeziSustavskePostavkeMegai(bool prisilno);
void posaljiJsonSustavskihPostavki(bool prisilno = false);
bool obradiPostavkeStapicaMegai(char* payload);
bool osvjeziPostavkeStapicaMegai(bool prisilno);
void posaljiJsonPostavkiStapica(bool prisilno = false);
bool obradiBATPostavkeMegai(char* payload);
bool osvjeziBATPostavkeMegai(bool prisilno);
void posaljiJsonBATPostavki(bool prisilno = false);
bool obradiSuncevePostavkeMegai(char* payload);
bool osvjeziSuncevePostavkeMegai(bool prisilno);
void posaljiJsonSuncevihPostavki(bool prisilno = false);
PostavkeOdgovorMegai posaljiSustavskePostavkeMegai(bool lcdPozadinskoOsvjetljenje,
                                                   bool logiranje,
                                                   bool rs485,
                                                   bool upsMod,
                                                   bool kocnicaZvona,
                                                   unsigned int inercijaZvona1Sekunde,
                                                   unsigned int inercijaZvona2Sekunde,
                                                   unsigned int impulsCekicaMs,
                                                   unsigned long timeoutMs);
PostavkeOdgovorMegai posaljiPostavkeStapicaMegai(unsigned int trajanjeRadniMin,
                                                 unsigned int trajanjeNedjeljaMin,
                                                 unsigned int trajanjeSlavljenjaMin,
                                                 unsigned int odgodaSlavljenjaSekunde,
                                                 unsigned long timeoutMs);
PostavkeOdgovorMegai posaljiBATPostavkeMegai(unsigned int satOd,
                                             unsigned int satDo,
                                             unsigned int modOtkucavanja,
                                             unsigned int modSlavljenja,
                                             unsigned int modMrtvackog,
                                             unsigned long timeoutMs);
PostavkeOdgovorMegai posaljiSuncevePostavkeMegai(bool jutroOmoguceno,
                                                 unsigned int jutroZvono,
                                                 int jutroOdgodaMin,
                                                 bool podneOmoguceno,
                                                 unsigned int podneZvono,
                                                 bool vecerOmoguceno,
                                                 unsigned int vecerZvono,
                                                 int vecerOdgodaMin,
                                                 bool nocnaRasvjeta,
                                                 unsigned long timeoutMs);
void ucitajWebAutentikaciju();
bool osigurajWebAutorizaciju();
void posaljiApiKomanduMegai(const char* naredba, const char* odgovor);
void obradiOtaUpload();
void zakaziRestartNakonOta();
void obradiZakazaniRestartNakonOta();
CmdOdgovorMegai posaljiKomanduMegaiIPricekaj(const char* naredba, unsigned long timeoutMs);
bool posaljiSetupWiFiMegai(const String &ssid, const String &lozinka, String &odgovor, unsigned long timeoutMs);
bool jeDecimalniBrojString(const String& vrijednost);
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

