#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include <PubSubClient.h>
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

// Parametri NTP klijenta za sinkronizaciju toranjskog sata.
char ntpPosluzitelj[40] = "pool.ntp.org";
static const long NTP_OFFSET_SEKUNDI = 0;
static const unsigned long NTP_INTERVAL_MS = 60000;
static const size_t SERIJSKI_BUFFER_MAX = 256;
static const size_t WEB_LOZINKA_MAX = 33;
static const size_t ESP_EEPROM_VELICINA = 96;
static const uint16_t WEB_AUTH_POTPIS = 0x5741;

// MQTT zadane vjerodajnice.
// Mega ih po potrebi moze prepisati kroz MQTT:CONNECT naredbu.
static const uint16_t MQTT_ZADANI_PORT = 1883;
static const size_t MQTT_BROKER_MAX = 64;
static const size_t MQTT_TOPIC_MAX = 96;
static const size_t MQTT_PAYLOAD_MAX = 192;
static const size_t MQTT_MAX_PRETPLATA = 16;
static const size_t MQTT_KORISNIK_MAX = 33;
static const size_t MQTT_LOZINKA_MAX = 33;
static const unsigned long MQTT_POKUSAJ_INTERVAL_MS = 5000;

WiFiUDP ntpUDP;
NTPClient ntpKlijent(ntpUDP, ntpPosluzitelj, NTP_OFFSET_SEKUNDI, NTP_INTERVAL_MS);

ESP8266WebServer webPosluzitelj(80);
WiFiClient mqttMrezniKlijent;
PubSubClient mqttKlijent(mqttMrezniKlijent);

String zadnjiOdgovorMega = "";
String webLozinka = "cista2906";

struct WebAuthConfig {
  uint16_t potpis;
  char lozinka[WEB_LOZINKA_MAX];
};

struct MegaStatus {
  bool valjan = false;
  String vrijeme = "";
  String izvor = "";
  bool vrijemePotvrdeno = false;
  bool wifiSpojen = false;
  bool mqttSpojen = false;
  bool mqttOmogucen = false;
  bool ntpOmogucen = false;
  bool dcfOmogucen = false;
  bool dcfUTijeku = false;
  bool kazaljkeUSinkronu = false;
  int pozicijaKazaljki = 0;
  bool plocaUSinkronu = false;
  int pozicijaPloce = 0;
  bool slavljenje = false;
  bool mrtvacko = false;
  bool otkucavanje = false;
  bool zvono1 = false;
  bool zvono2 = false;
  unsigned long zadnjeOsvjezenjeMs = 0;
};

MegaStatus megaStatus;
unsigned long zadnjiStatusZahtjevMs = 0;
static const unsigned long STATUS_INTERVAL_MS = 15000UL;

// Statusne zastavice za faze rada.
bool wifiIkadSpojen = false;
bool ntpIkadPostavljen = false;
bool ntpPoslanNakonSpajanja = false;
long zadnjiPoslaniNtpSatniKljuc = -1;

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

// MQTT stanje i konfiguracija.
bool mqttKonfiguriran = false;
bool mqttStatusPrijavljenKaoSpojen = false;
unsigned long mqttZadnjiPokusajSpajanja = 0;
char mqttBrokerAdresa[MQTT_BROKER_MAX] = "";
uint16_t mqttBrokerPort = MQTT_ZADANI_PORT;
char mqttKorisnik[MQTT_KORISNIK_MAX] = "toranj";
char mqttLozinka[MQTT_LOZINKA_MAX] = "toranj2024";
char mqttPretplate[MQTT_MAX_PRETPLATA][MQTT_TOPIC_MAX];
size_t mqttBrojPretplata = 0;

// Popis naredbi koje prihvaca toranjski sat.
const char *DOZVOLJENE_NAREDBE[] = {
  "ZVONO1_ON", "ZVONO1_OFF", "ZVONO2_ON", "ZVONO2_OFF",
  "OTKUCAVANJE_ON", "OTKUCAVANJE_OFF",
  "SLAVLJENJE_ON", "SLAVLJENJE_OFF",
  "MRTVACKO_ON", "MRTVACKO_OFF",
  "DCF_START"
};
const size_t BROJ_NAREDBI = sizeof(DOZVOLJENE_NAREDBE) / sizeof(DOZVOLJENE_NAREDBE[0]);

void poveziNaWiFi();
bool postaviStatickuKonfiguraciju();
void osvjeziNTPSat();
void posaljiNTPAkoTreba();
void posaljiNTPPremaMegai();
void obradiSerijskiUlaz();
void pokupiWifiKonfiguracijuIzSerijske(unsigned long millisTimeout = 3000);
void konfigurirajWebPosluzitelj();
void saljiNaredbuMegai(const String &naredba);
void prijaviPromjenuWiFiStatusa();
unsigned long dohvatiWiFiOdgoduNovogPokusaja();
bool jeNaredbaDozvoljena(const String &naredba);
bool jePrijestupnaGodina(int godina);
int danUTjednu(int godina, int mjesec, int dan);
int zadnjaNedjeljaUMjesecu(int godina, int mjesec);
bool jeLjetnoVrijemeEU(time_t utcEpoch);
time_t konvertirajUTCuLokalnoVrijeme(time_t utcEpoch);
String escapirajJsonString(const String &ulaz);
void obradiNTPSerijskuNaredbu(const String &linija);
void inicijalizirajMQTT();
void obradiMQTT();
void mqttPorukaPrimljena(char *tema, byte *payload, unsigned int duljina);
void obradiMQTTSerijskuNaredbu(const String &linija);
void postaviMQTTStatus(bool spojeno);
void ispisiMQTTStatus();
bool pokusajMQTTSpajanja(bool odmah);
bool spremiMQTTPretplatu(const String &tema);
void ponovnoPretplatiMQTTTeme();
bool parsirajMQTTConnectPayload(const String &payload, String &broker, uint16_t &port, String &korisnik, String &lozinka);
String ocistiJednolinijskiTekst(const String &ulaz, size_t maxDuljina);
void primijeniWiFiOmogucenost(bool omogucen);
void zatraziStatusMegae(bool prisilno = false);
void obradiStatusMegaLiniju(const String &linija);
bool dohvatiPoljeStatusa(const String &linija, const String &kljuc, String &vrijednost);
bool tekstUBool(const String &vrijednost);
String kreirajJsonStatus();
String generirajWebSucelje();
void ucitajWebAutentikaciju();
bool spremiWebAutentikaciju(const String &novaLozinka);
bool osigurajWebAutorizaciju();
bool spremiMQTTPostavkePrekoWeba(const String &broker,
                                 uint16_t port,
                                 const String &korisnik,
                                 const String &lozinka,
                                 bool omogucen);

void setup() {
  Serial.begin(9600);
  delay(200);
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

  Serial.println("FAZA: Pokretanje NTP klijenta");
  ntpKlijent.begin();

  Serial.println("FAZA: Konfiguracija MQTT klijenta");
  inicijalizirajMQTT();

  Serial.println("FAZA: Konfiguracija web posluzitelja");
  konfigurirajWebPosluzitelj();

  Serial.println("CFGREQ");
  Serial.println("FAZA: INIT zavrsen, ulazak u loop()");
}

void loop() {
  obradiSerijskiUlaz();

  if (wifiOmogucen) {
    if (WiFi.status() != WL_CONNECTED) {
      poveziNaWiFi();
    }

    osvjeziNTPSat();
    posaljiNTPAkoTreba();
    zatraziStatusMegae(false);
  }

  obradiMQTT();
  webPosluzitelj.handleClient();
}

void zatraziStatusMegae(bool prisilno) {
  unsigned long sada = millis();
  if (!prisilno && (sada - zadnjiStatusZahtjevMs) < STATUS_INTERVAL_MS) {
    return;
  }

  zadnjiStatusZahtjevMs = sada;
  Serial.println("STATUS?");
}

void prijaviPromjenuWiFiStatusa() {
  wl_status_t trenutniStatus = wifiOmogucen ? WiFi.status() : WL_DISCONNECTED;
  if (trenutniStatus == zadnjiPrijavljeniWiFiStatus) {
    return;
  }

  zadnjiPrijavljeniWiFiStatus = trenutniStatus;
  if (trenutniStatus == WL_CONNECTED) {
    ntpPoslanNakonSpajanja = false;
    Serial.println("WIFI:CONNECTED");
  } else {
    ntpPoslanNakonSpajanja = false;
    postaviMQTTStatus(false);
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
  wifiIkadSpojen = false;
  ntpIkadPostavljen = false;
  ntpPoslanNakonSpajanja = false;
  zadnjiPoslaniNtpSatniKljuc = -1;

  if (!wifiOmogucen) {
    if (mqttKlijent.connected()) {
      mqttKlijent.disconnect();
    }
    postaviMQTTStatus(false);
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
    prijaviPromjenuWiFiStatusa();
    return;
  }

  if (WiFi.status() == WL_CONNECTED) {
    if (wifiPokusajUToku) {
      Serial.println();
      Serial.print("WIFI: Spojen, IP: ");
      Serial.println(WiFi.localIP());
    }
    wifiIkadSpojen = true;
    wifiPokusajUToku = false;
    wifiBrojPokusajaZaredom = 0;
    prijaviPromjenuWiFiStatusa();
    return;
  }

  prijaviPromjenuWiFiStatusa();

  if (!wifiPokusajUToku && sada >= wifiSljedeciPokusajDozvoljen) {
    WiFi.mode(WIFI_STA);

    if (koristiDhcp) {
      WiFi.config(0U, 0U, 0U);
      Serial.println("WIFI: DHCP konfiguracija aktivna");
    } else if (!postaviStatickuKonfiguraciju()) {
      Serial.println("WIFI: Staticka konfiguracija neispravna, prelazim na DHCP");
      koristiDhcp = true;
      WiFi.config(0U, 0U, 0U);
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
    Serial.println("WIFI: ESP8266 nije prihvatio staticku konfiguraciju");
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

void posaljiNTPAkoTreba() {
  if (WiFi.status() != WL_CONNECTED || !ntpKlijent.isTimeSet()) {
    return;
  }

  time_t utcEpoch = ntpKlijent.getEpochTime();
  time_t lokalniEpoch = konvertirajUTCuLokalnoVrijeme(utcEpoch);
  struct tm lokalniTm;
  if (gmtime_r(&lokalniEpoch, &lokalniTm) == nullptr) {
    Serial.println("NTPLOG: lokalna satnica nije dostupna za slanje");
    return;
  }

  long satniKljuc = (lokalniTm.tm_year + 1900) * 1000000L +
                    (lokalniTm.tm_mon + 1) * 10000L +
                    lokalniTm.tm_mday * 100L +
                    lokalniTm.tm_hour;

  if (!ntpPoslanNakonSpajanja) {
    posaljiNTPPremaMegai();
    ntpPoslanNakonSpajanja = true;
    zadnjiPoslaniNtpSatniKljuc = satniKljuc;
    Serial.println("NTPLOG: prvo slanje nakon WiFi spajanja");
    return;
  }

  if (satniKljuc != zadnjiPoslaniNtpSatniKljuc) {
    posaljiNTPPremaMegai();
    zadnjiPoslaniNtpSatniKljuc = satniKljuc;
    Serial.println("NTPLOG: satna obnova vremena poslana Megi");
  }
}

void posaljiNTPPremaMegai() {
  time_t utcEpoch = ntpKlijent.getEpochTime();
  time_t lokalniEpoch = konvertirajUTCuLokalnoVrijeme(utcEpoch);
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
  Serial.println(isoBuffer);
}

void inicijalizirajMQTT() {
  mqttKlijent.setCallback(mqttPorukaPrimljena);
  mqttKlijent.setBufferSize(512);
  postaviMQTTStatus(false);
}

void obradiMQTT() {
  if (WiFi.status() != WL_CONNECTED) {
    if (mqttKlijent.connected()) {
      mqttKlijent.disconnect();
    }
    postaviMQTTStatus(false);
    return;
  }

  mqttKlijent.loop();

  bool stvarnoSpojen = mqttKlijent.connected();
  if (stvarnoSpojen != mqttStatusPrijavljenKaoSpojen) {
    postaviMQTTStatus(stvarnoSpojen);
  }

  if (!mqttKonfiguriran) {
    return;
  }

  if (!mqttKlijent.connected()) {
    pokusajMQTTSpajanja(false);
  }
}

void mqttPorukaPrimljena(char *tema, byte *payload, unsigned int duljina) {
  String temaString = ocistiJednolinijskiTekst(String(tema), MQTT_TOPIC_MAX - 1);
  String payloadString = "";
  payloadString.reserve(duljina);

  for (unsigned int i = 0; i < duljina; ++i) {
    char znak = static_cast<char>(payload[i]);
    if (znak == '\r' || znak == '\n') {
      payloadString += ' ';
    } else {
      payloadString += znak;
    }
  }

  payloadString = ocistiJednolinijskiTekst(payloadString, MQTT_PAYLOAD_MAX - 1);

  Serial.print("MQTT:MSG|");
  Serial.print(temaString);
  Serial.print("|");
  Serial.println(payloadString);
}

void postaviMQTTStatus(bool spojeno) {
  if (mqttStatusPrijavljenKaoSpojen == spojeno) {
    return;
  }

  mqttStatusPrijavljenKaoSpojen = spojeno;
  if (spojeno) {
    Serial.println("MQTT:CONNECTED");
  } else {
    Serial.println("MQTT:DISCONNECTED");
  }
}

void ispisiMQTTStatus() {
  bool spojeno = mqttKlijent.connected();
  mqttStatusPrijavljenKaoSpojen = spojeno;
  Serial.println(spojeno ? "MQTT:CONNECTED" : "MQTT:DISCONNECTED");
}

bool pokusajMQTTSpajanja(bool odmah) {
  if (!mqttKonfiguriran || WiFi.status() != WL_CONNECTED) {
    postaviMQTTStatus(false);
    return false;
  }

  unsigned long sada = millis();
  if (!odmah && (sada - mqttZadnjiPokusajSpajanja < MQTT_POKUSAJ_INTERVAL_MS)) {
    return false;
  }
  mqttZadnjiPokusajSpajanja = sada;

  mqttKlijent.setServer(mqttBrokerAdresa, mqttBrokerPort);

  String clientId = "toranj-esp-";
  clientId += String(ESP.getChipId(), HEX);

  bool spojeno = false;
  if (strlen(mqttKorisnik) > 0) {
    spojeno = mqttKlijent.connect(clientId.c_str(), mqttKorisnik, mqttLozinka);
  } else {
    spojeno = mqttKlijent.connect(clientId.c_str());
  }

  if (spojeno) {
    Serial.print("MQTTLOG: Spojen na broker ");
    Serial.print(mqttBrokerAdresa);
    Serial.print(":");
    Serial.println(mqttBrokerPort);
    ponovnoPretplatiMQTTTeme();
    postaviMQTTStatus(true);
    return true;
  }

  Serial.print("MQTTLOG: Spajanje nije uspjelo, rc=");
  Serial.println(mqttKlijent.state());
  postaviMQTTStatus(false);
  return false;
}

bool spremiMQTTPretplatu(const String &tema) {
  if (tema.length() == 0 || tema.length() >= MQTT_TOPIC_MAX) {
    Serial.println("MQTTLOG: tema za pretplatu je prazna ili preduga");
    return false;
  }

  for (size_t i = 0; i < mqttBrojPretplata; ++i) {
    if (tema.equals(mqttPretplate[i])) {
      return true;
    }
  }

  if (mqttBrojPretplata >= MQTT_MAX_PRETPLATA) {
    Serial.println("MQTTLOG: dosegnut limit spremljenih pretplata");
    return false;
  }

  tema.toCharArray(mqttPretplate[mqttBrojPretplata], MQTT_TOPIC_MAX);
  mqttBrojPretplata++;
  return true;
}

void ponovnoPretplatiMQTTTeme() {
  for (size_t i = 0; i < mqttBrojPretplata; ++i) {
    if (mqttPretplate[i][0] == '\0') {
      continue;
    }

    bool uspjeh = mqttKlijent.subscribe(mqttPretplate[i]);
    Serial.print("MQTTLOG: pretplata ");
    Serial.print(mqttPretplate[i]);
    Serial.println(uspjeh ? " OK" : " ERR");
  }
}

bool parsirajMQTTConnectPayload(const String &payload, String &broker, uint16_t &port, String &korisnik, String &lozinka) {
  int granica1 = payload.indexOf('|');
  int granica2 = (granica1 >= 0) ? payload.indexOf('|', granica1 + 1) : -1;
  int granica3 = (granica2 >= 0) ? payload.indexOf('|', granica2 + 1) : -1;
  if (granica1 <= 0 || granica2 <= granica1 || granica3 <= granica2) {
    return false;
  }

  broker = payload.substring(0, granica1);
  String portString = payload.substring(granica1 + 1, granica2);
  korisnik = payload.substring(granica2 + 1, granica3);
  lozinka = payload.substring(granica3 + 1);
  broker.trim();
  portString.trim();
  korisnik = ocistiJednolinijskiTekst(korisnik, MQTT_KORISNIK_MAX - 1);
  lozinka = ocistiJednolinijskiTekst(lozinka, MQTT_LOZINKA_MAX - 1);

  if (broker.length() == 0 || broker.length() >= MQTT_BROKER_MAX) {
    return false;
  }

  long procitaniPort = portString.toInt();
  if (procitaniPort <= 0 || procitaniPort > 65535) {
    return false;
  }
  if (korisnik.length() >= MQTT_KORISNIK_MAX || lozinka.length() >= MQTT_LOZINKA_MAX) {
    return false;
  }

  port = static_cast<uint16_t>(procitaniPort);
  return true;
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

void obradiMQTTSerijskuNaredbu(const String &linija) {
  if (linija == "MQTT:STATUS") {
    ispisiMQTTStatus();
    return;
  }

  if (linija == "MQTT:DISCONNECT") {
    if (mqttKlijent.connected()) {
      mqttKlijent.disconnect();
    }
    postaviMQTTStatus(false);
    return;
  }

  if (linija.startsWith("MQTT:CONNECT|")) {
    String broker = "";
    uint16_t port = MQTT_ZADANI_PORT;
    String korisnik = "";
    String lozinka = "";
    if (!parsirajMQTTConnectPayload(linija.substring(13), broker, port, korisnik, lozinka)) {
      Serial.println("MQTTLOG: neispravan CONNECT payload");
      postaviMQTTStatus(false);
      return;
    }

    broker.toCharArray(mqttBrokerAdresa, MQTT_BROKER_MAX);
    mqttBrokerPort = port;
    korisnik.toCharArray(mqttKorisnik, MQTT_KORISNIK_MAX);
    lozinka.toCharArray(mqttLozinka, MQTT_LOZINKA_MAX);
    mqttKonfiguriran = true;

    if (mqttKlijent.connected()) {
      mqttKlijent.disconnect();
      postaviMQTTStatus(false);
    }

    pokusajMQTTSpajanja(true);
    return;
  }

  if (linija.startsWith("MQTT:SUB|")) {
    String tema = ocistiJednolinijskiTekst(linija.substring(9), MQTT_TOPIC_MAX - 1);
    tema.trim();
    if (!spremiMQTTPretplatu(tema)) {
      Serial.println("MQTTLOG: spremanje pretplate nije uspjelo");
      return;
    }

    if (mqttKlijent.connected()) {
      bool uspjeh = mqttKlijent.subscribe(tema.c_str());
      Serial.print("MQTTLOG: SUB ");
      Serial.print(tema);
      Serial.println(uspjeh ? " OK" : " ERR");
    }
    return;
  }

  if (linija.startsWith("MQTT:PUB|")) {
    String payload = linija.substring(9);
    int granica = payload.indexOf('|');
    if (granica <= 0) {
      Serial.println("MQTTLOG: neispravan PUB payload");
      return;
    }

    String tema = ocistiJednolinijskiTekst(payload.substring(0, granica), MQTT_TOPIC_MAX - 1);
    String poruka = ocistiJednolinijskiTekst(payload.substring(granica + 1), MQTT_PAYLOAD_MAX - 1);
    tema.trim();

    if (tema.length() == 0) {
      Serial.println("MQTTLOG: prazna tema za publish");
      return;
    }

    if (!mqttKlijent.connected()) {
      if (!pokusajMQTTSpajanja(true)) {
        Serial.println("MQTTLOG: publish preskocen jer broker nije spojen");
        return;
      }
    }

    bool uspjeh = mqttKlijent.publish(tema.c_str(), poruka.c_str());
    Serial.print("MQTTLOG: PUB ");
    Serial.print(tema);
    Serial.println(uspjeh ? " OK" : " ERR");
    return;
  }

  Serial.print("MQTTLOG: nepoznata MQTT naredba ");
  Serial.println(linija);
}

void obradiSerijskiUlaz() {
  static String prijemniBuffer = "";

  while (Serial.available()) {
    char znak = static_cast<char>(Serial.read());
    if (znak == '\n') {
      prijemniBuffer.trim();
      if (prijemniBuffer.length() > 0) {
        zadnjiOdgovorMega = prijemniBuffer;

        if (prijemniBuffer.startsWith("STATUS:")) {
          obradiStatusMegaLiniju(prijemniBuffer);
        } else if (prijemniBuffer.startsWith("MQTTCFG:")) {
          String payload = prijemniBuffer.substring(8);
          String broker = "";
          uint16_t port = MQTT_ZADANI_PORT;
          String korisnik = "";
          String lozinka = "";
          bool omogucen = false;
          int g1 = payload.indexOf('|');
          int g2 = (g1 >= 0) ? payload.indexOf('|', g1 + 1) : -1;
          int g3 = (g2 >= 0) ? payload.indexOf('|', g2 + 1) : -1;
          if (g1 > 0 && g2 > g1 && g3 > g2) {
            String enabledTekst = payload.substring(0, g1);
            broker = payload.substring(g1 + 1, g2);
            String portTekst = payload.substring(g2 + 1, g3);
            korisnik = payload.substring(g3 + 1);
            broker.trim();
            korisnik.trim();
            omogucen = (enabledTekst == "1");
            long procitaniPort = portTekst.toInt();
            if (procitaniPort > 0 && procitaniPort <= 65535) {
              port = static_cast<uint16_t>(procitaniPort);
            }
            broker.toCharArray(mqttBrokerAdresa, MQTT_BROKER_MAX);
            mqttBrokerPort = port;
            korisnik.toCharArray(mqttKorisnik, MQTT_KORISNIK_MAX);
            mqttKonfiguriran = broker.length() > 0;
            if (!omogucen && mqttKlijent.connected()) {
              mqttKlijent.disconnect();
              postaviMQTTStatus(false);
            }
          }
        } else if (prijemniBuffer.startsWith("WIFIEN:")) {
          String payload = prijemniBuffer.substring(7);
          payload.trim();

          if (payload == "0" || payload == "1") {
            primijeniWiFiOmogucenost(payload == "1");
            Serial.println("ACK:WIFIEN");
          } else {
            Serial.println("ERR:WIFIEN");
          }
        } else if (prijemniBuffer.startsWith("WIFI:")) {
          String payload = prijemniBuffer.substring(5);
          bool uspjeh = false;

          int granice[5];
          int start = 0;
          for (int i = 0; i < 5; ++i) {
            granice[i] = payload.indexOf('|', start);
            if (granice[i] == -1) {
              Serial.println("WIFI RX: nedostaje separator | u postavkama");
              granice[0] = -1;
              break;
            }
            start = granice[i] + 1;
          }

          if (granice[0] != -1) {
            String noviSsid = payload.substring(0, granice[0]);
            String novaLozinka = payload.substring(granice[0] + 1, granice[1]);
            String dhcpZastavica = payload.substring(granice[1] + 1, granice[2]);
            String novaIp = payload.substring(granice[2] + 1, granice[3]);
            String novaMaska = payload.substring(granice[3] + 1, granice[4]);
            String noviGateway = payload.substring(granice[4] + 1);

            if (noviSsid.length() > 0 && novaLozinka.length() > 0 && dhcpZastavica.length() > 0) {
              wifiSsid = noviSsid;
              wifiLozinka = novaLozinka;
              koristiDhcp = dhcpZastavica == "1";
              statickaIp = novaIp;
              mreznaMaska = novaMaska;
              zadaniGateway = noviGateway;
              primljenaWifiKonfiguracija = true;

              Serial.print("WIFI RX: primljen SSID ");
              Serial.print(wifiSsid);
              Serial.print(", DHCP=");
              Serial.println(koristiDhcp ? "DA" : "NE");

              WiFi.disconnect();
              wifiPokusajUToku = false;
              wifiPokusajPocetak = 0;
              wifiSljedeciPokusajDozvoljen = 0;
              wifiBrojPokusajaZaredom = 0;
              wifiIkadSpojen = false;
              ntpIkadPostavljen = false;
              ntpPoslanNakonSpajanja = false;
              zadnjiPoslaniNtpSatniKljuc = -1;
              if (wifiOmogucen) {
                poveziNaWiFi();
              } else {
                prijaviPromjenuWiFiStatusa();
                Serial.println("WIFI RX: konfiguracija spremljena, WiFi je trenutno iskljucen");
              }
              uspjeh = true;
            } else {
              Serial.println("WIFI RX: neispravna duljina SSID/lozinke/DHCP oznake");
            }
          }

          Serial.println(uspjeh ? "ACK:WIFI" : "ERR:WIFI");
        } else if (prijemniBuffer == "NTPREQ:SYNC") {
          osvjeziNTPSat();
          if (WiFi.status() != WL_CONNECTED) {
            Serial.println("NTPLOG: rucni zahtjev odbijen jer WiFi nije spojen");
            Serial.println("ERR:NTPREQ");
          } else if (!ntpKlijent.isTimeSet()) {
            Serial.println("NTPLOG: rucni zahtjev odbijen jer NTP jos nije spreman");
            Serial.println("ERR:NTPREQ");
          } else {
            posaljiNTPPremaMegai();
            ntpPoslanNakonSpajanja = true;
            Serial.println("NTPLOG: rucni NTP zahtjev izvrsen");
            Serial.println("ACK:NTPREQ");
          }
        } else if (prijemniBuffer.startsWith("NTPCFG:")) {
          obradiNTPSerijskuNaredbu(prijemniBuffer);
        } else if (prijemniBuffer.startsWith("MQTT:")) {
          obradiMQTTSerijskuNaredbu(prijemniBuffer);
        }
      }
      prijemniBuffer = "";
    } else if (znak != '\r') {
      if (prijemniBuffer.length() < SERIJSKI_BUFFER_MAX) {
        prijemniBuffer += znak;
      } else {
        Serial.println("SERIJSKI RX: linija preduga, odbacujem");
        prijemniBuffer = "";
      }
    }
  }
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

String escapirajJsonString(const String &ulaz) {
  String izlaz = "";
  izlaz.reserve(ulaz.length() + 8);

  for (size_t i = 0; i < ulaz.length(); ++i) {
    char znak = ulaz.charAt(i);
    if (znak == '\\' || znak == '"') {
      izlaz += '\\';
      izlaz += znak;
    } else if (znak == '\r' || znak == '\n') {
      izlaz += ' ';
    } else {
      izlaz += znak;
    }
  }

  return izlaz;
}

bool dohvatiPoljeStatusa(const String &linija, const String &kljuc, String &vrijednost) {
  String trazeno = kljuc + "=";
  int pocetak = linija.indexOf(trazeno);
  if (pocetak < 0) {
    return false;
  }

  pocetak += trazeno.length();
  int kraj = linija.indexOf('|', pocetak);
  if (kraj < 0) {
    kraj = linija.length();
  }

  vrijednost = linija.substring(pocetak, kraj);
  vrijednost.trim();
  return true;
}

bool tekstUBool(const String &vrijednost) {
  return vrijednost == "1" || vrijednost.equalsIgnoreCase("true") || vrijednost.equalsIgnoreCase("da");
}

void ucitajWebAutentikaciju() {
  WebAuthConfig cfg{};
  EEPROM.get(0, cfg);

  if (cfg.potpis == WEB_AUTH_POTPIS && cfg.lozinka[0] != '\0') {
    cfg.lozinka[WEB_LOZINKA_MAX - 1] = '\0';
    webLozinka = String(cfg.lozinka);
    Serial.println("WEB AUTH: ucitana spremljena lozinka");
    return;
  }

  webLozinka = "cista2906";
  Serial.println("WEB AUTH: koristim zadanu lozinku");
}

bool spremiWebAutentikaciju(const String &novaLozinka) {
  String ociscena = ocistiJednolinijskiTekst(novaLozinka, WEB_LOZINKA_MAX - 1);
  ociscena.trim();
  if (ociscena.length() < 4) {
    return false;
  }

  WebAuthConfig cfg{};
  cfg.potpis = WEB_AUTH_POTPIS;
  ociscena.toCharArray(cfg.lozinka, sizeof(cfg.lozinka));
  EEPROM.put(0, cfg);
  if (!EEPROM.commit()) {
    Serial.println("WEB AUTH: commit lozinke nije uspio");
    return false;
  }

  webLozinka = ociscena;
  Serial.println("WEB AUTH: spremljena nova lozinka");
  return true;
}

bool osigurajWebAutorizaciju() {
  if (webPosluzitelj.authenticate("admin", webLozinka.c_str())) {
    return true;
  }

  webPosluzitelj.requestAuthentication(BASIC_AUTH, "Toranjski sat", "Unesite web lozinku");
  return false;
}

bool spremiMQTTPostavkePrekoWeba(const String &broker,
                                 uint16_t port,
                                 const String &korisnik,
                                 const String &lozinka,
                                 bool omogucen) {
  String cistiBroker = ocistiJednolinijskiTekst(broker, MQTT_BROKER_MAX - 1);
  String cistiKorisnik = ocistiJednolinijskiTekst(korisnik, MQTT_KORISNIK_MAX - 1);
  String cistaLozinka = ocistiJednolinijskiTekst(lozinka, MQTT_LOZINKA_MAX - 1);
  cistiBroker.trim();
  cistiKorisnik.trim();
  cistaLozinka.trim();

  if (cistiBroker.length() == 0 || cistiBroker.length() >= MQTT_BROKER_MAX) {
    return false;
  }
  if (port == 0 || port > 65535) {
    return false;
  }
  if (cistiKorisnik.length() >= MQTT_KORISNIK_MAX || cistaLozinka.length() >= MQTT_LOZINKA_MAX) {
    return false;
  }
  if (cistiBroker.indexOf('|') >= 0 || cistiKorisnik.indexOf('|') >= 0 || cistaLozinka.indexOf('|') >= 0) {
    return false;
  }

  cistiBroker.toCharArray(mqttBrokerAdresa, MQTT_BROKER_MAX);
  mqttBrokerPort = port;
  cistiKorisnik.toCharArray(mqttKorisnik, MQTT_KORISNIK_MAX);
  if (cistaLozinka.length() > 0) {
    cistaLozinka.toCharArray(mqttLozinka, MQTT_LOZINKA_MAX);
  }
  mqttKonfiguriran = true;

  String naredba = "WEBMQTT:";
  naredba += omogucen ? '1' : '0';
  naredba += '|';
  naredba += cistiBroker;
  naredba += '|';
  naredba += String(port);
  naredba += '|';
  naredba += cistiKorisnik;
  naredba += '|';
  naredba += cistaLozinka;
  Serial.println(naredba);
  return true;
}

void obradiStatusMegaLiniju(const String &linija) {
  String payload = linija.substring(7);
  String vrijednost = "";

  if (dohvatiPoljeStatusa(payload, "time", vrijednost)) megaStatus.vrijeme = vrijednost;
  if (dohvatiPoljeStatusa(payload, "src", vrijednost)) megaStatus.izvor = vrijednost;
  if (dohvatiPoljeStatusa(payload, "ok", vrijednost)) megaStatus.vrijemePotvrdeno = tekstUBool(vrijednost);
  if (dohvatiPoljeStatusa(payload, "wifi", vrijednost)) megaStatus.wifiSpojen = tekstUBool(vrijednost);
  if (dohvatiPoljeStatusa(payload, "mq", vrijednost)) megaStatus.mqttSpojen = tekstUBool(vrijednost);
  if (dohvatiPoljeStatusa(payload, "mqen", vrijednost)) megaStatus.mqttOmogucen = tekstUBool(vrijednost);
  if (dohvatiPoljeStatusa(payload, "ntp", vrijednost)) megaStatus.ntpOmogucen = tekstUBool(vrijednost);
  if (dohvatiPoljeStatusa(payload, "dcf", vrijednost)) megaStatus.dcfOmogucen = tekstUBool(vrijednost);
  if (dohvatiPoljeStatusa(payload, "dcfr", vrijednost)) megaStatus.dcfUTijeku = tekstUBool(vrijednost);
  if (dohvatiPoljeStatusa(payload, "hs", vrijednost)) megaStatus.kazaljkeUSinkronu = tekstUBool(vrijednost);
  if (dohvatiPoljeStatusa(payload, "hp", vrijednost)) megaStatus.pozicijaKazaljki = vrijednost.toInt();
  if (dohvatiPoljeStatusa(payload, "ps", vrijednost)) megaStatus.plocaUSinkronu = tekstUBool(vrijednost);
  if (dohvatiPoljeStatusa(payload, "pp", vrijednost)) megaStatus.pozicijaPloce = vrijednost.toInt();
  if (dohvatiPoljeStatusa(payload, "sl", vrijednost)) megaStatus.slavljenje = tekstUBool(vrijednost);
  if (dohvatiPoljeStatusa(payload, "mr", vrijednost)) megaStatus.mrtvacko = tekstUBool(vrijednost);
  if (dohvatiPoljeStatusa(payload, "ot", vrijednost)) megaStatus.otkucavanje = tekstUBool(vrijednost);
  if (dohvatiPoljeStatusa(payload, "b1", vrijednost)) megaStatus.zvono1 = tekstUBool(vrijednost);
  if (dohvatiPoljeStatusa(payload, "b2", vrijednost)) megaStatus.zvono2 = tekstUBool(vrijednost);

  megaStatus.valjan = true;
  megaStatus.zadnjeOsvjezenjeMs = millis();
}

String kreirajJsonStatus() {
  String tijelo = "{";
  tijelo += "\"wifi_ssid\":\"" + escapirajJsonString(WiFi.SSID()) + "\",";
  tijelo += "\"wifi_ip\":\"" + WiFi.localIP().toString() + "\",";
  tijelo += "\"wifi_connected\":";
  tijelo += (WiFi.status() == WL_CONNECTED) ? "true" : "false";
  tijelo += ",";
  tijelo += "\"mqtt\":\"";
  tijelo += (mqttKlijent.connected() ? "connected" : "disconnected");
  tijelo += "\",";
  tijelo += "\"mqtt_broker\":\"" + escapirajJsonString(String(mqttBrokerAdresa)) + "\",";
  tijelo += "\"mqtt_port\":";
  tijelo += mqttBrokerPort;
  tijelo += ",";
  tijelo += "\"mqtt_user\":\"" + escapirajJsonString(String(mqttKorisnik)) + "\",";
  tijelo += "\"mqtt_configured\":";
  tijelo += mqttKonfiguriran ? "true" : "false";
  tijelo += ",";
  tijelo += "\"ntp_server\":\"" + escapirajJsonString(String(ntpPosluzitelj)) + "\",";
  tijelo += "\"zadnji_odgovor\":\"" + escapirajJsonString(zadnjiOdgovorMega) + "\",";
  tijelo += "\"mega\":{";
  tijelo += "\"valid\":";
  tijelo += megaStatus.valjan ? "true" : "false";
  tijelo += ",";
  tijelo += "\"time\":\"" + escapirajJsonString(megaStatus.vrijeme) + "\",";
  tijelo += "\"source\":\"" + escapirajJsonString(megaStatus.izvor) + "\",";
  tijelo += "\"time_confirmed\":";
  tijelo += megaStatus.vrijemePotvrdeno ? "true" : "false";
  tijelo += ",";
  tijelo += "\"wifi_connected\":";
  tijelo += megaStatus.wifiSpojen ? "true" : "false";
  tijelo += ",";
  tijelo += "\"mqtt_connected\":";
  tijelo += megaStatus.mqttSpojen ? "true" : "false";
  tijelo += ",";
  tijelo += "\"mqtt_enabled\":";
  tijelo += megaStatus.mqttOmogucen ? "true" : "false";
  tijelo += ",";
  tijelo += "\"ntp_enabled\":";
  tijelo += megaStatus.ntpOmogucen ? "true" : "false";
  tijelo += ",";
  tijelo += "\"dcf_enabled\":";
  tijelo += megaStatus.dcfOmogucen ? "true" : "false";
  tijelo += ",";
  tijelo += "\"dcf_running\":";
  tijelo += megaStatus.dcfUTijeku ? "true" : "false";
  tijelo += ",";
  tijelo += "\"hands_synced\":";
  tijelo += megaStatus.kazaljkeUSinkronu ? "true" : "false";
  tijelo += ",";
  tijelo += "\"hands_position\":";
  tijelo += megaStatus.pozicijaKazaljki;
  tijelo += ",";
  tijelo += "\"plate_synced\":";
  tijelo += megaStatus.plocaUSinkronu ? "true" : "false";
  tijelo += ",";
  tijelo += "\"plate_position\":";
  tijelo += megaStatus.pozicijaPloce;
  tijelo += ",";
  tijelo += "\"celebration\":";
  tijelo += megaStatus.slavljenje ? "true" : "false";
  tijelo += ",";
  tijelo += "\"funeral\":";
  tijelo += megaStatus.mrtvacko ? "true" : "false";
  tijelo += ",";
  tijelo += "\"striking\":";
  tijelo += megaStatus.otkucavanje ? "true" : "false";
  tijelo += ",";
  tijelo += "\"bell1\":";
  tijelo += megaStatus.zvono1 ? "true" : "false";
  tijelo += ",";
  tijelo += "\"bell2\":";
  tijelo += megaStatus.zvono2 ? "true" : "false";
  tijelo += ",";
  tijelo += "\"age_ms\":";
  tijelo += megaStatus.valjan ? (millis() - megaStatus.zadnjeOsvjezenjeMs) : 0;
  tijelo += "}}";
  return tijelo;
}

String generirajWebSucelje() {
  return R"HTML(
<!doctype html>
<html lang="hr">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Toranjski sat</title>
  <style>
    :root { color-scheme: light; --bg:#f3efe6; --panel:#fffaf1; --line:#c8baa1; --text:#2c2418; --accent:#8d5b2a; --soft:#e7dcc8; }
    body { margin:0; font-family: Georgia, "Times New Roman", serif; background:linear-gradient(180deg,#efe6d3,#f7f3ea); color:var(--text); }
    .wrap { max-width:960px; margin:0 auto; padding:20px; }
    h1,h2 { margin:0 0 12px 0; }
    .grid { display:grid; grid-template-columns:repeat(auto-fit,minmax(220px,1fr)); gap:14px; }
    .panel { background:var(--panel); border:1px solid var(--line); border-radius:14px; padding:14px; box-shadow:0 10px 24px rgba(77,52,24,0.08); }
    .kv { display:grid; grid-template-columns:1fr auto; gap:6px 12px; font-size:15px; }
    .kv div:nth-child(odd) { color:#6a5842; }
    .cmds { display:grid; grid-template-columns:repeat(auto-fit,minmax(140px,1fr)); gap:10px; }
    button { border:1px solid var(--line); border-radius:10px; padding:12px 10px; background:#fff; color:var(--text); font-weight:700; cursor:pointer; }
    button:hover { background:var(--soft); }
    button.active { background:var(--accent); color:#fffaf1; border-color:#6e451c; }
    .wide { grid-column:1 / -1; }
    .log { white-space:pre-wrap; min-height:24px; color:#5f4a32; }
    .muted { color:#7a6a56; font-size:14px; }
  </style>
</head>
<body>
  <div class="wrap">
    <h1>Toranjski sat</h1>
    <p class="muted">Status i rucne komande preko ESP8266</p>
    <div class="grid">
      <section class="panel">
        <h2>Vrijeme</h2>
        <div class="kv" id="vrijeme"></div>
      </section>
      <section class="panel">
        <h2>Mreza</h2>
        <div class="kv" id="mreza"></div>
      </section>
      <section class="panel">
        <h2>MQTT</h2>
        <div class="kv" id="mqtt"></div>
      </section>
      <section class="panel">
        <h2>Pogon</h2>
        <div class="kv" id="pogon"></div>
      </section>
      <section class="panel">
        <h2>Zvona</h2>
        <div class="kv" id="zvona"></div>
      </section>
      <section class="panel wide">
        <h2>MQTT postavke</h2>
        <div class="cmds">
          <label style="display:flex;align-items:center;gap:8px;"><input id="mqttEnabled" type="checkbox"> MQTT aktivan</label>
          <input id="mqttBroker" type="text" placeholder="Broker" style="border:1px solid var(--line); border-radius:10px; padding:12px 10px; background:#fff; color:var(--text);">
          <input id="mqttPort" type="number" min="1" max="65535" placeholder="Port" style="border:1px solid var(--line); border-radius:10px; padding:12px 10px; background:#fff; color:var(--text);">
          <input id="mqttUser" type="text" placeholder="User" style="border:1px solid var(--line); border-radius:10px; padding:12px 10px; background:#fff; color:var(--text);">
          <input id="mqttPassword" type="password" placeholder="Nova lozinka (ostavi prazno za zadrzavanje)" style="border:1px solid var(--line); border-radius:10px; padding:12px 10px; background:#fff; color:var(--text);">
          <button onclick="spremiMqtt()">Spremi MQTT</button>
        </div>
      </section>
      <section class="panel wide">
        <h2>Rucne komande</h2>
        <div class="cmds">
          <button onclick="posalji('NTP_SYNC')">Pokreni NTP</button>
          <button onclick="posalji('DCF_START')">Pokreni DCF</button>
          <button id="btnSlavljenje" onclick="toggleCmd('celebration')">Slavljenje</button>
          <button id="btnMrtvacko" onclick="toggleCmd('funeral')">Mrtvacko</button>
          <button id="btnZvono1" onclick="toggleCmd('bell1')">Zvono 1</button>
          <button id="btnZvono2" onclick="toggleCmd('bell2')">Zvono 2</button>
        </div>
      </section>
      <section class="panel wide">
        <h2>Web lozinka</h2>
        <div class="cmds">
          <input id="novaLozinka" type="password" placeholder="Nova lozinka" style="border:1px solid var(--line); border-radius:10px; padding:12px 10px; background:#fff; color:var(--text);">
          <input id="potvrdaLozinke" type="password" placeholder="Potvrda lozinke" style="border:1px solid var(--line); border-radius:10px; padding:12px 10px; background:#fff; color:var(--text);">
          <button onclick="promijeniLozinku()">Promijeni lozinku</button>
        </div>
      </section>
      <section class="panel wide">
        <h2>Odgovor</h2>
        <div id="odgovor" class="log">Cekam podatke...</div>
      </section>
    </div>
  </div>
  <script>
    let zadnjiStatus = null;
    function ozn(v) { return v ? 'DA' : 'NE'; }
    function popuni(id, parovi) {
      const el = document.getElementById(id);
      el.innerHTML = parovi.map(([k,v]) => `<div>${k}</div><div>${v}</div>`).join('');
    }
    function postaviToggle(id, naziv, aktivno) {
      const btn = document.getElementById(id);
      if (!btn) return;
      btn.classList.toggle('active', !!aktivno);
      btn.textContent = `${naziv}: ${aktivno ? 'UKLJUCENO' : 'ISKLJUCENO'}`;
    }
    async function ucitaj() {
      const r = await fetch('/status', {cache:'no-store'});
      const s = await r.json();
      zadnjiStatus = s;
      const m = s.mega || {};
      popuni('vrijeme', [
        ['Vrijeme', m.time || '-'],
        ['Izvor', m.source || '-'],
        ['Potvrdeno', ozn(!!m.time_confirmed)],
        ['DCF u tijeku', ozn(!!m.dcf_running)]
      ]);
      popuni('mreza', [
        ['ESP WiFi', ozn(!!s.wifi_connected)],
        ['Mega WiFi', ozn(!!m.wifi_connected)],
        ['MQTT', ozn(!!m.mqtt_connected)],
        ['IP', s.wifi_ip || '-']
      ]);
      popuni('mqtt', [
        ['Aktivan', ozn(!!m.mqtt_enabled)],
        ['Broker', s.mqtt_broker || '-'],
        ['Port', (s.mqtt_port !== undefined ? s.mqtt_port : '-')],
        ['User', s.mqtt_user || '-']
      ]);
      popuni('pogon', [
        ['Kazaljke sync', ozn(!!m.hands_synced)],
        ['Kazaljke K-min', (m.hands_position !== undefined ? m.hands_position : '-')],
        ['Ploca sync', ozn(!!m.plate_synced)],
        ['Ploca pozicija', (m.plate_position !== undefined ? m.plate_position : '-')]
      ]);
      popuni('zvona', [
        ['Slavljenje', ozn(!!m.celebration)],
        ['Mrtvacko', ozn(!!m.funeral)],
        ['Otkucavanje', ozn(!!m.striking)],
        ['Zvono1/Zvono2', `${ozn(!!m.bell1)} / ${ozn(!!m.bell2)}`]
      ]);
      postaviToggle('btnSlavljenje', 'Slavljenje', !!m.celebration);
      postaviToggle('btnMrtvacko', 'Mrtvacko', !!m.funeral);
      postaviToggle('btnZvono1', 'Zvono 1', !!m.bell1);
      postaviToggle('btnZvono2', 'Zvono 2', !!m.bell2);
      document.getElementById('mqttEnabled').checked = !!m.mqtt_enabled;
      const mqttPoljaAktivna = document.activeElement &&
        ['mqttBroker', 'mqttPort', 'mqttUser', 'mqttPassword'].includes(document.activeElement.id);
      if (!mqttPoljaAktivna) {
        document.getElementById('mqttBroker').value = s.mqtt_broker || '';
        document.getElementById('mqttPort').value = (s.mqtt_port !== undefined ? s.mqtt_port : 1883);
        document.getElementById('mqttUser').value = s.mqtt_user || '';
      }
      document.getElementById('odgovor').textContent =
        `Zadnji odgovor: ${s.zadnji_odgovor || '-'}\nStanje staro: ${m.age_ms !== undefined ? m.age_ms : 0} ms`;
    }
    function komandaZaToggle(vrsta) {
      const m = (zadnjiStatus && zadnjiStatus.mega) ? zadnjiStatus.mega : {};
      if (vrsta === 'celebration') return m.celebration ? 'SLAVLJENJE_OFF' : 'SLAVLJENJE_ON';
      if (vrsta === 'funeral') return m.funeral ? 'MRTVACKO_OFF' : 'MRTVACKO_ON';
      if (vrsta === 'bell1') return m.bell1 ? 'ZVONO1_OFF' : 'ZVONO1_ON';
      if (vrsta === 'bell2') return m.bell2 ? 'ZVONO2_OFF' : 'ZVONO2_ON';
      return null;
    }
    async function toggleCmd(vrsta) {
      const cmd = komandaZaToggle(vrsta);
      if (!cmd) {
        document.getElementById('odgovor').textContent = 'Status jos nije ucitan.';
        return;
      }
      await posalji(cmd);
    }
    async function posalji(cmd) {
      const r = await fetch('/cmd?value=' + encodeURIComponent(cmd), {cache:'no-store'});
      const t = await r.text();
      document.getElementById('odgovor').textContent = t;
      setTimeout(ucitaj, 300);
    }
    async function promijeniLozinku() {
      const nova = document.getElementById('novaLozinka').value;
      const potvrda = document.getElementById('potvrdaLozinke').value;
      if (!nova || nova.length < 4) {
        document.getElementById('odgovor').textContent = 'Nova lozinka mora imati barem 4 znaka.';
        return;
      }
      if (nova !== potvrda) {
        document.getElementById('odgovor').textContent = 'Lozinke se ne podudaraju.';
        return;
      }
      const body = new URLSearchParams({ password: nova });
      const r = await fetch('/password', {
        method: 'POST',
        headers: {'Content-Type': 'application/x-www-form-urlencoded'},
        body
      });
      const t = await r.text();
      document.getElementById('odgovor').textContent = t;
      document.getElementById('novaLozinka').value = '';
      document.getElementById('potvrdaLozinke').value = '';
    }
    async function spremiMqtt() {
      const body = new URLSearchParams({
        enabled: document.getElementById('mqttEnabled').checked ? '1' : '0',
        broker: document.getElementById('mqttBroker').value,
        port: document.getElementById('mqttPort').value || '1883',
        user: document.getElementById('mqttUser').value,
        password: document.getElementById('mqttPassword').value
      });
      const broker = document.getElementById('mqttBroker').value;
      const port = document.getElementById('mqttPort').value || '1883';
      const user = document.getElementById('mqttUser').value;
      const password = document.getElementById('mqttPassword').value;
      if (broker.includes('|') || user.includes('|') || password.includes('|')) {
        document.getElementById('odgovor').textContent = 'MQTT polja ne smiju sadrzavati znak |';
        return;
      }
      const r = await fetch('/mqtt-save', {
        method: 'POST',
        headers: {'Content-Type': 'application/x-www-form-urlencoded'},
        body
      });
      const t = await r.text();
      document.getElementById('odgovor').textContent = t;
      document.getElementById('mqttPassword').value = '';
      setTimeout(ucitaj, 500);
    }
    ucitaj();
    setInterval(ucitaj, 15000);
  </script>
</body>
</html>
)HTML";
}

void obradiNTPSerijskuNaredbu(const String &linija) {
  String server = ocistiJednolinijskiTekst(linija.substring(7), sizeof(ntpPosluzitelj) - 1);
  server.trim();

  if (server.length() == 0) {
    Serial.println("NTPLOG: prazan NTP server, zadrzavam postojeci");
    Serial.println("ERR:NTPCFG");
    return;
  }

  server.toCharArray(ntpPosluzitelj, sizeof(ntpPosluzitelj));
  ntpKlijent.setPoolServerName(ntpPosluzitelj);
  ntpIkadPostavljen = false;
  ntpPoslanNakonSpajanja = false;
  zadnjiPoslaniNtpSatniKljuc = -1;

  Serial.print("NTPLOG: postavljen novi NTP server ");
  Serial.println(ntpPosluzitelj);
  Serial.println("ACK:NTPCFG");
}

void konfigurirajWebPosluzitelj() {
  Serial.println("WEB: Registriram / rutu");
  webPosluzitelj.on("/", []() {
    if (!osigurajWebAutorizaciju()) {
      return;
    }
    webPosluzitelj.send(200, "text/html; charset=utf-8", generirajWebSucelje());
  });

  Serial.println("WEB: Registriram /status rutu");
  webPosluzitelj.on("/status", []() {
    if (!osigurajWebAutorizaciju()) {
      return;
    }
    zatraziStatusMegae(true);
    webPosluzitelj.send(200, "application/json", kreirajJsonStatus());
  });

  Serial.println("WEB: Registriram /cmd rutu");
  webPosluzitelj.on("/cmd", []() {
    if (!osigurajWebAutorizaciju()) {
      return;
    }
    if (!webPosluzitelj.hasArg("value")) {
      webPosluzitelj.send(400, "text/plain", "Nedostaje argument value");
      return;
    }

    String naredba = webPosluzitelj.arg("value");

    if (naredba.equalsIgnoreCase("NTP_SYNC")) {
      osvjeziNTPSat();
      if (WiFi.status() != WL_CONNECTED) {
        webPosluzitelj.send(503, "text/plain", "WiFi nije spojen pa NTP nije dostupan");
        return;
      }
      if (!ntpKlijent.isTimeSet()) {
        webPosluzitelj.send(503, "text/plain", "NTP jos nije spreman na ESP8266");
        return;
      }

      posaljiNTPPremaMegai();
      ntpPoslanNakonSpajanja = true;
      webPosluzitelj.send(200, "text/plain", "NTP vrijeme poslano Megi");
      return;
    }

    if (naredba.equalsIgnoreCase("STATUS_REFRESH")) {
      zatraziStatusMegae(true);
      webPosluzitelj.send(200, "text/plain", "Zatrazeno novo stanje toranjskog sata");
      return;
    }

    if (!jeNaredbaDozvoljena(naredba)) {
      Serial.println("WEB CMD: naredba nije dozvoljena");
      webPosluzitelj.send(422, "text/plain", "Nepoznata naredba za toranjski sat");
      return;
    }

    saljiNaredbuMegai(naredba);
    zatraziStatusMegae(true);
    webPosluzitelj.send(200, "text/plain", "Naredba poslana toranjskom satu");
  });

  Serial.println("WEB: Registriram /password rutu");
  webPosluzitelj.on("/password", HTTP_POST, []() {
    if (!osigurajWebAutorizaciju()) {
      return;
    }
    if (!webPosluzitelj.hasArg("password")) {
      webPosluzitelj.send(400, "text/plain", "Nedostaje nova lozinka");
      return;
    }

    String novaLozinka = webPosluzitelj.arg("password");
    novaLozinka = ocistiJednolinijskiTekst(novaLozinka, WEB_LOZINKA_MAX - 1);
    novaLozinka.trim();
    if (novaLozinka.length() < 4) {
      webPosluzitelj.send(422, "text/plain", "Lozinka mora imati barem 4 znaka");
      return;
    }

    if (!spremiWebAutentikaciju(novaLozinka)) {
      webPosluzitelj.send(500, "text/plain", "Spremanje lozinke nije uspjelo");
      return;
    }

    webPosluzitelj.send(200, "text/plain", "Lozinka je promijenjena. Pri sljedecem otvaranju koristi novu lozinku.");
  });

  Serial.println("WEB: Registriram /mqtt-save rutu");
  webPosluzitelj.on("/mqtt-save", HTTP_POST, []() {
    if (!osigurajWebAutorizaciju()) {
      return;
    }
    if (!webPosluzitelj.hasArg("enabled") ||
        !webPosluzitelj.hasArg("broker") ||
        !webPosluzitelj.hasArg("port") ||
        !webPosluzitelj.hasArg("user") ||
        !webPosluzitelj.hasArg("password")) {
      webPosluzitelj.send(400, "text/plain", "Nedostaju MQTT polja");
      return;
    }

    const String enabledTekst = webPosluzitelj.arg("enabled");
    const String broker = webPosluzitelj.arg("broker");
    const String portTekst = webPosluzitelj.arg("port");
    const String user = webPosluzitelj.arg("user");
    const String password = webPosluzitelj.arg("password");
    const long port = portTekst.toInt();
    if (!(enabledTekst == "0" || enabledTekst == "1")) {
      webPosluzitelj.send(422, "text/plain", "Neispravna MQTT zastavica aktivacije");
      return;
    }
    if (port <= 0 || port > 65535) {
      webPosluzitelj.send(422, "text/plain", "Neispravan MQTT port");
      return;
    }

    if (!spremiMQTTPostavkePrekoWeba(broker,
                                     static_cast<uint16_t>(port),
                                     user,
                                     password,
                                     enabledTekst == "1")) {
      webPosluzitelj.send(422, "text/plain", "MQTT postavke nisu valjane");
      return;
    }

    webPosluzitelj.send(200, "text/plain", "MQTT postavke poslane toranjskom satu");
  });

  webPosluzitelj.onNotFound([]() {
    Serial.println("WEB: 404 - ruta ne postoji");
    webPosluzitelj.send(404, "text/plain", "Ruta ne postoji");
  });

  webPosluzitelj.begin();
  Serial.println("WEB: posluzitelj pokrenut na portu 80");
}

void saljiNaredbuMegai(const String &naredba) {
  Serial.print("CMD:");
  Serial.println(naredba);
}

bool jeNaredbaDozvoljena(const String &naredba) {
  for (size_t i = 0; i < BROJ_NAREDBI; ++i) {
    if (naredba.equalsIgnoreCase(DOZVOLJENE_NAREDBE[i])) {
      return true;
    }
  }
  return false;
}
