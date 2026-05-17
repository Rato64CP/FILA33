// esp_serial_status.cpp - STATUS snapshot i push prema ESP32 dashboardu
#include "esp_serial_internal.h"

ESPStatusSnapshot dohvatiTrenutniStatusSnapshotZaESP() {
  ESPStatusSnapshot snapshot{};
  snapshot.vrijemePotvrdjeno = jeVrijemePotvrdjenoZaAutomatiku();
  snapshot.ntpOmogucen = jeNTPOmogucen();
  snapshot.kazaljkeUSinkronu = suKazaljkeUSinkronu();
  snapshot.handPosition = dohvatiMemoriraneKazaljkeMinuta();
  snapshot.plocaUSinkronu = jePlocaUSinkronu();
  snapshot.platePosition = dohvatiPozicijuPloce();
  snapshot.slavljenje = jeSlavljenjeUTijeku();
  snapshot.mrtvacko = jeMrtvackoUTijeku();
  snapshot.otkucavanje = jeOtkucavanjeUTijeku();
  snapshot.zvono1 = jeZvonoAktivno(1);
  snapshot.zvono2 = jeZvonoAktivno(2);
  snapshot.pogrebnaSkriptaTip = dohvatiTipAktivnePogrebneSkripte();
  snapshot.sunceJutro = jeSuncevDogadajOmogucen(SUNCEVI_DOGADAJ_JUTRO);
  snapshot.suncePodne = jeSuncevDogadajOmogucen(SUNCEVI_DOGADAJ_PODNE);
  snapshot.sunceVecer = jeSuncevDogadajOmogucen(SUNCEVI_DOGADAJ_VECER);
  snapshot.tihiMod = jePrekidacTisineAktivan();
  return snapshot;
}

bool jesuStatusSnapshotiJednaki(const ESPStatusSnapshot& a,
                                const ESPStatusSnapshot& b) {
  return a.vrijemePotvrdjeno == b.vrijemePotvrdjeno &&
         a.ntpOmogucen == b.ntpOmogucen &&
         a.kazaljkeUSinkronu == b.kazaljkeUSinkronu &&
         a.handPosition == b.handPosition &&
         a.plocaUSinkronu == b.plocaUSinkronu &&
         a.platePosition == b.platePosition &&
         a.slavljenje == b.slavljenje &&
         a.mrtvacko == b.mrtvacko &&
         a.otkucavanje == b.otkucavanje &&
         a.zvono1 == b.zvono1 &&
         a.zvono2 == b.zvono2 &&
         a.pogrebnaSkriptaTip == b.pogrebnaSkriptaTip &&
         a.sunceJutro == b.sunceJutro &&
         a.suncePodne == b.suncePodne &&
         a.sunceVecer == b.sunceVecer &&
         a.tihiMod == b.tihiMod;
}

void posaljiStatusESPU() {
  const DateTime sada = dohvatiTrenutnoVrijeme();
  char vrijemeIso[21];
  snprintf_P(vrijemeIso,
             sizeof(vrijemeIso),
             PSTR("%04d-%02d-%02dT%02d:%02d:%02d"),
             sada.year(),
             sada.month(),
             sada.day(),
             sada.hour(),
             sada.minute(),
             sada.second());

  char statusLinija[192];
  snprintf_P(statusLinija,
             sizeof(statusLinija),
             PSTR("STATUS:time=%s|src=%s|ok=%d|wifi=%d|mq=%d|mqen=%d|ntp=%d|hs=%d|hp=%d|ps=%d|pp=%d|sl=%d|mr=%d|ot=%d|b1=%d|b2=%d|pk=%u|sj=%d|sp=%d|sv=%d|tm=%d"),
             vrijemeIso,
             dohvatiOznakuIzvoraVremena(),
             jeVrijemePotvrdjenoZaAutomatiku() ? 1 : 0,
             jeWiFiPovezanNaESP() ? 1 : 0,
             0,
             0,
             jeNTPOmogucen() ? 1 : 0,
             suKazaljkeUSinkronu() ? 1 : 0,
             dohvatiMemoriraneKazaljkeMinuta(),
             jePlocaUSinkronu() ? 1 : 0,
             dohvatiPozicijuPloce(),
             jeSlavljenjeUTijeku() ? 1 : 0,
             jeMrtvackoUTijeku() ? 1 : 0,
             jeOtkucavanjeUTijeku() ? 1 : 0,
             jeZvonoAktivno(1) ? 1 : 0,
             jeZvonoAktivno(2) ? 1 : 0,
             static_cast<unsigned>(dohvatiTipAktivnePogrebneSkripte()),
             jeSuncevDogadajOmogucen(SUNCEVI_DOGADAJ_JUTRO) ? 1 : 0,
             jeSuncevDogadajOmogucen(SUNCEVI_DOGADAJ_PODNE) ? 1 : 0,
             jeSuncevDogadajOmogucen(SUNCEVI_DOGADAJ_VECER) ? 1 : 0,
             jePrekidacTisineAktivan() ? 1 : 0);
  espSerijskiPort.println(statusLinija);
}

bool obradiESPStatusnuLiniju(const char* linija) {
  if (strcmp(linija, "STATUS?") == 0) {
    posaljiStatusESPU();
    return true;
  }

  if (strncmp(linija, "STATUS:", 7) == 0) {
    return true;
  }

  return false;
}

void osvjeziStatusPushPremaESP() {
  const ESPStatusSnapshot trenutniSnapshot = dohvatiTrenutniStatusSnapshotZaESP();
  if (zadnjiStatusPushInicijaliziran &&
      jesuStatusSnapshotiJednaki(trenutniSnapshot, zadnjiStatusPushSnapshot)) {
    return;
  }

  zadnjiStatusPushSnapshot = trenutniSnapshot;
  zadnjiStatusPushInicijaliziran = true;
  posaljiStatusESPU();
}
