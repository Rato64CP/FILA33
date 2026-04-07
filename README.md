🕰️ Automatika toranjskog sata

Firmware i upravljačka logika za toranjski sat temeljena na distribuiranoj arhitekturi:

Arduino Mega 2560 – real-time upravljanje (kazaljke, okretna ploča, zvona, čekići, lokalne postavke, recovery)
ESP8266 – mrežni sloj (WiFi, NTP, konfiguracija, servisni API)

Sustav je dizajniran kao fail-safe i offline-first, gdje osnovne funkcije sata rade potpuno neovisno o mreži.

✨ Funkcionalnosti sustava

🕒 Vođenje vremena
DS3231 RTC (primarni izvor)
NTP sinkronizacija (na zahtjev)
DCF77 prijemnik (opcijski)
upravljanje prioritetima izvora vremena

⚙️ Upravljanje mehanikom
kazaljke sata (precizna korekcija i sinkronizacija)
okretna ploča (parne / neparne faze)
zvona:
redovno zvonjenje
slavljenje
mrtvačko
čekići:
satno i polusatno otkucavanje

💾 Trajna pohrana
24C32 EEPROM
wear leveling (kružno spremanje)
spremanje kritičnog stanja sustava

🛡️ Pouzdanost
watchdog zaštita
power-loss recovery
automatski povrat u valjano stanje

🧭 Arhitektura sustava
            +------------------+
            |    ESP8266       |
            |------------------|
            | WiFi / NTP / API |
            +--------+---------+
                     |
                 Serial3
                     |
+--------------------+--------------------+
|           Arduino Mega 2560             |
|----------------------------------------|
| logika sata (autoritet)                |
| kazaljke / ploča / zvona / čekići      |
| EEPROM / recovery / watchdog           |
+--------------------+--------------------+
                     |
     +---------------+----------------+
     |        periferni uređaji       |
     | RTC / LCD / releji / tipke     |
     +--------------------------------+

👉 Mega je jedini autoritet za rad i stanje sustava

🔐 Pravila komunikacije Mega ↔ ESP
Mega inicira sve operacije (master)
ESP nikada ne šalje podatke samoinicijativno
NTP sinkronizacija se izvršava isključivo na zahtjev Mege
komunikacija mora biti:
neblokirajuća
otporna na greške

🔌 Serijska komunikacija

Mega koristi Serial3 za komunikaciju s ESP modulom.

Podržane naredbe:
WIFI:
WIFIEN:
WIFISTATUS?
NTPCFG:
NTPREQ:SYNC
NTP:
CMD:
STATUS?
Napomene:
ESP odgovara samo na zahtjev (nema automatskih poruka)

stare komande:

WEBCFG?
WEBCFGSET:

vraćaju:
ERR:WEBCFGDISABLED

📁 Struktura projekta

main/ (Arduino Mega firmware)
main.ino – inicijalizacija i glavna petlja
time_glob.* – RTC, NTP, DCF i prioriteti
esp_serial.* – komunikacija s ESP-om
kazaljke_sata.* – logika kazaljki
okretna_ploca.* – upravljanje pločom
zvonjenje.* – upravljanje zvonima
otkucavanje.* – čekići i otkucavanje
menu_system.*, tipke.* – LCD izbornik
postavke.* – konfiguracija
unified_motion_state.* – zajedničko stanje
power_recovery.*, watchdog.* – oporavak
wear_leveling.*, i2c_eeprom.* – EEPROM
esp_firmware/ (ESP8266 firmware)
WiFi povezivanje
NTP dohvat vremena
web sučelje i API

📶 WiFi setup

ESP može pokrenuti setup mrežu:

SSID: FILA33_setup
lozinka: toranj33
Aktivacija:
dugi pritisak tipke na GPIO14 (D5) → GND
Status LED:
GPIO12 (D6)
Web pristup:
http://192.168.4.1/
http://192.168.4.1/setup

Nakon konfiguracije:

ESP šalje WiFi podatke Megi
sustav ostaje usklađen

💾 EEPROM i recovery

24C32 čuva:
postavke
stanje kazaljki i ploče
wear leveling smanjuje trošenje

⚠️ Važno

Promjene u EEPROM strukturi zahtijevaju:

kompatibilnost sa starim zapisima
ILI
migracijsku logiku
Kritični moduli:
eeprom_konstante.h
unified_motion_state.*
power_recovery.*

⚠️ Ponašanje u slučaju grešaka

Situacija	Ponašanje sustava
gubitak WiFi	rad bez prekida (RTC)
kvar ESP-a	nema utjecaja na rad sata
reset Mege	recovery iz EEPROM-a
nestanak napajanja	nastavak iz zadnjeg stanja

🔧 Hardver

Arduino Mega 2560
ESP8266 modul
DS3231 RTC
24C32 EEPROM
LCD 16x2 (I2C)
DCF77 prijemnik
relejni izlazi (preko drivera – npr. ULN2803/ULN2804)
tipke za upravljanje

🛡️ Napomene za razvoj

glavna petlja mora biti neblokirajuća
Mega mora ostati autoritet sustava
kvar ESP-a ne smije utjecati na osnovni rad
recovery logika mora ostati konzistentna

pri izmjenama:

EEPROM strukture
logike gibanja

obavezno provjeriti kompatibilnost