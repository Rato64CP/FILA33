# 🔧 ZVONKO v. 1.0 - ESP firmware

Croatian version: [README.hr.md](README.hr.md)

This folder contains the firmware for the external `ESP32` module that acts as the network layer of the tower clock. The `ESP` communicates with the `Arduino Mega 2560` through `main/esp_serial.cpp`, but it does not take ownership of the `RTC`, bells, hammers, hands, or rotating plate.

## ✨ Role of the ESP module

- connects the tower clock to the local `WiFi` network
- maintains its own UDP `NTP` layer
- sends `NTP` time to the Mega only after `NTPREQ:SYNC`
- tracks internal `UTC` time in milliseconds since the last confirmed synchronization
- uses `NTP` seconds, the fractional part, and `RTT/2` correction for a more precise network timestamp
- confirms the first `NTP` sample after restart or WiFi reconnect with a second sample before the first synchronization of the tower clock
- provides the compact web dashboard and the service API towards the Mega
- accepts WiFi setup through a temporary `AP`
- remains only the network layer and does not bypass decisions made by `main/time_glob.cpp`, `main/prekidac_tisine.cpp`, and `main/power_recovery.cpp`

## 🧩 Firmware structure

- `esp_firmware.ino` contains shared global structures, configuration, and forward declarations
- `esp_boot_wifi.ino` handles the boot flow, `setup()/loop()`, WiFi connection, and the setup `AP`
- `esp_serial_mega.ino` handles the serial protocol between the `ESP32` bridge and `main/esp_serial.cpp`
- `esp_time_ntp.ino` contains `NTP` and calendar helper functions for tower-clock local time
- `esp_web.ino` contains `Basic Auth`, the `JSON` API, `OTA`, and all dashboard/settings/feast web pages

## 🌐 Active web routes

- `/` - main dashboard page
- `/settings` - separate page for safe web settings: `Sustav`, `Stapici`, `BAT`, and `Sunce`
- `/blagdani` - separate page for regular Masses and predefined fixed and movable feasts, with editing of enable flags and Mass times `HH:MM`
- `/setup` - setup page for entering a new `WiFi` network while the temporary `AP` is active
- `/update` - hidden `OTA` page for uploading new `ESP` firmware
- `/api/status` - `JSON` status of the WiFi connection and real runtime state used to color the dashboard buttons
- `/api/pokojnik` - starts the one-shot `POKOJNIK` sequence
- `/api/pokojnica` - starts the one-shot `POKOJNICA` sequence
- `/api/settings/system` - `JSON` fetch and save for the `Sustav` group
- `/api/settings/stapici` - `JSON` fetch and save for the `Stapici` group
- `/api/settings/bat` - `JSON` fetch and save for the `BAT` group
- `/api/settings/sunce` - `JSON` fetch and save for the `Sunce` group
- `/api/settings/blagdani` - `JSON` fetch and save for the `Blagdani` group

## 🧭 Dashboard

- the top 2x2 block uses `MUSKO`, `ZENSKO`, `SLAVI`, and `BRECA`
- below the top block there are two one-shot buttons: `POKOJNIK` and `POKOJNICA`
- `POKOJNIK` sends the sequence `male bell for 2 minutes -> wait for inertia -> funeral ringing for 10 minutes`
- `POKOJNICA` sends the sequence `female bell for 2 minutes -> wait for inertia -> funeral ringing for 10 minutes`
- the lower block uses `JUTRO`, `PODNE`, and `VECER`
- below the sun buttons there is a red `TIHI MOD` toggle
- the bottom of the dashboard includes a service link to `POSTAVKE`
- web `TIHI MOD` enters the same unified silent mode handled by `main/prekidac_tisine.cpp`
- if the physical silent-mode switch changes state, the dashboard shows the real state from the Mega after the next `STATUS:` refresh
- when the dashboard opens, it immediately performs one forced `STATUS?` fetch so that button colors match the real tower-clock state

## ⚙️ Web settings

- `/settings` intentionally edits only safe settings that do not move the hands, do not touch the rotating plate, and do not change time
- supported groups are:
  - `Sustav`
  - `Stapici`
  - `BAT`
  - `Sunce`
  - `Blagdani`
- `Sustav` includes `LCD light`, `logging`, `RS485`, `UPS mode`, `bell brake`, `INR1`, `INR2`, and `hammer pulse`
- `Stapici` includes `TR`, `TN`, `TS`, and celebration delay `S`
- `BAT` includes hours `od/do` and modes `OTK`, `S`, and `M`
- web `BAT od/do` means the range in which regular striking is allowed; outside that range the `Mega` blocks only striking through `main/postavke.cpp` and `main/otkucavanje.cpp`
- `Sunce` includes `Jutro`, `Podne`, `Vecer`, bell selection, morning/evening offsets, and `Nocna rasvjeta`
- `Blagdani` includes the daily and Sunday Mass plus a predefined list of `15` fixed and `7` movable feasts; each feast only edits its enable flag and `HH:MM` Mass time
- the daily Mass triggers only the male bell `30 min` before the configured Mass time `HH:MM`, using the weekday ringing duration from `main/postavke.cpp`
- the Sunday Mass and feast-day Mass trigger Sunday-style ringing on both bells `2 h` and `1 h` before the configured Mass time `HH:MM`, without extra celebration ringing
- an empty time field on `/blagdani` means that the corresponding regular Mass or feast is disabled regardless of the checkbox state
- all Mass-related ringing starts at second `:25`, synchronized with pin reading in `main/okretna_ploca.cpp`
- the `Mega` remains the only authority for validation and saving through `main/postavke.cpp`
- the `ESP32` only renders the form, sends the full payload, and after confirmation reads the real state back from the `Mega`

## 🔐 Authentication

- the dashboard `/` and all `/api/...` routes use `Basic Auth`
- the `/update` route uses the same `Basic Auth`
- the password is loaded from `EEPROM` or falls back to the default firmware value
- `/setup` does not require `Basic Auth` while the setup `AP` is active

## 📡 OTA updates

- `OTA` is implemented as the hidden web route `/update`
- it uploads the compiled `.bin` file for the `ESP32`
- during firmware writing, the `ESP` temporarily pauses regular `NTP` and other web/serial tasks that are not needed for the upload
- after a successful update, the `ESP` schedules a short restart and returns to normal operation
- the dashboard does not expose a visible link to `/update`; the route is opened manually by typing the address

## 🧵 Serial protocol towards the Mega

### `Mega -> ESP`

- `WIFI:<ssid>|<lozinka>|<dhcp>|<ip>|<maska>|<gateway>` sends tower-clock network settings
- `WIFIEN:0` and `WIFIEN:1` disable or enable the `WiFi` radio
- `WIFISTATUS?` asks for the current `WiFi` state of the network bridge
- `NTPCFG:<server>` sets the `NTP` server
- `NTPREQ:SYNC` asks for the current `NTP` time at a moment chosen by the `Mega`
- `SETREQ:SUSTAV`, `SETREQ:STAPICI`, `SETREQ:BAT`, `SETREQ:SUNCE`, `SETREQ:MISE`, `SETREQ:BLAGDANI_NEP`, and `SETREQ:BLAGDANI_POM` request the current state of a single web settings group from `main/postavke.*`

### `ESP -> Mega`

- `CFGREQ` asks for initial configuration after boot
- `WIFI:CONNECTED`, `WIFI:DISCONNECTED`, `WIFI:LOCAL_IP:...`, and `WIFI:MAC:...` report connection status
- `NTP:YYYY-MM-DDTHH:MM:SS.mmm;DST=0/1` sends tower-clock local time with milliseconds
- `SETUPWIFI:<ssid>|<lozinka>` forwards a new network entered through the setup `AP`
- `CMD:<naredba>` forwards service commands to `main/esp_serial.cpp`
- `STATUS:` returns the combined status used by the dashboard for button colors
- `SET:SUSTAV|...`, `SET:STAPICI|...`, `SET:BAT|...`, `SET:SUNCE|...`, `SET:MISE|...`, `SET:BLAGDANI_NEP|...`, and `SET:BLAGDANI_POM|...` return the current state of the individual groups
- `SETCFG:SUSTAV|...`, `SETCFG:STAPICI|...`, `SETCFG:BAT|...`, `SETCFG:SUNCE|...`, `SETCFG:MISE|...`, `SETCFG:BLAGDANI_NEP|...`, and `SETCFG:BLAGDANI_POM|...` send a new full payload for the corresponding group to the `Mega`
- `ACK:*`, `ERR:*`, and `NTPLOG:*` lines are used for confirmations and diagnostics

## ⏱️ UDP NTP flow

- the `ESP` does not use `NTPClient`; it uses its own UDP `NTP` fetch flow in `esp_time_ntp.ino`
- stale UDP packets are discarded before a new request so late replies do not corrupt new `RTT` measurements
- only valid `NTP` replies are accepted, including basic checks of `mode`, `stratum`, and time data
- the first `NTP` sample after restart or WiFi reconnect is not sent to the Mega immediately
- the first sample is stored, and the `ESP` immediately requests a second sample for stabilization
- only the confirmed second sample becomes the authority for the first `NTP` synchronization of the tower clock
- the `ESP` calculates a more precise `UTC ms` value from the reply and sends the millisecond part inside the `NTP:` record
- the `Mega` remains the only owner of RTC writes and of alignment to the `RTC SQW` second boundary

## 🛡️ Behavior under Mega safety blocks

- the `ESP` can keep WiFi and NTP alive while the `Mega` is in limited operation
- the `ESP` does not unlock `safe mode` and does not acknowledge latched faults
- when the `Mega` blocks automation because of `RTC` or `EEPROM` problems, the `ESP` remains only a helper source of networking and time, without authority over tower-clock mechanics
- after a WiFi watchdog reset, the `Mega` receives `NTP:` only when the `ESP` has again confirmed fresh time

## 📶 WiFi setup

- the setup `AP` uses the `SSID` `ZVONKO_setup`
- the setup `AP` password is `zvonko10`
- the setup `AP` can also be started by holding `LIJEVO + DESNO` on the Mega keypad, but only from the main clock screen
- on the `ESP32`, the default setup button is on `GPIO27` and the status LED is on `GPIO26`
- the serial connection to the `Mega` uses `GPIO16` as `RX` and `GPIO17` as `TX`
- while the setup `AP` is active, both `http://192.168.4.1/` and `http://192.168.4.1/setup` open the setup page
- after saving the network, the `ESP` forwards the new configuration to the Mega through `SETUPWIFI:`

## 🛠️ Upload and verification

1. Open `esp_firmware.ino` in `Arduino IDE` or `PlatformIO`.
2. Select the correct board, for example `ESP32 Dev Module`.
3. For the serial bridge, connect `Mega TX3 (pin 14)` to `ESP RX GPIO16` through a voltage divider and `ESP TX GPIO17` to `Mega RX3 (pin 15)`.
4. Verify that both boards share the same `GND`.

### OTA upload over the network

1. Compile the firmware in the same development environment and locate the output `.bin` file.
2. Open `http://<ip-esp>/update`.
3. Log in with the same `Basic Auth` credentials used by the tower-clock dashboard.
4. Select the new `.bin` file and wait for the success message.
5. Wait for the automatic restart of the `ESP` module before reopening the dashboard.

## ✅ What to verify after boot

- the serial monitor should show `CFGREQ`, `WIFI:CONNECTED`, and `WIFI:LOCAL_IP:...` when the network is available
- the first `NTP` after restart should show first-sample storage and second-sample confirmation inside `NTPLOG:`
- the `ESP` should not send `NTP:` on its own after connecting; `NTP` reaches the Mega only after `NTPREQ:SYNC`
- `http://<ip-esp>/api/status` should return `JSON` with WiFi state, main buttons, sun buttons, and `TIHI MOD`
- `http://<ip-esp>/settings` should open the page with the groups `Sustav`, `Stapici`, `BAT`, and `Sunce`, and pull the real state from the `Mega` on entry
- `http://<ip-esp>/blagdani` should open the separate page for regular and feast-day Masses and pull the real state from the `Mega` on entry
- `http://<ip-esp>/update` should open the `OTA` upload page and restart the `ESP` module after a successful upload
