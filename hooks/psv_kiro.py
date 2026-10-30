"""Bridge a Kiro agent to the PSV broker over ACP. Spawns and owns one session.

Unlike psv_codex.py, this does not attach to an existing conversation: it starts
`kiro-cli acp --agent-engine=v3`, creates a new session, and mirrors it to the
broker until interrupted. The Vita is that session's only user interface. Run one
per working directory; it does not install a login service, so restart it after a
reboot. Experimental.

    hooks/.venv/bin/python hooks/psv_kiro.py

An MQTT broker must already be running, and the Kiro CLI must be logged in
(`kiro-cli login`). See docs/kiro-integration.md.
"""

import argparse
import fcntl
import os
import signal
import socket
import sys
from pathlib import Path

from psv_remote import kiro_registry
from psv_remote.acp_connection import AcpConnection
from psv_remote.bus import open_bus
from psv_remote.rpc import NativeError
from psv_remote.config import load_config
from psv_remote.kiro_bridge import run


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kiro-cli", default="kiro-cli", help="Kiro CLI binary path")
    parser.add_argument("--host-id", default=socket.gethostname(),
                        help="Stable broker-wide identity for this host")
    parser.add_argument("--cwd", type=Path, default=Path.cwd(),
                        help="Working directory for the Kiro session")
    args = parser.parse_args()
    if not args.host_id:
        parser.error("--host-id must not be empty")

    cfg = load_config()
    if not cfg.enabled:
        return 0
    cfg.state_dir.mkdir(parents=True, exist_ok=True)

    # One bridge per (host, cwd): a coarse lock so two launchers don't spawn two
    # Kiro sessions for the same working directory. The session id scopes broker
    # topics; the native thread id is only known after the session is created, so
    # the lock keys on cwd, which is what the user launched against.
    cwd = str(args.cwd.resolve())
    lock_key = kiro_registry.lock_key(args.host_id, cwd)
    lock_path = cfg.state_dir / f"kiro-{lock_key}.bridge.lock"
    with lock_path.open("a") as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            parser.error("A Kiro bridge for this working directory is already running")
        native = None
        # The VS Code panel lists running bridges from this entry ("starting" until
        # the session id is known).
        kiro_registry.write_entry(cfg.state_dir, lock_key, os.getpid(), cwd)
        try:
            native = AcpConnection(cfg, cwd, kiro_cli=args.kiro_cli, engine="v3")
            # Reconnects through broker outages; ends on Ctrl-C/SIGTERM, a dead
            # kiro-cli, or the session being removed on the Vita.
            run(lambda: open_bus(cfg), native, cfg, args.host_id,
                on_ready=lambda sid: kiro_registry.write_entry(cfg.state_dir, lock_key, os.getpid(), cwd, sid))
        finally:
            kiro_registry.remove_entry(cfg.state_dir, lock_key)
            if native is not None:
                native.close()
    return 0


def interrupt(signum, frame):
    raise KeyboardInterrupt


if __name__ == "__main__":
    signal.signal(signal.SIGTERM, interrupt)
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        raise SystemExit(0)
    except NativeError as error:
        # The message is a fixed hint (login required, wrong flags, not found),
        # never native payload text.
        print(str(error), file=sys.stderr)
        raise SystemExit(1)
