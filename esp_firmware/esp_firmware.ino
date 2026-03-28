#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <ESP8266WebServer.h>
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

// Parametri NTP klijenta za sinkronizaciju toranjskog sata.
char ntpPosluzitelj[40] = "pool.ntp.org";
static const long NTP_OFFSET_SEKUNDI = 0;
static const unsigned long NTP_INTERVAL_MS = 60000;
static const size_t SERIJSKI_BUFFER_MAX = 256;

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
  "MRTVACKO_ON", "MRTVACKO_OFF"
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

void setup() {
  Serial.begin(9600);
  delay(200);

  Serial.println("ESP BOOT");
  Serial.println("FAZA: Povezivanje na WiFi");

  pokupiWifiKonfiguracijuIzSerijske();
  if (!primljenaWifiKonfiguracija || WiFi.status() != WL_CONNECTED) {
    poveziNaWiFi();
  }

  Serial.println("FAZA: Pokretanje NTP klijenta");
  ntpKlijent.begin();

  Serial.println("FAZA: Konfiguracija MQTT klijenta");
  inicijalizirajMQTT();

  Serial.println("FAZA: Konfiguracija web posluzitelja");
  konfigurirajWebPosluzitelj();

  Serial.println("FAZA: INIT zavrsen, ulazak u loop()");
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    if (wifiIkadSpojen) {
      Serial.println("FAZA: WiFi veza izgubljena, ponovno spajanje...");
    } else {
      Serial.println("FAZA: WiFi jos nije uspostavljen, pokusavam...");
    }
    poveziNaWiFi();
  }

  osvjeziNTPSat();
  obradiSerijskiUlaz();
  obradiMQTT();
  posaljiNTPAkoTreba();

  webPosluzitelj.handleClient();
}

void prijaviPromjenuWiFiStatusa() {
  wl_status_t trenutniStatus = WiFi.status();
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

void poveziNaWiFi() {
  unsigned long sada = millis();

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
        Serial.print("RX MEGA: ");
        Serial.println(zadnjiOdgovorMega);

        if (prijemniBuffer.startsWith("WIFI:")) {
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
              poveziNaWiFi();
              uspjeh = true;
            } else {
              Serial.println("WIFI RX: neispravna duljina SSID/lozinke/DHCP oznake");
            }
          }

          Serial.println(uspjeh ? "ACK:WIFI" : "ERR:WIFI");
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
  Serial.println("WEB: Registriram /status rutu");
  webPosluzitelj.on("/status", []() {
    String tijelo = "{\"wifi\":\"" + escapirajJsonString(WiFi.SSID()) + "\",";
    tijelo += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
    tijelo += "\"mqtt\":\"";
    tijelo += (mqttKlijent.connected() ? "connected" : "disconnected");
    tijelo += "\",";
    tijelo += "\"ntp_server\":\"" + escapirajJsonString(String(ntpPosluzitelj)) + "\",";
    tijelo += "\"zadnji_odgovor\":\"" + escapirajJsonString(zadnjiOdgovorMega) + "\"}";
    webPosluzitelj.send(200, "application/json", tijelo);
  });

  Serial.println("WEB: Registriram /cmd rutu");
  webPosluzitelj.on("/cmd", []() {
    if (!webPosluzitelj.hasArg("value")) {
      webPosluzitelj.send(400, "text/plain", "Nedostaje argument value");
      return;
    }

    String naredba = webPosluzitelj.arg("value");
    Serial.print("WEB CMD: stigla naredba ");
    Serial.println(naredba);

    if (!jeNaredbaDozvoljena(naredba)) {
      Serial.println("WEB CMD: naredba nije dozvoljena");
      webPosluzitelj.send(422, "text/plain", "Nepoznata naredba za toranjski sat");
      return;
    }

    saljiNaredbuMegai(naredba);
    webPosluzitelj.send(200, "text/plain", "Naredba poslana");
  });

  webPosluzitelj.onNotFound([]() {
    Serial.println("WEB: 404 - ruta ne postoji");
    webPosluzitelj.send(404, "text/plain", "Ruta ne postoji");
  });

  webPosluzitelj.begin();
  Serial.println("WEB: posluzitelj pokrenut na portu 80");
}

void saljiNaredbuMegai(const String &naredba) {
  Serial.print("SLANJE CMD: ");
  Serial.println(naredba);
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
