"""Supervise one bridge per live native thread; discovery never sends controls."""

import fcntl
import hashlib
import json
import os
import sqlite3
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path

from .codex_discovery import AppServerDiscovery, IPCDiscovery
from .bus import BusError, open_bus
from .codex_control import ManagerControl, configuration_key, configuration_summary
from .config import DEFAULT_PATH, LEGACY_PATH
from .pending import _safe
from .rpc import DependencyError, NativeError
from .remote import scoped_session_id


def session_lock_path(cfg, host_id, thread_id):
    return cfg.state_dir / f"{scoped_session_id('codex', host_id, thread_id)}.bridge.lock"


def lock_file(path):
    path.parent.mkdir(parents=True, exist_ok=True)
    handle = path.open("a")
    try:
        fcntl.flock(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError:
        handle.close()
        return None
    return handle


@dataclass
class Worker:
    route: object
    process: object


class Supervisor:
    def __init__(self, cfg, host_id, limit=16, clock=time.monotonic, spawn=None):
        self.cfg, self.host_id, self.limit, self.clock = cfg, host_id, limit, clock
        self.spawn = spawn or self._spawn
        self.workers, self.retry_at, self.failures = {}, {}, {}

    def _spawn(self, route):
        entry = Path(__file__).resolve().parents[1] / "psv_codex.py"
        args = [sys.executable, str(entry), route.transport, "--socket", str(route.socket_path),
                "--thread", route.thread_id, "--host-id", self.host_id]
        if route.transport == "ipc":
            args.append("--experimental-ipc")
        sid = scoped_session_id("codex", self.host_id, route.thread_id)
        with (self.cfg.state_dir / f"{sid}.bridge.log").open("ab") as log:
            return subprocess.Popen(args, stdin=subprocess.DEVNULL, stdout=log, stderr=log)

    @staticmethod
    def _stop(worker):
        if worker.process.poll() is None:
            worker.process.terminate()
        try:
            worker.process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            worker.process.kill()
            worker.process.wait(timeout=3)

    def reconcile(self, routes):
        # Frontend IPC wins when a shared server also reports the same native ID.
        # Broker identity is agent+host+thread, not the discovery endpoint.
        desired = {}
        for route in sorted(routes, key=lambda route: route.transport != "ipc"):
            desired.setdefault(route.thread_id, route)
        desired = dict(list(desired.items())[:self.limit])
        for sid, worker in list(self.workers.items()):
            if desired.get(sid) != worker.route:
                self._stop(worker)
                del self.workers[sid]
            elif worker.process.poll() is not None:
                worker.process.wait()
                del self.workers[sid]
                self.failures[sid] = min(self.failures.get(sid, 0) + 1, 5)
                self.retry_at[sid] = self.clock() + 2 ** self.failures[sid]
        for sid in list(self.retry_at):
            if sid not in desired:
                self.retry_at.pop(sid, None)
                self.failures.pop(sid, None)
        for sid, route in desired.items():
            if sid in self.workers or self.clock() < self.retry_at.get(sid, 0):
                continue
            # A manual bridge can coexist with the manager. Never terminate or
            # clear the retained state of a bridge this supervisor did not start.
            available = lock_file(session_lock_path(self.cfg, self.host_id, sid))
            if available is None:
                continue
            available.close()  # the child acquires the same lock before native attach
            try:
                self.workers[sid] = Worker(route, self.spawn(route))
            except OSError:
                self.retry_at[sid] = self.clock() + 5

    def close(self):
        for sid, worker in list(self.workers.items()):
            self._stop(worker)
            del self.workers[sid]


def broker_configured(env=os.environ) -> bool:
    """False when this machine has no broker setting at all (so the 127.0.0.1 default is a guess)."""
    return "PSV_REMOTE_HOST" in env or DEFAULT_PATH.exists() or LEGACY_PATH.exists()


def broker_problem(cfg, configured: bool, error: str) -> str:
    where = f"{cfg.broker_host}:{cfg.broker_port}"
    if not configured:
        return (f"Can't reach the MQTT broker at {where}: no broker is set on this machine. Set Broker Host for "
                "this host (in a Remote SSH window, in its Remote settings tab), or create "
                "~/.config/psv-agent-remote/config.toml.")
    return f"Can't reach the MQTT broker at {where} ({error}). Check that it is running and reachable from this machine."


class BrokerProbe:
    """Checks the broker from the manager itself: bridges report failures only in their own logs."""

    OK_EVERY_S, FAILING_EVERY_S = 60, 10

    def __init__(self, connect, cfg, configured: bool, clock=time.monotonic):
        self.connect, self.cfg, self.configured, self.clock = connect, cfg, configured, clock
        self.reachable: bool | None = None
        self.message = ""
        self.next_at = 0.0

    def poll(self) -> bool:
        """Check if due; True when reachability changed (so it is worth logging)."""
        if self.clock() < self.next_at:
            return False
        before = self.reachable
        try:
            self.connect().close()
            self.reachable, self.message = True, ""
        except BusError as error:
            self.reachable = False
            self.message = broker_problem(self.cfg, self.configured, str(error).removeprefix("connect failed: "))
        self.next_at = self.clock() + (self.OK_EVERY_S if self.reachable else self.FAILING_EVERY_S)
        return self.reachable != before

    def state(self) -> dict | None:
        return None if self.reachable is None else {"reachable": self.reachable, "message": self.message}


SESSIONS_MAX = 32
_STATE_ORDER = {"waiting": 0, "running": 1, "idle": 2}


class BrokerWatch:
    """What the Vita sees, for the VS Code panel: every session's status, and the
    Vita's own online/mode flags. Fed from one broker subscription."""

    RETRY_S = 10

    def __init__(self, bus_factory, clock=time.monotonic):
        self.bus_factory, self.clock = bus_factory, clock
        self.bus, self.retry_at = None, 0.0
        self.down = False  # the last connect failed or the connection dropped
        self.sessions: dict[str, dict] = {}
        self.projects: dict[str, str] = {}  # last known, kept after a session is cleared
        self.online: bool | None = None
        self.mode: str | None = None

    def feed(self, topic: str, payload: bytes) -> None:
        if topic == "claude/psv/online":
            self.online = payload == b"1"
        elif topic == "claude/psv/mode":
            self.mode = payload.decode("utf-8", "replace") if payload in (b"sofa", b"desk") else None
        elif topic.startswith("claude/session/") and topic.endswith("/status"):
            sid = topic[len("claude/session/"):-len("/status")]
            if not payload:
                self.sessions.pop(sid, None)
                return
            try:
                data = json.loads(payload)
            except (ValueError, UnicodeDecodeError):
                return
            if not isinstance(data, dict):
                return
            project = str(data.get("project") or "?")
            self.projects[sid] = project
            self.sessions[sid] = {"sid": sid, "agent": str(data.get("agent") or "claude"),
                                  "project": project, "state": str(data.get("state") or "unknown")}

    def project_of(self, sid: str) -> str | None:
        return self.projects.get(sid)

    def poll(self) -> None:
        """Connect when due, then drain what arrived; never blocks for long."""
        if self.bus is None:
            if self.clock() < self.retry_at:
                return
            try:
                self.bus = self.bus_factory()
                for topic in ("claude/session/+/status", "claude/psv/online", "claude/psv/mode"):
                    self.bus.subscribe(topic)
                self.down = False
            except BusError:
                self._drop()
                return
        try:
            for _ in range(500):
                message = self.bus.next_message(0.01)
                if message is None:
                    break
                self.feed(*message)
        except BusError:
            self._drop()

    def _drop(self) -> None:
        if self.bus is not None:
            try:
                self.bus.close()
            except Exception:
                pass
        self.bus, self.retry_at, self.down = None, self.clock() + self.RETRY_S, True
        self.sessions.clear()  # retained values come again on the next subscription

    def snapshot(self, local_sids: set[str]) -> dict:
        listed = sorted(self.sessions.values(),
                        key=lambda s: (_STATE_ORDER.get(s["state"], 3), s["project"].lower(), s["sid"]))
        sessions = [{**s, "local": s["sid"] in local_sids} for s in listed[:SESSIONS_MAX]]
        return {"vita": {"online": self.online, "mode": self.mode},
                "sessions": None if self.down else sessions}

    def close(self) -> None:
        self._drop()


def removed_sessions(state_dir: Path, running: set[str], watch: BrokerWatch) -> list[dict]:
    """Sessions this manager bridges that were removed on the Vita (only those can be restored)."""
    return [{"sid": sid, "project": watch.project_of(sid) or "Codex session"}
            for sid in sorted(running) if (Path(state_dir) / f"{_safe(sid)}.dismissed").exists()]


def bridged_sessions(host_id: str, workers) -> set[str]:
    """Broker session ids of the running bridges (Supervisor.workers is keyed by thread id)."""
    return {scoped_session_id("codex", host_id, thread_id) for thread_id in list(workers)}


def make_restore(cfg, host_id: str, workers):
    """"Show on Vita": delete a running bridge's dismissal; its next tick republishes."""
    def restore(sid) -> bool:
        if sid not in bridged_sessions(host_id, workers):
            return False
        (Path(cfg.state_dir) / f"{_safe(sid)}.dismissed").unlink(missing_ok=True)
        return True
    return restore


class DiscoverySlot:
    def __init__(self, factory, clock=time.monotonic):
        self.factory, self.clock = factory, clock
        self.client, self.retry_at, self.failures = None, 0, 0

    def scan(self):
        if self.clock() < self.retry_at:
            return []
        try:
            if self.client is None:
                self.client = self.factory()
            routes = self.client.scan()
            self.failures = 0
            return routes
        except DependencyError:
            self.close()
            raise
        except (OSError, NativeError, sqlite3.Error, KeyError, ValueError) as error:
            self.close()
            self.failures = min(self.failures + 1, 5)
            self.retry_at = self.clock() + 2 ** self.failures
            print(f"Codex discovery unavailable ({type(error).__name__}); retrying.", file=sys.stderr, flush=True)
            return []

    def close(self):
        if self.client is not None:
            try:
                self.client.close()
            except OSError:
                pass
            self.client = None


def watch(cfg, host_id, *, ipc_path=None, database=None, app_paths=(), cwd=None, limit=16, lease_managed=False):
    digest = hashlib.sha256(host_id.encode()).hexdigest()[:16]
    lock = lock_file(cfg.state_dir / f"codex-{digest}.watch.lock")
    if lock is None:
        raise NativeError("A Codex manager for this host is already running")
    slots = []
    if ipc_path is not None:
        slots.append(DiscoverySlot(lambda: IPCDiscovery(ipc_path, database, cwd)))
    for path in dict.fromkeys(app_paths):
        slots.append(DiscoverySlot(lambda path=path: AppServerDiscovery(path, cwd)))
    supervisor = Supervisor(cfg, host_id, limit)
    options = dict(ipc_path=ipc_path, database=database, app_paths=list(app_paths), cwd=cwd, limit=limit)
    control = None
    count = None
    probe = BrokerProbe(lambda: open_bus(cfg), cfg, broker_configured())
    watch = BrokerWatch(lambda: open_bus(cfg))

    restore = make_restore(cfg, host_id, supervisor.workers)
    try:
        control = ManagerControl(cfg, host_id, configuration_key(cfg, host_id, options), lease_managed,
                                 configuration_summary(cfg, host_id, options), restore)
        while not control.should_stop():
            routes = [route for slot in slots for route in slot.scan()]
            supervisor.reconcile(routes)
            current = len(supervisor.workers)
            if probe.poll():
                print(probe.message or f"MQTT broker reachable at {cfg.broker_host}:{cfg.broker_port}.", flush=True)
            watch.poll()
            running = bridged_sessions(host_id, supervisor.workers)
            panel = {**watch.snapshot(running), "removed": removed_sessions(cfg.state_dir, running, watch)}
            control.update(current, len({route.thread_id for route in routes}), broker=probe.state(), panel=panel)
            if current != count:
                print(f"Codex manager: {current} bridge(s) running.", flush=True)
                count = current
            control.stop.wait(2)
    finally:
        try:
            watch.close()
            supervisor.close()
            for slot in slots:
                slot.close()
        finally:
            if control is not None:
                control.close()
            lock.close()
