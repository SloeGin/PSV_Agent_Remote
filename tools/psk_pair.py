"""Generate the pre-shared key that pairs a PSV with tls:// brokers.

    python tools/psk_pair.py OUT_DIR [--identity psv] [--force]

Writes OUT_DIR/psk.txt (copy it to ux0:data/psv_agent_remote/ on the PSV, on a
network you trust) and OUT_DIR/mosquitto.psk (the broker's psk_file). Both hold
the same "<identity>:<64 hex>" line and are readable by you only.
"""

import argparse
import os
import secrets
import sys
from pathlib import Path

FILES = ("psk.txt", "mosquitto.psk")


def _write_private(path: Path, text: str) -> None:
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    try:
        os.fchmod(fd, 0o600)  # O_CREAT's mode does not apply to an existing file
        os.write(fd, text.encode())
    finally:
        os.close(fd)


def generate(out_dir: Path, identity: str, force: bool) -> None:
    out_dir = Path(out_dir)
    if ":" in identity or not identity.strip():
        sys.exit("identity must be non-empty and must not contain ':'")
    if not force:
        for name in FILES:
            if (out_dir / name).exists():
                sys.exit(f"{out_dir / name} already exists; pass --force to replace the key")
    out_dir.mkdir(parents=True, exist_ok=True)
    line = f"{identity}:{secrets.token_bytes(32).hex()}\n"
    for name in FILES:
        _write_private(out_dir / name, line)


def main(argv=None) -> None:
    p = argparse.ArgumentParser(description="Generate the PSV <-> broker pre-shared key.")
    p.add_argument("out_dir", type=Path)
    p.add_argument("--identity", default="psv")
    p.add_argument("--force", action="store_true", help="replace existing key files")
    args = p.parse_args(argv)
    generate(args.out_dir, args.identity, args.force)
    print(f"Wrote {args.out_dir / 'psk.txt'} (for the PSV) and {args.out_dir / 'mosquitto.psk'} (for the broker).")
    print("Copy psk.txt to the PSV at ux0:data/psv_agent_remote/ on a network you trust (e.g. at home).")
    print("Do not send these files through chat or email.")


if __name__ == "__main__":
    main()
