"""Bridge existing Codex sessions to the PSV broker. Does not install hooks."""

import argparse
import fcntl
import signal
import socket
import sys
from pathlib import Path

from psv_remote.bus import open_bus
from psv_remote.codex_bridge import run
from psv_remote.codex_connections import AppServer, FrontendIPC
from psv_remote.rpc import DependencyError
from psv_remote.codex_manager import watch
from psv_remote.config import load_config
from psv_remote.remote import scoped_session_id


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("transport", choices=("app-server", "ipc", "watch", "vscode-client"))
    parser.add_argument("--thread", help="Exact ID of an existing live Codex thread (single bridge)")
    parser.add_argument("--socket", type=Path, help="Existing runtime's Unix socket")
    parser.add_argument("--host-id", default=socket.gethostname(), help="Stable broker-wide identity for this host")
    parser.add_argument("--experimental-ipc", action="store_true", help="Opt into the version-dependent frontend IPC")
    parser.add_argument("--ipc-socket", type=Path, help="Watch: frontend coordination socket")
    parser.add_argument("--app-server-socket", type=Path, action="append", default=[],
                        help="Watch: existing CLI runtime socket (repeat for multiple servers)")
    parser.add_argument("--state-db", type=Path, help="Watch: read-only local Codex thread index")
    parser.add_argument("--cwd", type=Path, help="Watch only sessions whose working directory matches this path")
    parser.add_argument("--max-sessions", type=int, default=16, help="Watch: maximum managed sessions (1-16)")
    parser.add_argument("--lease-managed", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--replace-managed", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_args()
    managed = args.transport in ("watch", "vscode-client")
    if not args.host_id or (not managed and not args.thread):
        parser.error("--host-id must not be empty; single bridge mode requires --thread")
    if not 1 <= args.max_sessions <= 16:
        parser.error("--max-sessions must be between 1 and the Vita limit of 16")
    if managed and (args.thread or args.socket):
        parser.error("watch discovers thread IDs; use --ipc-socket / --app-server-socket for endpoints")
    if args.transport == "ipc" and not args.experimental_ipc:
        parser.error("Frontend IPC is internal; use --experimental-ipc after checking version compatibility")
    if args.ipc_socket and not args.experimental_ipc:
        parser.error("--ipc-socket requires --experimental-ipc")
    cfg = load_config()
    if not cfg.enabled and args.transport != "vscode-client":
        return 0
    root = Path.home() / ".codex"
    if managed:
        app_paths = args.app_server_socket
        default_app = root / "app-server-control/app-server-control.sock"
        if not app_paths and (not args.experimental_ipc or default_app.exists()):
            app_paths = [default_app]
        options = dict(ipc_path=(args.ipc_socket or root / "ipc/ipc.sock") if args.experimental_ipc else None,
                       database=args.state_db or root / "state_5.sqlite", app_paths=app_paths,
                       cwd=str(args.cwd.resolve()) if args.cwd else None, limit=args.max_sessions)
        if args.transport == "vscode-client":
            from psv_remote.vscode_client import run_client
            return run_client(cfg, args.host_id, options, args.replace_managed)
        return watch(cfg, args.host_id, **options, lease_managed=args.lease_managed)
    path = args.socket or root / ("ipc/ipc.sock" if args.transport == "ipc" else "app-server-control/app-server-control.sock")
    sid = scoped_session_id("codex", args.host_id, args.thread)
    cfg.state_dir.mkdir(parents=True, exist_ok=True)
    # One local bridge for this broker session; never replace another listener.
    with (cfg.state_dir / f"{sid}.bridge.lock").open("a") as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            parser.error("A bridge for this Codex thread is already running")
        native = bus = None
        try:
            native = (FrontendIPC if args.transport == "ipc" else AppServer)(path, args.thread)
            bus = open_bus(cfg)
            run(bus, native, cfg, args.host_id)
        finally:
            if native is not None:
                native.close()
            if bus is not None:
                bus.close()
    return 0


def interrupt(signum, frame):
    raise KeyboardInterrupt


if __name__ == "__main__":
    signal.signal(signal.SIGTERM, interrupt)
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        raise SystemExit(0)
    except DependencyError as error:
        print(str(error), file=sys.stderr)
        raise SystemExit(1)
    except Exception as error:
        # Native errors can describe private commands; avoid writing payloads to
        # the terminal or a shared log. Preserve details only in the exception chain.
        print(f"Codex bridge stopped ({type(error).__name__}). Check the runtime, thread and broker.", file=sys.stderr)
        raise SystemExit(1)
