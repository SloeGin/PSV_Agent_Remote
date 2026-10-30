"""One VS Code window's lease on a shared manager, with JSON-lines status."""

import http.client
import json
import os
import subprocess
import sys
import threading
import time
import uuid
from pathlib import Path

from .codex_control import configuration_key, configuration_summary, control_path, request


def emit(kind, **fields):
    print(json.dumps({"type": kind, **fields}), flush=True)


def manager_arguments(host_id, options):
    args = ["watch", "--lease-managed", "--host-id", host_id, "--max-sessions", str(options["limit"])]
    if options["ipc_path"] is not None:
        args += ["--experimental-ipc", "--ipc-socket", str(options["ipc_path"])]
    if options["database"] is not None:
        args += ["--state-db", str(options["database"])]
    for path in options["app_paths"]:
        args += ["--app-server-socket", str(path)]
    if options["cwd"] is not None:
        args += ["--cwd", options["cwd"]]
    return args


def _differences(theirs, ours) -> str:
    if not isinstance(theirs, dict):
        return "different settings"  # an older manager doesn't report them
    diffs = [f"{name} {theirs.get(name, 'unknown')} (this window uses {value})"
             for name, value in ours.items() if theirs.get(name) != value]
    return "different settings: " + "; ".join(diffs) if diffs else "different settings"


def resolve_conflict(state, ours, replace):
    """A running manager has other settings: ("replace", None) or ("conflict", message).

    An extension manager no window is using is simply replaced (typically left
    over from before a settings change or an update); Reconnect (replace) takes
    over one that is in use. A manager started outside VS Code is never stopped.
    """
    if state.get("mode") == "extension" and (replace or not state.get("windows")):
        return "replace", None
    differs = _differences(state.get("settings"), ours)
    if state.get("mode") == "extension":
        return "conflict", (f"Another VS Code window is using the bridge with {differs}. Make the PSV Agent "
                            "Remote settings match, or run Reconnect here to take it over.")
    return "conflict", (f"A bridge manager started outside VS Code is running with {differs}. Stop it "
                        "(pkill -f \"psv_codex.py watch\"), then run Reconnect.")


def run_client(cfg, host_id, options, replace=False):
    if not cfg.enabled:
        emit("status", state="disabled", message="Bridge disabled in the shared configuration.")
        return
    stopped = threading.Event()
    def stdin_closed():
        while sys.stdin.buffer.read(1): pass
        stopped.set()  # extension host crash also closes the pipe
    threading.Thread(target=stdin_closed, daemon=True).start()
    client = uuid.uuid4().hex
    key = configuration_key(cfg, host_id, options)
    ours = configuration_summary(cfg, host_id, options)
    cfg.state_dir.mkdir(parents=True, exist_ok=True)
    logfile = control_path(cfg, host_id).with_suffix(".log")
    spawned = None
    next_spawn = 0
    offset = 0
    last_message = None
    errors = (OSError, ValueError, KeyError, http.client.HTTPException)
    try:
        while not stopped.is_set():
            try:
                state = request(cfg, host_id)
                if state.get("configuration") != key:
                    action, message = resolve_conflict(state, ours, replace)
                    if action == "replace":
                        request(cfg, host_id, "stop")
                        stopped.wait(1)
                        continue
                    if message != last_message: emit("log", message=message)
                    last_message = message
                    emit("status", state="conflict", message=message)
                else:
                    state = request(cfg, host_id, "lease", client)
                    emit("status", state="running", bridges=state["bridges"],
                         discovered=state["discovered"], mode=state["mode"], log_path=str(logfile))
                    last_message = None
                    replace = False
            except errors:
                emit("status", state="starting", message="Starting or waiting for the local manager.")
                now = time.monotonic()
                if now >= next_spawn and (spawned is None or spawned.poll() is not None):
                    with logfile.open("ab") as log:
                        entry = Path(__file__).resolve().parents[1] / "psv_codex.py"
                        spawned = subprocess.Popen([sys.executable, str(entry), *manager_arguments(host_id, options)],
                                                   stdin=subprocess.DEVNULL, stdout=log, stderr=log,
                                                   start_new_session=True)
                    next_spawn = now + 15
            try:
                if logfile.exists():
                    size = logfile.stat().st_size
                    if offset > size: offset = 0
                    with logfile.open("rb") as log:
                        log.seek(max(offset, size - 8192))
                        data = log.read(8192)
                        offset = log.tell()
                    if data: emit("log", message=data.decode("utf-8", errors="replace").rstrip())
            except OSError:
                pass
            stopped.wait(3)
    finally:
        try:
            request(cfg, host_id, "release", client)
        except errors:
            pass
