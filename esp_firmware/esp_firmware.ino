#include <ESP8266WiFi.h>
#include <WiFiClientSecureBearSSL.h>
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
static const size_t ESP_EEPROM_VELICINA = 256;
static const uint16_t WEB_AUTH_POTPIS = 0x5741;
static const uint16_t MQTT_POTPIS = 0x4D51;
static const int ESP_EEPROM_ADRESA_WEB = 0;
static const int ESP_EEPROM_ADRESA_MQTT = 48;

// MQTT zadane vjerodajnice.
// Mega ih po potrebi moze prepisati kroz MQTT:CONNECT naredbu.
static const uint16_t MQTT_ZADANI_PORT = 1883;
static const size_t MQTT_BROKER_MAX = 64;
static const size_t MQTT_TOPIC_MAX = 96;
static const size_t MQTT_PAYLOAD_MAX = 192;
static const size_t MQTT_MAX_PRETPLATA = 16;
static const size_t MQTT_KORISNIK_MAX = 33;
static const size_t MQTT_LOZINKA_MAX = 33;
static const unsigned long MQTT_POKUSAJ_INTERVAL_MS = 15000;
static const unsigned long MQTT_PAUZA_NAKON_WEB_AKTIVNOSTI_MS = 10000UL;

WiFiUDP ntpUDP;
NTPClient ntpKlijent(ntpUDP, ntpPosluzitelj, NTP_OFFSET_SEKUNDI, NTP_INTERVAL_MS);

ESP8266WebServer webPosluzitelj(80);
WiFiClient mqttMrezniKlijent;
BearSSL::WiFiClientSecure mqttSigurniMrezniKlijent;
PubSubClient mqttKlijent(mqttMrezniKlijent);

String webLozinka = "cista2906";

struct WebAuthConfig {
  uint16_t potpis;
  char lozinka[WEB_LOZINKA_MAX];
};

struct MQTTConfig {
  uint16_t potpis;
  uint8_t omogucen;
  char broker[MQTT_BROKER_MAX];
  uint16_t port;
  char korisnik[MQTT_KORISNIK_MAX];
  char lozinka[MQTT_LOZINKA_MAX];
};

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
bool mqttOmogucenLokalno = false;
bool mqttStatusPrijavljenKaoSpojen = false;
unsigned long mqttZadnjiPokusajSpajanja = 0;
unsigned long zadnjaWebAktivnostMs = 0;
bool mqttKoristiTls = false;
char mqttBrokerAdresa[MQTT_BROKER_MAX] = "10f183556c2e427caa6ba30fd179cbca.s1.eu.hivemq.cloud";
uint16_t mqttBrokerPort = 8883;
char mqttKorisnik[MQTT_KORISNIK_MAX] = "zvonacista";
char mqttLozinka[MQTT_LOZINKA_MAX] = "DATAx12##";
char mqttPretplate[MQTT_MAX_PRETPLATA][MQTT_TOPIC_MAX];
size_t mqttBrojPretplata = 0;

void poveziNaWiFi();
bool postaviStatickuKonfiguraciju();
void osvjeziNTPSat();
void posaljiNTPAkoTreba();
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
String kreirajJsonStatus();
void ucitajWebAutentikaciju();
bool spremiWebAutentikaciju(const String &novaLozinka);
bool osigurajWebAutorizaciju();
bool spremiMQTTPostavkePrekoWeba(const String &broker,
                                 uint16_t port,
                                 const String &korisnik,
                                 const String &lozinka,
                                 bool omogucen);
void oznaciWebAktivnost();
void ucitajMQTTKonfiguraciju();
bool spremiMQTTKonfiguraciju();
void primijeniMQTTKlijentaPremaPortu();
void prijaviMQTTOmogucenostMegai();
void posaljiApiKomanduMegai(const char* naredba, const char* odgovor);

void setup() {
  Serial.begin(9600);
  delay(200);
  EEPROM.begin(ESP_EEPROM_VELICINA);
  ucitajWebAutentikaciju();
  ucitajMQTTKonfiguraciju();

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

  prijaviMQTTOmogucenostMegai();
  Serial.println("CFGREQ");
  Serial.println("FAZA: INIT zavrsen, ulazak u loop()");
}

void loop() {
  obradiSerijskiUlaz();
  webPosluzitelj.handleClient();
  yield();

  if (wifiOmogucen) {
    if (WiFi.status() != WL_CONNECTED) {
      poveziNaWiFi();
    }

    osvjeziNTPSat();
    posaljiNTPAkoTreba();
  }

  obradiMQTT();
  webPosluzitelj.handleClient();
  yield();
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
    Serial.print("WIFI:LOCAL_IP:");
    Serial.println(WiFi.localIP().toString());
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
  mqttKlijent.setSocketTimeout(1);
  primijeniMQTTKlijentaPremaPortu();
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
  if (!mqttOmogucenLokalno) {
    if (mqttKlijent.connected()) {
      mqttKlijent.disconnect();
    }
    postaviMQTTStatus(false);
    return;
  }

  if (!mqttKlijent.connected()) {
    if ((millis() - zadnjaWebAktivnostMs) < MQTT_PAUZA_NAKON_WEB_AKTIVNOSTI_MS) {
      return;
    }
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
  if (!mqttKonfiguriran || !mqttOmogucenLokalno || WiFi.status() != WL_CONNECTED) {
    postaviMQTTStatus(false);
    return false;
  }

  unsigned long sada = millis();
  if (!odmah && (sada - mqttZadnjiPokusajSpajanja < MQTT_POKUSAJ_INTERVAL_MS)) {
    return false;
  }
  mqttZadnjiPokusajSpajanja = sada;

  primijeniMQTTKlijentaPremaPortu();
  mqttKlijent.setServer(mqttBrokerAdresa, mqttBrokerPort);

  char clientId[24];
  snprintf(clientId, sizeof(clientId), "toranj-esp-%06lx", static_cast<unsigned long>(ESP.getChipId()));

  bool spojeno = false;
  if (strlen(mqttKorisnik) > 0) {
    spojeno = mqttKlijent.connect(clientId, mqttKorisnik, mqttLozinka);
  } else {
    spojeno = mqttKlijent.connect(clientId);
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
    mqttOmogucenLokalno = false;
    spremiMQTTKonfiguraciju();
    if (mqttKlijent.connected()) {
      mqttKlijent.disconnect();
    }
    postaviMQTTStatus(false);
    return;
  }

  if (linija == "MQTT:CONNECT_SAVED") {
    if (!mqttKonfiguriran) {
      Serial.println("MQTTLOG: spremljena konfiguracija nije dostupna");
      postaviMQTTStatus(false);
    } else if (!mqttOmogucenLokalno) {
      Serial.println("MQTTLOG: spremljena konfiguracija postoji, ali je MQTT lokalno iskljucen");
      postaviMQTTStatus(false);
    } else {
      pokusajMQTTSpajanja(true);
    }
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
    mqttOmogucenLokalno = true;
    spremiMQTTKonfiguraciju();

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
  static char prijemniBuffer[SERIJSKI_BUFFER_MAX + 1] = {0};
  static size_t prijemnaDuljina = 0;

  while (Serial.available()) {
    char znak = static_cast<char>(Serial.read());
    if (znak == '\n') {
      prijemniBuffer[prijemnaDuljina] = '\0';
      String linija = String(prijemniBuffer);
      linija.trim();

      if (linija.length() > 0) {
        if (linija.startsWith("STATUS:")) {
          // Status toranjskog sata se vise ne cachea na ESP-u.
        } else if (linija.startsWith("MQTTCFG:")) {
          String payload = linija.substring(8);
          int g1 = payload.indexOf('|');
          if (g1 > 0) {
            String enabledTekst = payload.substring(0, g1);
            enabledTekst.trim();
            if (enabledTekst == "0" || enabledTekst == "1") {
              mqttOmogucenLokalno = (enabledTekst == "1") && mqttKonfiguriran;
              if (!mqttOmogucenLokalno && mqttKlijent.connected()) {
                mqttKlijent.disconnect();
                postaviMQTTStatus(false);
              }
              spremiMQTTKonfiguraciju();
            } else if (mqttKlijent.connected()) {
              mqttKlijent.disconnect();
              postaviMQTTStatus(false);
            }
          }
        } else if (linija.startsWith("WIFIEN:")) {
          String payload = linija.substring(7);
          payload.trim();

          if (payload == "0" || payload == "1") {
            primijeniWiFiOmogucenost(payload == "1");
            Serial.println("ACK:WIFIEN");
          } else {
            Serial.println("ERR:WIFIEN");
          }
        } else if (linija.startsWith("WIFI:")) {
          String payload = linija.substring(5);
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
        } else if (linija == "NTPREQ:SYNC") {
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
        } else if (linija.startsWith("NTPCFG:")) {
          obradiNTPSerijskuNaredbu(linija);
        } else if (linija.startsWith("MQTT:")) {
          obradiMQTTSerijskuNaredbu(linija);
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

void oznaciWebAktivnost() {
  zadnjaWebAktivnostMs = millis();
}

void ucitajMQTTKonfiguraciju() {
  MQTTConfig cfg{};
  EEPROM.get(ESP_EEPROM_ADRESA_MQTT, cfg);

  if (cfg.potpis == MQTT_POTPIS && cfg.broker[0] != '\0') {
    cfg.broker[MQTT_BROKER_MAX - 1] = '\0';
    cfg.korisnik[MQTT_KORISNIK_MAX - 1] = '\0';
    cfg.lozinka[MQTT_LOZINKA_MAX - 1] = '\0';

    strncpy(mqttBrokerAdresa, cfg.broker, MQTT_BROKER_MAX - 1);
    mqttBrokerAdresa[MQTT_BROKER_MAX - 1] = '\0';
    mqttBrokerPort = (cfg.port == 0) ? MQTT_ZADANI_PORT : cfg.port;
    strncpy(mqttKorisnik, cfg.korisnik, MQTT_KORISNIK_MAX - 1);
    mqttKorisnik[MQTT_KORISNIK_MAX - 1] = '\0';
    strncpy(mqttLozinka, cfg.lozinka, MQTT_LOZINKA_MAX - 1);
    mqttLozinka[MQTT_LOZINKA_MAX - 1] = '\0';
    mqttOmogucenLokalno = (cfg.omogucen != 0);
    mqttKonfiguriran = true;
    Serial.println("MQTTLOG: ucitana spremljena MQTT konfiguracija iz ESP EEPROM-a");
    return;
  }

  mqttKonfiguriran = (mqttBrokerAdresa[0] != '\0');
  mqttOmogucenLokalno = false;
  Serial.println("MQTTLOG: koristim zadanu MQTT konfiguraciju iz firmwarea");
}

bool spremiMQTTKonfiguraciju() {
  MQTTConfig cfg{};
  cfg.potpis = MQTT_POTPIS;
  cfg.omogucen = mqttOmogucenLokalno ? 1 : 0;
  strncpy(cfg.broker, mqttBrokerAdresa, MQTT_BROKER_MAX - 1);
  cfg.broker[MQTT_BROKER_MAX - 1] = '\0';
  cfg.port = mqttBrokerPort;
  strncpy(cfg.korisnik, mqttKorisnik, MQTT_KORISNIK_MAX - 1);
  cfg.korisnik[MQTT_KORISNIK_MAX - 1] = '\0';
  strncpy(cfg.lozinka, mqttLozinka, MQTT_LOZINKA_MAX - 1);
  cfg.lozinka[MQTT_LOZINKA_MAX - 1] = '\0';

  EEPROM.put(ESP_EEPROM_ADRESA_MQTT, cfg);
  if (!EEPROM.commit()) {
    Serial.println("MQTTLOG: commit MQTT konfiguracije nije uspio");
    return false;
  }

  return true;
}

void primijeniMQTTKlijentaPremaPortu() {
  const bool trebaTls = (mqttBrokerPort == 8883);
  mqttKoristiTls = trebaTls;

  if (trebaTls) {
    mqttSigurniMrezniKlijent.setInsecure();
    mqttKlijent.setClient(mqttSigurniMrezniKlijent);
  } else {
    mqttKlijent.setClient(mqttMrezniKlijent);
  }

  mqttKlijent.setBufferSize(512);
  mqttKlijent.setSocketTimeout(1);
}

void prijaviMQTTOmogucenostMegai() {
  Serial.print("WEBMQTTEN:");
  Serial.println(mqttOmogucenLokalno ? '1' : '0');
}

void posaljiApiKomanduMegai(const char* naredba, const char* odgovor) {
  if (naredba == nullptr || odgovor == nullptr) {
    webPosluzitelj.send(500, "text/plain", "API konfiguracija nije valjana");
    return;
  }

  oznaciWebAktivnost();
  Serial.print("CMD:");
  Serial.println(naredba);
  webPosluzitelj.send(200, "text/plain", odgovor);
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
  mqttOmogucenLokalno = omogucen;
  primijeniMQTTKlijentaPremaPortu();

  if (!spremiMQTTKonfiguraciju()) {
    return false;
  }

  prijaviMQTTOmogucenostMegai();
  if (!mqttOmogucenLokalno) {
    if (mqttKlijent.connected()) {
      mqttKlijent.disconnect();
    }
    postaviMQTTStatus(false);
  } else if (WiFi.status() == WL_CONNECTED) {
    pokusajMQTTSpajanja(true);
  }
  return true;
}

String kreirajJsonStatus() {
  String tijelo = "{";
  tijelo.reserve(192);
  tijelo += "\"wifi_ip\":\"" + WiFi.localIP().toString() + "\",";
  tijelo += "\"wifi_connected\":";
  tijelo += (WiFi.status() == WL_CONNECTED) ? "true" : "false";
  tijelo += ",";
  tijelo += "\"mqtt_broker\":\"" + escapirajJsonString(String(mqttBrokerAdresa)) + "\",";
  tijelo += "\"mqtt_port\":";
  tijelo += mqttBrokerPort;
  tijelo += ",";
  tijelo += "\"mqtt_user\":\"" + escapirajJsonString(String(mqttKorisnik)) + "\",";
  tijelo += "\"mqtt_enabled\":";
  tijelo += mqttOmogucenLokalno ? "true" : "false";
  tijelo += "}";
  return tijelo;
}

static const char WEB_POCETNA_STRANICA[] PROGMEM = R"HTML(
<!doctype html>
<html lang="hr">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Toranjski sat</title>
  <style>
    :root { color-scheme: light; --bg:#f3efe6; --panel:#fffaf1; --line:#c8baa1; --text:#2c2418; --soft:#e7dcc8; }
    body { margin:0; font-family: Georgia, "Times New Roman", serif; background:linear-gradient(180deg,#efe6d3,#f7f3ea); color:var(--text); }
    .wrap { max-width:760px; margin:0 auto; padding:20px; }
    .panel { background:var(--panel); border:1px solid var(--line); border-radius:14px; padding:16px; box-shadow:0 10px 24px rgba(77,52,24,0.08); margin-bottom:14px; }
    .cmds { display:grid; grid-template-columns:repeat(auto-fit,minmax(160px,1fr)); gap:10px; }
    .nav { display:flex; flex-wrap:wrap; gap:10px; margin-bottom:14px; }
    .nav a, button { border:1px solid var(--line); border-radius:10px; padding:12px 10px; background:#fff; color:var(--text); font-weight:700; cursor:pointer; text-decoration:none; text-align:center; }
    button:hover, .nav a:hover { background:var(--soft); }
    .log { white-space:pre-wrap; min-height:24px; color:#5f4a32; }
    .muted { color:#7a6a56; font-size:14px; }
  </style>
</head>
<body>
  <div class="wrap">
    <h1>Toranjski sat</h1>
    <p class="muted">Web sucelje je sada namijenjeno samo za postavke.</p>
    <section class="panel">
      <h2>Postavke</h2>
      <div class="nav">
        <a href="/detalji">MQTT i lozinka</a>
      </div>
    </section>
  </div>
</body>
</html>
)HTML";

static const char WEB_DETALJI_STRANICA[] PROGMEM = R"HTML(
<!doctype html>
<html lang="hr">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Postavke i servis</title>
  <style>
    :root { color-scheme: light; --bg:#f3efe6; --panel:#fffaf1; --line:#c8baa1; --text:#2c2418; --soft:#e7dcc8; }
    body { margin:0; font-family: Georgia, "Times New Roman", serif; background:linear-gradient(180deg,#efe6d3,#f7f3ea); color:var(--text); }
    .wrap { max-width:880px; margin:0 auto; padding:20px; }
    .grid { display:grid; grid-template-columns:repeat(auto-fit,minmax(220px,1fr)); gap:14px; }
    .panel { background:var(--panel); border:1px solid var(--line); border-radius:14px; padding:16px; box-shadow:0 10px 24px rgba(77,52,24,0.08); }
    .cmds { display:grid; grid-template-columns:repeat(auto-fit,minmax(180px,1fr)); gap:10px; }
    h1,h2,p { margin:0 0 12px 0; }
    input, button, a.link { border:1px solid var(--line); border-radius:10px; padding:12px 10px; background:#fff; color:var(--text); }
    button, a.link { font-weight:700; cursor:pointer; text-decoration:none; text-align:center; }
    button:hover, a.link:hover { background:var(--soft); }
    .log { white-space:pre-wrap; min-height:24px; color:#5f4a32; }
    .muted { color:#7a6a56; font-size:14px; }
  </style>
</head>
<body>
  <div class="wrap">
    <h1>Postavke</h1>
    <p class="muted">Web sucelje sluzi samo za spremanje MQTT i sigurnosnih postavki.</p>
    <p class="muted"><a href="/" style="color:inherit;">Povratak na pocetnu</a></p>
    <div class="grid">
      <section class="panel">
        <h2>MQTT postavke</h2>
        <div class="cmds">
          <label style="display:flex;align-items:center;gap:8px;"><input id="mqttEnabled" type="checkbox"> MQTT aktivan</label>
          <input id="mqttBroker" type="text" placeholder="Broker">
          <input id="mqttPort" type="number" min="1" max="65535" placeholder="Port">
          <input id="mqttUser" type="text" placeholder="User">
          <input id="mqttPassword" type="password" placeholder="Nova lozinka (ostavi prazno za zadrzavanje)">
          <button onclick="spremiMqtt()">Spremi MQTT</button>
        </div>
      </section>
      <section class="panel">
        <h2>Web lozinka</h2>
        <div class="cmds">
          <input id="novaLozinka" type="password" placeholder="Nova lozinka">
          <input id="potvrdaLozinke" type="password" placeholder="Potvrda lozinke">
          <button onclick="promijeniLozinku()">Promijeni lozinku</button>
        </div>
      </section>
      <section class="panel" style="grid-column:1 / -1;">
        <h2>API naredbe</h2>
        <p class="muted">API koristi istu Basic Auth prijavu kao i web sucelje.</p>
        <div class="log">GET /api/bell1/on
GET /api/bell1/off
GET /api/bell2/on
GET /api/bell2/off
GET /api/slavljenje/on
GET /api/slavljenje/off
GET /api/mrtvacko/on
GET /api/mrtvacko/off</div>
      </section>
      <section class="panel" style="grid-column:1 / -1;">
        <h2>Odgovor</h2>
        <div id="odgovor" class="log">Spremno za postavke i servis.</div>
      </section>
    </div>
  </div>
  <script>
    async function ucitajMqtt() {
      const r = await fetch('/status', {cache:'no-store'});
      const s = await r.json();
      document.getElementById('mqttEnabled').checked = !!s.mqtt_enabled;
      document.getElementById('mqttBroker').value = s.mqtt_broker || '';
      document.getElementById('mqttPort').value = (s.mqtt_port !== undefined ? s.mqtt_port : 1883);
      document.getElementById('mqttUser').value = s.mqtt_user || '';
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
      const r = await fetch('/password', { method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'}, body });
      const t = await r.text();
      document.getElementById('odgovor').textContent = t;
      document.getElementById('novaLozinka').value = '';
      document.getElementById('potvrdaLozinke').value = '';
    }
    async function spremiMqtt() {
      const broker = document.getElementById('mqttBroker').value;
      const port = document.getElementById('mqttPort').value || '1883';
      const user = document.getElementById('mqttUser').value;
      const password = document.getElementById('mqttPassword').value;
      if (broker.includes('|') || user.includes('|') || password.includes('|')) {
        document.getElementById('odgovor').textContent = 'MQTT polja ne smiju sadrzavati znak |';
        return;
      }
      const body = new URLSearchParams({
        enabled: document.getElementById('mqttEnabled').checked ? '1' : '0',
        broker, port, user, password
      });
      const r = await fetch('/mqtt-save', { method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'}, body });
      const t = await r.text();
      document.getElementById('odgovor').textContent = t;
      document.getElementById('mqttPassword').value = '';
      if (r.ok) setTimeout(ucitajMqtt, 500);
    }
    ucitajMqtt();
  </script>
</body>
</html>
)HTML";

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
    oznaciWebAktivnost();
    webPosluzitelj.send_P(200, "text/html; charset=utf-8", WEB_POCETNA_STRANICA);
  });

  Serial.println("WEB: Registriram /detalji rutu");
  webPosluzitelj.on("/detalji", []() {
    if (!osigurajWebAutorizaciju()) {
      return;
    }
    oznaciWebAktivnost();
    webPosluzitelj.send_P(200, "text/html; charset=utf-8", WEB_DETALJI_STRANICA);
  });

  Serial.println("WEB: Registriram /status rutu");
  webPosluzitelj.on("/status", []() {
    if (!osigurajWebAutorizaciju()) {
      return;
    }
    oznaciWebAktivnost();
    webPosluzitelj.send(200, "application/json", kreirajJsonStatus());
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

  Serial.println("WEB: Registriram /password rutu");
  webPosluzitelj.on("/password", HTTP_POST, []() {
    if (!osigurajWebAutorizaciju()) {
      return;
    }
    oznaciWebAktivnost();
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
    oznaciWebAktivnost();
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
