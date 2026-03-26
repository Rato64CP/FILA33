# 🕰️ Automatika toranjskog sata

Firmware za **Arduino Mega 2560** koji upravlja toranjskim satom kroz četiri ključna mehanička sklopa:
- kazaljke,
- okretna ploča,
- zvona,
- čekići (otkucavanje).

Sustav koristi **DS3231 RTC** kao primarni izvor vremena, **ESP (Serial3)** za NTP/MQTT integraciju i **24C32 EEPROM** za trajnu pohranu stanja i postavki.

---

## ✨ Glavne funkcionalnosti

- Sinkronizacija vremena preko RTC-a, NTP-a i DCF77.
- Dinamička korekcija kazaljki korak-po-korak (bez agresivnih skokova).
- Dvofazno upravljanje okretnom pločom (PARNI/NEPARNI relej).
- Automatsko i ručno upravljanje zvonima.
- Satno i polusatno otkucavanje čekića uz podršku za tihi period.
- Recovery nakon watchdog/power-loss reseta uz periodično spremanje kritičnog stanja.
- LCD izbornik s 6 tipki za lokalne postavke.

---

## 🧱 Arhitektura modula (`main/`)

- `main.ino` – orkestracija inicijalizacije i glavne petlje.
- `time_glob.*` – rad s vremenom (RTC/NTP/DCF status i fallback logika).
- `esp_serial.*` – serijska komunikacija s ESP-om (`NTP:`, `CMD:`, `MQTT:`).
- `kazaljke_sata.*` – upravljanje koracima kazaljki i sinkronizacijom prema vremenu.
- `okretna_ploca.*` – logika ciljne pozicije i dvofaznih koraka ploče.
- `zvonjenje.*` – upravljanje zvonima i inercijom.
- `otkucavanje.*` – satno/polusatno otkucavanje, slavljenje i mrtvačko.
- `menu_system.*` + `tipke.*` – korisnički izbornik i ulaz s tipki.
- `postavke.*` – učitavanje/spremanje postavki i validacija integriteta.
- `unified_motion_state.*` – jedinstveni model stanja kazaljki + ploče.
- `power_recovery.*` + `watchdog.*` – sigurnost rada 24/7.
- `wear_leveling.*` + `i2c_eeprom.*` – pristup i raspodjela zapisa u 24C32 EEPROM.

---

## 🔌 Hardverske komponente

- Arduino Mega 2560
- DS3231 RTC + 24C32 EEPROM (I2C)
- ESP modul (UART3)
- LCD 2x16 (I2C)
- Relejni izlazi za kazaljke, ploču, zvona i čekiće
- DCF77 prijemnik
- 6 tipki za izbornik + 2 tipke (slavljenje/mrtvačko) + 2 ručne sklopke zvona

---

## 🧭 Brzi pregled rada

1. `setup()` inicijalizira vrijeme, postavke, ulaze/izlaze, module i recovery.
2. `loop()` ciklički obrađuje:
   - komunikaciju i UI,
   - zvona i čekiće,
   - kazaljke i ploču,
   - sinkronizaciju i periodično spremanje stanja.
3. Watchdog se osvježava u petlji da sustav ostane stabilan u 24/7 radu.

---

## 📚 Dokumentacija

- Tehnička dokumentacija ponašanja sustava:
  - `docs/tehnicka_dokumentacija_firmware_sustava.md`
- Analiza modularnosti menija/tipki/postavki:
  - `docs/analiza_modularnosti_meni_tipke_postavke.md`

---

## 🛡️ Napomena za razvoj

Kod promjena koje diraju toranjski sat i povezane komponente (kazaljke, ploča, zvona, čekići), obavezno:
- sačuvati kompatibilnost EEPROM zapisa,
- zadržati neblokirajući rad glavne petlje,
- potvrditi da watchdog i recovery putanje ostaju funkcionalne.

