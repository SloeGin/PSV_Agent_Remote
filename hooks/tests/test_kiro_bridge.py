
import json
from copy import deepcopy

import pytest

from fakebus import FakeBus
from psv_remote import topics
from psv_remote.bridge import Bridge
from psv_remote.codex_rpc import NativeError
from psv_remote.config import Config
from psv_remote.kiro_bridge import KiroProfile
from psv_remote.remote import encode

FOUR_OPTIONS = [
    {"optionId": "accept", "kind": "allow_once"},
    {"optionId": "always-accept", "kind": "allow_always"},
    {"optionId": "reject", "kind": "reject_once"},
    {"optionId": "always-reject", "kind": "reject_always"},
]


def permission(request_id="perm-1"):
    return {"id": request_id, "params": {
        "sessionId": "s1",
        "toolCall": {"toolCallId": "c1", "title": "Write File"},
        "options": FOUR_OPTIONS,
        "_meta": {"kiro": {"consent": {"capability": "fs_write", "resource": "out.txt",
                                       "workspaceRoot": "/project"}}}}}


def snap(state="waiting", requests=None, items=None, active=None):
    return {"id": "native-thread", "cwd": "/project", "state": state,
            "activeTurnId": active, "items": items or [{"k": "you", "t": "hi"}],
            "modeId": "vibe", "requests": requests if requests is not None else [permission()]}


class Native:
    client = "cli"

    def __init__(self, raw):
        self.raw = raw
        self.answers, self.starts, self.steers, self.stops = [], [], [], []

    def snapshot(self): return deepcopy(self.raw)

    def respond(self, request, response):
        self.answers.append((request, response))
        self.raw["requests"] = []

    def start(self, text, message_id):
        self.starts.append((text, message_id))
        self.raw = snap(state="running", requests=[], active=message_id)
        return message_id

    def steer(self, text, message_id):
        self.steers.append((text, message_id))
        return self.raw.get("activeTurnId")

    def stop(self, turn_id):
        self.stops.append(turn_id)
        self.raw["state"] = "idle"
        self.raw["activeTurnId"] = None
        return turn_id


@pytest.fixture
def setup(tmp_path):
    bus = FakeBus()
    native = Native(snap())
    clock = [10.0]
    bridge = Bridge(bus, native, Config(state_dir=tmp_path, wait_timeout_s=5), "host",
                    KiroProfile(), clock=lambda: clock[0])
    bridge.tick()
    bridge.on_message(topics.ONLINE, b"1")
    bridge.on_message(topics.MODE, b"sofa")
    return bridge, bus, native, clock


def respond(bridge, rid, **fields):
    bridge.on_message(topics.response(bridge.session.sid, rid), encode({"rid": rid, **fields}))


def progress(bus, sid):
    return [json.loads(payload)["state"] for topic, payload, _ in bus.published
            if topic == topics.reply_state(sid) and payload]


def published_status(bus, sid):
    out = None
    for topic, payload, _ in bus.published:
        if topic == topics.status(sid) and payload:
            out = json.loads(payload)
    return out


def test_session_announces_kiro_identity_and_capabilities(setup):
    bridge, bus, _, _ = setup
    status = published_status(bus, bridge.session.sid)
    assert status["agent"] == "kiro"
    assert status["capabilities"] == {"approval": True, "questions": False, "reply": True, "stop": True}
    assert bridge.session.sid.startswith("kiro-")


def test_allow_maps_to_accept_option(setup):
    bridge, _, native, _ = setup
    rid = next(iter(bridge.pending))
    respond(bridge, rid, choice="allow")
    assert native.answers[0][1] == {"outcome": {"outcome": "selected", "optionId": "accept"}}


def test_allow_always_echoes_consent(setup):
    bridge, _, native, _ = setup
    rid = next(iter(bridge.pending))
    respond(bridge, rid, choice="allow_always")
    outcome = native.answers[0][1]
    assert outcome["outcome"]["optionId"] == "always-accept"
    assert outcome["_meta"]["kiro"]["consent"]["resource"] == "out.txt"


def test_deny_stop_rejects_then_cancels_the_turn(setup):
    bridge, _, native, _ = setup
    native.raw["activeTurnId"] = "turn-x"  # a turn is running behind the request
    bridge.tick()
    rid = next(iter(bridge.pending))
    respond(bridge, rid, choice="deny_stop")
    assert native.answers[0][1] == {"outcome": {"outcome": "selected", "optionId": "reject"}}
    assert native.stops == ["turn-x"]  # after_respond issued the cancel


def test_deny_does_not_cancel(setup):
    bridge, _, native, _ = setup
    native.raw["activeTurnId"] = "turn-x"
    bridge.tick()
    rid = next(iter(bridge.pending))
    respond(bridge, rid, choice="deny")
    assert native.stops == []


def test_busy_reply_steers_the_active_turn(setup):
    bridge, bus, native, _ = setup
    native.raw = snap(state="running", requests=[], active="turn-x")
    bridge.tick()
    bridge.on_message(topics.reply(bridge.session.sid),
                      encode({"id": "r1", "kind": "text", "text": "adjust course"}))
    assert native.steers == [("adjust course", "psv-r1")]
    assert native.starts == []  # steered, not a new turn
    assert "steering" in progress(bus, bridge.session.sid)


def test_idle_reply_starts_a_turn(setup):
    bridge, bus, native, _ = setup
    native.raw = snap(state="idle", requests=[], active=None)
    bridge.tick()
    bridge.on_message(topics.reply(bridge.session.sid),
                      encode({"id": "r2", "kind": "text", "text": "continue"}))
    assert native.starts == [("continue", "psv-r2")]
    assert native.steers == []


def test_stop_reply_interrupts_and_confirms(setup):
    bridge, bus, native, _ = setup
    native.raw = snap(state="running", requests=[], active="turn-x")
    bridge.tick()
    bridge.on_message(topics.reply(bridge.session.sid), encode({"id": "s1", "kind": "stop"}))
    assert native.stops == ["turn-x"]
    bridge.tick()  # snapshot now idle -> confirmed
    assert progress(bus, bridge.session.sid)[-1] == "stopped"


def test_mac_choice_hands_off_without_answering_native(setup):
    bridge, _, native, _ = setup
    rid = next(iter(bridge.pending))
    respond(bridge, rid, choice="mac")
    assert native.answers == []


# ---- review fix: Kiro has no computer-side approval prompt, so never drop one ----

def live_sheets(bus, sid):
    prefix = topics.request(sid, "")
    return sorted(topic[len(prefix):] for topic in bus.retained if topic.startswith(prefix))


def test_expired_approval_is_offered_again(setup):
    bridge, bus, _, clock = setup
    (first,) = live_sheets(bus, bridge.session.sid)
    clock[0] += 6  # past the 5 s wait
    bridge.tick()
    (second,) = live_sheets(bus, bridge.session.sid)
    assert second != first  # a fresh sheet with a new rid and a new countdown


def test_desk_mode_hides_then_sofa_brings_it_back(setup):
    bridge, bus, _, _ = setup
    bridge.on_message(topics.MODE, b"desk")  # plugging the Vita in switches to desk
    assert live_sheets(bus, bridge.session.sid) == []
    bridge.on_message(topics.MODE, b"sofa")
    assert len(live_sheets(bus, bridge.session.sid)) == 1


def test_offline_vita_gets_it_when_back(setup):
    bridge, bus, _, _ = setup
    bridge.on_message(topics.ONLINE, b"0")
    assert live_sheets(bus, bridge.session.sid) == []
    bridge.on_message(topics.ONLINE, b"1")
    assert len(live_sheets(bus, bridge.session.sid)) == 1


# ---- review fix: a broker drop must not end the Kiro session the bridge owns ----

from psv_remote.bus import BusError  # noqa: E402
from psv_remote.kiro_bridge import run as kiro_run  # noqa: E402


class ScriptedBus(FakeBus):
    """Hands out its inbox, then fails with `then` (BusError = drop, KeyboardInterrupt = Ctrl-C)."""

    def __init__(self, inbox, then):
        super().__init__()
        self.script, self.then = list(inbox), then

    def next_message(self, timeout_s):
        if self.script:
            return self.script.pop(0)
        raise self.then


def test_broker_drop_keeps_the_kiro_session(tmp_path):
    native = Native(snap())
    first = ScriptedBus([(topics.ONLINE, b"1"), (topics.MODE, b"sofa")], BusError("dropped"))
    second = ScriptedBus([(topics.ONLINE, b"1"), (topics.MODE, b"sofa")], KeyboardInterrupt())
    buses, naps = [first, BusError("still down"), second], []

    def factory():
        bus = buses.pop(0)
        if isinstance(bus, Exception):
            raise bus
        return bus

    with pytest.raises(KeyboardInterrupt):
        kiro_run(factory, native, Config(state_dir=tmp_path, wait_timeout_s=5), "host", sleep=naps.append)
    assert naps and buses == []  # it retried instead of giving up
    reply_topic = next(t for t in second.subscribed if t.endswith("/reply"))
    sid = reply_topic.removeprefix("claude/session/").removesuffix("/reply")
    assert published_status(second, sid)["agent"] == "kiro"  # announced again on the new connection
    old = [t for t in first.retained if "/request/" in t]
    new = {t for t, p, _ in second.published if "/request/" in t and p}  # (Ctrl-C withdraws it again)
    assert len(old) == 1 and len(new) == 1 and old[0] not in new  # the sheet is offered afresh
    assert (old[0], b"", True) in second.published  # and the stale one is withdrawn
    assert native.starts == [] and native.stops == []  # the session itself was never touched
