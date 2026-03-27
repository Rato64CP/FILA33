# 🕰️ Automatika toranjskog sata

Firmware za **Arduino Mega 2560** koji upravlja toranjskim satom kroz četiri ključna mehanička sklopa:
- kazaljke
- okretna ploča
- zvona
- čekići (otkucavanje)

Sustav koristi **DS3231 RTC** kao primarni izvor vremena, **ESP** preko `Serial3` za NTP/MQTT integraciju i **24C32 EEPROM** za trajnu pohranu stanja i postavki.

---

## ✨ Glavne funkcionalnosti

- Sinkronizacija vremena preko RTC-a, NTP-a i DCF77.
- Dinamička korekcija kazaljki korak-po-korak bez agresivnih skokova.
- Dvofazno upravljanje okretnom pločom preko PARNI i NEPARNI relejskih faza.
- Automatsko i ručno upravljanje zvonima.
- Satno i polusatno otkucavanje čekića uz podršku za tihi period.
- Recovery nakon watchdog i power-loss reseta uz periodično spremanje kritičnog stanja.
- Wear-leveling EEPROM-a s trajnim pamćenjem zadnjeg zapisanog slota radi točnog recoveryja kazaljki i ploče nakon boota.
- LCD izbornik sa 6 tipki za lokalne postavke.

---

## 🧱 Arhitektura modula (`main/`)

- `main.ino` – orkestracija inicijalizacije i glavne petlje.
- `time_glob.*` – rad s vremenom, RTC/NTP/DCF status i fallback logika.
- `esp_serial.*` – serijska komunikacija s ESP-om (`NTP:`, `CMD:`, `MQTT:`).
- `kazaljke_sata.*` – upravljanje koracima kazaljki i sinkronizacijom prema vremenu.
- `okretna_ploca.*` – logika ciljne pozicije i dvofaznih koraka ploče.
- `zvonjenje.*` – upravljanje zvonima i inercijom.
- `otkucavanje.*` – satno i polusatno otkucavanje, slavljenje i mrtvačko.
- `menu_system.*` i `tipke.*` – korisnički izbornik i ulaz s tipki.
- `postavke.*` – učitavanje i spremanje postavki te validacija integriteta.
- `unified_motion_state.*` – jedinstveni model stanja kazaljki i ploče.
- `power_recovery.*` i `watchdog.*` – sigurnost rada 24/7 i recovery nakon boota.
- `wear_leveling.*` i `i2c_eeprom.*` – pristup i raspodjela zapisa u 24C32 EEPROM-u.

---

## 💾 EEPROM i Recovery

- `main/wear_leveling.*` trajno pamti zadnji uspješno zapisani slot za svaki segment 24C32 EEPROM-a.
- `main/unified_motion_state.*`, `main/power_recovery.*`, `main/postavke.*` i `main/time_glob.*` nakon boota zato čitaju najnoviji zapis, a ne stariji slot.
- Ova logika je važna za toranjski sat jer sprječava lažnu korekciju kazaljki ili ploče nakon kratkog nestanka napajanja.

---

## 🔧 Hardverske komponente

- Arduino Mega 2560
- DS3231 RTC + 24C32 EEPROM (I2C)
- ESP modul (UART3)
- LCD 2x16 (I2C)
- Relejni izlazi za kazaljke, ploču, zvona i čekiće
- DCF77 prijemnik
- 6 tipki za izbornik
- 2 tipke za slavljenje i mrtvačko
- 2 ručne sklopke zvona

---

## 🧭 Brzi pregled rada

1. `setup()` inicijalizira vrijeme, postavke, ulaze/izlaze, module i recovery.
2. `loop()` ciklički obrađuje komunikaciju, UI, zvona, čekiće, kazaljke, ploču, sinkronizaciju i periodično spremanje stanja.
3. Watchdog se osvježava u petlji kako bi sustav ostao stabilan u 24/7 radu.

---

## 📚 Dokumentacija

- Tehnička dokumentacija ponašanja sustava: `docs/tehnicka_dokumentacija_firmware_sustava.md`
- Analiza modularnosti menija, tipki i postavki: `docs/analiza_modularnosti_meni_tipke_postavke.md`

---

## 🛡️ Napomena za razvoj

Kod promjena koje diraju toranjski sat i povezane komponente, obavezno:
- sačuvati kompatibilnost EEPROM zapisa
- zadržati neblokirajući rad glavne petlje
- potvrditi da watchdog i recovery putanje ostaju funkcionalne
- kod izmjena recovery logike provjeriti usklađenost modula `main/eeprom_konstante.h`, `main/unified_motion_state.*` i `main/power_recovery.*`
