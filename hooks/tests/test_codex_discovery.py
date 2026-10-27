import sqlite3
from pathlib import Path

import pytest

from psv_remote import codex_discovery as discovery
from psv_remote.codex_connections import AppServerClient
from psv_remote.codex_rpc import NativeError, RpcError


@pytest.fixture
def database(tmp_path):
    path = tmp_path / "index.sqlite"
    with sqlite3.connect(path) as db:
        db.execute("CREATE TABLE threads(id TEXT, source TEXT, cwd TEXT, archived INT, updated_at INT)")
        db.executemany("INSERT INTO threads VALUES(?,?,?,?,?)", [
            ("active", "vscode", "/repo", 0, 3), ("closed", "cli", "/repo", 0, 2),
            ("archived", "vscode", "/repo", 1, 9), ("subagent", '{"subagent":{}}', "/repo", 0, 10),
            ("other", "vscode", "/other", 0, 1)])
    return path


def test_index_only_supplies_interactive_unarchived_candidates(database, tmp_path):
    assert discovery.candidates(database) == ["active", "closed", "other"]
    assert discovery.candidates(database, "/repo") == ["active", "closed"]
    absent = tmp_path / "absent.sqlite"
    with pytest.raises(sqlite3.Error):
        discovery.candidates(absent)
    assert not absent.exists()


class Peer:
    def drain(self): return []


class Router:
    def __init__(self, path):
        self.peer, self.owners, self.calls = Peer(), {"active": "owner"}, []

    def _call(self, method, params, timeout=5):
        assert method == "thread-owner-discovery" and params["hostId"] == "local"
        self.calls.append(params["conversationId"])
        if params["conversationId"] not in self.owners:
            raise RpcError("no-client-found")
        return {"handledByClientId": self.owners[params["conversationId"]]}

    def close(self): pass


def test_ipc_index_history_is_never_promoted_without_live_owner(monkeypatch, database):
    monkeypatch.setattr(discovery, "IPCRouter", Router)
    client = discovery.IPCDiscovery(Path("ipc.sock"), database)
    assert [route.thread_id for route in client.scan()] == ["active"]
    client.router.owners = {"active": "new-owner", "closed": "other-owner"}
    assert [r.owner for r in client.scan()] == ["new-owner", "other-owner"]
    client.router.owners = {}
    assert client.scan() == []


def test_candidate_rotation_does_not_starve_old_live_thread(monkeypatch):
    monkeypatch.setattr(discovery, "IPCRouter", Router)
    ids = [str(i) for i in range(70)]
    monkeypatch.setattr(discovery, "candidates", lambda *args: ids)
    clock = [0]
    client = discovery.IPCDiscovery(Path("ipc.sock"), Path("db"), clock=lambda: clock[0])
    client.router.owners = {"69": "owner"}
    for _ in range(3):
        routes = client.scan()
        clock[0] += 2
    assert [r.thread_id for r in routes] == ["69"]
    assert set(client.router.calls) == set(ids)


def test_ipc_protocol_error_is_not_an_authoritative_empty_result(monkeypatch, database):
    monkeypatch.setattr(discovery, "IPCRouter", Router)
    client = discovery.IPCDiscovery(Path("ipc.sock"), database)
    def fail(*args, **kwargs): raise RpcError("incompatible-version")
    client.router._call = fail
    with pytest.raises(RpcError): client.scan()


def test_app_server_filters_children_and_cwd_without_resuming(monkeypatch):
    class Client:
        def __init__(self, path): self.calls = []
        def loaded(self): return ["cli", "child", "other", "closed"]
        def _call(self, method, params):
            self.calls.append(method)
            assert method == "thread/read"
            sid = params["threadId"]
            if sid == "closed": raise RpcError("gone")
            return {"thread": {"source": "cli" if sid != "child" else {"subagent": {}},
                               "cwd": "/other" if sid == "other" else "/repo"}}
    monkeypatch.setattr(discovery, "AppServerClient", Client)
    client = discovery.AppServerDiscovery(Path("cli.sock"), "/repo")
    assert [r.thread_id for r in client.scan()] == ["cli"]
    assert client.client.calls == ["thread/read"] * 4


def test_loaded_threads_pagination_and_malformed_cursor():
    client = AppServerClient.__new__(AppServerClient)
    client.peer = Peer()
    pages = iter([{"data": ["one"], "nextCursor": "next"}, {"data": ["two"], "nextCursor": None}])
    client._call = lambda method, params: next(pages)
    assert client.loaded() == ["one", "two"]
    client._call = lambda method, params: {"data": ["one"], "nextCursor": "repeat"}
    with pytest.raises(NativeError): client.loaded()
