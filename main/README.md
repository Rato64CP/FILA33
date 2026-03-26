🕰️ Toranjski sat – automatika (RTC + NTP + DCF)

Sustav za upravljanje toranjskim satom, zvonima i okretnom pločom
Arduino Mega + ESP8266 + DS3231 + 24C32 EEPROM

🔧 Hardware
Board
Arduino Mega 2560 (AVR)
Glavne komponente
RTC: DS3231
EEPROM: 24C32 (I2C)
LCD: 16x2 (I2C – 0x27)
WiFi: ESP8266 (Serial komunikacija)
DCF77 prijemnik
Releji: preko ULN2803 + optokapleri
Tipke: 6 kom (menu navigacija)
Pinout (sažeto)
Releji
Funkcija	Pin
Parne kazaljke	D22
Neparne kazaljke	D23
Parne ploče	D24
Neparne ploče	D25
Zvono 1	D26
Zvono 2	D27
Čekić muški	D28
Čekić ženski	D29
Ulazi
Funkcija	Pin
DCF signal	D35
Ploča ulazi (1–5)	D30–D34
Tipke
Tipka	Pin
UP	D36
DOWN	D37
LEFT	D38
RIGHT	D39
SELECT	D40
BACK	D41

⏱️ Upravljanje vremenom
Prioritet izvora vremena
RTC (DS3231) – primarni (offline rad)
NTP (ESP8266) – sinkronizacija
DCF77 – dodatni fallback
Sinkronizacija kazaljki
K-minuta sustav (0–719)
Pohrana u EEPROM (wear leveling)
Automatska korekcija:
Normalna (<10 min razlike)
Agresivna (≥10 min)
Dinamički recalculation nakon svakog impulsa

🔔 Zvona i otkucavanje
Modovi rada
Normal
Both (oba zvona)
Celebration
Funeral
Pravila
Čekići blokirani tijekom inercije zvona
Pola sata → žensko zvono
Puni sat → broj otkucaja

⚙️ Okretna ploča
64 pozicije (0–63)
1 pozicija = 15 minuta
Radno vrijeme: 04:59 – 20:44
Noćni režim: pozicija 63
Logika
Svaki korak:
PARNI → 6s
NEPARNI → 6s
EEPROM zapis nakon svake faze
Recovery ako ostane “P” stanje

📡 MQTT (ESP8266)
Publish
vrijeme
izvor vremena (RTC/NTP/DCF)
status zvona
status čekića
mod rada
Subscribe
uključi zvono
slavljenje / mrtvačko
uključi/isključi otkucavanje
korekcija kazaljki

🖥️ LCD prikaz
Linija 1
HH:MM:SS SRC S W
SRC = RTC / NTP / DCF
S = status toranjskog sata
N = normalno
R = korekcija kazaljki ili okretne ploce
E = greska / recovery
W = WiFi status
Linija 2
normalno: datum
aktivnost: poruke (zvono, korekcija, sync)
greške: blink

🔄 Boot logika
Učitavanje EEPROM
RTC inicijalizacija
I2C scan
Pokretanje modula:
kazaljke
ploča
zvona
Power recovery
Watchdog start
ESP / WiFi / MQTT

🛡️ Pouzdanost
Watchdog (8s)
Wear leveling EEPROM
Graceful shutdown
Automatski recovery nakon:
nestanka struje
watchdog reseta
otpornost na šum (50m kabeli)

📚 Biblioteke
LiquidCrystal_I2C
FastLED

⚠️ Napomene
Sustav radi potpuno offline (RTC)
ESP8266 služi samo za:
NTP
MQTT
Parni/neparni impulsi moraju se strogo poštovati
EEPROM se ažurira nakon svakog impulsa

📈 Verzija
v024 – 23.03.2026
Stabilna verzija – svi moduli integrirani, bez compile grešaka

🔧 TODO (preporuka za dalje)
ograničiti MQTT reconnect spam
popraviti SSID ispis iz EEPROM-a
osigurati single init za sve module
dodatna validacija NTP vremena
