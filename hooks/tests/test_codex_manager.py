import subprocess
from pathlib import Path

from psv_remote.codex_discovery import Route
from psv_remote.codex_manager import DiscoverySlot, Supervisor, lock_file, session_lock_path
from psv_remote.codex_rpc import NativeError
from psv_remote.config import Config


class Process:
    def __init__(self):
        self.code, self.terminated, self.killed = None, False, False

    def poll(self): return self.code
    def terminate(self): self.terminated, self.code = True, 0
    def kill(self): self.killed, self.code = True, -9
    def wait(self, timeout=None): return self.code


def route(sid="thread", owner="owner", transport="ipc"):
    return Route(transport, Path(transport + ".sock"), sid, owner)


def setup(tmp_path):
    clock, spawned = [0], []
    def spawn(route):
        process = Process()
        spawned.append(process)
        return process
    supervisor = Supervisor(Config(state_dir=tmp_path), "host", clock=lambda: clock[0], spawn=spawn)
    return supervisor, spawned, clock


def test_one_worker_per_thread_and_unchanged_discovery_is_idempotent(tmp_path):
    manager, spawned, _ = setup(tmp_path)
    manager.reconcile([route(transport="app-server"), route()])
    manager.reconcile([route(), route(transport="app-server")])
    assert len(spawned) == 1 and manager.workers["thread"].route.transport == "ipc"
    manager.close()
    assert spawned[0].terminated


def test_owner_change_and_closed_thread_stop_only_owned_children(tmp_path):
    manager, spawned, _ = setup(tmp_path)
    manager.reconcile([route()])
    manager.reconcile([route(owner="new-owner")])
    assert len(spawned) == 2 and spawned[0].terminated
    manager.reconcile([])
    assert spawned[1].terminated and not manager.workers


def test_external_manual_bridge_lock_prevents_duplicate_and_is_not_killed(tmp_path):
    manager, spawned, _ = setup(tmp_path)
    external = lock_file(session_lock_path(manager.cfg, "host", "thread"))
    try:
        manager.reconcile([route()])
        assert spawned == []
        manager.close()
        assert lock_file(session_lock_path(manager.cfg, "host", "thread")) is None
    finally:
        external.close()
    manager.reconcile([route()])
    assert len(spawned) == 1
    manager.close()


def test_failed_worker_reconnects_with_backoff_not_a_tight_loop(tmp_path):
    manager, spawned, clock = setup(tmp_path)
    manager.reconcile([route()])
    spawned[0].code = 1
    manager.reconcile([route()])
    manager.reconcile([route()])
    assert len(spawned) == 1
    clock[0] = 3
    manager.reconcile([route()])
    assert len(spawned) == 2
    manager.close()


def test_session_limit_and_shutdown_escalation(tmp_path):
    manager, spawned, _ = setup(tmp_path)
    manager.limit = 2
    manager.reconcile([route(str(i)) for i in range(4)])
    assert len(spawned) == 2
    calls = []
    def wait(timeout=None):
        calls.append(timeout)
        if timeout == 10: raise subprocess.TimeoutExpired("bridge", 10)
    spawned[0].wait = wait
    manager.close()
    assert spawned[0].killed and calls == [10, 3]


def test_discovery_disconnect_isolated_backoff_and_reconnect():
    clock, clients = [0], []
    class Client:
        def __init__(self): self.closed = False
        def scan(self):
            if len(clients) == 1: raise NativeError("disconnected")
            return [route()]
        def close(self): self.closed = True
    def factory():
        client = Client()
        clients.append(client)
        return client
    slot = DiscoverySlot(factory, clock=lambda: clock[0])
    assert slot.scan() == [] and clients[0].closed
    assert slot.scan() == [] and len(clients) == 1
    clock[0] = 3
    assert slot.scan() == [route()] and len(clients) == 2
    slot.close()
