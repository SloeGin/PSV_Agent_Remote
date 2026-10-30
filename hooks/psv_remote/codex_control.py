"""Local manager health/control and expiring VS Code window leases."""

import hashlib
import hmac
import http.client
import json
import os
import secrets
import tempfile
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


def control_path(cfg, host_id):
    digest = hashlib.sha256(host_id.encode()).hexdigest()[:16]
    return cfg.state_dir / f"codex-{digest}.watch.json"


def configuration_key(cfg, host_id, options):
    data = {"host": host_id, "broker": [cfg.broker_host, cfg.broker_port],
            "timeout": cfg.wait_timeout_s, **options}
    return hashlib.sha256(json.dumps(data, sort_keys=True, default=str).encode()).hexdigest()


def configuration_summary(cfg, host_id, options) -> dict[str, str]:
    """The settings configuration_key() hashes, readable, for explaining a mismatch."""
    return {
        "broker": f"{cfg.broker_host}:{cfg.broker_port}",
        "host ID": host_id,
        "approval timeout": f"{cfg.wait_timeout_s:g} s",
        "IPC socket": str(options["ipc_path"]) if options.get("ipc_path") is not None else "off",
        "state database": str(options["database"]) if options.get("database") is not None else "default",
        "App Server sockets": ", ".join(str(p) for p in options.get("app_paths") or []) or "none",
        "workspace": str(options["cwd"]) if options.get("cwd") is not None else "any",
        "max sessions": str(options.get("limit")),
    }


class Leases:
    def __init__(self, managed=False, clock=time.monotonic):
        self.managed, self.clock = managed, clock
        self.clients = {}
        self.empty_since = clock()

    def renew(self, client):
        self.clients[client] = self.clock()
        self.empty_since = None

    def release(self, client):
        self.clients.pop(client, None)
        if not self.clients and self.empty_since is None:
            self.empty_since = self.clock()

    def expired(self):
        now = self.clock()
        for client, stamp in list(self.clients.items()):
            if now - stamp > 15:
                self.release(client)
        return self.managed and self.empty_since is not None and now - self.empty_since > 8


class ManagerControl:
    def __init__(self, cfg, host_id, key, managed=False, settings=None, restore=None):
        self.path = control_path(cfg, host_id)
        self.settings = settings
        self.restore = restore  # restore(sid) -> bool: "Show on Vita" for a removed session
        self.panel = {}
        self.token, self.run_id = secrets.token_hex(32), secrets.token_hex(16)
        self.key = key
        self.stop = threading.Event()
        self.lock = threading.Lock()
        self.leases = Leases(managed)
        self.bridges, self.discovered = 0, 0
        self.broker = None
        self.mode = "extension" if managed else "persistent"
        control = self

        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *args): pass

            def do_GET(self): self.respond()
            def do_POST(self): self.respond()

            def respond(self):
                self.connection.settimeout(2)
                if not hmac.compare_digest(self.headers.get("Authorization", ""), "Bearer " + control.token):
                    self.send_error(403)
                    return
                try:
                    size = int(self.headers.get("Content-Length", "0"))
                    if not 0 <= size <= 4096: raise ValueError("size")
                    data = json.loads(self.rfile.read(size)) if size else {}
                    if data.get("run_id") != control.run_id: raise ValueError("identity")
                    with control.lock:
                        client = data.get("client")
                        if self.path in ("/lease", "/release"):
                            if not isinstance(client, str) or not 1 <= len(client) <= 64: raise ValueError("client")
                            if self.path == "/lease": control.leases.renew(client)
                            else: control.leases.release(client)
                        elif self.path == "/restore" and self.command == "POST":
                            sid = data.get("sid")
                            if not isinstance(sid, str) or control.restore is None or not control.restore(sid):
                                raise ValueError("restore")
                        elif self.path == "/stop" and self.command == "POST":
                            if control.mode != "extension": raise ValueError("persistent manager")
                            control.stop.set()
                        elif self.path != "/state": raise ValueError("method")
                        payload = json.dumps(control.state()).encode()
                    self.send_response(200)
                    self.send_header("Content-Type", "application/json")
                    self.send_header("Content-Length", str(len(payload)))
                    self.end_headers()
                    self.wfile.write(payload)
                except (ValueError, TypeError, OSError):
                    self.send_error(400)

        self.server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.server.daemon_threads = True
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        descriptor = {"port": self.server.server_port, "token": self.token, "run_id": self.run_id}
        cfg.state_dir.mkdir(parents=True, exist_ok=True)
        try:
            fd, name = tempfile.mkstemp(prefix=".codex-control-", dir=cfg.state_dir)
            try:
                with os.fdopen(fd, "w") as stream:
                    json.dump(descriptor, stream)
                os.replace(name, self.path)  # mkstemp is private (0600)
            finally:
                if os.path.exists(name): os.unlink(name)
        except Exception:
            self.server.shutdown()
            self.server.server_close()
            raise

    def state(self):
        return {"protocol": 1, "run_id": self.run_id, "pid": os.getpid(), "mode": self.mode,
                "configuration": self.key, "bridges": self.bridges, "discovered": self.discovered,
                "windows": len(self.leases.clients),
                **({"broker": self.broker} if self.broker is not None else {}),
                **self.panel,
                **({"settings": self.settings} if self.settings is not None else {})}

    def update(self, bridges, discovered, broker=None, panel=None):
        with self.lock:
            self.bridges, self.discovered = bridges, discovered
            if broker is not None:
                self.broker = broker
            if panel is not None:
                self.panel = panel  # vita, sessions, removed: for the VS Code panel

    def should_stop(self):
        with self.lock:
            return self.stop.is_set() or self.leases.expired()

    def close(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=2)
        self.path.unlink(missing_ok=True)


def request(cfg, host_id, action="state", client=None):
    descriptor = json.loads(control_path(cfg, host_id).read_text())
    connection = http.client.HTTPConnection("127.0.0.1", descriptor["port"], timeout=2)
    try:
        payload = {"run_id": descriptor["run_id"], "client": client}
        connection.request("POST", "/" + action, json.dumps(payload), {
            "Authorization": "Bearer " + descriptor["token"], "Content-Type": "application/json"})
        response = connection.getresponse()
        data = response.read(4096)
        if response.status != 200: raise ValueError("Manager rejected control request")
        state = json.loads(data)
        if state.get("protocol") != 1 or state.get("run_id") != descriptor["run_id"]:
            raise ValueError("Incompatible manager")
        return state
    finally:
        connection.close()
