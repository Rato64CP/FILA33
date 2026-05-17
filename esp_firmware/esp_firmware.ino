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

// Glavni ulazni modul ESP32 firmwarea toranjskog sata.
// Ovdje ostaju globalne strukture, konfiguracija i forward deklaracije,
// dok su vece funkcionalne cjeline razdvojene u zasebne .ino datoteke:
// - esp_boot_wifi.ino
// - esp_serial_mega.ino
// - esp_time_ntp.ino
// - esp_web.ino
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
static const uint8_t BROJ_NEPOMICNIH_BLAGDANA = 15;
static const uint8_t BROJ_POMICNIH_BLAGDANA = 7;

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

struct MegaNepomicniBlagdan {
  bool omogucen;
  uint8_t satMise;
  uint8_t minutaMise;
};

struct MegaPomicniBlagdan {
  bool omogucen;
  uint8_t satMise;
  uint8_t minutaMise;
};

struct MegaBlagdanskePostavke {
  bool poznateMise;
  bool poznatiNepomicni;
  bool poznatiPomicni;
  MegaNepomicniBlagdan nepomicni[BROJ_NEPOMICNIH_BLAGDANA];
  MegaPomicniBlagdan pomicni[BROJ_POMICNIH_BLAGDANA];
  bool dnevnaMisaOmogucena;
  uint8_t dnevnaMisaSat;
  uint8_t dnevnaMisaMinuta;
  bool nedjeljnaMisaOmogucena;
  uint8_t nedjeljnaMisaSat;
  uint8_t nedjeljnaMisaMinuta;
  unsigned long serijskiBrojMise;
  unsigned long serijskiBrojNepomicni;
  unsigned long serijskiBrojPomicni;
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
uint8_t megaPogrebnaSkriptaTip = 0;
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
  0UL
};
MegaPostavkeStapica megaPostavkeStapica = {
  false,
  0U,
  0U,
  0U,
  0U,
  0UL
};
MegaBATPostavke megaBATPostavke = {
  false,
  0U,
  0U,
  0U,
  0U,
  0U,
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
  0UL
};
MegaBlagdanskePostavke megaBlagdanskePostavke = {};

static bool suSveBlagdanskeSkupinePoznate() {
  return megaBlagdanskePostavke.poznateMise &&
         megaBlagdanskePostavke.poznatiNepomicni &&
         megaBlagdanskePostavke.poznatiPomicni;
}

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
bool obradiMisePostavkeMegai(char* payload);
bool osvjeziMisePostavkeMegai(bool prisilno);
bool obradiNepomicneBlagdaneMegai(char* payload);
bool osvjeziNepomicneBlagdaneMegai(bool prisilno);
bool obradiPomicneBlagdaneMegai(char* payload);
bool osvjeziPomicneBlagdaneMegai(bool prisilno);
void posaljiJsonBlagdanskihPostavki(bool prisilno = false);
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
PostavkeOdgovorMegai posaljiMisePostavkeMegai(bool dnevnaOmogucena,
                                              unsigned int dnevnaSat,
                                              unsigned int dnevnaMinuta,
                                              bool nedjeljnaOmogucena,
                                              unsigned int nedjeljnaSat,
                                              unsigned int nedjeljnaMinuta,
                                              unsigned long timeoutMs);
PostavkeOdgovorMegai posaljiNepomicneBlagdaneMegai(const MegaNepomicniBlagdan* postavke,
                                                   uint8_t brojPostavki,
                                                   unsigned long timeoutMs);
PostavkeOdgovorMegai posaljiPomicneBlagdaneMegai(const MegaPomicniBlagdan* postavke,
                                                 uint8_t brojPostavki,
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
bool jePotpisaniDecimalniBrojString(const String& vrijednost);
void pokreniSetupPristupnuTocku();
void zaustaviSetupPristupnuTocku(bool zbogTimeouta);
void odrzavajSetupTipku();
void odrzavajSetupPristupnuTocku();
void osvjeziWiFiStatusLedicu();
void posaljiHtmlStranicuIzProgMema(PGM_P stranica);

