# 🧩 Raspberry Pi most za toranjski sat

Ova mapa sadrzi Python serijski most za `Raspberry Pi 4B` koji povezuje `Arduino Mega 2560` toranjskog sata s `Home Assistantom` i mreznim uslugama na Raspberryju.

## ✨ Uloga mosta

- prima serijske poruke od `Mega 2560`
- vraca lokalno vrijeme Raspberryja na `NTPREQ:SYNC`
- vraca status mreze na `WIFISTATUS?`
- prima runtime status toranjskog sata iz `STATUS:...`
- po zelji objavljuje stanje na `MQTT` za `Home Assistant`
- po zelji prima `MQTT` naredbe i prosljeduje ih Megi kao `CMD:...`

## 🔌 Preporuceno spajanje UART

- `Mega TX1 pin 18` -> djelitelj napona -> `Raspberry Pi GPIO15 / RXD`
- `Raspberry Pi GPIO14 / TXD` -> `Mega RX1 pin 19`
- `GND Mega` -> `GND Raspberry Pi`

Napomena:
- `Mega -> Pi` mora ici preko djelitelja napona ili level shiftera jer `Mega` radi na `5 V`, a `Pi` na `3.3 V`
- `Pi -> Mega` moze ici direktno

## ⚙️ Priprema Raspberry Pi 4B

1. Ukljuci `UART` na Raspberryju.
2. Iskljuci Linux serial konzolu na tom istom UART-u.
3. Instaliraj Python ovisnosti:

```bash
python3 -m pip install -r pi_bridge/requirements.txt
```

## ▶️ Pokretanje mosta

Primjer bez MQTT-a:

```bash
python3 pi_bridge/mega_bridge.py --port /dev/ttyAMA0 --baud 9600
```

Primjer s MQTT brokerom za `Home Assistant`:

```bash
python3 pi_bridge/mega_bridge.py \
  --port /dev/ttyAMA0 \
  --baud 9600 \
  --mqtt-host 127.0.0.1 \
  --mqtt-port 1883 \
  --mqtt-topic fila33/toranj
```

## 📡 MQTT topic-i za Home Assistant

Most objavljuje:

- `fila33/toranj/bridge/online`
- `fila33/toranj/bridge/network_enabled`
- `fila33/toranj/bridge/network_config`
- `fila33/toranj/bridge/ntp_server`
- `fila33/toranj/mega/status_raw`
- `fila33/toranj/mega/status`

Most prima:

- `fila33/toranj/command/cmd`
- `fila33/toranj/command/raw`
- `fila33/toranj/command/status_request`
- `fila33/toranj/command/ntp_sync`

## 🏠 Preporuka za Home Assistant u Dockeru

- `Home Assistant` neka ostane u Dockeru
- `MQTT broker` moze biti zaseban Docker kontejner, npr. `Mosquitto`
- `mega_bridge.py` je najjednostavnije pokrenuti na host Raspberry Pi sustavu kao `systemd` servis

Razlog:
- host lakse pristupa `UART` uredjaju
- serijski most ostaje odvojen od samog `Home Assistanta`
- toranjski sat ostaje stabilan i nakon restarta pojedinog kontejnera

## 🧱 Sto most trenutno emulira

Da bi `Mega` radila bez velike prerade firmwarea, most emulira dosadasnji serijski protokol mreznog modula:

- `WIFI:...`
- `WIFIEN:...`
- `WIFISTATUS?`
- `NTPCFG:...`
- `NTPREQ:SYNC`
- `STATUS:...`
- `CMD:...`

To znaci da `Mega` i dalje ostaje autoritet za kazaljke, okretnu plocu, zvona, cekice, sinkronizaciju i recovery logiku toranjskog sata.
