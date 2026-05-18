# 🔧 ZVONKO v. 1.0 - Mega firmware

Croatian version: [README.hr.md](README.hr.md)

This folder contains the main `Arduino Mega 2560` firmware for `ZVONKO v. 1.0`. The Mega is the primary controller of the tower clock and the only source of truth for mechanics, settings, and recovery.

## ✨ Mega responsibilities

- control of the clock hands
- control of the rotating program plate
- control of bells and hammers
- separate inertia settings `INR1` and `INR2` for two bells
- selection of bell-brake mode through `K:0/1`
- safety shutdown of manual `ZVONO 1`, `ZVONO 2`, and `SLAVLJENJE` switches after `30 min` of continuous activation
- thermal protection of celebration ringing after `3 minutes` by inserting a `3 s` pause every `30 s`
- festive celebration ringing and the special funeral schedule for `All Saints' Day / All Souls' Day`
- local settings through the LCD menu and buttons
- storage of settings and state in external `24C32 EEPROM` or `FM24W256 FRAM`
- recovery after watchdog and power-loss resets
- watchdog `safe mode` and service unlock after repeated resets
- runtime `EEPROM` diagnostics and latched-fault acknowledgement through the LCD and buttons
- handling of RTC and NTP time sources
- degraded operating modes for `RTC` and `EEPROM` when faults repeat
- unified silent mode, BAT logic, local overrides, and the web-based silent-mode toggle
- `UPS mode` with dedicated mains monitoring input

## 🧭 Mega / ESP split

- `Mega 2560` makes all runtime decisions for the tower clock
- the external `ESP32` is only the network layer
- the network layer provides WiFi, NTP, WiFi setup, and the wireless service API
- the `ESP32` web interface can edit only safe groups: `Sustav`, `Stapici`, `BAT`, `Sunce`, and `Blagdani`, while the `Mega` remains the only authority for validation and persistent storage
- `main/mise_automatika.*` handles regular daily and Sunday Masses plus special feast-day Masses, independently from `main/sunceva_automatika.*`
- `main/pogrebne_skripte.*` handles one-shot `POKOJNIK` and `POKOJNICA` sequences from the `ESP32` dashboard
- `POKOJNIK` starts the male bell for `2 minutes`, waits for inertia to finish, then starts funeral ringing for `10 minutes`
- `POKOJNICA` starts the female bell for `2 minutes`, waits for inertia to finish, then starts funeral ringing for `10 minutes`
- `Blagdani` use a predefined list of fixed and movable feasts; the web and serial layers only edit enable flags and the Mass time `HH:MM`, while the tower clock itself performs Sunday-style ringing `2 h` and `1 h` before Mass without extra celebration ringing
- `Redovite mise` store the daily and Sunday Mass time `HH:MM`; the daily Mass uses only the male bell `30 min` before Mass, while the Sunday Mass uses Sunday-style ringing on both bells `2 h` and `1 h` before Mass
- an empty time from the web layer means that the corresponding daily Mass, Sunday Mass, or feast is disabled regardless of the checkbox state
- all Mass-related ringing in `main/mise_automatika.cpp` starts at second `:25`, synchronized with pin reading in `main/okretna_ploca.cpp`
- `BAT od/do` from the web layer and the local menu is interpreted as the range in which regular striking is allowed; outside that range the `Mega` blocks only striking

## 🧩 Key modules

- `main.ino` - initialization and main loop
- `time_glob.*` - time-source handling, DST, and synchronization
- `esp_serial.*` - public UART bridge layer towards the external network bridge
- `esp_serial_internal.h` - shared internal contract between serial submodules
- `esp_serial_status.cpp` - `STATUS` snapshot and push updates towards the `ESP32` dashboard
- `esp_serial_ntp.cpp` - WiFi, NTP, and time coordination towards the network bridge
- `esp_serial_postavke.cpp` - `SETREQ/SETCFG` exchange for settings groups
- `esp_serial_cmd.cpp` - `CMD:` commands for bells, celebration ringing, silent mode, and funeral scripts
- `esp_serial_parser.cpp` - parser for incoming lines from the `ESP32` bridge
- `kazaljke_sata.*` - hand movement and synchronization
- `okretna_ploca.*` - plate position, phases, steps, and pin reading
- `mise_automatika.*` - regular daily/Sunday Masses and feast-day Masses
- `pogrebne_skripte.*` - one-shot `POKOJNIK` and `POKOJNICA` sequences
- `zvonjenje.*` - bell control and related states
- `otkucavanje.*` - hourly and half-hour striking
- `slavljenje_mrtvacko.*` - celebration ringing, funeral mode, and thumbwheel timing
- `prekidac_tisine.*` - unified silent mode and silent-mode indicator
- `ups_nadzor.*` - mains monitoring and output blocking while the tower clock runs only from UPS power
- `menu_system.*`, `lcd_display.*`, `tipke.*` - local user interface
- `postavke.*` - core persistent settings logic
- `postavke_skladistenje.*` - checksums, container read/write logic, and EEPROM helpers
- `postavke_mreza.*` - WiFi, IP, and NTP text validation for the `ESP32` bridge
- `postavke_kalendar.*` - liturgical calendar, feast days, and Mass schedules
- `unified_motion_state.*` - shared motion state
- `power_recovery.*` and `watchdog.*` - 24/7 reliability and recovery
- `wear_leveling.*` and `i2c_eeprom.*` - persistent storage and write distribution

## ⏱️ Time sources

- `DS3231 RTC` is the primary source for offline operation
- `NTP` comes through the `ESP32`, but the `Mega 2560` chooses when synchronization is allowed
- automatic CET/CEST switching remains under tower-clock firmware control
- the `Mega` requests `NTP` only in a safe window when the hands and rotating plate are not mid-step
- after a restart, the age of the last synchronization is reconstructed from RTC time so a new boot does not falsely appear “fresh for 24 hours”
- after repeated invalid RTC reads, `RTC OGRANICEN RAD` becomes active and automation remains conservative until the RTC recovers
- loss of `RTC/I2C` connectivity enables a strict output fail-safe in `main/time_glob.cpp`, so bells, hammers, hands, and the rotating plate remain blocked until the `DS3231` recovers

## 🔄 Serial communication with the ESP

- the `Mega` uses `Serial3` for the external `ESP32` network bridge
- `Serial1` is the active `RS485` transport, while communication towards the `ESP` remains on `Serial3`
- active flows are `WIFI:`, `WIFIEN:`, `WIFISTATUS?`, `NTPCFG:`, `NTPREQ:SYNC`, `NTP:`, `CMD:`, `STATUS?`, `SETREQ:*`, and `SETCFG:*` for the groups `SUSTAV`, `STAPICI`, `BAT`, `SUNCE`, `MISE`, `BLAGDANI_NEP`, and `BLAGDANI_POM`
- `NTPREQ:SYNC` is a controlled request towards the `ESP` only when the tower-clock mechanism is idle
- the external network bridge no longer sends `NTP:` on its own schedule; it only answers `Mega` requests
- the first `NTP` after restart or WiFi reconnection is confirmed by the `ESP` with a second sample before the `Mega` accepts it for the tower clock
- accepted `NTP` records and the start of regular striking are aligned to the `RTC SQW` second boundary when available
- `SETREQ:SUSTAV`, `SETREQ:STAPICI`, `SETREQ:BAT`, `SETREQ:SUNCE`, `SETREQ:MISE`, `SETREQ:BLAGDANI_NEP`, and `SETREQ:BLAGDANI_POM` ask for the current state of a single web settings group from `main/postavke.*`
- `SETCFG:SUSTAV|...`, `SETCFG:STAPICI|...`, `SETCFG:BAT|...`, `SETCFG:SUNCE|...`, `SETCFG:MISE|...`, `SETCFG:BLAGDANI_NEP|...`, and `SETCFG:BLAGDANI_POM|...` send a new full payload for the corresponding group towards the `Mega`

## 💾 EEPROM and recovery

- external `24C32 EEPROM` or `FM24W256 FRAM` stores settings and critical runtime state
- even though the physical capacity may be larger, the tower clock intentionally keeps the compatible layout within the first `4096 B`
- `UnifiedMotionState` uses `24` rotating slots for the hands and rotating plate
- every `UnifiedMotionState` record carries a checksum so corrupted slots can be skipped after power loss during a write
- the last time-synchronization record has its own checksum and a compatible legacy fallback
- `power_recovery.*` restores the hands and rotating plate to a consistent state after restart
- watchdog resets are stored in a separate EEPROM block, and only repeated watchdog resets without a power-loss marker lead to `safe mode`
- `safe mode` blocks mechanics and shows `SUSTAV ZAKLJUCAN / PREVISE RESETA` until the operator holds `ENT / SELECT` for `5 s`
- `power_recovery.*` performs a periodic `EEPROM` health check every `6 hours`
- latched `EEPROM` faults remain stored until manual operator acknowledgement and enable degraded `EEPROM` mode
- in degraded `EEPROM` mode, periodic backups and helper records from `time_glob.*` are paused
- `LCD`, `RTC`, external `EEPROM/FRAM`, and the service `I2C` scan all use shared `Wire` bus preparation with timeout
- `EEPROM/I2C` retry and polling loops refresh the watchdog whenever it is active
- whenever EEPROM layout or recovery logic changes, always review:
  - `eeprom_konstante.h`
  - `unified_motion_state.*`
  - `power_recovery.*`

## 🔩 Hardware controlled by the Mega

- two `Koncar 0.55 kW / 380 V` three-phase motors, one for `Zvono 1` and one for `Zvono 2`
- microswitches on the rear shaft of each bell motor for phase reversal and safe bell-operation transitions
- two `310 VDC` electromagnetic hammers, one per bell, with pulses of about `0.01 s`
- the hand-drive motor with gearbox running from `EVEN/ODD` pulses of about `6 s`, matching the logic in `main/kazaljke_sata.*`
- the electrical cabinet with bell phase-reversal contactors, hammer contactors, fuses, and protective equipment
- relays for the even/odd hand phases
- relays for the rotating plate
- outputs for bells and hammers
- `DS3231 RTC` and external `24C32 EEPROM` or `FM24W256 FRAM` over `I2C`
- `16x2 LCD` over `I2C`
- `00-99` thumbwheel for funeral-ringing duration
- the silent-mode toggle switch and silent-mode indicator lamp
- a mains-monitoring input for `UPS` mode
- LED indicators for `ZVONO 1`, `ZVONO 2`, `SLAVLJENJE`, and `MRTVACKO`
- 6 direct local-menu buttons: `GORE`, `DOLJE`, `LIJEVO`, `DESNO`, `DA`, `NE`
- rotating-plate position editing in the menu moves only through valid `15 min` steps
- the main LCD shows `NEMA STRUJE!` in `UPS` mode and abbreviates Monday to `PON.` for a clean date layout
- the first LCD row uses positions `11-13` for `NTP`, `MAN`, `ERR`, or `---`, while positions `15-16` show the `DS3231` temperature
- the activity `*`, `R/N`, and WiFi `W` markers are no longer shown on the main row
- the time colon blinks at `1/2 SQW` only while `main/zvonjenje.cpp`, `main/otkucavanje.cpp`, `main/kazaljke_sata.cpp`, or `main/okretna_ploca.cpp` is currently active, or while WiFi is disconnected

## ✅ Development guidelines

- the main loop must remain non-blocking
- the `Mega 2560` must remain safe and independent of constant network access
- an `ESP32` fault or restart must not endanger the basic clock operation
- `I2C` access for the `LCD`, `RTC`, and external `EEPROM/FRAM` must remain on the shared bus-preparation path with timeout
- every change touching the hands, plate, bells, or recovery should be checked against the existing modules in `main/`
