"""Check a TLS-PSK broker set up with tools/mosquitto-psk.conf.

    python tools/check_broker.py --lan-ip 192.168.1.20 --psk-file mosquitto.psk

Run it on the broker machine. Needs mosquitto_pub / mosquitto_sub (mosquitto-clients).
Exits 0 only if every check passes:
  1. plain 1883 is not reachable on the LAN address;
  2. 8883 with the right key round-trips a message;
  3. 8883 with a wrong key is refused.
"""

import argparse
import secrets
import subprocess
import sys
import time
from pathlib import Path

TOPIC = "claude/psv/check"


def _psk_args(identity: str, key_hex: str) -> list[str]:
    return ["--psk-identity", identity, "--psk", key_hex]


def plain_closed(ip: str) -> bool:
    r = subprocess.run(["mosquitto_pub", "-h", ip, "-p", "1883", "-t", TOPIC, "-m", "x"],
                       capture_output=True, timeout=10)
    return r.returncode != 0


def psk_roundtrip(ip: str, identity: str, key_hex: str) -> bool:
    token = secrets.token_hex(8)
    sub = subprocess.Popen(["mosquitto_sub", "-h", ip, "-p", "8883", "-t", TOPIC, "-C", "1", "-W", "5",
                            *_psk_args(identity, key_hex)], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    try:
        time.sleep(1)  # let the subscriber connect first
        subprocess.run(["mosquitto_pub", "-h", ip, "-p", "8883", "-t", TOPIC, "-m", token,
                        *_psk_args(identity, key_hex)], capture_output=True, timeout=10)
        out, _ = sub.communicate(timeout=10)
    finally:
        if sub.poll() is None:
            sub.kill()
    return out.decode().strip() == token


def wrong_key_refused(ip: str, identity: str) -> bool:
    r = subprocess.run(["mosquitto_pub", "-h", ip, "-p", "8883", "-t", TOPIC, "-m", "x",
                        *_psk_args(identity, secrets.token_hex(32))], capture_output=True, timeout=10)
    return r.returncode != 0


def main(argv=None) -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--lan-ip", required=True, help="this machine's LAN address")
    p.add_argument("--psk-file", type=Path, required=True, help="the mosquitto.psk from psk_pair.py")
    args = p.parse_args(argv)
    identity, key_hex = args.psk_file.read_text().strip().split(":", 1)
    checks = [
        ("plain 1883 closed on the LAN", lambda: plain_closed(args.lan_ip)),
        ("8883 accepts the paired key", lambda: psk_roundtrip(args.lan_ip, identity, key_hex)),
        ("8883 refuses a wrong key", lambda: wrong_key_refused(args.lan_ip, identity)),
    ]
    ok = True
    for name, check in checks:
        passed = check()
        ok = ok and passed
        print(f"{'PASS' if passed else 'FAIL'}  {name}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
