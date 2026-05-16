# 🔧 ZVONKO v. 1.0 - ESP firmware

Ova podmapa sadrzi firmware za vanjski `ESP32` modul koji radi kao mrezni sloj toranjskog sata. `ESP` serijski suraduje s `Arduino Megom 2560` kroz [main/esp_serial.cpp](../main/esp_serial.cpp), ali ne preuzima vlasnistvo nad `RTC`-om, zvonima, cekicima, kazaljkama ni okretnom plocom.

## ✨ Uloga ESP modula

- spaja toranjski sat na lokalnu `WiFi` mrezu
- odrzava vlastiti UDP `NTP` sloj
- salje `NTP` vrijeme Megi samo nakon `NTPREQ:SYNC`
- interno prati `UTC` u milisekundama od zadnje potvrdene sinkronizacije
- koristi `NTP` sekunde, `fraction` dio i `RTT/2` korekciju za precizniji mrezni timestamp
- potvrduje prvi `NTP` uzorak nakon restarta ili `WiFi` reconnecta drugim uzorkom prije prve sinkronizacije toranjskog sata
- pruza svedeni web dashboard i servisni API prema Megi
- prihvaca setup `WiFi` kroz privremeni `AP`
- ostaje pomocni mrezni sloj i ne zaobilazi odluke koje donose [main/time_glob.cpp](../main/time_glob.cpp), [main/prekidac_tisine.cpp](../main/prekidac_tisine.cpp) i [main/power_recovery.cpp](../main/power_recovery.cpp)

## 🌐 Aktivne web rute

- `/` - jedina glavna stranica dashboarda
- `/settings` - zasebna stranica za sigurne web postavke `Sustav`, `Stapici`, `BAT` i `Sunce`
- `/blagdani` - zasebna stranica za redovite mise te unaprijed zadane nepomicne i pomicne blagdane s uredjivanjem ukljucenja i vremena mise `HH:MM`; prazno vrijeme znaci da je misa ili blagdan iskljucen
- `/setup` - setup stranica za unos nove `WiFi` mreze dok je aktivan privremeni `AP`
- `/update` - skrivena `OTA` stranica za upload novog `ESP` firmwarea
- `/api/status` - `JSON` status `WiFi` veze i stvarnog stanja koje dashboard boja prikazuje
- `/api/settings/system` - `JSON` dohvat i spremanje skupine `Sustav`
- `/api/settings/stapici` - `JSON` dohvat i spremanje skupine `Stapici`
- `/api/settings/bat` - `JSON` dohvat i spremanje skupine `BAT`
- `/api/settings/sunce` - `JSON` dohvat i spremanje skupine `Sunce`
- `/api/settings/blagdani` - `JSON` dohvat i spremanje skupine `Blagdani`
- `/api/...` - servisne naredbe prema Megi

## 🧭 Dashboard

- gornji 2x2 blok koristi tipke `MUSKO`, `ZENSKO`, `SLAVI`, `BRECA`
- gornje tipke su naglasene i tamnije plave kad su ukljucene
- donji blok koristi tipke `JUTRO`, `PODNE`, `VECER`
- ispod suncevih tipki postoji crveni toggle `TIHI MOD`
- pri dnu dashboarda postoji i servisni link `POSTAVKE`
- `TIHI MOD` preko weba ulazi u isti jedinstveni tihi rezim kao [main/prekidac_tisine.cpp](../main/prekidac_tisine.cpp)
- ako korisnik promijeni stanje fizickim kip-prekidacem tihog moda, dashboard nakon sljedezeg `STATUS:` osvjezavanja prikazuje stvarno stanje iz Mege
- dashboard pri prvom otvaranju odmah radi jedan prisilni dohvat `STATUS?` kako bi se tipke obojile prema stvarnom stanju toranjskog sata

## ⚙️ Web postavke

- stranica `/settings` namjerno uredjuje samo sigurne postavke koje ne pomicu kazaljke, ne diraju okretajnu plocu i ne mijenjaju vrijeme
- trenutno podrzava skupine:
- `Sustav`
- `Stapici`
- `BAT`
- `Sunce`
- `Blagdani`
- `Sustav` ukljucuje `LCD svjetlo`, `Logiranje`, `RS485`, `UPS mod`, `Kocnicu zvona`, `INR1`, `INR2` i `Impuls cekica`
- `Stapici` ukljucuju trajanja `TR`, `TN`, `TS` i odgodu slavljenja `S`
- `BAT` ukljucuje sate `od/do` i modove `OTK`, `S` i `M`
- `BAT od/do` na webu znaci raspon u kojem je redovno otkucavanje dopusteno; izvan tog raspona `Mega` blokira samo otkucavanje kroz [main/postavke.cpp](../main/postavke.cpp) i [main/otkucavanje.cpp](../main/otkucavanje.cpp)
- `Sunce` ukljucuje `Jutro`, `Podne`, `Vecer`, odabir zvona, jutarnje/vecernje odgode i `Nocnu rasvjetu`
- `Blagdani` ukljucuju dnevnu i nedjeljnu misu te unaprijed zadanu listu `15` nepomicnih i `7` pomicnih blagdana; na webu se za svaki blagdan uredjuje samo ukljucenje i vrijeme mise `HH:MM`
- dnevna misa pokrece samo `MUSKO` zvono `30 min` prije upisanog vremena mise `HH:MM`, uz radno trajanje zvonjenja iz [main/postavke.cpp](../main/postavke.cpp)
- nedjeljna i blagdanska misa pokrecu nedjeljno zvonjenje oba zvona `2 h` i `1 h` prije upisanog vremena mise `HH:MM`, bez dodatnog `slavljenja`
- prazno polje vremena na `/blagdani` znaci da su odgovarajuca redovita misa ili blagdan iskljuceni, bez obzira na stanje kvacice
- sva misna zvonjenja startaju u `25.` sekundi minute, sinkronizirano s citanjem cavala iz [main/okretna_ploca.cpp](../main/okretna_ploca.cpp)
- `Mega` ostaje jedini autoritet za validaciju i spremanje kroz [main/postavke.cpp](../main/postavke.cpp)
- `ESP32` samo prikazuje formu, salje cijeli paket i nakon potvrde ponovno cita stvarno stanje s `Mege`

## 🔐 Autentikacija

- dashboard `/` i sve `/api/...` rute koriste `Basic Auth`
- ruta `/update` koristi isti `Basic Auth`
- lozinka se ucitava iz `EEPROM`-a ili pada na zadanu firmware vrijednost
- `/setup` ne trazi `Basic Auth` dok je aktivan setup `AP`

## 📡 OTA nadogradnja

- `OTA` je izveden kao skrivena web ruta `/update`
- koristi se upload kompajlirane `.bin` datoteke za `ESP32`
- tijekom upisa firmwarea `ESP` privremeno zaustavlja redovni `NTP` i ostale web/serijske poslove koji nisu potrebni za upload
- nakon uspjesne nadogradnje `ESP` sam zakazuje kratki restart i vraca se u normalan rad
- dashboard ne prikazuje link prema `/update`; ruta se otvara rucnim upisom adrese u pregledniku

## 🧵 Serijski protokol prema Megi

### `Mega -> ESP`

- `WIFI:<ssid>|<lozinka>|<dhcp>|<ip>|<maska>|<gateway>` salje mrezne postavke toranjskog sata
- `WIFIEN:0` i `WIFIEN:1` gase ili pale `WiFi` radio
- `WIFISTATUS?` trazi trenutno `WiFi` stanje mreznog mosta
- `NTPCFG:<server>` postavlja `NTP` server
- `NTPREQ:SYNC` trazi trenutno `NTP` vrijeme u trenutku koji odabere `Mega`
- `SETREQ:SUSTAV`, `SETREQ:STAPICI`, `SETREQ:BAT`, `SETREQ:SUNCE`, `SETREQ:MISE`, `SETREQ:BLAGDANI_NEP` i `SETREQ:BLAGDANI_POM` traze trenutno stanje pojedine web skupine iz `main/postavke.*`

### `ESP -> Mega`

- `CFGREQ` trazi pocetnu konfiguraciju nakon boota
- `WIFI:CONNECTED`, `WIFI:DISCONNECTED`, `WIFI:LOCAL_IP:...`, `WIFI:MAC:...` prijavljuju stanje veze
- `NTP:YYYY-MM-DDTHH:MM:SS.mmm;DST=0/1` salje lokalno vrijeme toranjskog sata s milisekundama
- `SETUPWIFI:<ssid>|<lozinka>` prosljeduje novu mrezu koju je korisnik upisao kroz setup `AP`
- `CMD:<naredba>` prenosi servisne naredbe prema [main/esp_serial.cpp](../main/esp_serial.cpp)
- `STATUS:` vraca objedinjeni status koji dashboard koristi za boju tipki
- `SET:SUSTAV|lcd=...|log=...|rs=...|ups=...|koc=...|inr1=...|inr2=...|imp=...` vraca trenutne `Sustav` postavke
- `SET:STAPICI|tr=...|tn=...|ts=...|odg=...` vraca postavke stapica
- `SET:BAT|od=...|do=...|otk=...|sl=...|mr=...` vraca `BAT` postavke
- `SET:SUNCE|ju=...|jb=...|jo=...|pu=...|pb=...|vu=...|vb=...|vo=...|nr=...` vraca sunceve postavke
- `SET:MISE|rd=...|nd=...` vraca redovite dnevne i nedjeljne mise
- `SET:BLAGDANI_NEP|f0=...|...` vraca stanje unaprijed zadanih nepomicnih blagdana
- `SET:BLAGDANI_POM|p0=...|...` vraca stanje unaprijed zadanih pomicnih blagdana
- `SETCFG:SUSTAV|...`, `SETCFG:STAPICI|...`, `SETCFG:BAT|...`, `SETCFG:SUNCE|...`, `SETCFG:MISE|...`, `SETCFG:BLAGDANI_NEP|...` i `SETCFG:BLAGDANI_POM|...` salju novi puni paket odgovarajuce skupine prema `Megi`
- `ACK:*`, `ERR:*` i `NTPLOG:*` linije sluze za potvrde i dijagnostiku mreznog mosta

## ⏱️ UDP NTP tok

- `ESP` vise ne koristi `NTPClient`, nego vlastiti UDP `NTP` dohvat u [esp_firmware.ino](esp_firmware.ino)
- prije novog upita odbacuju se zaostali UDP paketi kako kasni odgovor ne bi pokvario novo `RTT` mjerenje
- prihvacaju se samo valjani `NTP` odgovori, uz osnovnu provjeru `mode`, `stratum` vrijednosti i vremena
- prvi `NTP` uzorak nakon restarta ili `WiFi` reconnecta ne salje se odmah Megi
- prvi uzorak se pamti, a `ESP` odmah trazi drugi uzorak radi stabilizacije
- tek potvrden drugi uzorak postaje autoritet za prvu `NTP` sinkronizaciju toranjskog sata
- `ESP` iz odgovora racuna precizniji `UTC ms`, a prema Megi sada salje i milisekundni dio `NTP:` zapisa
- `Mega` i dalje ostaje jedini vlasnik `RTC` upisa i poravnanja na `RTC SQW` granicu sekunde, ali pri odluci o pomaku koristi i milisekundni dio uzorka

## 🛡️ Rad uz sigurnosne blokade Mege

- `ESP` i dalje moze odrzavati `WiFi` i `NTP` dok je `Mega` u ogranicenom radu
- `ESP` ne otkljucava `safe mode` i ne potvrduje latched faultove
- kad `Mega` blokira automatiku zbog `RTC` ili `EEPROM` problema, `ESP` ostaje samo pomocni izvor mreze i vremena bez ovlasti nad mehanikom toranjskog sata
- nakon `WiFi` watchdog reseta `Mega` dobiva `NTP:` tek kad `ESP` ponovno potvrdi svjeze vrijeme

## 🚫 Sto vise nije aktivno

- nema ruta `/mise` ni `/mise/blagdani`
- nema zasebne `ESP` automatike za mise, blagdane ni posebnu zvonjavu; web samo uredjuje postavke, a `Mega` vodi stvarni raspored toranjskog sata
- nema `TIME?` fallbacka prema Megi
- nema ruta `/detalji`, `/clock-config`, `/hand-service`, `/plate-service` ni `/password`
- nema web uredjivanja vremena, datuma, kazaljki ni okretne ploce toranjskog sata
- nema web uredjivanja vremena, datuma, kazaljki ni okretne ploce; blagdanski web sloj pokriva samo ukljucenje i vrijeme mise unaprijed zadanih blagdana

## 📶 Setup WiFi

- setup `AP` ima `SSID` `ZVONKO_setup`
- lozinka setup `AP`-a je `zvonko10`
- setup `AP` se moze pokrenuti i dugim istovremenim pritiskom `lijevo + desno` na Mega tipkovnici, ali samo s glavnog prikaza sata
- na `ESP32` zadano se koristi tipka na `GPIO27` i status `LED` na `GPIO26`
- serijska veza prema `Megi` koristi odvojene pinove `GPIO16` kao `RX` i `GPIO17` kao `TX`
- dok je setup `AP` aktivan, i root ruta `http://192.168.4.1/` i `http://192.168.4.1/setup` otvaraju setup stranicu
- nakon spremanja mreze `ESP` prosljeduje novu konfiguraciju Megi preko `SETUPWIFI:`

## 🛠️ Upload i provjera

1. Otvori `esp_firmware.ino` u `Arduino IDE`-u ili `PlatformIO` okruzenju.
2. Odaberi odgovarajucu plocicu.
- `ESP32 Dev Module` ili odgovarajuci `ESP32` profil za `ESP32`
3. Za `ESP32` spoji `Mega TX3 (pin 14)` na `ESP RX GPIO16` preko djelitelja napona te `ESP TX GPIO17` na `Mega RX3 (pin 15)`.
4. Prenesi firmware i provjeri da su `GND` vodovi zajednicki.

Napomena:
- `OTA` nadogradnja ne trazi `GPIO1/GPIO3`; toranjski sat sigurnije radi kad je serijska veza prema `Megi` odvojena na `GPIO16/GPIO17`.
- mnoge `ESP32` plocice imaju `GPIO1/GPIO3` oznacene kao `TX/RX`, ali te pinove ovdje namjerno ne koristimo za radni most prema `Megi`.

### OTA upload preko mreze

1. U istom razvojnom okruzenju kompajliraj firmware i pronadi izlaznu `.bin` datoteku.
2. Otvori `http://<ip-esp>/update`.
3. Prijavi se istim `Basic Auth` podacima kao za dashboard toranjskog sata.
4. Odaberi novu `.bin` datoteku i pricekaj potvrdu o uspjesnoj nadogradnji.
5. Pricekaj automatski restart `ESP` modula prije novog otvaranja dashboarda.

## ✅ Sto provjeriti nakon boota

- serijski monitor treba pokazati `CFGREQ`, `WIFI:CONNECTED` i `WIFI:LOCAL_IP:...` kada je mreza dostupna
- prvi `NTP` nakon restarta treba u `NTPLOG:` prikazati spremanje prvog uzorka i potvrdu drugim uzorkom
- `ESP` vise ne treba sam slati `NTP:` po spajanju; `NTP` prema Megi ide tek nakon `NTPREQ:SYNC`
- `http://<ip-esp>/api/status` treba vratiti `JSON` sa stanjem `WiFi` veze, glavnih tipki, suncevih tipki i `TIHOG MODA`
- `http://<ip-esp>/settings` treba otvoriti stranicu sa skupinama `Sustav`, `Stapici`, `BAT` i `Sunce` te pri ulazu povuci stvarno stanje s `Mege`
- `http://<ip-esp>/blagdani` treba otvoriti zasebnu stranicu za redovite i blagdanske mise te pri ulazu povuci stvarno stanje s `Mege`
- `http://<ip-esp>/api/settings/system?force=1` treba vratiti `JSON` sa stanjem `Sustav` postavki
- `http://<ip-esp>/api/settings/stapici?force=1` treba vratiti `JSON` sa stanjem `Stapica`
- `http://<ip-esp>/api/settings/bat?force=1` treba vratiti `JSON` sa stanjem `BAT` postavki
- `http://<ip-esp>/api/settings/sunce?force=1` treba vratiti `JSON` sa stanjem `Sunce` postavki
- `http://<ip-esp>/api/settings/blagdani?force=1` treba vratiti `JSON` sa stanjem `Blagdana`
- pocetna stranica treba prikazati samo glavne tipke, sunceve tipke i crveni `TIHI MOD`
- API pozivi poput `http://<ip-esp>/api/bell1/on` i `http://<ip-esp>/api/quiet/on` trebaju poslati odgovarajuci `CMD:` prema Megi
- `http://<ip-esp>/update` treba otvoriti `OTA` upload stranicu i nakon uspjesnog slanja firmwarea izazvati restart `ESP` modula

## 📄 Datoteke

- [esp_firmware.ino](esp_firmware.ino) - glavni firmware za `WiFi`, setup `AP`, UDP `NTP`, dashboard i servisni web/API sloj toranjskog sata
- [Popis ESP web API ruta toranjskog sata](../docs/esp_web_api_toranjskog_sata.md)
