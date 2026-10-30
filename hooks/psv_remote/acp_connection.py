"""Connection to a Kiro agent over ACP (Agent Client Protocol).

Unlike the Codex strategies, which attach to a conversation another frontend
already owns, this spawns `kiro-cli acp --agent-engine=v3` and OWNS the session
it creates with session/new. It maintains a local replica from the session/update
stream and the pending session/request_permission requests, and exposes the same
native interface the bridge expects of a Codex connection:

    .client                       a short client tag for the session status
    .snapshot()                   current Kiro-shaped state dict (for the adapter)
    .start(text, message_id)      idle reply: start a turn, return its turn id
    .steer(text, message_id)      busy reply: steer the active turn
    .stop(turn_id)                interrupt the active turn, return its turn id
    .respond(request, response)   answer a permission request with an ACP outcome
    .close()                      terminate the owned agent process

The Kiro-shaped snapshot is translated by adapters/kiro.py; this module keeps no
Codex or agent-native approval semantics of its own.
"""

import json
import os
import signal
import subprocess
import tempfile
import time
from pathlib import Path

from .codex_rpc import NativeError, Peer, RpcError

# ACP method names. session/* are standard; _kiro/* are negotiated extensions.
M_INITIALIZE = "initialize"
M_SESSION_NEW = "session/new"
M_SESSION_PROMPT = "session/prompt"
M_SESSION_CANCEL = "session/cancel"
M_SESSION_UPDATE = "session/update"
M_REQUEST_PERMISSION = "session/request_permission"
M_STEER = "_session/steer"
M_GET_ACCESS_TOKEN = "_kiro/auth/getAccessToken"

PROTOCOL_VERSION = 1
CLIENT_INFO = {"name": "psv_agent_remote", "version": "0.1.0"}
START_TIMEOUT_S = 20.0
MAX_LINE = 8 * 1024 * 1024
STDERR_TAIL = 64 * 1024  # enough of kiro-cli's stderr to classify a startup failure

# A few clean startup-failure phrases, matched to a short reason without echoing
# raw stderr (which can carry machine-specific diagnostics).
_STARTUP_SIGNATURES = (
    ("not logged in", "Kiro CLI is not logged in; run `kiro-cli login`"),
    ("please log in", "Kiro CLI is not logged in; run `kiro-cli login`"),
    ("unexpected argument", "This Kiro CLI does not accept the ACP V3 flags"),
    ("unrecognized", "This Kiro CLI does not accept the ACP V3 flags"),
)


def _classify_startup(stderr: bytes) -> str | None:
    text = stderr.decode("utf-8", errors="ignore").lower()
    for phrase, reason in _STARTUP_SIGNATURES:
        if phrase in text:
            return reason
    return None


class AcpConnection:
    """One spawned `kiro-cli acp` agent with a single owned session."""

    client = "cli"  # Kiro CLI is a local CLI agent

    def __init__(self, cfg, cwd: str, kiro_cli: str = "kiro-cli", engine: str = "v3",
                 agent: str | None = None):
        """Spawn `kiro-cli acp` and create a session it owns (session/new).

        Attaching to an existing IDE session is deliberately not supported: the
        IDE does not stream its turns or approvals to a second ACP client. See
        docs/kiro-integration.md.
        """
        self.cfg = cfg
        self.cwd = str(Path(cwd).resolve()) if cwd else os.getcwd()
        self.session_id: str | None = None
        self.mode_id: str | None = None
        # Replica of the current turn, built from session/update notifications.
        self._turn_items: list[dict] = []
        self._turn_id: str | None = None
        self._turn_active = False
        self._prompt_key: str | None = None
        self._prompt_inbox = None
        # Text echoed locally for the current turn, to drop a duplicate
        # user_message_chunk if Kiro ever streams one for the same message.
        self._echoed_user: str | None = None
        # Pending agent-to-client permission requests, keyed by JSON-RPC id.
        self._permissions: dict = {}
        self._process = self._spawn(kiro_cli, engine, agent)
        try:
            self.peer = self._make_peer()
            self._initialize()
            self._new_session()
        except BaseException:
            self.close()
            raise

    # ---- process / transport ----

    def _spawn(self, kiro_cli: str, engine: str, agent: str | None) -> subprocess.Popen:
        command = [kiro_cli, "acp"]
        if engine == "v3":
            command.append("--agent-engine=v3")
        elif agent:
            command += ["--agent", agent]
        # stderr goes to an unlinked temp file: a pipe nobody reads fills up during a
        # long session and blocks kiro-cli. Only startup failures read it back.
        self._stderr = tempfile.TemporaryFile()
        try:
            return subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                    stderr=self._stderr)
        except FileNotFoundError as error:
            self._stderr.close()
            raise NativeError("Kiro CLI not found; install it or set the Python path") from error

    def _make_peer(self) -> Peer:
        out, inp = self._process.stdout, self._process.stdin
        buffer = bytearray()

        def read():
            nonlocal buffer
            while True:
                index = buffer.find(b"\n")
                if index >= 0:
                    line = bytes(buffer[:index])
                    del buffer[:index + 1]
                    if line.strip():
                        return json.loads(line)
                    continue
                chunk = out.read1(65536) if hasattr(out, "read1") else out.read(65536)
                if not chunk:
                    raise NativeError("Kiro ACP agent closed the connection")
                buffer.extend(chunk)
                if len(buffer) > MAX_LINE:
                    raise NativeError("Kiro ACP line exceeds supported size")

        def write(message):
            inp.write(json.dumps(message, ensure_ascii=False, allow_nan=False).encode() + b"\n")
            inp.flush()

        def close():
            for stream in (inp, out):
                try:
                    stream.close()
                except OSError:
                    pass

        return Peer(read, write, close)

    def _start_error(self, fallback: str) -> NativeError:
        """Classify a startup failure from the child's stderr, after it exits."""
        self._process.terminate()
        try:
            self._process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            self._process.kill()
            self._process.wait()
        try:
            self._stderr.seek(0, os.SEEK_END)
            self._stderr.seek(max(0, self._stderr.tell() - STDERR_TAIL))
            stderr = self._stderr.read()
        except (OSError, ValueError, AttributeError):
            stderr = b""
        return NativeError(_classify_startup(stderr or b"") or fallback)

    # ---- handshake ----

    def _initialize(self):
        try:
            self.peer.call({"method": M_INITIALIZE, "params": {
                "protocolVersion": PROTOCOL_VERSION,
                "clientCapabilities": {"fs": {"readTextFile": False, "writeTextFile": False},
                                       "terminal": False},
                "clientInfo": CLIENT_INFO,
            }}, timeout=START_TIMEOUT_S)
        except NativeError as error:
            raise self._start_error("Kiro ACP agent did not complete initialize") from error

    def _new_session(self):
        result = self.peer.call({"method": M_SESSION_NEW, "params": {
            "cwd": self.cwd, "mcpServers": [],
        }}, timeout=START_TIMEOUT_S)
        session_id = result.get("sessionId")
        if not isinstance(session_id, str) or not session_id:
            raise NativeError("Kiro did not return a session id")
        self.session_id = session_id
        modes = result.get("modes")
        if isinstance(modes, dict):
            self.mode_id = modes.get("currentModeId")

    # ---- event pump ----

    def _pump(self):
        """Drain agent-to-client traffic and settle a finished prompt.

        session/update notifications update the replica. Agent-to-client requests
        we do not act on (auth) are refused with a JSON-RPC error so the agent is
        never left waiting; permission requests are recorded for the bridge to
        answer through respond(). The session/prompt request settles only at turn
        end, so its response is polled here, not awaited.
        """
        for message in self.peer.drain():
            method = message.get("method")
            if method is None:
                continue  # a late/unmatched response; prompt responses go via poll
            params = message.get("params") or {}
            if "id" in message:
                self._on_request(message["id"], method, params)
            elif method == M_SESSION_UPDATE:
                self._apply_update(params)
        self._settle_prompt()

    def _settle_prompt(self):
        if self._prompt_key is None:
            return
        try:
            result = self.peer.poll(self._prompt_key, self._prompt_inbox)
        except NativeError:
            self._prompt_key = self._prompt_inbox = None
            self._turn_active = False
            return
        if result is None:
            return  # turn still running
        # The prompt returned: the turn is over. stopReason is informational.
        self._prompt_key = self._prompt_inbox = None
        self._turn_active = False
        self._cancel_permissions()  # a request can't outlive its turn

    def _cancel_permissions(self):
        """Answer every pending permission request as cancelled (ACP requires it after
        session/cancel) so no stale approval sheet or "waiting" state is left behind."""
        for request_id in list(self._permissions):
            self.peer.send({"id": request_id, "result": {"outcome": {"outcome": "cancelled"}}})
        self._permissions.clear()

    def _on_request(self, request_id, method, params):
        if method == M_REQUEST_PERMISSION and params.get("sessionId") == self.session_id:
            # Record it; the bridge answers via respond(). Do not reply yet.
            self._permissions[request_id] = params
            return
        # Any other agent-to-client request (for example auth) is declined so the
        # agent does not hang. The CLI owns auth by default in V3.
        self.peer.send({"id": request_id,
                        "error": {"code": -32601, "message": "unsupported client request"}})

    def _apply_update(self, params):
        if params.get("sessionId") != self.session_id:
            return
        update = params.get("update")
        if not isinstance(update, dict):
            return
        kind = update.get("sessionUpdate")
        if kind == "agent_message_chunk":
            self._append_text("assistant", update.get("content"))
        elif kind == "user_message_chunk":
            text = _content_text(update.get("content"))
            # Drop a streamed/replayed echo of the message we already added locally.
            if self._echoed_user is not None and text and self._echoed_user.startswith(text):
                self._echoed_user = self._echoed_user[len(text):] or None
                return
            self._append_text("you", text)
        elif kind == "tool_call":
            title = update.get("title")
            if isinstance(title, str) and title:
                self._turn_items.append({"k": "tool", "t": title})
        elif kind == "session_info_update":
            self._apply_lifecycle(update)

    def _apply_lifecycle(self, update):
        meta = ((update.get("_meta") or {}).get("kiro") or {})
        info = meta.get("kind")
        # Kiro sends the user message BEFORE turn_start, so turn_start must not
        # clear the transcript. The turn is reset in start(), the only place a
        # turn begins in a session the bridge owns; that also bounds memory.
        if info in ("turn_start", "steering_injected"):
            self._turn_active = True
        elif info in ("turn_end", "turn_completion"):
            self._turn_active = False

    def _append_text(self, who, content):
        text = _content_text(content)
        if not text:
            return
        if self._turn_items and self._turn_items[-1]["k"] == who:
            self._turn_items[-1]["t"] += text
        else:
            self._turn_items.append({"k": who, "t": text})

    # ---- native interface (matches the Codex connection contract) ----

    def snapshot(self) -> dict:
        self._pump()
        state = "waiting" if self._permissions else "running" if self._turn_active else "idle"
        return {
            "id": self.session_id,
            "cwd": self.cwd,
            "state": state,
            "activeTurnId": self._turn_id if self._turn_active else None,
            "items": list(self._turn_items),
            "modeId": self.mode_id,
            "requests": [{"id": rid, "params": params} for rid, params in self._permissions.items()],
        }

    def start(self, text: str, message_id: str) -> str | None:
        """Idle reply: start a turn. Returns a turn id the bridge confirms later.

        session/prompt is a request that settles only at turn end, so it is sent
        with begin() and its response is polled in _pump, never awaited.
        """
        self._pump()
        if self._turn_active or self._prompt_key is not None:
            return None
        # A new turn: drop the previous one (the Vita shows the last exchange only)
        # and echo the prompt locally, since Kiro does not stream it back.
        self._turn_items = [{"k": "you", "t": text}]
        self._echoed_user = text
        self._turn_id = message_id
        self._turn_active = True
        self._prompt_key, self._prompt_inbox = self.peer.begin({
            "method": M_SESSION_PROMPT, "params": {
                "sessionId": self.session_id,
                "prompt": [{"type": "text", "text": text}],
                "_meta": {"kiro": {"clientMessageId": message_id}},
            }})
        return message_id

    def steer(self, text: str, message_id: str) -> str | None:
        """Busy reply: steer the active turn. Returns the active turn id or None."""
        self._pump()
        if not self._turn_active or not self._turn_id:
            return None
        # Echo the steered message into the conversation too.
        self._append_text("you", text)
        # Steering rides the existing prompt request; it is a notification.
        self.peer.send({"method": M_STEER, "params": {
            "sessionId": self.session_id,
            "prompt": [{"type": "text", "text": text}],
            "_meta": {"kiro": {"clientMessageId": message_id}},
        }})
        return self._turn_id

    def stop(self, turn_id: str) -> str | None:
        """Interrupt the active turn. Returns the turn id, or None if not active."""
        self._pump()
        if not self._turn_active or self._turn_id != turn_id:
            return None
        self.peer.send({"method": M_SESSION_CANCEL, "params": {"sessionId": self.session_id}})
        self._cancel_permissions()
        return turn_id

    def respond(self, request: dict, response: dict):
        """Answer a permission request with an ACP outcome from the adapter."""
        self._pump()
        request_id = request.get("id")
        if request_id not in self._permissions:
            raise RpcError("Permission request is no longer pending")
        outcome = response.get("outcome")
        if not isinstance(outcome, dict):
            raise RpcError("Missing permission outcome")
        reply = {"outcome": outcome}
        if "_meta" in response:
            reply["_meta"] = response["_meta"]
        self.peer.send({"id": request_id, "result": reply})
        self._permissions.pop(request_id, None)

    def close(self):
        peer = getattr(self, "peer", None)
        if peer is not None:
            try:
                peer.close()
            except NativeError:
                pass
        stderr = getattr(self, "_stderr", None)
        if stderr is not None:
            stderr.close()
        process = getattr(self, "_process", None)
        if process is None:
            return
        try:
            process.send_signal(signal.SIGTERM)
            process.wait(timeout=3)
        except (subprocess.TimeoutExpired, ProcessLookupError, OSError):
            try:
                process.kill()
                process.wait(timeout=2)
            except (subprocess.TimeoutExpired, ProcessLookupError, OSError):
                pass


def _content_text(content) -> str:
    """Extract plain text from an ACP content block or list of blocks."""
    if isinstance(content, str):
        return content
    if isinstance(content, dict):
        return content.get("text", "") if content.get("type") == "text" else ""
    if isinstance(content, list):
        return "".join(_content_text(block) for block in content)
    return ""
