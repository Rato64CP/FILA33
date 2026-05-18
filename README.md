🕰️ ZVONKO v. 1.0

ZVONKO v. 1.0 is the firmware and control logic for a tower clock system based on a division of tasks between an Arduino Mega 2560 and an ESP network layer.

✨ What the system does
keeps time using a DS3231 RTC and controlled NTP requests
controls the clock hands with correction and synchronization logic
controls the rotating program plate through two-phase steps and pin reading
manages the bells, hammers, celebration ringing, and funeral ringing
supports separate inertia settings INR1 and INR2 for two different bells
supports the K:0/1 option for operation with or without a bell brake
introduces thermal protection for celebration ringing after 3 minutes of operation by inserting a 3-second pause every 30 seconds
supports festive celebration ringing and a special funeral ringing schedule for All Saints’ Day / All Souls’ Day
stores settings and critical system state in an external 24C32 EEPROM or FM24W256 FRAM
restores the system to a valid state after a watchdog or power-loss reset
locks the mechanism in safe mode if too many watchdog resets occur within a short period
monitors the health of the RTC and EEPROM subsystems and switches to limited operation when a fault becomes repeatable
keeps an EEPROM fault latched until manually acknowledged by the operator
supports a unified silent mode for the Easter silence, a manual toggle switch, and a virtual web toggle via the ESP dashboard
supports UPS mode, which keeps the tower clock logic alive without mains power while blocking outputs to the hands, bells, and hammers

🧭 Architecture
The Arduino Mega 2560 controls the clock hands, rotating plate, bells, hammers, local settings, and recovery logic.
The ESP network layer provides WiFi, NTP, wireless service API, OTA updates, and the service dashboard.
The Mega 2560 is the single source of truth for the tower clock state.
Basic clock operation must remain possible even without a network connection.

🔐 Mega <-> ESP rules
The Mega initiates critical operations related to tower clock operation.
The ESP does not make mechanical decisions regarding the hands, plate, bells, or hammers.
NTP synchronization is performed only when the Mega determines that the moment is safe.
A fault or restart of the external network layer must not stop the basic clock operation.

🔄 Serial communication
The Mega uses Serial3 for the external ESP32 as the only active network bridge.
Active commands are:
WIFI:, WIFIEN:, WIFISTATUS?, NTPCFG:, NTPREQ:SYNC, NTP:, CMD:, STATUS?, SETREQ:*, and SETCFG:* for the web groups SUSTAV, STAPICI, BAT, SUNCE, MISE, BLAGDANI_NEP, and BLAGDANI_POM.
The Mega 2560 selects a safe moment for NTPREQ:SYNC only when both the hands and the rotating plate are idle.
The ESP no longer sends NTP: automatically after connection or hourly, but only responds to a request from the Mega.
The first NTP after ESP restart or WiFi reconnection is confirmed by a second sample before the first synchronization of the tower clock.
The ESP32 now has separate /settings and /blagdani pages, while the Mega remains the only authority for validating and saving all web settings.

🧩 Project structure
main/ – main tower clock firmware for the Arduino Mega 2560
esp_firmware/ – auxiliary firmware for the external ESP32
main/main.ino – initialization and main loop
main/time_glob.* – RTC, NTP, DST, and time source priorities
main/mise_automatika.* – regular daily/Sunday Masses and special feast-day Masses
main/esp_serial.* – serial communication with the ESP module
main/kazaljke_sata.* – clock hand logic and correction
main/okretna_ploca.* – position, phase, and pin-reading control
main/zvonjenje.* – bell and inertia control
main/otkucavanje.* – hammers, hourly and half-hour striking
main/slavljenje_mrtvacko.* – special hammer modes and funeral thumbwheel timer
main/pogrebne_skripte.* – one-time POKOJNIK and POKOJNICA sequences
main/prekidac_tisine.* – unified silent mode and silent-mode indicator
main/ups_nadzor.* – mains monitoring and UPS mode
main/menu_system.*, main/tipke.*, main/lcd_display.* – local LCD menu and input
main/postavke.* – reading, validating, and saving settings
main/unified_motion_state.* – shared state of the hands and plate
main/power_recovery.* and main/watchdog.* – recovery and 24/7 reliability
main/wear_leveling.* and main/i2c_eeprom.* – persistent storage in external 24C32 EEPROM or FM24W256 FRAM

📶 WiFi setup
The ESP32 can start a temporary setup network named ZVONKO_setup.
The setup network password is zvonko10.
The setup AP is activated by holding the button on GPIO27 to GND.
The serial connection between ESP32 and Mega uses separate pins: GPIO16 as RX and GPIO17 as TX.
The setup AP can also be activated by pressing and holding the left + right buttons simultaneously on the keypad, but only from the main clock display.
The status LED uses GPIO26.
The setup page is available at http://192.168.4.1/ and http://192.168.4.1/setup.
After saving a new network, the ESP32 forwards the WiFi data to the Mega so that the entire tower clock remains synchronized.
The ESP service dashboard now uses the main buttons MUSKO, ZENSKO, SLAVI, BRECA, the one-time funeral buttons POKOJNIK and POKOJNICA, the sun-related buttons JUTRO, PODNE, VECER, and the red TIHI MOD toggle.
POKOJNIK starts the male bell for 2 minutes, waits for the inertia period to finish, and then starts funeral ringing for 10 minutes.
POKOJNICA starts the female bell for 2 minutes, waits for the inertia period to finish, and then starts funeral ringing for 10 minutes.
Through /settings and /blagdani, the ESP32 can now safely edit system settings, bell pin settings, BAT, sun ringing, regular Masses, and predefined feast-day Masses without modifying the time, date, clock hands, or rotating plate.
A daily Mass starts only the male bell 30 minutes before the entered Mass time HH:MM.
Sunday and feast-day Masses start Sunday-style ringing with both bells 2 hours and 1 hour before the entered Mass time HH:MM, without additional celebration ringing.
An empty time field on the web interface means that the corresponding regular Mass or feast is disabled, regardless of the checkbox state.
All Mass bell ringing starts at the 25th second of the minute, synchronized with pin reading in main/okretna_ploca.cpp.

💾 EEPROM and recovery
The external 24C32 EEPROM or FM24W256 FRAM stores settings, UnifiedMotionState, DST status, and critical backup data.
Although the FM24W256 has a larger physical capacity, the tower clock firmware intentionally keeps the existing compatible layout within the first 4096 bytes, so both memory variants remain compatible.
UnifiedMotionState uses 24 rotating slots for the clock hands and rotating plate.
Each UnifiedMotionState slot has a checksum, and invalid or partially written records are skipped.
The last time synchronization record now has its own checksum while remaining compatible with the old format.
power_recovery.* restores the hands and plate to a consistent state after restart.
Watchdog resets are tracked through a persistent counter, and after several consecutive watchdog resets, safe mode is activated.
Safe mode blocks the hands, plate, bells, and hammers until the operator holds ENT / SELECT for 5 seconds.
EEPROM health is checked both at boot and periodically every 6 hours.
An EEPROM fault remains latched in memory until manually acknowledged by the operator.
When EEPROM is in degraded mode, periodic backups and auxiliary records such as DST and last synchronization are paused.
The I2C bus uses a shared Wire timeout and bus reset for the LCD, DS3231, external FRAM storage, and service scanning.
EEPROM/I2C retry and polling loops refresh the watchdog when active so that auxiliary writes do not unnecessarily push the tower clock toward a watchdog reset.

When making changes that affect the EEPROM layout or recovery logic, always check:

main/eeprom_konstante.h
main/unified_motion_state.*
main/power_recovery.*

🔕 Silent mode and BAT
The unified silent mode can be activated by the Easter silence, a manual toggle switch, or the virtual web toggle on the ESP dashboard.
The silent-mode indicator lights up only when the final effective silent mode is actually active.
Silent mode blocks bells, hammers, celebration ringing, and funeral ringing, but does not stop the clock hands or rotating plate.
UPS mode turns on the silent-mode indicator and displays NEMA STRUJE! on the LCD while the tower clock mechanism runs only from auxiliary power.
BAT from/to defines the time range in which regular striking is allowed.
Outside the BAT from/to range, regular striking is blocked, while bells, sun automation, and the rotating plate continue to operate.
Example: BAT from 6 and BAT to 22 means that striking operates from 06:00 to 22:00 and is disabled outside that range.
For an overnight range such as 22–6, 22:00 is still allowed to strike, after which the quieter night period begins.
Sun automation and plate pins continue working during the BAT range.
The morning sun bell can enable striking earlier, before the end of the BAT range.
Feast-day celebration ringing waits for the real end of bells, striking, and inertia before starting.

🖥️ LCD display
The first row of the main LCD display no longer uses the activity asterisk * or the R/N markers.
The time source marker NTP, MAN, ERR, or --- is now shown in fields 11–13 of the first row.
Fields 15–16 of the first row show the temperature of the DS3231 RTC module.
WiFi no longer has a separate W indicator on the LCD.
The colon in the time display blinks at the 1/2 SQW rhythm only when the tower clock is doing something or when WiFi is not connected.
If WiFi is connected and the mechanism is idle, the colon remains steadily lit.
Until time is confirmed, the main display remains in safe ERR mode and does not show unverified RTC time as if it were valid.

⚠️ Error behavior
Loss of WiFi connection: the tower clock continues operating from the RTC.
ESP32 fault: no impact on the basic operation of the hands, plate, bells, and hammers.
Mega 2560 reset: recovery from saved state.
Power loss: continuation from the last valid state.
Loss of RTC SQW pulse: the hands and plate have a millis() fallback for safely turning off the active phase.
Loss of RTC/I2C connection: output fail-safe is activated, and the relays for the bells, hammers, hands, and plate remain blocked until the DS3231 recovers.
Repeated watchdog resets without a power-loss marker: activates SUSTAV ZAKLJUCAN / PREVISE RESETA.
Repeated invalid RTC readings: activates RTC OGRANICEN RAD / CEKAM OPORAVAK, and time-based automation is temporarily blocked.
EEPROM fault: activates a latched fault, and periodic EEPROM writes and health checks are stopped until acknowledgement.
The bell indicator flashes during inertia so the operator knows that the hammers should not yet be used.
The celebration ringing indicator flashes during the thermal pause, although celebration ringing remains active.
If UPS mode is active and mains power is lost, the main LCD shows NEMA STRUJE! instead of the date.

🔧 Hardware
Arduino Mega 2560
ESP32
DS3231 RTC
24C32 EEPROM or FM24W256 FRAM
16x2 LCD via I2C
two Končar 0.55 kW / 380 V three-phase electric motors, one for each tower bell
microswitches on the rear shaft of each bell motor for phase switching and bell operation transition
two 310 VDC electromagnetic hammers, one for each bell, with a pulse of approximately 0.01 s
tower clock drive motor for the hands, with a gear mechanism operating through EVEN/ODD pulses lasting approximately 6 s
electrical cabinet with contactors for bell phase reversal, hammer contactors, fuses, and other protective equipment
00–99 thumbwheel for funeral ringing duration
silent-mode toggle switch and silent-mode indicator lamp
LED indicators for BELL 1, BELL 2, CELEBRATION, and FUNERAL
relay outputs for the hands, plate, bells, and hammers
6 direct buttons for the local menu and service functions: UP, DOWN, LEFT, RIGHT, YES, NO
the local service layer uses ENT / SELECT also for unlocking safe mode and acknowledging latched faults
the local System menu now edits UPS mode, K:0/1, INR1, and INR2

📚 Additional README files
- [README za Mega firmware](main/README.md)
- [README za ESP firmware](esp_firmware/README.md)
- [Popis ESP web API ruta toranjskog sata](docs/esp_web_api_toranjskog_sata.md)
- [Tehnicka dokumentacija firmware sustava](docs/tehnicka_dokumentacija_firmware_sustava.md)

🛠️ Development notes
The main loop must remain non-blocking.
The Mega 2560 must remain the authority for the tower clock state.
A fault or restart of the ESP32 must not affect the basic clock operation.
I2C access for the LCD, RTC, and external FRAM should remain based on the shared bus preparation with timeout.
Changes affecting the clock hands, plate, bells, time synchronization, or recovery must be checked against the existing modules in main/.