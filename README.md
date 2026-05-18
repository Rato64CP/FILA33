# 🕰️ ZVONKO v. 1.0

Croatian version: [README.hr.md](README.hr.md)

`ZVONKO v. 1.0` is the firmware and control logic for a tower clock system built around a split architecture:

- `Arduino Mega 2560` handles all real clock mechanics and safety-critical logic.
- `ESP32` provides WiFi, NTP, OTA updates, the service dashboard, and safe web-based settings.

The tower clock must remain operational even if the network layer is unavailable.

## ✨ What the system does

- keeps time using a `DS3231 RTC` with controlled `NTP` synchronization
- drives the clock hands with synchronization and correction logic
- drives the rotating program plate through two-phase motion and pin reading
- controls bells, hammers, celebration ringing, and funeral ringing
- supports separate bell inertia settings `INR1` and `INR2`
- supports `K:0/1` operation with or without the bell brake
- protects celebration ringing thermally after `3 minutes` by inserting a `3 s` pause every `30 s`
- supports festive celebration ringing and a special funeral schedule for `All Saints' Day / All Souls' Day`
- stores settings and critical state in external `24C32 EEPROM` or `FM24W256 FRAM`
- restores the system after watchdog resets and power-loss resets
- enters `safe mode` after repeated watchdog resets
- monitors `RTC` and `EEPROM` health and switches to limited operation when faults become repeatable
- keeps EEPROM faults latched until the operator acknowledges them
- supports a unified silent mode for Easter silence, a physical toggle switch, and an ESP dashboard toggle
- supports `UPS mode`, which keeps tower clock logic alive without mains power while blocking mechanical outputs

## 🧭 Architecture

- `Arduino Mega 2560` is the single source of truth for the tower clock state.
- `ESP32` is an auxiliary network bridge only.
- The `ESP32` does not make mechanical decisions about hands, plate, bells, or hammers.
- The `Mega` decides when `NTP` is allowed and when the moment is safe for synchronization.
- A fault or restart of the network bridge must not stop the basic clock operation.

## 🔐 Mega ↔ ESP rules

- critical runtime decisions originate on the `Mega`
- `NTP` synchronization is requested only when the `Mega` considers the mechanism idle and safe
- the `ESP32` web interface can edit settings, but the `Mega` validates and saves them
- regular clock operation must continue without WiFi or internet access

## 🔄 Serial communication

- the `Mega` uses `Serial3` as the active serial link to the external `ESP32`
- active protocol groups are:
  - `WIFI:`
  - `WIFIEN:`
  - `WIFISTATUS?`
  - `NTPCFG:`
  - `NTPREQ:SYNC`
  - `NTP:`
  - `CMD:`
  - `STATUS?`
  - `SETREQ:*`
  - `SETCFG:*`
- active settings groups are:
  - `SUSTAV`
  - `STAPICI`
  - `BAT`
  - `SUNCE`
  - `MISE`
  - `BLAGDANI_NEP`
  - `BLAGDANI_POM`
- the `ESP32` does not send unsolicited `NTP` updates anymore; it only answers `Mega` requests
- the first `NTP` after boot or WiFi reconnection is confirmed by a second sample before the tower clock accepts it
- the `ESP32` provides separate `/settings` and `/blagdani` pages

## 🧩 Project structure

- `main/` – main tower clock firmware for `Arduino Mega 2560`
- `esp_firmware/` – auxiliary firmware for the external `ESP32`
- `main/main.ino` – initialization and main loop
- `main/time_glob.*` – RTC, NTP, DST, and time-source priorities
- `main/mise_automatika.*` – regular daily/Sunday Masses and special feast-day Masses
- `main/esp_serial.*` – public serial bridge API
- `main/esp_serial_internal.h` – internal interface shared by serial submodules
- `main/esp_serial_status.cpp` – `STATUS` snapshot and push updates to `ESP32`
- `main/esp_serial_ntp.cpp` – WiFi, NTP, and time-coordination logic
- `main/esp_serial_postavke.cpp` – `SETREQ/SETCFG` settings exchange
- `main/esp_serial_cmd.cpp` – `CMD:` actions for bells, silent mode, celebration, and funeral scripts
- `main/esp_serial_parser.cpp` – incoming line parser for the ESP bridge
- `main/kazaljke_sata.*` – clock-hand motion and correction
- `main/okretna_ploca.*` – plate position, phase logic, and pin reading
- `main/zvonjenje.*` – bell control and inertia handling
- `main/otkucavanje.*` – hourly and half-hour striking
- `main/slavljenje_mrtvacko.*` – celebration, funeral mode, and thumbwheel timing
- `main/pogrebne_skripte.*` – one-shot `POKOJNIK` and `POKOJNICA` sequences
- `main/prekidac_tisine.*` – unified silent mode and silent-mode indicator
- `main/ups_nadzor.*` – mains monitoring and UPS mode
- `main/menu_system.*`, `main/tipke.*`, `main/lcd_display.*` – local LCD menu and input
- `main/postavke.*` – core persistent settings logic
- `main/postavke_skladistenje.*` – checksums, EEPROM containers, and low-level settings storage helpers
- `main/postavke_mreza.*` – WiFi, IP, and NTP string validation helpers
- `main/postavke_kalendar.*` – liturgical calendar, feast-day settings, and Mass settings
- `main/unified_motion_state.*` – shared state of hands and rotating plate
- `main/power_recovery.*` and `main/watchdog.*` – recovery and 24/7 reliability
- `main/wear_leveling.*` and `main/i2c_eeprom.*` – persistent storage on external `24C32 EEPROM` or `FM24W256 FRAM`

## 📶 WiFi and service dashboard

- the `ESP32` can start a temporary setup network called `ZVONKO_setup`
- setup password: `zvonko10`
- setup AP can be activated by holding the button on `GPIO27` to `GND`
- setup AP can also be activated by holding `LEFT + RIGHT` on the local keypad from the main clock screen
- setup pages are available at:
  - `http://192.168.4.1/`
  - `http://192.168.4.1/setup`
- the ESP serial link to the `Mega` uses:
  - `GPIO16` as `RX`
  - `GPIO17` as `TX`
- status LED uses `GPIO26`
- after saving WiFi settings, the `ESP32` forwards them to the `Mega`

### Dashboard buttons

- `MUSKO`
- `ZENSKO`
- `SLAVI`
- `BRECA`
- `POKOJNIK`
- `POKOJNICA`
- `JUTRO`
- `PODNE`
- `VECER`
- `TIHI MOD`

### Funeral dashboard scripts

- `POKOJNIK`
  - starts the male bell for `2 minutes`
  - waits for inertia to finish
  - starts funeral ringing for `10 minutes`
- `POKOJNICA`
  - starts the female bell for `2 minutes`
  - waits for inertia to finish
  - starts funeral ringing for `10 minutes`

### Web settings

- `/settings` edits:
  - `Sustav`
  - `Stapici`
  - `BAT`
  - `Sunce`
- `/blagdani` edits:
  - regular Mass times
  - predefined fixed feasts
  - predefined movable feasts
- the web interface does not edit:
  - time
  - date
  - clock-hand position
  - rotating-plate position

## ⛪ Mass and feast-day ringing

- daily Mass starts only the male bell `30 minutes` before the configured Mass time `HH:MM`
- Sunday Mass starts Sunday-style ringing on both bells:
  - `2 hours` before Mass
  - `1 hour` before Mass
- feast-day Mass follows the same Sunday-style ringing on both bells:
  - `2 hours` before Mass
  - `1 hour` before Mass
- no extra celebration ringing is triggered from Mass automation
- an empty time field on the web interface means the corresponding Mass or feast is disabled
- all Mass-related bell events start at second `:25`, synchronized with rotating-plate pin reading in `main/okretna_ploca.cpp`

## 💾 EEPROM and recovery

- external `24C32 EEPROM` or `FM24W256 FRAM` stores settings, `UnifiedMotionState`, DST status, and critical backup data
- even with `FM24W256`, the firmware intentionally keeps the compatible layout within the first `4096 bytes`
- `UnifiedMotionState` uses `24` rotating slots for the hands and rotating plate
- each `UnifiedMotionState` slot has a checksum and invalid or partially written records are skipped
- the last synchronization record has its own checksum while remaining compatible with older stored data
- `power_recovery.*` restores the hands and rotating plate to a consistent state after restart
- watchdog resets are tracked persistently and repeated resets trigger `safe mode`
- `safe mode` blocks hands, rotating plate, bells, and hammers until the operator holds `ENT / SELECT` for `5 seconds`
- EEPROM health is checked at boot and every `6 hours`
- EEPROM faults remain latched until manual operator acknowledgement
- in degraded EEPROM mode, periodic backups and auxiliary records such as DST and last synchronization are paused

### Important safety note

The tower clock uses a shared I2C bus preparation with timeout for:

- `LCD`
- `DS3231 RTC`
- external `24C32 EEPROM` / `FM24W256 FRAM`
- service scanning

This is handled through `main/i2c_bus.*`, where `Wire.setWireTimeout(...)` is enabled so the controller does not freeze permanently if the RTC or memory module disappears from the bus.

When changing EEPROM layout or recovery logic, always review:

- `main/eeprom_konstante.h`
- `main/unified_motion_state.*`
- `main/power_recovery.*`

## 🔕 Silent mode and BAT

- the unified silent mode can be activated by:
  - Easter silence
  - a physical toggle switch
  - the virtual ESP dashboard toggle
- the silent-mode indicator lights only when the final effective silent mode is truly active
- silent mode blocks:
  - bells
  - hammers
  - celebration ringing
  - funeral ringing
- silent mode does not stop:
  - clock hands
  - rotating plate
- `UPS mode` lights the silent indicator and shows `NEMA STRUJE!` on the LCD while outputs stay blocked

### BAT behavior

- `BAT from/to` defines the time range in which regular striking is allowed
- outside that range, regular striking is blocked
- bells, sun automation, and the rotating plate continue to operate outside the BAT striking window
- example:
  - `BAT from 6`
  - `BAT to 22`
  - striking is allowed from `06:00` to `22:00`
- for an overnight range such as `22-6`, `22:00` is still allowed to strike and the quiet period begins after that moment

## 🖥️ LCD display

- the first row no longer uses:
  - activity `*`
  - `R/N` markers
  - separate WiFi `W` marker
- fields `11-13` of the first row show:
  - `NTP`
  - `MAN`
  - `ERR`
  - `---`
- fields `15-16` show the temperature of the `DS3231 RTC` module
- the time colon blinks at the `1/2 SQW` rhythm only when:
  - the tower clock is actively doing something
  - or WiFi is disconnected
- if WiFi is connected and the mechanism is idle, the colon remains steadily lit
- until time is confirmed, the display stays in safe `ERR` mode and does not show unverified RTC time as valid

## ⚠️ Error behavior

- WiFi loss: the tower clock continues from the RTC
- ESP32 fault: no impact on the basic operation of hands, plate, bells, or hammers
- Mega reset: state recovery from saved persistent state
- power loss: continuation from the last valid state
- loss of RTC SQW pulse: hands and rotating plate stop their active phase safely
- loss of RTC/I2C connection: an output fail-safe blocks bells, hammers, hands, and rotating plate until the DS3231 recovers
- repeated watchdog resets without a power-loss marker: `SUSTAV ZAKLJUCAN / PREVISE RESETA`
- repeated invalid RTC readings: `RTC OGRANICEN RAD / CEKAM OPORAVAK`
- EEPROM fault: latched fault plus paused periodic EEPROM writes and health checks
- the bell indicator flashes during inertia
- the celebration indicator flashes during thermal pause while celebration remains logically active
- if UPS mode is active and mains power is gone, the main LCD shows `NEMA STRUJE!`

## 🔧 Hardware

- `Arduino Mega 2560`
- `ESP32`
- `DS3231 RTC`
- `24C32 EEPROM` or `FM24W256 FRAM`
- `16x2 LCD` via `I2C`
- two `Koncar 0.55 kW / 380 V` three-phase motors, one for each bell
- microswitches on the rear shaft of each bell motor for phase switching and bell-operation transition
- two `310 VDC` electromagnetic hammers, one per bell, with a pulse of approximately `0.01 s`
- clock-hand drive motor with gear mechanism driven by `EVEN/ODD` pulses lasting approximately `6 s`
- electrical cabinet with contactors for bell phase reversal, hammer contactors, fuses, and protective equipment
- `00-99` thumbwheel for funeral ringing duration
- silent-mode toggle switch and silent-mode indicator lamp
- LED indicators for:
  - `ZVONO 1`
  - `ZVONO 2`
  - `SLAVLJENJE`
  - `MRTVACKO`
- relay outputs for:
  - hands
  - rotating plate
  - bells
  - hammers
- 6 local service buttons:
  - `UP`
  - `DOWN`
  - `LEFT`
  - `RIGHT`
  - `YES`
  - `NO`

## 📚 Additional README files

- [Mega firmware README](main/README.md)
- [ESP firmware README](esp_firmware/README.md)
- [ESP web API routes](docs/esp_web_api_toranjskog_sata.md)
- [Technical firmware documentation](docs/tehnicka_dokumentacija_firmware_sustava.md)

## 🛠️ Development notes

- the main loop must remain non-blocking
- `Mega 2560` must remain the authority for tower clock state
- an `ESP32` restart or fault must not affect basic clock operation
- I2C access for the LCD, RTC, and external memory must continue to use shared bus preparation with timeout
- changes affecting hands, rotating plate, bells, time synchronization, or recovery should always be checked against the existing modules in `main/`
