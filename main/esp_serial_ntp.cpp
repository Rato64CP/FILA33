// esp_serial_ntp.cpp - WiFi, NTP i vremenska koordinacija prema ESP32 mostu
#include "esp_serial_internal.h"

namespace {

bool vrijemeProslo(unsigned long sadaMs, unsigned long ciljMs) {
  return static_cast<long>(sadaMs - ciljMs) >= 0;
}

bool jeValjanaIPv4AdresaZaLCD(const char* tekst) {
  if (tekst == nullptr || tekst[0] == '\0') {
    return false;
  }

  uint8_t brojSegmenata = 0;
  const char* pokazivac = tekst;

  while (*pokazivac != '\0') {
    if (brojSegmenata >= 4 || !isDigit(*pokazivac)) {
      return false;
    }

    int segment = 0;
    uint8_t brojZnamenki = 0;
    while (isDigit(*pokazivac)) {
      segment = segment * 10 + (*pokazivac - '0');
      if (segment > 255) {
        return false;
      }
      ++pokazivac;
      ++brojZnamenki;
      if (brojZnamenki > 3) {
        return false;
      }
    }

    if (brojZnamenki == 0) {
      return false;
    }

    ++brojSegmenata;
    if (*pokazivac == '.') {
      ++pokazivac;
      if (*pokazivac == '\0') {
        return false;
      }
    } else if (*pokazivac != '\0') {
      return false;
    }
  }

  return brojSegmenata == 4;
}

bool jeSiguranProzorZaNTPZahtjev(const DateTime& sada) {
  return sada.second() >= NTP_SIGURNA_SEKUNDA_MIN &&
         sada.second() <= NTP_SIGURNA_SEKUNDA_MAX;
}

bool jeSigurnaMinutaZaNTPZahtjev(const DateTime& sada) {
  return sada.minute() != 0;
}

bool mehanikaTornjskogSataMirujeZaNTP() {
  if (jeOtkucavanjeUTijeku()) {
    return false;
  }

  if (jeRucnaBlokadaKazaljkiAktivna() || jeRucnaBlokadaPloceAktivna()) {
    return false;
  }

  if (!mozeSeRucnoNamjestatiKazaljke() || !mozeSeRucnoNamjestatiPloca()) {
    return false;
  }

  if (jeVrijemePotvrdjenoZaAutomatiku() &&
      (!suKazaljkeUSinkronu() || !jePlocaUSinkronu())) {
    return false;
  }

  return true;
}

bool jePouzdanoVrijemeDostupnoZaNTP(const DateTime& sada) {
  return jeVrijemePotvrdjenoZaAutomatiku() && sada.unixtime() != 0;
}

uint32_t odrediMinutniKljucNTPZahtjeva(const DateTime& sada) {
  if (jePouzdanoVrijemeDostupnoZaNTP(sada)) {
    return sada.unixtime() / 60UL;
  }

  return millis() / 60000UL;
}

uint32_t odrediSatniKljucNTPZahtjeva(const DateTime& sada) {
  if (jePouzdanoVrijemeDostupnoZaNTP(sada)) {
    return sada.unixtime() / 3600UL;
  }

  return millis() / 3600000UL;
}

bool parsirajISOVrijeme(const char* iso, DateTime& lokalniDt, uint16_t& lokalneMs) {
  const int osnovnaDuljina = 19;
  const size_t duljina = strlen(iso);
  const bool imaZuluneSufiks = (duljina == 20 && iso[19] == 'Z') ||
                               (duljina == 24 && iso[23] == 'Z');
  const size_t osnovnaDuljinaBezZ =
      imaZuluneSufiks ? (duljina - 1U) : duljina;

  if (!(osnovnaDuljinaBezZ == osnovnaDuljina || osnovnaDuljinaBezZ == 23U)) return false;
  if (iso[4] != '-' || iso[7] != '-' || iso[10] != 'T' ||
      iso[13] != ':' || iso[16] != ':') {
    return false;
  }

  const bool imaMilisekunde = (osnovnaDuljinaBezZ == 23U);
  if (imaMilisekunde && iso[19] != '.') {
    return false;
  }

  for (int i = 0; i < osnovnaDuljina; ++i) {
    if (i == 4 || i == 7 || i == 10 || i == 13 || i == 16) continue;
    if (!isDigit(iso[i])) return false;
  }

  if (imaMilisekunde) {
    if (!isDigit(iso[20]) || !isDigit(iso[21]) || !isDigit(iso[22])) {
      return false;
    }
  }

  char broj[5];
  memcpy(broj, iso, 4);
  broj[4] = '\0';
  const int godina = atoi(broj);

  broj[0] = iso[5];
  broj[1] = iso[6];
  broj[2] = '\0';
  const int mjesec = atoi(broj);

  broj[0] = iso[8];
  broj[1] = iso[9];
  const int dan = atoi(broj);

  broj[0] = iso[11];
  broj[1] = iso[12];
  const int sat = atoi(broj);

  broj[0] = iso[14];
  broj[1] = iso[15];
  const int minuta = atoi(broj);

  broj[0] = iso[17];
  broj[1] = iso[18];
  const int sekunda = atoi(broj);

  lokalneMs = 0;
  if (imaMilisekunde) {
    char msBroj[4];
    msBroj[0] = iso[20];
    msBroj[1] = iso[21];
    msBroj[2] = iso[22];
    msBroj[3] = '\0';
    lokalneMs = static_cast<uint16_t>(atoi(msBroj));
  }

  if (godina < MIN_NTP_GODINA || godina > MAX_NTP_GODINA ||
      mjesec < 1 || mjesec > 12) {
    return false;
  }

  static const uint8_t daniUMjesecu[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  const bool prijestupnaGodina =
      ((godina % 4 == 0) && (godina % 100 != 0)) || (godina % 400 == 0);
  int maxDana = daniUMjesecu[mjesec - 1];
  if (mjesec == 2 && prijestupnaGodina) {
    maxDana = 29;
  }

  const bool poljaIspravna =
      dan >= 1 && dan <= maxDana &&
      sat >= 0 && sat <= 23 &&
      minuta >= 0 && minuta <= 59 &&
      sekunda >= 0 && sekunda <= 59;
  if (!poljaIspravna) return false;

  lokalniDt = DateTime(godina, mjesec, dan, sat, minuta, sekunda);
  return true;
}

}  // namespace

bool jeAktivnaPocetnaOdgodaWiFi() {
  if (!wifiPocetnaOdgodaAktivna) {
    return false;
  }

  if (!vrijemeProslo(millis(), wifiPocetnaOdgodaDoMs)) {
    return true;
  }

  wifiPocetnaOdgodaAktivna = false;
  wifiPocetnaOdgodaDoMs = 0;
  vrijemePrvogWiFiStatusUpitaMs = 0;
  zadnjiWiFiStatusRecoveryUpitMs = 0;
  drugiWiFiStatusUpitPoslan = false;
  posaljiPCLog(F("WiFi/NTP: istekla pocetna odgoda nakon povratka napajanja, krecem s provjerom mreze"));
  return false;
}

void potvrdiWiFiPovezanostAkoTreba(const __FlashStringHelper* razlog) {
  if (wifiPovezanNaESP) {
    return;
  }

  wifiPovezanNaESP = true;
  postaviWiFiStatus(true);
  prioritetniNtpZahtjevNaCekanju = true;
  zadnjiAutomatskiNtpZahtjevMinutniKljuc = NTP_KLJUC_NEPOSTAVLJEN;
  zadnjiAutomatskiNtpZahtjevSatniKljuc = NTP_KLJUC_NEPOSTAVLJEN;
  posaljiPCLog(razlog);
}

void zatraziWiFiStatusESP() {
  espSerijskiPort.println(F("WIFISTATUS?"));
}

void posaljiWifiPostavkeESP() {
  espSerijskiPort.print(F("WIFI:"));
  espSerijskiPort.print(dohvatiWifiSsid());
  espSerijskiPort.print('|');
  espSerijskiPort.print(dohvatiWifiLozinku());
  espSerijskiPort.print('|');
  espSerijskiPort.print(koristiDhcpMreza() ? '1' : '0');
  espSerijskiPort.print('|');
  espSerijskiPort.print(dohvatiStatickuIP());
  espSerijskiPort.print('|');
  espSerijskiPort.print(dohvatiMreznuMasku());
  espSerijskiPort.print('|');
  espSerijskiPort.println(dohvatiZadaniGateway());

  posaljiPCLog(F("Poslane WiFi postavke mreznom mostu"));
}

void posaljiWiFiStatusESP() {
  espSerijskiPort.print(F("WIFIEN:"));
  espSerijskiPort.println(jeWiFiOmogucen() ? '1' : '0');
  posaljiPCLog(jeWiFiOmogucen()
                   ? F("Poslana naredba mreznom mostu: mreza ukljucena")
                   : F("Poslana naredba mreznom mostu: mreza iskljucena"));
}

void posaljiNTPPostavkeESP() {
  espSerijskiPort.print(F("NTPCFG:"));
  espSerijskiPort.println(dohvatiNTPServer());

  char log[80];
  snprintf_P(log,
             sizeof(log),
             PSTR("Poslan NTP server mreznom mostu: %s"),
             dohvatiNTPServer());
  posaljiPCLog(log);
}

void posaljiNTPZahtjevESP() {
  const DateTime sada = dohvatiTrenutnoVrijeme();
  zadnjiAutomatskiNtpZahtjevMinutniKljuc = odrediMinutniKljucNTPZahtjeva(sada);
  zadnjiAutomatskiNtpZahtjevSatniKljuc = odrediSatniKljucNTPZahtjeva(sada);
  espSerijskiPort.println(F("NTPREQ:SYNC"));
  posaljiPCLog(F("Poslan zahtjev mreznom mostu za NTP osvjezavanje toranjskog sata"));
}

void zatraziPrioritetnuNTPSinkronizaciju() {
  prioritetniNtpZahtjevNaCekanju = true;
  zadnjiAutomatskiNtpZahtjevMinutniKljuc = NTP_KLJUC_NEPOSTAVLJEN;
  zadnjiAutomatskiNtpZahtjevSatniKljuc = NTP_KLJUC_NEPOSTAVLJEN;
  posaljiPCLog(F("NTP: zabiljezen prioritetni zahtjev nakon rucne promjene vremena"));
}

void obradiAutomatskiNTPZahtjevESP() {
  if (!jeNTPOmogucen() || !jeWiFiPovezanNaESP() || jeAktivnaPocetnaOdgodaWiFi()) {
    return;
  }

  const DateTime sada = dohvatiTrenutnoVrijeme();
  const bool vrijemePouzdanoZaRaspored = jePouzdanoVrijemeDostupnoZaNTP(sada);
  if (!mehanikaTornjskogSataMirujeZaNTP()) {
    return;
  }

  if (vrijemePouzdanoZaRaspored &&
      (!jeSigurnaMinutaZaNTPZahtjev(sada) ||
       !jeSiguranProzorZaNTPZahtjev(sada))) {
    return;
  }

  const uint32_t minutniKljuc = odrediMinutniKljucNTPZahtjeva(sada);
  const uint32_t satniKljuc = odrediSatniKljucNTPZahtjeva(sada);

  if (!jeVrijemePotvrdjenoZaAutomatiku() || jeSinkronizacijaZastarjela()) {
    prioritetniNtpZahtjevNaCekanju = true;
  }

  if (prioritetniNtpZahtjevNaCekanju) {
    if (minutniKljuc == zadnjiAutomatskiNtpZahtjevMinutniKljuc) {
      return;
    }

    posaljiNTPZahtjevESP();
    if (!jeVrijemePotvrdjenoZaAutomatiku()) {
      posaljiPCLog(F("Automatski NTP zahtjev: cekam prvu potvrdu vremena za toranjski sat"));
    } else {
      posaljiPCLog(F("Automatski NTP zahtjev: obnavljam svjezu NTP sinkronizaciju toranjskog sata"));
    }
    return;
  }

  if (satniKljuc == zadnjiAutomatskiNtpZahtjevSatniKljuc) {
    return;
  }

  posaljiNTPZahtjevESP();
  posaljiPCLog(F("Automatski NTP zahtjev: siguran prozor za obnovu vremena toranjskog sata"));
}

bool parsirajNTPPayload(const char* payload,
                        DateTime& dt,
                        uint16_t& milisekunde,
                        bool& imaEksplicitanDST,
                        bool& dstAktivanIzvori) {
  imaEksplicitanDST = false;
  dstAktivanIzvori = false;
  milisekunde = 0;

  const char* dstSuffix = strstr(payload, ";DST=");
  if (dstSuffix == nullptr) {
    return parsirajISOVrijeme(payload, dt, milisekunde);
  }

  const size_t isoDuljina = static_cast<size_t>(dstSuffix - payload);
  const bool valjanaOsnovnaDuljina =
      isoDuljina == 19 || isoDuljina == 23 ||
      (isoDuljina == 20 && payload[19] == 'Z') ||
      (isoDuljina == 24 && payload[23] == 'Z');
  if (!valjanaOsnovnaDuljina) {
    return false;
  }

  if ((dstSuffix[5] != '0' && dstSuffix[5] != '1') || dstSuffix[6] != '\0') {
    return false;
  }

  char isoBuffer[25];
  if (isoDuljina >= sizeof(isoBuffer)) {
    return false;
  }

  memcpy(isoBuffer, payload, isoDuljina);
  isoBuffer[isoDuljina] = '\0';
  if (!parsirajISOVrijeme(isoBuffer, dt, milisekunde)) {
    return false;
  }

  imaEksplicitanDST = true;
  dstAktivanIzvori = (dstSuffix[5] == '1');
  return true;
}

bool obradiESPWiFiINtpLiniju(const char* linija) {
  if (strcmp(linija, "WIFI:CONNECTED") == 0) {
    const bool bioSpojen = wifiPovezanNaESP;
    wifiPovezanNaESP = true;
    postaviWiFiStatus(true);
    if (!bioSpojen) {
      prioritetniNtpZahtjevNaCekanju = true;
      zadnjiAutomatskiNtpZahtjevMinutniKljuc = NTP_KLJUC_NEPOSTAVLJEN;
      zadnjiAutomatskiNtpZahtjevSatniKljuc = NTP_KLJUC_NEPOSTAVLJEN;
    }
    posaljiPCLog(F("Mrezni most status mreze: spojeno"));
    return true;
  }

  if (strcmp(linija, "WIFI:DISCONNECTED") == 0) {
    wifiPovezanNaESP = false;
    postaviWiFiStatus(false);
    prioritetniNtpZahtjevNaCekanju = false;
    zadnjiAutomatskiNtpZahtjevMinutniKljuc = NTP_KLJUC_NEPOSTAVLJEN;
    zadnjiAutomatskiNtpZahtjevSatniKljuc = NTP_KLJUC_NEPOSTAVLJEN;
    posaljiPCLog(F("Mrezni most status mreze: odspojeno"));
    return true;
  }

  if (strncmp(linija, "WIFI:LOCAL_IP:", 14) == 0) {
    const char* ipAdresa = linija + 14;
    potvrdiWiFiPovezanostAkoTreba(F("Mrezni most status mreze: spojeno (potvrda preko lokalne IP)"));

    if (jeValjanaIPv4AdresaZaLCD(ipAdresa)) {
      strncpy(zadnjaLokalnaWiFiIP, ipAdresa, sizeof(zadnjaLokalnaWiFiIP) - 1);
      zadnjaLokalnaWiFiIP[sizeof(zadnjaLokalnaWiFiIP) - 1] = '\0';

      char log[48];
      snprintf_P(log, sizeof(log), PSTR("Mrezni most lokalna IP: %s"), ipAdresa);
      posaljiPCLog(log);
    } else {
      logirajLinijuESP(F("Mrezni most: neispravna lokalna IP: "), ipAdresa);
    }
    return true;
  }

  if (strncmp(linija, "WIFI:LCD:", 9) == 0) {
    const char* payload = linija + 9;
    if (payload[0] != '\0') {
      prikaziWiFiDijagnostiku(payload);

      char log[48];
      snprintf_P(log,
                 sizeof(log),
                 PSTR("Mrezni most WiFi LCD sazetak: %s"),
                 payload);
      posaljiPCLog(log);
    }
    return true;
  }

  if (strncmp(linija, "WIFI:MAC:", 9) == 0) {
    const char* macAdresa = linija + 9;
    if (strlen(macAdresa) == 17) {
      strncpy(zadnjaWiFiMACAdresa, macAdresa, sizeof(zadnjaWiFiMACAdresa) - 1);
      zadnjaWiFiMACAdresa[sizeof(zadnjaWiFiMACAdresa) - 1] = '\0';

      char log[48];
      snprintf_P(log,
                 sizeof(log),
                 PSTR("Mrezni most MAC adresa: %s"),
                 zadnjaWiFiMACAdresa);
      posaljiPCLog(log);
    } else {
      logirajLinijuESP(F("Mrezni most: neispravna MAC adresa: "), macAdresa);
    }
    return true;
  }

  if (strncmp(linija, "WIFI:", 5) == 0) {
    const char* poruka = linija + 5;
    while (*poruka == ' ') {
      ++poruka;
    }
    logirajLinijuESP(F("Mrezni most WiFi: "), poruka);
    return true;
  }

  if (strncmp(linija, "NTPLOG:", 7) == 0) {
    const char* poruka = linija + 7;
    while (*poruka == ' ') {
      ++poruka;
    }

    if (strncmp(poruka, "osvjezeno, epoch=", 17) == 0) {
      ntpCekanjePrijavljeno = false;
      return true;
    }

    if (strcmp(poruka, "jos nije postavljeno vrijeme, cekam...") == 0) {
      if (!ntpCekanjePrijavljeno) {
        posaljiPCLog(F("Mrezni most NTP: jos nije postavljeno vrijeme"));
        ntpCekanjePrijavljeno = true;
      }
      return true;
    }

    ntpCekanjePrijavljeno = false;
    logirajLinijuESP(F("Mrezni most NTP: "), poruka);
    return true;
  }

  if (strncmp(linija, "NTP:", 4) == 0) {
    const char* iso = linija + 4;
    DateTime ntpVrijeme;
    uint16_t ntpMilisekunde = 0;
    bool imaEksplicitanDST = false;
    bool dstAktivanIzvori = false;
    if (!parsirajNTPPayload(iso,
                            ntpVrijeme,
                            ntpMilisekunde,
                            imaEksplicitanDST,
                            dstAktivanIzvori)) {
      espSerijskiPort.println(F("ERR:NTP"));
      logirajLinijuESP(F("Neispravan NTP format: "), iso);
      return true;
    }

    potvrdiWiFiPovezanostAkoTreba(F("Mrezni most status mreze: spojeno (potvrda preko NTP sinkronizacije)"));

    if (jeNTPOmogucen()) {
      prioritetniNtpZahtjevNaCekanju = false;
      azurirajVrijemeIzNTP(ntpVrijeme,
                           ntpMilisekunde,
                           imaEksplicitanDST,
                           dstAktivanIzvori);
    }
    espSerijskiPort.println(F("ACK:NTP"));
    if (jeNTPOmogucen()) {
      logirajLinijuESP(F("Primljen NTP iz ESP-a: "), iso);
    } else {
      logirajLinijuESP(F("Preskocen NTP iz ESP-a jer je NTP iskljucen: "), iso);
    }
    return true;
  }

  return false;
}
