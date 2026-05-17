// esp_time_ntp.ino - kalendarske i NTP pomocne funkcije za mrezni most toranjskog sata

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
  if (izlaz == nullptr || velicinaIzlaza == 0) {
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

