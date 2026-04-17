#!/usr/bin/env python3
"""Serijski most izmedu toranjskog sata i Raspberry Pi / Home Assistanta."""

from __future__ import annotations

import argparse
import json
import logging
import socket
import sys
import time
import uuid
from dataclasses import dataclass, field
from datetime import datetime
from typing import Callable
from zoneinfo import ZoneInfo

try:
    import serial
    from serial import SerialException
except ImportError as exc:  # pragma: no cover - ovisi o okruzenju na Raspberryju
    raise SystemExit(
        "Nedostaje pyserial. Instaliraj ga naredbom: pip install pyserial"
    ) from exc

try:
    import paho.mqtt.client as mqtt
except ImportError:  # pragma: no cover - MQTT je opcionalan
    mqtt = None


@dataclass
class MreznaKonfiguracija:
    ssid: str = ""
    lozinka: str = ""
    dhcp: bool = True
    ip: str = ""
    maska: str = ""
    gateway: str = ""


@dataclass
class StanjeMosta:
    mreza_omogucena: bool = True
    ntp_server: str = "pool.ntp.org"
    mreza: MreznaKonfiguracija = field(default_factory=MreznaKonfiguracija)
    zadnji_status_raw: str = ""
    zadnji_status: dict[str, str] = field(default_factory=dict)


def postavi_logging(razina: str) -> None:
    logging.basicConfig(
        level=getattr(logging, razina.upper(), logging.INFO),
        format="%(asctime)s %(levelname)s %(message)s",
    )


def procitaj_argumente() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Raspberry Pi serijski most za toranjski sat i Home Assistant."
    )
    parser.add_argument("--port", required=True, help="Serijski port prema Mega 2560, npr. /dev/ttyAMA0")
    parser.add_argument("--baud", type=int, default=9600, help="Brzina serijske veze")
    parser.add_argument(
        "--timezone",
        default="Europe/Zagreb",
        help="Vremenska zona za NTP odgovor prema tornjskom satu",
    )
    parser.add_argument(
        "--mqtt-host",
        default="",
        help="MQTT broker za Home Assistant integraciju; ako je prazno, MQTT je iskljucen",
    )
    parser.add_argument("--mqtt-port", type=int, default=1883, help="MQTT port")
    parser.add_argument("--mqtt-user", default="", help="MQTT korisnik")
    parser.add_argument("--mqtt-password", default="", help="MQTT lozinka")
    parser.add_argument(
        "--mqtt-topic",
        default="fila33/toranj",
        help="Osnovni MQTT topic za toranjski sat",
    )
    parser.add_argument(
        "--reconnect-delay",
        type=float,
        default=5.0,
        help="Pauza izmedu pokusaja ponovnog otvaranja serijskog porta",
    )
    parser.add_argument(
        "--log-level",
        default="INFO",
        help="Razina logiranja: DEBUG, INFO, WARNING, ERROR",
    )
    return parser.parse_args()


def formatiraj_mac_adresu() -> str:
    mac = uuid.getnode()
    okteti = [(mac >> pomak) & 0xFF for pomak in range(40, -1, -8)]
    return ":".join(f"{oktet:02X}" for oktet in okteti)


def dohvati_lokalnu_ip() -> str:
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.connect(("1.1.1.1", 80))
            return sock.getsockname()[0]
    except OSError:
        try:
            return socket.gethostbyname(socket.gethostname())
        except OSError:
            return ""


def je_mreza_dostupna(stanje: StanjeMosta) -> bool:
    if not stanje.mreza_omogucena:
        return False
    ip = dohvati_lokalnu_ip()
    return bool(ip and ip != "127.0.0.1")


def parsiraj_status_payload(payload: str) -> dict[str, str]:
    izlaz: dict[str, str] = {}
    for segment in payload.split("|"):
        if "=" not in segment:
            continue
        kljuc, vrijednost = segment.split("=", 1)
        izlaz[kljuc] = vrijednost
    return izlaz


def trenutni_iso_lokalno(vremenska_zona: ZoneInfo) -> str:
    return datetime.now(tz=vremenska_zona).strftime("%Y-%m-%dT%H:%M:%S")


class MqttMost:
    def __init__(
        self,
        host: str,
        port: int,
        korisnik: str,
        lozinka: str,
        topic: str,
        posalji_raw_naredbu: Callable[[str], None],
        posalji_cmd_naredbu: Callable[[str], None],
        posalji_ntp: Callable[[], None],
    ) -> None:
        self.host = host
        self.port = port
        self.korisnik = korisnik
        self.lozinka = lozinka
        self.topic = topic.rstrip("/")
        self.posalji_raw_naredbu = posalji_raw_naredbu
        self.posalji_cmd_naredbu = posalji_cmd_naredbu
        self.posalji_ntp = posalji_ntp
        self.klijent: mqtt.Client | None = None

    def pokreni(self) -> None:
        if not self.host:
            return
        if mqtt is None:
            raise SystemExit(
                "MQTT je zatražen, ali paho-mqtt nije instaliran. Pokreni: pip install paho-mqtt"
            )

        self.klijent = mqtt.Client()
        if self.korisnik:
            self.klijent.username_pw_set(self.korisnik, self.lozinka)
        self.klijent.will_set(f"{self.topic}/bridge/online", "0", qos=1, retain=True)
        self.klijent.on_connect = self._on_connect
        self.klijent.on_message = self._on_message
        self.klijent.connect(self.host, self.port, keepalive=60)
        self.klijent.loop_start()

    def zaustavi(self) -> None:
        if self.klijent is None:
            return
        self.objavi(f"{self.topic}/bridge/online", "0", retain=True)
        self.klijent.loop_stop()
        self.klijent.disconnect()

    def objavi(self, topic: str, payload: str, retain: bool = False) -> None:
        if self.klijent is None:
            return
        self.klijent.publish(topic, payload, qos=0, retain=retain)

    def objavi_json(self, topic: str, payload: dict[str, str], retain: bool = False) -> None:
        self.objavi(topic, json.dumps(payload, ensure_ascii=True), retain=retain)

    def _on_connect(self, client: mqtt.Client, _userdata, _flags, reason_code, _properties=None) -> None:
        logging.info("MQTT spojen, reason=%s", reason_code)
        client.subscribe(f"{self.topic}/command/cmd")
        client.subscribe(f"{self.topic}/command/raw")
        client.subscribe(f"{self.topic}/command/status_request")
        client.subscribe(f"{self.topic}/command/ntp_sync")
        self.objavi(f"{self.topic}/bridge/online", "1", retain=True)

    def _on_message(self, _client: mqtt.Client, _userdata, msg: mqtt.MQTTMessage) -> None:
        payload = msg.payload.decode("utf-8", errors="ignore").strip()
        if msg.topic.endswith("/command/cmd"):
            if payload:
                self.posalji_cmd_naredbu(payload)
        elif msg.topic.endswith("/command/raw"):
            if payload:
                self.posalji_raw_naredbu(payload)
        elif msg.topic.endswith("/command/status_request"):
            self.posalji_raw_naredbu("STATUS?")
        elif msg.topic.endswith("/command/ntp_sync"):
            self.posalji_ntp()


class SerijskiMost:
    def __init__(self, args: argparse.Namespace) -> None:
        self.args = args
        self.vremenska_zona = ZoneInfo(args.timezone)
        self.stanje = StanjeMosta()
        self.serial_port: serial.Serial | None = None
        self.mqtt = MqttMost(
            host=args.mqtt_host,
            port=args.mqtt_port,
            korisnik=args.mqtt_user,
            lozinka=args.mqtt_password,
            topic=args.mqtt_topic,
            posalji_raw_naredbu=self.posalji_raw_liniju,
            posalji_cmd_naredbu=self.posalji_cmd_naredbu,
            posalji_ntp=self.posalji_ntp_vrijeme,
        )

    def pokreni(self) -> None:
        self.mqtt.pokreni()
        while True:
            try:
                self._otvori_serijski_port()
                self._serijska_petlja()
            except SerialException as exc:
                logging.warning("Serijski port nije dostupan: %s", exc)
            except KeyboardInterrupt:
                raise
            except Exception:
                logging.exception("Neocekivana greska u serijskom mostu")
            finally:
                self._zatvori_serijski_port()
                time.sleep(self.args.reconnect_delay)

    def _otvori_serijski_port(self) -> None:
        if self.serial_port is not None and self.serial_port.is_open:
            return
        logging.info("Otvaram serijski port %s @ %s", self.args.port, self.args.baud)
        self.serial_port = serial.Serial(self.args.port, self.args.baud, timeout=0.5)
        time.sleep(0.5)
        self.posalji_raw_liniju("CFGREQ")

    def _zatvori_serijski_port(self) -> None:
        if self.serial_port is not None and self.serial_port.is_open:
            self.serial_port.close()
        self.serial_port = None

    def _serijska_petlja(self) -> None:
        assert self.serial_port is not None
        while True:
            redak = self.serial_port.readline()
            if not redak:
                continue
            linija = redak.decode("utf-8", errors="ignore").strip()
            if not linija:
                continue
            self.obradi_liniju_iz_mege(linija)

    def posalji_raw_liniju(self, linija: str) -> None:
        if self.serial_port is None or not self.serial_port.is_open:
            logging.warning("Serijski port nije otvoren, preskacem slanje: %s", linija)
            return
        izlaz = (linija.strip() + "\n").encode("utf-8")
        self.serial_port.write(izlaz)
        self.serial_port.flush()
        logging.debug("Pi -> Mega: %s", linija)

    def posalji_cmd_naredbu(self, naredba: str) -> None:
        self.posalji_raw_liniju(f"CMD:{naredba}")

    def posalji_ntp_vrijeme(self) -> None:
        iso_vrijeme = trenutni_iso_lokalno(self.vremenska_zona)
        self.posalji_raw_liniju(f"NTPLOG: Pi salje vrijeme {iso_vrijeme}")
        self.posalji_raw_liniju(f"NTP:{iso_vrijeme}")

    def odgovori_statusom_mreze(self) -> None:
        if je_mreza_dostupna(self.stanje):
            ip = dohvati_lokalnu_ip()
            self.posalji_raw_liniju("WIFI:CONNECTED")
            if ip:
                self.posalji_raw_liniju(f"WIFI:LOCAL_IP:{ip}")
            self.posalji_raw_liniju(f"WIFI:MAC:{formatiraj_mac_adresu()}")
        else:
            self.posalji_raw_liniju("WIFI:DISCONNECTED")
        self.posalji_raw_liniju("ACK:WIFISTATUS")

    def obradi_liniju_iz_mege(self, linija: str) -> None:
        logging.debug("Mega -> Pi: %s", linija)

        if linija == "WIFISTATUS?":
            self.odgovori_statusom_mreze()
            return

        if linija.startswith("WIFIEN:"):
            payload = linija[7:].strip()
            self.stanje.mreza_omogucena = payload == "1"
            self.posalji_raw_liniju("ACK:WIFIEN")
            self.mqtt.objavi(
                f"{self.args.mqtt_topic}/bridge/network_enabled",
                "1" if self.stanje.mreza_omogucena else "0",
                retain=True,
            )
            return

        if linija.startswith("WIFI:"):
            dijelovi = linija[5:].split("|")
            if len(dijelovi) == 6:
                self.stanje.mreza = MreznaKonfiguracija(
                    ssid=dijelovi[0],
                    lozinka=dijelovi[1],
                    dhcp=dijelovi[2] == "1",
                    ip=dijelovi[3],
                    maska=dijelovi[4],
                    gateway=dijelovi[5],
                )
                self.posalji_raw_liniju("ACK:WIFI")
                self.mqtt.objavi_json(
                    f"{self.args.mqtt_topic}/bridge/network_config",
                    {
                        "ssid": self.stanje.mreza.ssid,
                        "dhcp": "1" if self.stanje.mreza.dhcp else "0",
                        "ip": self.stanje.mreza.ip,
                        "maska": self.stanje.mreza.maska,
                        "gateway": self.stanje.mreza.gateway,
                    },
                    retain=True,
                )
            else:
                self.posalji_raw_liniju("ERR:WIFI")
            return

        if linija.startswith("NTPCFG:"):
            self.stanje.ntp_server = linija[7:].strip() or self.stanje.ntp_server
            self.posalji_raw_liniju("ACK:NTPCFG")
            self.mqtt.objavi(
                f"{self.args.mqtt_topic}/bridge/ntp_server",
                self.stanje.ntp_server,
                retain=True,
            )
            return

        if linija == "NTPREQ:SYNC":
            self.posalji_ntp_vrijeme()
            return

        if linija.startswith("STATUS:"):
            payload = linija[7:]
            self.stanje.zadnji_status_raw = payload
            self.stanje.zadnji_status = parsiraj_status_payload(payload)
            self.mqtt.objavi(f"{self.args.mqtt_topic}/mega/status_raw", payload)
            self.mqtt.objavi_json(
                f"{self.args.mqtt_topic}/mega/status",
                self.stanje.zadnji_status,
                retain=False,
            )
            return

        if linija.startswith("ACK:") or linija.startswith("ERR:"):
            self.mqtt.objavi(f"{self.args.mqtt_topic}/bridge/last_reply", linija)
            return

        logging.info("Neobradena linija s Mege: %s", linija)
        self.mqtt.objavi(f"{self.args.mqtt_topic}/bridge/unhandled", linija)


def main() -> int:
    args = procitaj_argumente()
    postavi_logging(args.log_level)
    most = SerijskiMost(args)
    try:
        most.pokreni()
    except KeyboardInterrupt:
        logging.info("Serijski most zaustavljen na zahtjev korisnika")
        most.mqtt.zaustavi()
        return 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
