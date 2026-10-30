"""One VS Code window's lease on a shared manager, with JSON-lines status."""

import http.client
import json
import os
import signal
import subprocess
import sys
import threading
import time
import uuid
from pathlib import Path

from . import kiro_registry
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


PANEL_FIELDS = ("vita", "sessions", "removed")


def running_status(state, log_path, kiro=None) -> dict:
    """The status line for a manager this window is using; an unreachable broker wins.

    It also carries what the side-bar panel shows; a manager too old to report it
    leaves those fields out, and the panel asks for a Reconnect.
    """
    broker = state.get("broker") or {}
    status = {"type": "status", "state": "running", "bridges": state["bridges"], "discovered": state["discovered"],
              "mode": state["mode"], "log_path": str(log_path)}
    if broker.get("reachable") is False:
        status.update(state="broker", message=broker.get("message", "Can't reach the MQTT broker."))
    if all(field in state for field in PANEL_FIELDS):
        status.update({field: state[field] for field in PANEL_FIELDS})
        if kiro is not None:
            status["kiro"] = kiro
    return status


def state_dir_problem(cfg) -> str | None:
    """Why Kiro actions must not trust cfg.state_dir, or None.

    Its registry names pids to signal and its logs are opened for writing, so it must
    be a real directory owned by this user that nobody else can write to (the
    default under /tmp can be created first by another user).
    """
    import stat
    try:
        info = os.lstat(cfg.state_dir)
    except FileNotFoundError:
        return None  # created by us, with a safe mode, on first use
    except OSError as error:
        return f"Can't check the state folder {cfg.state_dir} ({type(error).__name__})."
    if not stat.S_ISDIR(info.st_mode) or info.st_uid != os.getuid() or info.st_mode & 0o022:
        return (f"The state folder {cfg.state_dir} can be changed by other users, so Kiro sessions are "
                "not started or stopped from here. Set state_dir in ~/.config/psv-agent-remote/config.toml.")
    return None


def kiro_status(cfg, host_id, folders) -> dict:
    """Kiro bridges running on this machine, and why a start failed in one of the window's folders."""
    problem = state_dir_problem(cfg)
    if problem:
        return {"bridges": [], "errors": {folder: problem for folder in folders}}
    # psv_kiro registers the resolved path; report bridges as the window names its folders.
    as_named = {os.path.realpath(folder): folder for folder in folders}
    bridges = [{**{k: v for k, v in e.items() if k != "key"}, "cwd": as_named.get(e["cwd"], e["cwd"])}
               for e in kiro_registry.entries(cfg.state_dir)]
    running = {bridge["cwd"] for bridge in bridges}
    errors = {}
    for folder in folders:
        if folder not in running:
            error = kiro_registry.last_error(cfg.state_dir, kiro_registry.lock_key(host_id, os.path.realpath(folder)))
            if error:
                errors[folder] = error
    return {"bridges": bridges, "errors": errors}


def kiro_start(cfg, host_id, cwd) -> None:
    """Start a Kiro bridge for cwd as its own session, so it outlives VS Code.

    It inherits this process's environment (broker settings, and the launcher's
    PYTHONPATH with the bundled libraries) and logs where the panel looks for errors.
    """
    if state_dir_problem(cfg):
        return
    cwd = os.path.realpath(cwd)  # the same key psv_kiro uses, even through a symlink
    if any(entry["cwd"] == cwd for entry in kiro_registry.entries(cfg.state_dir)):
        return
    entry = Path(__file__).resolve().parents[1] / "psv_kiro.py"
    log_path = cfg.state_dir / f"kiro-{kiro_registry.lock_key(host_id, cwd)}.log"
    cfg.state_dir.mkdir(mode=0o700, parents=True, exist_ok=True)
    try:  # a fresh log (an old failure must not linger), never through a planted symlink
        fd = os.open(log_path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC | os.O_NOFOLLOW, 0o600)
    except OSError:
        return
    with os.fdopen(fd, "wb") as log:
        subprocess.Popen([sys.executable, str(entry), "--cwd", cwd, "--host-id", host_id],
                         stdin=subprocess.DEVNULL, stdout=log, stderr=log, start_new_session=True)


def kiro_stop(cfg, cwd) -> None:
    """SIGTERM the Kiro bridge for cwd; it ends kiro-cli and clears its broker topics."""
    if state_dir_problem(cfg):
        return
    cwd = os.path.realpath(cwd)
    for entry in kiro_registry.entries(cfg.state_dir):
        if entry["cwd"] == cwd and isinstance(entry["pid"], int):
            try:
                os.kill(entry["pid"], signal.SIGTERM)
            except OSError:
                pass


def handle_command(cfg, host_id, line, folders) -> None:
    """One JSON command from the extension; anything malformed is ignored."""
    try:
        command = json.loads(line)
    except ValueError:
        return
    if not isinstance(command, dict):
        return
    if command.get("cmd") == "folders":
        paths = command.get("paths")
        if isinstance(paths, list) and all(isinstance(p, str) for p in paths):
            folders[:] = paths
    elif command.get("cmd") == "kiro_start" and isinstance(command.get("cwd"), str):
        kiro_start(cfg, host_id, command["cwd"])
    elif command.get("cmd") == "kiro_stop" and isinstance(command.get("cwd"), str):
        kiro_stop(cfg, command["cwd"])
    elif command.get("cmd") == "restore" and isinstance(command.get("sid"), str):
        try:
            request(cfg, host_id, "restore", sid=command["sid"])
        except (OSError, ValueError, KeyError, http.client.HTTPException):
            pass


def read_commands(stream, handle, stopped) -> None:
    """Hand each line to handle until EOF, then set stopped (the window left or crashed).

    A command that fails is logged and skipped, so one bad Start can't silence
    every later command or keep the window's lease alive past EOF.
    """
    try:
        for line in stream:
            try:
                handle(line.decode("utf-8", "replace") if isinstance(line, bytes) else line)
            except Exception as error:
                emit("log", message=f"Command failed ({type(error).__name__}).")
    finally:
        stopped.set()


def run_client(cfg, host_id, options, replace=False):
    if not cfg.enabled:
        emit("status", state="disabled", message="Bridge disabled in the shared configuration.")
        return
    stopped = threading.Event()
    folders = []  # the window's workspace folders, sent by the extension

    def stdin_closed():  # JSON commands from the extension, one per line
        read_commands(sys.stdin.buffer, lambda line: handle_command(cfg, host_id, line, folders), stopped)
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
                    status = running_status(state, logfile, kiro_status(cfg, host_id, folders))
                    emit(status.pop("type"), **status)
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
