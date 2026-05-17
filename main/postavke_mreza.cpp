// postavke_mreza.cpp - Mrezna validacija i NTP helperi za toranjski sat
#include <ctype.h>
#include <string.h>

#include "postavke_mreza.h"

namespace {

bool jeValjanMrezniTekst(const char* ulaz, size_t maxDuljina, bool dopustiPrazno) {
  bool imaZnakova = false;
  for (size_t i = 0; i < maxDuljina; i++) {
    const char c = ulaz[i];
    if (c == '\0') {
      return dopustiPrazno ? true : imaZnakova;
    }
    if (!isprint(static_cast<unsigned char>(c))) {
      return false;
    }
    if (c == '|') {
      return false;
    }
    imaZnakova = true;
  }
  return false;
}

bool jeValjanIPv4Tekst(const char* ulaz, size_t maxDuljina) {
  bool imaZnamenku = false;
  for (size_t i = 0; i < maxDuljina; i++) {
    const char c = ulaz[i];
    if (c == '\0') {
      return imaZnamenku;
    }
    if (!isdigit(static_cast<unsigned char>(c)) && c != '.') {
      return false;
    }
    if (isdigit(static_cast<unsigned char>(c))) {
      imaZnamenku = true;
    }
  }
  return false;
}

void osigurajNullTerminiraneMreznePostavke(EepromLayout::PostavkeSpremnik& spremnik) {
  spremnik.wifiSsid[sizeof(spremnik.wifiSsid) - 1] = '\0';
  spremnik.wifiLozinka[sizeof(spremnik.wifiLozinka) - 1] = '\0';
  spremnik.statickaIp[sizeof(spremnik.statickaIp) - 1] = '\0';
  spremnik.mreznaMaska[sizeof(spremnik.mreznaMaska) - 1] = '\0';
  spremnik.zadaniGateway[sizeof(spremnik.zadaniGateway) - 1] = '\0';
  spremnik.ntpServer[sizeof(spremnik.ntpServer) - 1] = '\0';
}

}  // namespace

bool jeKodiranNtpStatus(const char* ntpServer) {
  return ntpServer != nullptr &&
         (ntpServer[0] == '0' || ntpServer[0] == '1') &&
         ntpServer[1] != '\0';
}

bool procitajNtpOmogucenostIzTeksta(const char* ntpServer) {
  if (jeKodiranNtpStatus(ntpServer)) {
    return ntpServer[0] == '1';
  }
  return true;
}

const char* dohvatiNtpServerBezZastavice(const char* ntpServer) {
  if (jeKodiranNtpStatus(ntpServer)) {
    return ntpServer + 1;
  }
  return (ntpServer != nullptr) ? ntpServer : "";
}

void kodirajNtpServer(char* odrediste,
                      size_t velicina,
                      const char* ntpServer,
                      bool omogucen) {
  if (odrediste == nullptr || velicina < 3) {
    return;
  }

  const char* siguranServer = dohvatiNtpServerBezZastavice(ntpServer);
  if (siguranServer[0] == '\0') {
    siguranServer = "pool.ntp.org";
  }

  odrediste[0] = omogucen ? '1' : '0';
  strncpy(odrediste + 1, siguranServer, velicina - 2);
  odrediste[velicina - 1] = '\0';
}

bool sanitizirajMreznaPolja(EepromLayout::PostavkeSpremnik& spremnik) {
  bool biloPromjena = false;
  osigurajNullTerminiraneMreznePostavke(spremnik);

  if (!jeValjanMrezniTekst(spremnik.wifiSsid, sizeof(spremnik.wifiSsid), true)) {
    strncpy(spremnik.wifiSsid, "WiFi", sizeof(spremnik.wifiSsid) - 1);
    spremnik.wifiSsid[sizeof(spremnik.wifiSsid) - 1] = '\0';
    biloPromjena = true;
  }
  if (!jeValjanMrezniTekst(spremnik.wifiLozinka, sizeof(spremnik.wifiLozinka), true)) {
    strncpy(spremnik.wifiLozinka, "password", sizeof(spremnik.wifiLozinka) - 1);
    spremnik.wifiLozinka[sizeof(spremnik.wifiLozinka) - 1] = '\0';
    biloPromjena = true;
  }
  if (!jeValjanIPv4Tekst(spremnik.statickaIp, sizeof(spremnik.statickaIp))) {
    strncpy(spremnik.statickaIp, "192.168.1.100", sizeof(spremnik.statickaIp) - 1);
    spremnik.statickaIp[sizeof(spremnik.statickaIp) - 1] = '\0';
    biloPromjena = true;
  }
  if (!jeValjanIPv4Tekst(spremnik.mreznaMaska, sizeof(spremnik.mreznaMaska))) {
    strncpy(spremnik.mreznaMaska, "255.255.255.0", sizeof(spremnik.mreznaMaska) - 1);
    spremnik.mreznaMaska[sizeof(spremnik.mreznaMaska) - 1] = '\0';
    biloPromjena = true;
  }
  if (!jeValjanIPv4Tekst(spremnik.zadaniGateway, sizeof(spremnik.zadaniGateway))) {
    strncpy(spremnik.zadaniGateway, "192.168.1.1", sizeof(spremnik.zadaniGateway) - 1);
    spremnik.zadaniGateway[sizeof(spremnik.zadaniGateway) - 1] = '\0';
    biloPromjena = true;
  }
  if (!jeValjanMrezniTekst(
          dohvatiNtpServerBezZastavice(spremnik.ntpServer),
          sizeof(spremnik.ntpServer) - (jeKodiranNtpStatus(spremnik.ntpServer) ? 1 : 0),
          false)) {
    kodirajNtpServer(
        spremnik.ntpServer,
        sizeof(spremnik.ntpServer),
        "pool.ntp.org",
        procitajNtpOmogucenostIzTeksta(spremnik.ntpServer));
    biloPromjena = true;
  }
  if (spremnik.wifiOmogucen > 1) {
    spremnik.wifiOmogucen = true;
    biloPromjena = true;
  }
  if (spremnik.rs485Omogucen > 1) {
    spremnik.rs485Omogucen = false;
    biloPromjena = true;
  }

  return biloPromjena;
}
