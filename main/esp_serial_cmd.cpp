// esp_serial_cmd.cpp - CMD naredbe koje Mega prima preko ESP32 dashboarda
#include "esp_serial_internal.h"

ESPCmdIshod obradiESPCmdLiniju(const char* komanda) {
  bool uspjeh = true;

  if      (strcmp(komanda, "ZVONO1_ON") == 0)       ukljuciZvono(1);
  else if (strcmp(komanda, "ZVONO1_OFF") == 0)      iskljuciZvono(1);
  else if (strcmp(komanda, "ZVONO2_ON") == 0)       ukljuciZvono(2);
  else if (strcmp(komanda, "ZVONO2_OFF") == 0)      iskljuciZvono(2);
  else if (strcmp(komanda, "GASI_SVE") == 0) {
    iskljuciObaZvonaSinkronizirano();
    zaustaviSlavljenje();
    zaustaviMrtvacko();
    zaustaviPogrebneSkripte();
  }
  else if (strcmp(komanda, "OTKUCAVANJE_OFF") == 0) postaviBlokaduOtkucavanja(true);
  else if (strcmp(komanda, "OTKUCAVANJE_ON") == 0)  postaviBlokaduOtkucavanja(false);
  else if (strcmp(komanda, "SLAVLJENJE_ON") == 0)   uspjeh = pokusajZapocetiSlavljenjeBezCekanja();
  else if (strcmp(komanda, "SLAVLJENJE_OFF") == 0)  zaustaviSlavljenje();
  else if (strcmp(komanda, "MRTVACKO_ON") == 0)     uspjeh = pokusajZapocetiMrtvackoBezCekanja();
  else if (strcmp(komanda, "MRTVACKO_OFF") == 0)    zaustaviMrtvacko();
  else if (strcmp(komanda, "POKOJNIK") == 0)        uspjeh = prebaciPokojnika();
  else if (strcmp(komanda, "POKOJNICA") == 0)       uspjeh = prebaciPokojnicu();
  else if (strcmp(komanda, "TIHI_ON") == 0)         postaviWebTihiRezim(true);
  else if (strcmp(komanda, "TIHI_OFF") == 0)        postaviWebTihiRezim(false);
  else if (strcmp(komanda, "SUNCE_JUTRO_ON") == 0)
    postaviSuncevDogadaj(
        SUNCEVI_DOGADAJ_JUTRO,
        true,
        dohvatiZvonoZaSuncevDogadaj(SUNCEVI_DOGADAJ_JUTRO),
        dohvatiOdgoduSuncevogDogadajaMin(SUNCEVI_DOGADAJ_JUTRO));
  else if (strcmp(komanda, "SUNCE_JUTRO_OFF") == 0)
    postaviSuncevDogadaj(
        SUNCEVI_DOGADAJ_JUTRO,
        false,
        dohvatiZvonoZaSuncevDogadaj(SUNCEVI_DOGADAJ_JUTRO),
        dohvatiOdgoduSuncevogDogadajaMin(SUNCEVI_DOGADAJ_JUTRO));
  else if (strcmp(komanda, "SUNCE_PODNE_ON") == 0)
    postaviSuncevDogadaj(
        SUNCEVI_DOGADAJ_PODNE,
        true,
        dohvatiZvonoZaSuncevDogadaj(SUNCEVI_DOGADAJ_PODNE),
        dohvatiOdgoduSuncevogDogadajaMin(SUNCEVI_DOGADAJ_PODNE));
  else if (strcmp(komanda, "SUNCE_PODNE_OFF") == 0)
    postaviSuncevDogadaj(
        SUNCEVI_DOGADAJ_PODNE,
        false,
        dohvatiZvonoZaSuncevDogadaj(SUNCEVI_DOGADAJ_PODNE),
        dohvatiOdgoduSuncevogDogadajaMin(SUNCEVI_DOGADAJ_PODNE));
  else if (strcmp(komanda, "SUNCE_VECER_ON") == 0)
    postaviSuncevDogadaj(
        SUNCEVI_DOGADAJ_VECER,
        true,
        dohvatiZvonoZaSuncevDogadaj(SUNCEVI_DOGADAJ_VECER),
        dohvatiOdgoduSuncevogDogadajaMin(SUNCEVI_DOGADAJ_VECER));
  else if (strcmp(komanda, "SUNCE_VECER_OFF") == 0)
    postaviSuncevDogadaj(
        SUNCEVI_DOGADAJ_VECER,
        false,
        dohvatiZvonoZaSuncevDogadaj(SUNCEVI_DOGADAJ_VECER),
        dohvatiOdgoduSuncevogDogadajaMin(SUNCEVI_DOGADAJ_VECER));
  else return ESP_CMD_NEPOZNATA;

  return uspjeh ? ESP_CMD_OK : ESP_CMD_ZAUZETO;
}
