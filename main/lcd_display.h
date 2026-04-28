// lcd_display.h
#pragma once

#include <stdint.h>
#include <LiquidCrystal_I2C.h>

extern LiquidCrystal_I2C lcd;

void inicijalizirajLCD();
void prikaziSat();
void prikaziPoruku(const char* redak1, const char* redak2 = "");
void prikaziZakljucaniSustav();
void prisiliOsvjezavanjeGlavnogPrikazaLCD();
void postaviWiFiStatus(bool aktivan);
void prikaziWiFiDijagnostiku(const char* tekst);
void primijeniLCDPozadinskoOsvjetljenje(bool ukljuci);

// Signalizacija statusa otkucavanja na LCD-u
void signalizirajZvono_Ringing(uint8_t zvono);
void signalizirajHammer1_Active();
void signalizirajHammer2_Active();
void signalizirajCelebration_Mode();
void signalizirajFuneral_Mode();
void signalizirajError_RTC();
void signalizirajError_EEPROM();
void signalizirajUpozorenjeRtcBaterije();
void potvrdiUpozorenjeRtcBaterije();
bool jeUpozorenjeRtcBaterijeAktivno();
void signalizirajLatchedFaultEEPROM();
void potvrdiLatchedFaultEEPROM();
