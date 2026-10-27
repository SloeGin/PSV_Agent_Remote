"""One existing Codex session bridged to MQTT, with native-state confirmation."""

import json
import re
import time
import uuid
from dataclasses import dataclass

from . import remote, topics
from .adapters.codex import Request, ordered_turns, remote_request, session_view
from .bus import Bus
from .codex_rpc import NativeError, RpcError
from .config import Config
from .replies import ReplyLedger, parse_reply


@dataclass
class Pending:
    request: Request
    deadline: float
    sent: bool = False


@dataclass
class Delivery:
    message: dict
    turn_id: str
    deadline: float


class CodexBridge:
    def __init__(self, bus: Bus, native, cfg: Config, host_id: str, clock=time.monotonic):
        self.bus, self.native, self.cfg, self.clock = bus, native, cfg, clock
        self.view = session_view(native.snapshot())
        sid = remote.scoped_session_id("codex", host_id, self.view.native_id)
        self.session = remote.Session(sid, self.view.cwd, native.client, "codex")
        self.ledger = ReplyLedger(cfg.state_dir, sid)
        self.pending: dict[str, Pending] = {}
        self.suppressed: set[str] = set()
        self.queued: dict | None = None
        self.delivery: Delivery | None = None
        self.online = False
        self.mode = "desk"  # wait for retained mode before forwarding any decision
        self.last_status = None
        self.last_turn = None
        self.last_publish = 0
        self.tracker = remote.tracker(cfg, sid)
        # A killed worker may leave retained sheets. Clear only our persisted
        # request IDs before issuing fresh IDs; never replay old decisions.
        for rid in self.tracker.ids():
            if re.fullmatch(r"[a-f0-9]{12}", rid):
                self._clear_request(rid)
        bus.subscribe(topics.ONLINE)
        bus.subscribe(topics.MODE)
        bus.subscribe(topics.reply(sid))
        bus.subscribe(topics.response(sid, "+"))

    def _reply_state(self, rid, state):
        remote.publish_reply_state(self.bus, self.session.sid, rid, state)

    def _clear_reply(self):
        self.bus.publish(topics.reply(self.session.sid), b"", retain=True, qos=1)

    def _clear_request(self, rid):
        self.pending.pop(rid, None)
        self.bus.publish(topics.request(self.session.sid, rid), b"", retain=True)
        self.tracker.remove(rid)

    def _refresh(self):
        self.view = session_view(self.native.snapshot())
        self.session = remote.Session(self.session.sid, self.view.cwd, self.native.client, "codex")

    def _sync_requests(self):
        candidates = {}
        for native in self.view.pending:
            request = remote_request(native)
            if request is not None:
                candidates[request.key] = request
        self.suppressed.intersection_update(candidates)
        allowed = self.online and self.mode == "sofa"
        for rid, pending in list(self.pending.items()):
            key = pending.request.key
            if key not in candidates:
                self._clear_request(rid)
            elif not allowed or self.clock() >= pending.deadline:
                self.suppressed.add(key)
                self._clear_request(rid)
        active = {pending.request.key for pending in self.pending.values()}
        if not allowed:
            return
        for key, request in candidates.items():
            if key in active or key in self.suppressed:
                continue
            rid = uuid.uuid4().hex[:12]
            timeout = self.cfg.wait_timeout_s
            native_timeout = request.native["params"].get("autoResolutionMs")
            if type(native_timeout) is int and native_timeout > 0:
                timeout = min(timeout, native_timeout / 1000)
            self.pending[rid] = Pending(request, self.clock() + timeout)
            self.tracker.add(rid)
            payload = {**request.payload, "rid": rid, "cwd": self.view.cwd,
                       "timeout_s": timeout, "ts": int(time.time())}
            self.bus.publish(topics.request(self.session.sid, rid), remote.encode(payload), retain=True)

    def _confirm_delivery(self):
        delivery = self.delivery
        if delivery is None:
            return
        turns = ordered_turns(self.view.raw)
        turn = next((turn for turn in turns if turn.get("turnId") == delivery.turn_id), None)
        kind, rid = delivery.message["kind"], delivery.message["id"]
        confirmed = turn is not None and (kind == "text" or turn.get("status") == "interrupted")
        if confirmed:
            self._reply_state(rid, "delivered" if kind == "text" else "stopped")
            self._clear_reply()
            self.delivery = None
        elif self.clock() >= delivery.deadline:
            self._reply_state(rid, "failed")
            self._clear_reply()
            self.delivery = None

    def _send(self, message):
        # Claim before the native operation: reconnects and ambiguous timeouts
        # must not replay a prompt or interrupt into a later turn.
        rid = message["id"]
        self.ledger.mark(rid)
        try:
            if message["kind"] == "text":
                turn = self.native.start(message["text"], "psv-" + rid)
            else:
                turn = self.native.stop(self.view.active_turn) if self.view.active_turn else None
            if not isinstance(turn, str) or not turn:
                self._reply_state(rid, "ignored")
                self._clear_reply()
                return
            self.delivery = Delivery(message, turn, self.clock() + 10)
            if message["kind"] == "stop":
                self._reply_state(rid, "stopping")
        except NativeError:
            self._reply_state(rid, "failed")
            self._clear_reply()
            raise

    def tick(self):
        self._refresh()
        self._confirm_delivery()
        self._sync_requests()
        if self.queued and self.view.state == "idle" and self.delivery is None:
            message, self.queued = self.queued, None
            self._send(message)
        summary = next((item["t"] for item in reversed(self.view.items) if item["k"] == "assistant"), "")
        capabilities = {"approval": True, "questions": True, "reply": True, "stop": True}
        status = (self.view.state, self.view.cwd, summary, capabilities)
        if status != self.last_status or self.clock() - self.last_publish >= 30:
            remote.publish_status(self.bus, self.session, self.view.state, summary, capabilities)
            self.last_status, self.last_publish = status, self.clock()
        if self.view.items != self.last_turn:
            remote.publish_turn(self.bus, self.session.sid, self.view.items)
            self.last_turn = self.view.items

    def on_message(self, topic, payload):
        if topic == topics.ONLINE:
            self.online = payload == b"1"
            self._sync_requests()
            return
        if topic == topics.MODE:
            self.mode = "sofa" if payload == b"sofa" else "desk"
            self._sync_requests()
            return
        if topic == topics.reply(self.session.sid):
            self._on_reply(payload)
            return
        prefix = topics.response(self.session.sid, "")
        if not topic.startswith(prefix):
            return
        rid = topic[len(prefix):]
        pending = self.pending.get(rid)
        if pending is None or pending.sent:
            return
        try:
            message = json.loads(payload)
        except (ValueError, UnicodeDecodeError):
            return
        if not isinstance(message, dict) or message.get("rid") != rid:
            return
        self._refresh()  # broker delivery can race a computer-side resolution
        self._sync_requests()
        if rid not in self.pending:
            return
        if message.get("choice") == "mac":
            self.suppressed.add(pending.request.key)
            self._clear_request(rid)
            return
        response = pending.request.answer(message)
        if response is None:
            return
        pending.sent = True
        try:
            self.native.respond(pending.request.native, response)
        except RpcError:
            self.suppressed.add(pending.request.key)
            self._clear_request(rid)
        # Keep the sheet until native state confirms the request is gone. On
        # timeout it falls back to the unchanged computer-side prompt.

    def _on_reply(self, payload):
        message = parse_reply(payload)
        if message is None:
            return
        rid, kind = message["id"], message["kind"]
        if kind == "cancel":
            if self.queued and self.queued["id"] == rid:
                self.queued = None
                self.ledger.mark(rid)
                self._reply_state(rid, "cancelled")
                self._clear_reply()
            return
        if rid in self.ledger.seen():
            return
        if self.delivery is not None:
            if self.delivery.message["id"] != rid:
                self._reply_state(rid, "ignored")
            return
        self._refresh()
        self._reply_state(rid, "received")
        if kind == "stop":
            if self.queued:
                self.ledger.mark(self.queued["id"])
                self._reply_state(self.queued["id"], "cancelled")
                self.queued = None
            self._send(message)
        elif self.view.state == "idle":
            self._send(message)
        else:
            if self.queued and self.queued["id"] != rid:
                self.ledger.mark(self.queued["id"])
                self._reply_state(self.queued["id"], "cancelled")
            self.queued = message
            self._reply_state(rid, "queued")

    def close(self):
        for rid in list(self.pending):
            remote.quietly(self._clear_request, rid)
        remote.quietly(self.bus.publish, topics.status(self.session.sid), b"", retain=True)
        remote.quietly(self.bus.publish, topics.turn(self.session.sid), b"", retain=True)


def run(bus, native, cfg, host_id):
    bridge = CodexBridge(bus, native, cfg, host_id)
    try:
        while True:
            bridge.tick()
            message = bus.next_message(0.1)
            if message is not None:
                bridge.on_message(*message)
    finally:
        bridge.close()
