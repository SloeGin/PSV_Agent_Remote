import json
from copy import deepcopy

import pytest

from fakebus import FakeBus
from psv_remote import topics
from psv_remote.codex_bridge import CodexBridge
from psv_remote.rpc import NativeError
from psv_remote.config import Config
from psv_remote.remote import encode
from test_codex_adapter import approval, question, state


class Native:
    client = "vscode"

    def __init__(self, raw):
        self.raw = raw
        self.answers, self.starts, self.stops = [], [], []
        self.fail_after_start = False

    def snapshot(self): return deepcopy(self.raw)

    def respond(self, request, response):
        self.answers.append((request, response))
        self.raw["requests"] = []

    def start(self, text, message_id):
        self.starts.append((text, message_id))
        self.raw = state(status="inProgress")
        self.raw["turns"][0]["turnId"] = "new-turn"
        if self.fail_after_start:
            raise NativeError("connection lost after dispatch")
        return "new-turn"

    def stop(self, turn_id):
        self.stops.append(turn_id)
        self.raw["turns"][0]["status"] = "interrupted"
        self.raw["threadRuntimeStatus"] = {"type": "idle"}
        return turn_id


@pytest.fixture
def setup(tmp_path):
    bus = FakeBus()
    native = Native(state([approval()]))
    clock = [10.0]
    bridge = CodexBridge(bus, native, Config(state_dir=tmp_path, wait_timeout_s=5), "test-host",
                         clock=lambda: clock[0])
    bridge.tick()
    bridge.on_message(topics.ONLINE, b"1")
    bridge.on_message(topics.MODE, b"sofa")
    return bridge, bus, native, clock


def respond(bridge, rid, **fields):
    bridge.on_message(topics.response(bridge.session.sid, rid), encode({"rid": rid, **fields}))


def progress(bus, sid):
    return [json.loads(payload)["state"] for topic, payload, _ in bus.published
            if topic == topics.reply_state(sid) and payload]


def test_wire_approval_response_applies_once_and_clears_on_native_resolution(setup):
    bridge, bus, native, _ = setup
    rid = next(iter(bridge.pending))
    respond(bridge, rid, choice="allow")
    respond(bridge, rid, choice="allow")
    assert len(native.answers) == 1 and native.answers[0][1] == {"decision": "accept"}
    bridge.tick()
    assert not bridge.pending
    assert topics.request(bridge.session.sid, rid) not in bus.retained


@pytest.mark.parametrize("change", ["resolved", "command", "turn"])
def test_native_change_between_display_and_response_never_applies_old_choice(setup, change):
    bridge, _, native, _ = setup
    rid = next(iter(bridge.pending))
    if change == "resolved": native.raw["requests"] = []
    if change == "command": native.raw["requests"][0]["params"]["command"] = "new command"
    if change == "turn": native.raw["turns"][0]["turnId"] = "other-turn"
    respond(bridge, rid, choice="allow")
    assert native.answers == []


@pytest.mark.parametrize("gate,value", [(topics.MODE, b"desk"), (topics.ONLINE, b"0")])
def test_mode_or_connectivity_change_clears_sheet_without_answering_native(setup, gate, value):
    bridge, bus, native, _ = setup
    rid = next(iter(bridge.pending))
    bridge.on_message(gate, value)
    respond(bridge, rid, choice="allow")
    assert native.answers == [] and len(native.raw["requests"]) == 1
    assert topics.request(bridge.session.sid, rid) not in bus.retained


def test_expiry_hands_off_to_computer_and_does_not_reannounce(setup):
    bridge, _, native, clock = setup
    rid = next(iter(bridge.pending))
    clock[0] += 6
    bridge.tick()
    respond(bridge, rid, choice="allow")
    bridge.tick()
    assert not bridge.pending and native.answers == []


def test_structured_question_roundtrip_and_unsupported_other(setup):
    bridge, _, native, _ = setup
    native.raw = state([question(isOther=False)])
    bridge.tick()
    rid = next(iter(bridge.pending))
    respond(bridge, rid, answers=[{"other": "not offered"}])
    assert native.answers == []
    respond(bridge, rid, answers=[{"selected": [1]}])
    assert native.answers[-1][1] == {"answers": {"native-question-id": {"answers": ["Beta"]}}}


def test_queued_text_can_be_cancelled_and_stop_cannot_restart_agent(setup):
    bridge, bus, native, _ = setup
    reply_topic = topics.reply(bridge.session.sid)
    bridge.on_message(reply_topic, encode({"id": "text", "kind": "text", "text": "later"}))
    assert bridge.queued and not native.starts
    bridge.on_message(reply_topic, encode({"id": "stop", "kind": "stop"}))
    bridge.tick()
    assert native.stops == ["turn"] and not native.starts and bridge.queued is None
    assert "stopped" in progress(bus, bridge.session.sid)


def test_reply_is_delivered_only_after_native_turn_is_observed_and_is_deduplicated(setup):
    bridge, bus, native, _ = setup
    native.raw = state(status="completed")
    payload = encode({"id": "reply", "kind": "text", "text": "Continue"})
    bridge.on_message(topics.reply(bridge.session.sid), payload)
    assert "delivered" not in progress(bus, bridge.session.sid)
    bridge.tick()
    bridge.on_message(topics.reply(bridge.session.sid), payload)
    assert len(native.starts) == 1
    assert progress(bus, bridge.session.sid)[-1] == "delivered"


def test_ambiguous_native_failure_is_not_replayed_after_bridge_restart(setup):
    bridge, bus, native, _ = setup
    native.raw = state(status="completed")
    native.fail_after_start = True
    payload = encode({"id": "uncertain", "kind": "text", "text": "Continue"})
    with pytest.raises(NativeError):
        bridge.on_message(topics.reply(bridge.session.sid), payload)
    second = CodexBridge(bus, native, bridge.cfg, "test-host")
    second.on_message(topics.reply(second.session.sid), payload)
    assert len(native.starts) == 1


def test_native_rpc_ack_is_not_a_stopped_confirmation(setup):
    bridge, bus, native, clock = setup
    native.stop = lambda turn: turn  # acknowledge, but don't update native state
    bridge.on_message(topics.reply(bridge.session.sid), encode({"id": "stop", "kind": "stop"}))
    bridge.tick()
    assert "stopped" not in progress(bus, bridge.session.sid)
    clock[0] += 11
    bridge.tick()
    assert progress(bus, bridge.session.sid)[-1] == "failed"


def test_other_session_and_garbage_response_are_ignored(setup):
    bridge, _, native, _ = setup
    rid = next(iter(bridge.pending))
    bridge.on_message(topics.response("another-session", rid), encode({"rid": rid, "choice": "allow"}))
    bridge.on_message(topics.response(bridge.session.sid, rid), b"invalid json")
    assert native.answers == []


def test_restarted_worker_clears_crash_left_sheet_and_does_not_replay_decision(setup):
    bridge, bus, native, _ = setup
    old_rid = next(iter(bridge.pending))
    second = CodexBridge(bus, native, bridge.cfg, "test-host")
    assert topics.request(bridge.session.sid, old_rid) not in bus.retained
    second.on_message(topics.ONLINE, b"1")
    second.on_message(topics.MODE, b"sofa")
    assert len(second.pending) == 1 and old_rid not in second.pending
    respond(second, old_rid, choice="allow")
    assert native.answers == []
