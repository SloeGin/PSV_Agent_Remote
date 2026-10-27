"""Read-only discovery. Stored IDs are candidates, never proof of a live session."""

import sqlite3
import time
from dataclasses import dataclass
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

from .codex_connections import AppServerClient, IPCRouter
from .codex_rpc import NativeError, RpcError, RpcTimeout


@dataclass(frozen=True)
class Route:
    transport: str
    socket_path: Path
    thread_id: str
    owner: str = ""


def candidates(database: Path, cwd: str | None = None) -> list[str]:
    # mode=ro includes the live WAL, cannot create/migrate the database, and
    # avoids reading prompts, titles, transcripts or credentials.
    with sqlite3.connect(database.absolute().as_uri() + "?mode=ro", uri=True, timeout=0.5) as db:
        query = "SELECT id FROM threads WHERE archived=0 AND source IN ('vscode','cli')"
        params = ()
        if cwd is not None:
            query += " AND cwd=?"
            params = (cwd,)
        rows = db.execute(query + " ORDER BY updated_at DESC, id", params)
        return [sid for (sid,) in rows if isinstance(sid, str) and sid]


class IPCDiscovery:
    def __init__(self, path, database, cwd=None, clock=time.monotonic):
        self.path, self.database, self.cwd, self.clock = path, database, cwd, clock
        self.router = IPCRouter(path)
        self.checked, self.owners = {}, {}

    def scan(self):
        self.router.peer.drain()  # discovery never follows conversation streams
        ids = candidates(self.database, self.cwd)
        eligible = set(ids)
        self.checked = {sid: stamp for sid, stamp in self.checked.items() if sid in eligible}
        self.owners = {sid: owner for sid, owner in self.owners.items() if sid in eligible}
        # Bound work per scan while rotating fairly through every candidate.
        # New IDs take priority; already scanned history cannot starve old live tabs.
        queue = sorted(ids, key=lambda sid: self.checked.get(sid, float("-inf")))[:32]
        def discover(sid):
            try:
                response = self.router._call("thread-owner-discovery", {
                    "hostId": "local", "conversationId": sid}, timeout=1)
            except RpcTimeout:
                return sid, None  # read-only probe can be retried; controls cannot
            except RpcError as error:
                if str(error) != "no-client-found":
                    raise
                return sid, None
            else:
                owner = response.get("handledByClientId")
                if not isinstance(owner, str) or not owner:
                    raise NativeError("Invalid frontend owner discovery")
                return sid, owner
        # A missing frontend can take the full discovery timeout. Probe in a
        # bounded pool so one closed tab cannot delay every active conversation.
        with ThreadPoolExecutor(max_workers=8) as pool:
            results = list(pool.map(discover, queue))
        for sid, owner in results:
            if owner is None:
                self.owners.pop(sid, None)
            else:
                self.owners[sid] = owner
            self.checked[sid] = self.clock()
        return [Route("ipc", self.path, sid, self.owners[sid]) for sid in ids if sid in self.owners]

    def close(self):
        self.router.close()


class AppServerDiscovery:
    def __init__(self, path, cwd=None):
        self.path, self.cwd = path, cwd
        self.client = AppServerClient(path)

    def scan(self):
        routes = []
        for sid in self.client.loaded():
            try:
                info = self.client._call("thread/read", {"threadId": sid})["thread"]
            except RpcError:
                continue  # a thread may close between list and read
            if info.get("source") not in ("cli", "vscode"):
                continue  # never surface spawned agents as interactive sessions
            if self.cwd is None or info.get("cwd") == self.cwd:
                routes.append(Route("app-server", self.path, sid))
        return routes

    def close(self):
        self.client.close()
