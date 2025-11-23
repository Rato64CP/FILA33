#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <ESP8266WebServer.h>
#include <time.h>

// Konfiguracija WiFi mreže
const char *WIFI_SSID    = "SVETI PETAR";
const char *WIFI_LOZINKA = "cista2906";

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

// Popis naredbi koje prihvaća toranjski sat
const char *DOZVOLJENE_NAREDBE[] = {
  "ZVONO1_ON",  "ZVONO1_OFF",  "ZVONO2_ON",  "ZVONO2_OFF",
  "OTKUCAVANJE_ON", "OTKUCAVANJE_OFF",
  "SLAVLJENJE_ON",  "SLAVLJENJE_OFF",
  "MRTVACKO_ON",    "MRTVACKO_OFF"
};
const size_t BROJ_NAREDBI = sizeof(DOZVOLJENE_NAREDBE) / sizeof(DOZVOLJENE_NAREDBE[0]);

void poveziNaWiFi();
void osvjeziNTPSat();
void posaljiNTPPremaMegai();
void obradiSerijskiUlaz();
void konfigurirajWebPosluzitelj();
void saljiNaredbuMegai(const String &naredba);
bool jeNaredbaDozvoljena(const String &naredba);

void setup() {
  Serial.begin(9600);  // dijelimo vezu s Megom
  delay(200);

  Serial.println("ESP BOOT");                // DEBUG: start
  Serial.println("FAZA: Povezivanje na WiFi");

  poveziNaWiFi();

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
  WiFi.mode(WIFI_STA);

  Serial.print("WIFI: Spajam se na SSID: ");
  Serial.println(WIFI_SSID);

  WiFi.begin(WIFI_SSID, WIFI_LOZINKA);

  unsigned long pocetak = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");  // DEBUG: indikacija čekanja
    if (millis() - pocetak > 20000) {
      Serial.println();
      Serial.println("WIFI: Timeout 20s, ponavljam pokušaj...");
      pocetak = millis();  // nastavi pokušavati svakih 20 s
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_LOZINKA);
    }
  }

  Serial.println();
  Serial.print("WIFI: Spojen, IP: ");
  Serial.println(WiFi.localIP());
  wifiIkadSpojen = true;
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