# Upute za rad s projektom toranjskog sata

- Sav tekst u dokumentaciji i komentarima piši na hrvatskom jeziku.
- README održavaj strukturiran s emoji naslovima i točkastim popisima gdje je moguće.
- Kod dokumentiraj referiranjem na postojeće funkcije i module unutar `main/` direktorija (ne `src/`).
- Svaku novu uputu jasno poveži s toranjskim satom ili povezanim komponentama (kazaljke, okretna ploča, zvona, čekići, sinkronizacija vremena, watchdog/recovery).
- Pri izmjenama koje diraju EEPROM raspored ili recovery logiku obavezno provjeri usklađenost datoteka `eeprom_konstante.h`, `unified_motion_state.*` i `power_recovery.*`.
