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
static const size_t SERIJSKI_BUFFER_MAX = 256;

WiFiUDP ntpUDP;
NTPClient ntpKlijent(ntpUDP, NTP_POSLUZITELJ, NTP_OFFSET_SEKUNDI, NTP_INTERVAL_MS);

ESP8266WebServer webPosluzitelj(80);

String zadnjiOdgovorMega = "";

// Statusne zastavice za "faze"
bool wifiIkadSpojen = false;
bool ntpIkadPostavljen = false;
bool ntpPoslanNakonSpajanja = false;
int zadnjiPoslaniNtpSatniKljuc = -1;

// Upravljanje nenametljivim pokušajima WiFi spajanja
bool wifiPokusajUToku = false;
unsigned long wifiPokusajPocetak = 0;
unsigned long wifiSljedeciPokusajDozvoljen = 0;
int wifiBrojPokusajaZaredom = 0;
static const unsigned long WIFI_POKUSAJ_TIMEOUT_MS = 20000;
static const unsigned long WIFI_ODGODA_POKUSAJA_MS = 5000;
static const int WIFI_MAX_POKUSAJA_PRIJE_ODGODE = 3;
wl_status_t zadnjiPrijavljeniWiFiStatus = WL_IDLE_STATUS;

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
void posaljiNTPAkoTreba();
void posaljiNTPPremaMegai();
void obradiSerijskiUlaz();
void pokupiWifiKonfiguracijuIzSerijske(unsigned long millisTimeout = 3000);
void konfigurirajWebPosluzitelj();
void saljiNaredbuMegai(const String &naredba);
void prijaviPromjenuWiFiStatusa();
bool jeNaredbaDozvoljena(const String &naredba);
bool jePrijestupnaGodina(int godina);
int danUTjednu(int godina, int mjesec, int dan);
int zadnjaNedjeljaUMjesecu(int godina, int mjesec);
bool jeLjetnoVrijemeEU(time_t utcEpoch);
time_t konvertirajUTCuLokalnoVrijeme(time_t utcEpoch);

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
    Serial.println("WIFI:DISCONNECTED");
  }
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
    prijaviPromjenuWiFiStatusa();
    return;
  }

  prijaviPromjenuWiFiStatusa();

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
    prijaviPromjenuWiFiStatusa();

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
      Serial.print("NTPLOG: Prvi put postavljeno vrijeme, epoch=");
      Serial.println(ntpKlijent.getEpochTime());
      ntpIkadPostavljen = true;
    } else if (sada - zadnjiLog > 60000) { // svake minute malo loga
      Serial.print("NTPLOG: osvjezeno, epoch=");
      Serial.println(ntpKlijent.getEpochTime());
      zadnjiLog = sada;
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

  int satniKljuc = (lokalniTm.tm_year + 1900) * 1000000L +
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
  Serial.println(isoBuffer);  // Mega očekuje lokalni ISO bez vremenske zone
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
              ntpPoslanNakonSpajanja = false;
              zadnjiPoslaniNtpSatniKljuc = -1;
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
  static const int daniUMjesecu[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
  int brojDana = daniUMjesecu[mjesec - 1];
  if (mjesec == 2 && jePrijestupnaGodina(godina)) {
    brojDana = 29;
  }

  int pomakDoNedjelje = danUTjednu(godina, mjesec, brojDana);  // 0 = nedjelja
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
