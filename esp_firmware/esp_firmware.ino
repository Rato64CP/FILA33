#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <ESP8266WebServer.h>
#include <time.h>

// Konfiguracija WiFi mreže
String wifiSsid    = "SVETI PETAR";
String wifiLozinka = "cista2906";
bool koristiDhcp   = true;
String statickaIp  = "192.168.1.200";
String mreznaMaska = "255.255.255.0";
String zadaniGateway = "192.168.1.1";
bool primljenaWifiKonfiguracija = false;

// Parametri NTP klijenta
static const char *NTP_POSLUZITELJ       = "pool.ntp.org";
static const long  NTP_OFFSET_SEKUNDI    = 0;           // UTC
static const unsigned long NTP_INTERVAL_MS   = 60000;   // osvježavanje NTP-a
static const unsigned long SLANJE_INTERVAL_MS = 30000;  // slanje Megi

WiFiUDP ntpUDP;
NTPClient ntpKlijent(ntpUDP, NTP_POSLUZITELJ, NTP_OFFSET_SEKUNDI, NTP_INTERVAL_MS);

ESP8266WebServer webPosluzitelj(80);

unsigned long zadnjeSlanjeMillis = 0;
String zadnjiOdgovorMega = "";

// Statusne zastavice za "faze"
bool wifiIkadSpojen = false;
bool ntpIkadPostavljen = false;

// Upravljanje nenametljivim pokušajima WiFi spajanja
bool wifiPokusajUToku = false;
unsigned long wifiPokusajPocetak = 0;
unsigned long wifiSljedeciPokusajDozvoljen = 0;
int wifiBrojPokusajaZaredom = 0;
static const unsigned long WIFI_POKUSAJ_TIMEOUT_MS = 20000;
static const unsigned long WIFI_ODGODA_POKUSAJA_MS = 5000;
static const int WIFI_MAX_POKUSAJA_PRIJE_ODGODE = 3;

// Popis naredbi koje prihvaća toranjski sat
const char *DOZVOLJENE_NAREDBE[] = {
  "ZVONO1_ON",  "ZVONO1_OFF",  "ZVONO2_ON",  "ZVONO2_OFF",
  "OTKUCAVANJE_ON", "OTKUCAVANJE_OFF",
  "SLAVLJENJE_ON",  "SLAVLJENJE_OFF",
  "MRTVACKO_ON",    "MRTVACKO_OFF"
};
const size_t BROJ_NAREDBI = sizeof(DOZVOLJENE_NAREDBE) / sizeof(DOZVOLJENE_NAREDBE[0]);

void poveziNaWiFi();
bool postaviStatickuKonfiguraciju();
void osvjeziNTPSat();
void posaljiNTPPremaMegai();
void obradiSerijskiUlaz();
void pokupiWifiKonfiguracijuIzSerijske(unsigned long millisTimeout = 3000);
void konfigurirajWebPosluzitelj();
void saljiNaredbuMegai(const String &naredba);
bool jeNaredbaDozvoljena(const String &naredba);

void setup() {
  Serial.begin(9600);  // dijelimo vezu s Megom
  delay(200);

  Serial.println("ESP BOOT");                // DEBUG: start
  Serial.println("FAZA: Povezivanje na WiFi");

  pokupiWifiKonfiguracijuIzSerijske();
  if (!primljenaWifiKonfiguracija || WiFi.status() != WL_CONNECTED) {
    poveziNaWiFi();
  }

  Serial.println("FAZA: Pokretanje NTP klijenta");
  ntpKlijent.begin();

  Serial.println("FAZA: Konfiguracija web poslužitelja");
  konfigurirajWebPosluzitelj();

  Serial.println("FAZA: INIT završen, ulazak u loop()");
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    // WiFi pao – prijavi i pokušaj ponovo
    if (wifiIkadSpojen) {
      Serial.println("FAZA: WiFi veza izgubljena, ponovno spajanje...");
    } else {
      Serial.println("FAZA: WiFi još nije uspostavljen, pokušavam...");
    }
    poveziNaWiFi();
  }

  osvjeziNTPSat();      // DEBUG unutar funkcije
  obradiSerijskiUlaz(); // DEBUG unutar funkcije

  unsigned long sada = millis();
  if (ntpIkadPostavljen && sada - zadnjeSlanjeMillis >= SLANJE_INTERVAL_MS) {
    posaljiNTPPremaMegai();
    zadnjeSlanjeMillis = sada;
  }

  webPosluzitelj.handleClient();
}

void poveziNaWiFi() {
  unsigned long sada = millis();

  // Ako je spojeno, resetiraj brojače i izađi
  if (WiFi.status() == WL_CONNECTED) {
    if (wifiPokusajUToku) {
      Serial.println();
      Serial.print("WIFI: Spojen, IP: ");
      Serial.println(WiFi.localIP());
    }
    wifiIkadSpojen = true;
    wifiPokusajUToku = false;
    wifiBrojPokusajaZaredom = 0;
    return;
  }

  // Provjeri treba li započeti novi pokušaj
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

  // Tijekom pokušaja, povremeno provjeri timeout kako bi loop() ostao živ
  if (wifiPokusajUToku && (sada - wifiPokusajPocetak >= WIFI_POKUSAJ_TIMEOUT_MS)) {
    Serial.println();
    Serial.println("WIFI: Timeout pokušaja, odspajam i čekam prije novog pokušaja");
    WiFi.disconnect();
    wifiPokusajUToku = false;

    // Nakon nekoliko uzastopnih pokušaja napravi dulju pauzu
    if (wifiBrojPokusajaZaredom >= WIFI_MAX_POKUSAJA_PRIJE_ODGODE) {
      wifiSljedeciPokusajDozvoljen = sada + WIFI_ODGODA_POKUSAJA_MS;
      wifiBrojPokusajaZaredom = 0;
    } else {
      wifiSljedeciPokusajDozvoljen = sada + 500;
    }
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

  // zovi update redovno
  bool promjena = ntpKlijent.update();

  if (promjena && ntpKlijent.isTimeSet()) {
    if (!ntpIkadPostavljen) {
      Serial.print("NTP: Prvi put postavljeno vrijeme, epoch=");
      Serial.println(ntpKlijent.getEpochTime());
      ntpIkadPostavljen = true;
    } else if (sada - zadnjiLog > 60000) { // svake minute malo loga
      Serial.print("NTP: osvjezeno, epoch=");
      Serial.println(ntpKlijent.getEpochTime());
      zadnjiLog = sada;
    }
  } else if (!ntpKlijent.isTimeSet() && (sada - zadnjiLog > 10000)) {
    Serial.println("NTP: jos nije postavljeno vrijeme, cekam...");
    zadnjiLog = sada;
  }
}

void posaljiNTPPremaMegai() {
  time_t epoch = ntpKlijent.getEpochTime();
  struct tm *utc = gmtime(&epoch);
  if (utc == nullptr) {
    Serial.println("NTP: gmtime vratio nullptr, preskacem slanje");
    return;
  }

  char isoBuffer[25];
  strftime(isoBuffer, sizeof(isoBuffer), "%Y-%m-%dT%H:%M:%SZ", utc);

  Serial.print("SLANJE: NTP linija prema Megi: ");
  Serial.println(isoBuffer);

  Serial.print("NTP:");
  Serial.println(isoBuffer);  // format koji očekuje Mega (obradiESPSerijskuKomunikaciju)
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
            String noviSsid     = payload.substring(0, granice[0]);
            String novaLozinka  = payload.substring(granice[0] + 1, granice[1]);
            String dhcpZastavica = payload.substring(granice[1] + 1, granice[2]);
            String novaIp       = payload.substring(granice[2] + 1, granice[3]);
            String novaMaska    = payload.substring(granice[3] + 1, granice[4]);
            String noviGateway  = payload.substring(granice[4] + 1);

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
              wifiIkadSpojen = false;
              ntpIkadPostavljen = false;
              zadnjeSlanjeMillis = 0;
              poveziNaWiFi();
              uspjeh = true;
            } else {
              Serial.println("WIFI RX: neispravna duljina SSID/lozinke/DHCP oznake");
            }
          }

          Serial.println(uspjeh ? "ACK:WIFI" : "ERR:WIFI");
        }
      }
      prijemniBuffer = "";
    } else if (znak != '\r') {
      prijemniBuffer += znak;
    }
  }
}

void konfigurirajWebPosluzitelj() {
  Serial.println("WEB: Registriram /status rutu");
  webPosluzitelj.on("/status", []() {
    String tijelo = "{\"wifi\":\"" + WiFi.SSID() + "\"," +
                    "\"ip\":\"" + WiFi.localIP().toString() + "\"," +
                    "\"zadnji_odgovor\":\"" + zadnjiOdgovorMega + "\"}";
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
      Serial.println("WEB CMD: Naredba nije dozvoljena");
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
  Serial.println("WEB: Posluzitelj pokrenut na portu 80");
}

void saljiNaredbuMegai(const String &naredba) {
  Serial.print("SLANJE CMD: ");
  Serial.println(naredba);
  Serial.print("CMD:");
  Serial.println(naredba);  // Mega obrađuje CMD: linije
}

bool jeNaredbaDozvoljena(const String &naredba) {
  for (size_t i = 0; i < BROJ_NAREDBI; ++i) {
    if (naredba.equalsIgnoreCase(DOZVOLJENE_NAREDBE[i])) {
      return true;
    }
  }
  return false;
}