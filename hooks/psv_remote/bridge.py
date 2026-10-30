"""Shared bridge: one live agent session mirrored to MQTT, confirmed by native state.

Agent-specific behavior lives in an AgentProfile, so any agent with a live native
connection can reuse the same request lifecycle, reply/stop flow and status
publishing. The profile supplies the adapter's session_view/remote_request, the
agent name and capabilities, how a turn delivery is confirmed, and how a busy
reply is delivered. Codex is the current user (codex_bridge.CodexProfile).
"""

import json
import re
import time
import uuid
from dataclasses import dataclass

from . import remote, topics
from .bus import Bus, BusError
from .codex_rpc import NativeError, RpcError
from .config import Config
from .replies import ReplyLedger, parse_reply


@dataclass
class Pending:
    request: object  # adapter Request
    deadline: float
    sent: bool = False


@dataclass
class Delivery:
    message: dict
    turn_id: str
    deadline: float


class AgentProfile:
    """Per-agent behavior the bridge delegates to (for example codex_bridge.CodexProfile)."""

    name = "agent"
    capabilities = {"approval": True, "questions": True, "reply": True, "stop": True}
    # True when the agent shows its own approval prompt if the Vita doesn't answer.
    # Without one (Kiro), a missed sheet must be offered again, never dropped.
    has_native_fallback = True

    def session_view(self, raw):
        raise NotImplementedError

    def remote_request(self, native):
        raise NotImplementedError

    def confirm_delivery(self, view, delivery) -> str | None:
        """Return 'delivered'/'stopped' once native state confirms the turn, else None."""
        raise NotImplementedError

    def deliver_busy_reply(self, bridge, message) -> None:
        """Handle a reply that arrived while the session was not idle."""
        raise NotImplementedError

    def after_respond(self, bridge, request, message) -> None:
        """Hook after a permission answer is sent. Default: nothing extra."""
        return None


class Bridge:
    def __init__(self, bus: Bus, native, cfg: Config, host_id: str, profile: AgentProfile,
                 clock=time.monotonic):
        self.bus, self.native, self.cfg, self.clock = bus, native, cfg, clock
        self.profile = profile
        self.view = profile.session_view(native.snapshot())
        sid = remote.scoped_session_id(profile.name, host_id, self.view.native_id)
        self.session = remote.Session(sid, self.view.cwd, native.client, profile.name)
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
        self._subscribe()

    def _subscribe(self):
        sid = self.session.sid
        for topic in (topics.ONLINE, topics.MODE, topics.reply(sid), topics.response(sid, "+")):
            self.bus.subscribe(topic)

    def attach(self, bus):
        """Carry on over a new broker connection after a drop; the native session is untouched.

        Open sheets are withdrawn and offered afresh once the Vita's retained
        online/mode arrive again, and status and turn are republished.
        """
        stale = list(self.pending)
        self.bus = bus
        self.online, self.mode = False, "desk"
        for rid in stale:
            remote.quietly(self._clear_request, rid)
        self.last_status = self.last_turn = None
        self._subscribe()

    def _reply_state(self, rid, state):
        remote.publish_reply_state(self.bus, self.session.sid, rid, state)

    def _clear_reply(self):
        self.bus.publish(topics.reply(self.session.sid), b"", retain=True, qos=1)

    def _clear_request(self, rid):
        self.pending.pop(rid, None)
        self.bus.publish(topics.request(self.session.sid, rid), b"", retain=True)
        self.tracker.remove(rid)

    def _refresh(self):
        self.view = self.profile.session_view(self.native.snapshot())
        self.session = remote.Session(self.session.sid, self.view.cwd, self.native.client, self.profile.name)

    def _sync_requests(self):
        candidates = {}
        for native in self.view.pending:
            request = self.profile.remote_request(native)
            if request is not None:
                candidates[request.key] = request
        self.suppressed.intersection_update(candidates)
        allowed = self.online and self.mode == "sofa"
        for rid, pending in list(self.pending.items()):
            key = pending.request.key
            if key not in candidates:
                self._clear_request(rid)
            elif not allowed or self.clock() >= pending.deadline:
                if self.profile.has_native_fallback:
                    self.suppressed.add(key)  # the computer-side prompt takes over
                self._clear_request(rid)  # otherwise it returns with a fresh rid
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
        state = self.profile.confirm_delivery(self.view, delivery)
        if state is not None:
            self._reply_state(delivery.message["id"], state)
            self._clear_reply()
            self.delivery = None
        elif self.clock() >= delivery.deadline:
            self._reply_state(delivery.message["id"], "failed")
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
        capabilities = self.profile.capabilities
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
        # A deny-and-stop also interrupts the turn; the profile decides whether a
        # choice implies a native stop beyond the permission answer.
        self.profile.after_respond(self, pending.request, message)
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
            self.profile.deliver_busy_reply(self, message)

    def close(self):
        for rid in list(self.pending):
            remote.quietly(self._clear_request, rid)
        remote.quietly(self.bus.publish, topics.status(self.session.sid), b"", retain=True)
        remote.quietly(self.bus.publish, topics.turn(self.session.sid), b"", retain=True)


def run(bus, native, cfg, host_id, profile: AgentProfile):
    bridge = Bridge(bus, native, cfg, host_id, profile)
    try:
        while True:
            bridge.tick()
            message = bus.next_message(0.1)
            if message is not None:
                bridge.on_message(*message)
    finally:
        bridge.close()


BACKOFF_MAX_S = 30


def run_reconnecting(bus_factory, native, cfg, host_id, profile: AgentProfile, sleep=time.sleep):
    """Like run(), for a native session the bridge owns (Kiro): a broker outage
    must not end it, so the bus is reopened with backoff and the bridge reattached."""
    bridge, backoff = None, 1
    try:
        while True:
            try:
                bus = bus_factory()
            except BusError:
                sleep(backoff)
                backoff = min(backoff * 2, BACKOFF_MAX_S)
                continue
            backoff = 1
            if bridge is None:
                bridge = Bridge(bus, native, cfg, host_id, profile)
            else:
                bridge.attach(bus)
            try:
                while True:
                    bridge.tick()
                    message = bus.next_message(0.1)
                    if message is not None:
                        bridge.on_message(*message)
            except BusError:
                remote.quietly(bus.close)
                sleep(1)
    finally:
        if bridge is not None:
            bridge.close()
