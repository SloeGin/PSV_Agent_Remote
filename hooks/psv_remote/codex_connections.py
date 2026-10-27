"""Connections to an existing Codex runtime. Neither strategy starts an agent."""

import json
import socket
import struct
import time
from pathlib import Path

from .adapters.codex import ordered_turns, request_key, session_view, COMMAND_APPROVAL, USER_INPUT
from .codex_rpc import DependencyError, NativeError, Peer, RpcError
from .codex_state import Replica, StateError

MAX_FRAME = 4 * 1024 * 1024
IPC_VERSIONS = {"initialize": 0, "thread-owner-discovery": 1,
                "thread-follower-read-model-settings": 1, "thread-follower-start-turn": 2,
                "thread-follower-interrupt-turn": 4,
                "thread-follower-command-approval-decision": 1,
                "thread-follower-submit-user-input": 1}


def _exact(stream, size):
    chunks = bytearray()
    while len(chunks) < size:
        chunk = stream.recv(size - len(chunks))
        if not chunk:
            raise NativeError("Codex socket closed")
        chunks.extend(chunk)
    return bytes(chunks)


def _same_request(state, request):
    key = request_key(request)
    if not any(request_key(current) == key for current in session_view(state).pending):
        raise RpcError("Request has changed or was answered on the computer")


class IPCRouter:
    """Initialized local coordination connection; never claims a thread."""

    def __init__(self, socket_path: Path):
        self.owner = None
        self.client_id = "initializing"
        self.stream = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        try:
            self.stream.settimeout(5)
            self.stream.connect(str(socket_path))
            self.stream.settimeout(None)
        except Exception:
            self.stream.close()
            raise

        def read():
            size = struct.unpack("<I", _exact(self.stream, 4))[0]
            if not 0 < size <= MAX_FRAME:
                raise NativeError("Codex IPC frame exceeds supported size")
            return json.loads(_exact(self.stream, size))

        def write(message):
            payload = json.dumps(message, ensure_ascii=False, allow_nan=False).encode()
            if len(payload) > MAX_FRAME:
                raise NativeError("Codex IPC message too large")
            self.stream.sendall(struct.pack("<I", len(payload)) + payload)

        def close():
            try:
                self.stream.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            self.stream.close()

        self.peer = Peer(read, write, close, ipc=True)
        try:
            response = self._call("initialize", {"clientType": "psv_agent_remote"})
            self.client_id = response["result"]["clientId"]
        except Exception:
            self.peer.close()
            raise

    def _call(self, method, params, timeout=5):
        message = {"type": "request", "sourceClientId": self.client_id,
                   "method": method, "params": params, "version": IPC_VERSIONS[method], "timeoutMs": int(timeout * 1000)}
        if self.owner:
            message["targetClientId"] = self.owner
        return self.peer.call(message, timeout=timeout + 0.5)

    def close(self):
        self.peer.close()


class FrontendIPC(IPCRouter):
    client = "vscode"

    def __init__(self, socket_path: Path, thread_id: str):
        self.thread_id = thread_id
        self.replica = Replica(thread_id)
        super().__init__(socket_path)
        try:
            owner = self._call("thread-owner-discovery", {"hostId": "local", "conversationId": thread_id})
            self.owner = owner["handledByClientId"]
            if not isinstance(self.owner, str) or not self.owner:
                raise NativeError("No live frontend owner for this Codex thread")
            self._follow(True)
            self.snapshot()
        except Exception:
            self.close()
            raise

    def _follow(self, following):
        self.peer.send({"type": "broadcast", "sourceClientId": self.client_id,
                        "version": 1, "method": "thread-stream-following-changed",
                        "targetClientIds": [self.owner], "params": {
                            "conversationId": self.thread_id, "hostId": "local", "following": following}})

    def snapshot(self):
        deadline = time.monotonic() + 5
        recovering = False
        while True:
            for message in self.peer.drain():
                params = message.get("params", {})
                if not isinstance(params, dict):
                    continue
                if (message.get("method") == "client-status-changed"
                        and params.get("clientId") == self.owner and params.get("status") == "disconnected"):
                    raise NativeError("Codex frontend owner disconnected")
                if (message.get("method") != "thread-stream-state-changed"
                        or message.get("sourceClientId") != self.owner
                        or params.get("hostId") != "local"
                        or params.get("conversationId") != self.thread_id):
                    continue
                if message.get("version") != 11:
                    raise NativeError("Unsupported Codex frontend stream version; controls disabled")
                try:
                    self.replica.apply(params["change"])
                except (StateError, KeyError):
                    if not recovering:
                        self._follow(False)
                        self._follow(True)
                        recovering = True
            if self.replica.state is not None:
                return self.replica.state
            if time.monotonic() >= deadline:
                raise NativeError("Codex did not provide a valid state snapshot")
            time.sleep(0.02)

    def respond(self, request, response):
        _same_request(self.snapshot(), request)
        method = request["method"]
        if method == COMMAND_APPROVAL:
            return self._call("thread-follower-command-approval-decision", {
                "conversationId": self.thread_id, "requestId": request["id"], "decision": response["decision"]})
        if method == USER_INPUT:
            return self._call("thread-follower-submit-user-input", {
                "conversationId": self.thread_id, "requestId": request["id"], "response": response})
        raise RpcError("Unsupported native request")

    def start(self, text, message_id):
        state = self.snapshot()
        if session_view(state).state != "idle":
            raise RpcError("Codex is not idle")
        restore = {"id": message_id, "text": text, "cwd": state.get("cwd", ""),
                   "createdAt": int(time.time() * 1000), "context": {
                       "prompt": text, "addedFiles": [], "fileAttachments": [], "imageAttachments": [],
                       "ideContext": None, "workspaceRoots": [state.get("cwd", "")]}}
        response = self._call("thread-follower-start-turn", {
            "conversationId": self.thread_id, "turnStart": {
                "request": {"threadId": self.thread_id, "clientUserMessageId": message_id,
                            "input": [{"type": "text", "text": text, "text_elements": []}]},
                "context": {"restoreMessage": restore, "attachments": [], "inheritThreadSettings": True}}})
        return response["result"]["result"]["turn"]["id"]

    def stop(self, turn_id):
        if session_view(self.snapshot()).active_turn != turn_id:
            return None
        response = self._call("thread-follower-interrupt-turn", {
            "conversationId": self.thread_id, "mode": "user-stop", "expectedTurnId": turn_id})
        return response.get("result", {}).get("interruptedTurnId")

    def close(self):
        if self.owner:
            try:
                self._follow(False)
            except NativeError:
                pass
        self.peer.close()


class AppServerClient:
    """Initialized App Server connection, without loading/subscribing any thread."""

    def __init__(self, socket_path: Path):
        try:
            from websockets.sync.client import unix_connect
            from websockets.exceptions import WebSocketException
        except ImportError as error:
            raise DependencyError('Install the optional Codex dependency: python -m pip install "websockets>=14"') from error
        try:
            stream = unix_connect(str(socket_path), uri="ws://localhost", open_timeout=5, max_size=MAX_FRAME)
        except WebSocketException as error:
            raise NativeError("Could not connect to the existing App Server") from error
        self.peer = Peer(lambda: json.loads(stream.recv()),
                         lambda message: stream.send(json.dumps(message, ensure_ascii=False, allow_nan=False)),
                         stream.close)
        try:
            self.peer.call({"method": "initialize", "params": {
                "clientInfo": {"name": "psv_agent_remote", "version": "0.1.0"},
                "capabilities": {"experimentalApi": True}}})
            self.peer.send({"method": "initialized"})
        except Exception:
            self.peer.close()
            raise

    def _call(self, method, params):
        return self.peer.call({"method": method, "params": params})

    def loaded(self):
        self.peer.drain()
        ids, cursor, seen = [], None, set()
        while True:
            page = self._call("thread/loaded/list", {"cursor": cursor, "limit": 100})
            data = page.get("data")
            if not isinstance(data, list) or any(not isinstance(sid, str) or not sid for sid in data):
                raise NativeError("Invalid loaded thread list")
            ids.extend(data)
            cursor = page.get("nextCursor")
            if cursor is None:
                return ids
            if not isinstance(cursor, str) or not cursor or cursor in seen:
                raise NativeError("Invalid loaded thread pagination")
            seen.add(cursor)

    def close(self):
        self.peer.close()


class AppServer(AppServerClient):
    client = "cli"

    def __init__(self, socket_path: Path, thread_id: str):
        self.thread_id = thread_id
        self.requests = {}
        self.last_fetch = 0
        self.state = {}
        super().__init__(socket_path)
        try:
            if thread_id not in self.loaded():
                raise NativeError("Thread is not loaded in this server; attach to the CLI's existing runtime")
            deadline = time.monotonic() + 5
            while True:
                try:
                    self.metadata = self._call("thread/resume", {"threadId": thread_id, "excludeTurns": True})
                    break
                except RpcError as error:
                    if "is empty" not in str(error) or time.monotonic() >= deadline:
                        raise
                    time.sleep(0.1)
            self.snapshot(force=True)
        except Exception:
            self.close()
            raise

    def snapshot(self, force=False):
        changed = False
        for message in self.peer.drain():
            params = message.get("params", {})
            if not isinstance(params, dict) or params.get("threadId") != self.thread_id:
                continue
            changed = True
            method = message.get("method", "")
            if "id" in message and type(message["id"]) in (str, int):
                self.requests[message["id"]] = message
            if method == "serverRequest/resolved":
                self.requests.pop(params.get("requestId"), None)
            if method == "turn/completed":
                tid = params.get("turn", {}).get("id")
                self.requests = {rid: value for rid, value in self.requests.items()
                                 if value["params"].get("turnId") != tid}
        if force or changed or time.monotonic() - self.last_fetch >= 0.5:
            info = self._call("thread/read", {"threadId": self.thread_id})["thread"]
            page = self._call("thread/turns/list", {"threadId": self.thread_id, "limit": 1,
                                                     "sortDirection": "desc", "itemsView": "full"})
            turns = [{**turn, "turnId": turn["id"]} for turn in reversed(page.get("data", []))]
            self.state = {"id": self.thread_id, "cwd": info.get("cwd", self.metadata.get("cwd", "")),
                          "threadRuntimeStatus": info.get("status", {}), "turns": turns,
                          "requests": list(self.requests.values())}
            self.last_fetch = time.monotonic()
        return self.state

    def respond(self, request, response):
        _same_request(self.snapshot(force=True), request)
        self.peer.send({"id": request["id"], "result": response})

    def start(self, text, message_id):
        if session_view(self.snapshot(force=True)).state != "idle":
            raise RpcError("Codex is not idle")
        result = self._call("turn/start", {"threadId": self.thread_id, "clientUserMessageId": message_id,
                                           "input": [{"type": "text", "text": text}]})
        self.last_fetch = 0
        return result["turn"]["id"]

    def stop(self, turn_id):
        if session_view(self.snapshot(force=True)).active_turn != turn_id:
            return None
        try:
            self._call("turn/interrupt", {"threadId": self.thread_id, "turnId": turn_id})
        except RpcError as error:
            if "no active turn" in str(error).lower():
                return None
            raise
        self.last_fetch = 0
        return turn_id  # bridge waits for native interrupted state before confirming

    def close(self):
        self.peer.close()
