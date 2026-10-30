"""Supervise one bridge per live native thread; discovery never sends controls."""

import fcntl
import hashlib
import sqlite3
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path

from .codex_discovery import AppServerDiscovery, IPCDiscovery
from .codex_control import ManagerControl, configuration_key
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
    try:
        control = ManagerControl(cfg, host_id, configuration_key(cfg, host_id, options), lease_managed)
        while not control.should_stop():
            routes = [route for slot in slots for route in slot.scan()]
            supervisor.reconcile(routes)
            current = len(supervisor.workers)
            control.update(current, len({route.thread_id for route in routes}))
            if current != count:
                print(f"Codex manager: {current} bridge(s) running.", flush=True)
                count = current
            control.stop.wait(2)
    finally:
        try:
            supervisor.close()
            for slot in slots:
                slot.close()
        finally:
            if control is not None:
                control.close()
            lock.close()
