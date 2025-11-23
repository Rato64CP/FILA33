#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <ESP8266WebServer.h>
#include <time.h>

// Konfiguracija WiFi mreže (zamijeni stvarnim podacima prije spremanja na modul)
const char *WIFI_SSID = "SVETI PETAR";
const char *WIFI_LOZINKA = "cista2906";

// Parametri NTP klijenta usklađeni s očekivanjima modula u src/esp_serial.cpp
static const char *NTP_POSLUZITELJ = "pool.ntp.org";
static const long NTP_OFFSET_SEKUNDI = 0;          // UTC, pomak prilagodi prema potrebi
static const unsigned long NTP_INTERVAL_MS = 60000;  // osvježavanje svakih 60 sekundi
static const unsigned long SLANJE_INTERVAL_MS = 30000;  // šaljemo prema Megi svakih 30 sekundi

WiFiUDP ntpUDP;
NTPClient ntpKlijent(ntpUDP, NTP_POSLUZITELJ, NTP_OFFSET_SEKUNDI, NTP_INTERVAL_MS);

ESP8266WebServer webPosluzitelj(80);

unsigned long zadnjeSlanjeMillis = 0;
String zadnjiOdgovorMega = "";

// Popis naredbi koje prihvaća toranjski sat preko modula src/esp_serial.cpp
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
  Serial.begin(9600);  // vezu dijelimo s modulom esp_serial.cpp na Megi 2560
  delay(50);

  poveziNaWiFi();
  ntpKlijent.begin();
  konfigurirajWebPosluzitelj();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    poveziNaWiFi();
  }

  osvjeziNTPSat();
  obradiSerijskiUlaz();

  unsigned long sada = millis();
  if (ntpKlijent.isTimeSet() && sada - zadnjeSlanjeMillis >= SLANJE_INTERVAL_MS) {
    posaljiNTPPremaMegai();
    zadnjeSlanjeMillis = sada;
  }

  webPosluzitelj.handleClient();
}

void poveziNaWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_LOZINKA);

  unsigned long pocetak = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    if (millis() - pocetak > 20000) {
      pocetak = millis();  // nastavi pokušavati svakih 20 s
    }
  }
}

void osvjeziNTPSat() {
  ntpKlijent.update();
}

void posaljiNTPPremaMegai() {
  time_t epoch = ntpKlijent.getEpochTime();
  struct tm *utc = gmtime(&epoch);
  if (utc == nullptr) {
    return;
  }

  char isoBuffer[25];
  strftime(isoBuffer, sizeof(isoBuffer), "%Y-%m-%dT%H:%M:%SZ", utc);

  Serial.print("NTP:");
  Serial.println(isoBuffer);  // format koji `obradiESPSerijskuKomunikaciju()` očekuje
}

void obradiSerijskiUlaz() {
  static String prijemniBuffer = "";

  while (Serial.available()) {
    char znak = static_cast<char>(Serial.read());
    if (znak == '\n') {
      prijemniBuffer.trim();
      if (prijemniBuffer.length() > 0) {
        zadnjiOdgovorMega = prijemniBuffer;  // spremamo ACK/ERR radi prikaza na status stranici
      }
      prijemniBuffer = "";
    } else {
      prijemniBuffer += znak;
    }
  }
}

void konfigurirajWebPosluzitelj() {
  webPosluzitelj.on("/status", []() {
    String tijelo = "{\"wifi\":\"" + WiFi.SSID() + "\",";
    tijelo += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
    tijelo += "\"zadnji_odgovor\":\"" + zadnjiOdgovorMega + "\"}";
    webPosluzitelj.send(200, "application/json", tijelo);
  });

  webPosluzitelj.on("/cmd", []() {
    if (!webPosluzitelj.hasArg("value")) {
      webPosluzitelj.send(400, "text/plain", "Nedostaje argument value");
      return;
    }

    String naredba = webPosluzitelj.arg("value");
    if (!jeNaredbaDozvoljena(naredba)) {
      webPosluzitelj.send(422, "text/plain", "Nepoznata naredba za toranjski sat");
      return;
    }

    saljiNaredbuMegai(naredba);
    webPosluzitelj.send(200, "text/plain", "Naredba poslana");
  });

  webPosluzitelj.onNotFound([]() {
    webPosluzitelj.send(404, "text/plain", "Ruta ne postoji");
  });

  webPosluzitelj.begin();
}

void saljiNaredbuMegai(const String &naredba) {
  Serial.print("CMD:");
  Serial.println(naredba);  // modul src/esp_serial.cpp obrađuje naredbe zvona i otkucaja
}

bool jeNaredbaDozvoljena(const String &naredba) {
  for (size_t i = 0; i < BROJ_NAREDBI; ++i) {
    if (naredba.equalsIgnoreCase(DOZVOLJENE_NAREDBE[i])) {
      return true;
    }
  }
  return false;
}
