import json

import pytest

from fakebus import FakeBus
from psv_remote import topics
from psv_remote.listener import FRAME, Listener, stop_flag

SID = "s1"
REPLY = topics.reply(SID)
STATE = topics.reply_state(SID)


def st(state: str) -> bytes:
    return json.dumps({"state": state}).encode()


def rp(rid: str, kind: str, text: str = "") -> bytes:
    msg = {"id": rid, "kind": kind}
    if text:
        msg["text"] = text
    return json.dumps(msg).encode()


def states(bus: FakeBus) -> list[str]:
    return [json.loads(p)["state"] for t, p, _ in bus.published if t == STATE and p]


@pytest.fixture
def bus():
    return FakeBus()


@pytest.fixture
def lis(bus, tmp_path):
    return Listener(bus, SID, tmp_path)


def put_reply(bus, payload):
    bus.retained[REPLY] = payload  # as a broker would hold it
    return payload


def test_text_when_idle_delivers(bus, lis):
    lis.on_status(st("idle"))
    assert lis.on_reply(put_reply(bus, rp("a", "text", "Continue."))) == FRAME + "Continue."
    assert states(bus) == ["received", "delivered"]
    assert REPLY not in bus.retained
    turn = json.loads(bus.retained[topics.turn(SID)])
    assert turn["items"] == [{"k": "you", "t": "Continue."}]


def test_text_when_working_queues_then_delivers_on_idle(bus, lis):
    lis.on_status(st("running"))
    assert lis.on_reply(put_reply(bus, rp("a", "text", "more tests"))) is None
    assert states(bus) == ["received", "queued"]
    assert REPLY in bus.retained  # a successor listener inherits it
    assert lis.on_status(st("idle")) == FRAME + "more tests"
    assert states(bus)[-1] == "delivered"
    assert REPLY not in bus.retained


def test_unknown_state_holds(bus, lis):
    assert lis.on_reply(put_reply(bus, rp("a", "text", "x"))) is None
    assert states(bus) == ["received", "queued"]
    assert lis.on_status(st("idle")) == FRAME + "x"


def test_waiting_counts_as_working(bus, lis):
    lis.on_status(st("waiting"))
    assert lis.on_reply(put_reply(bus, rp("a", "text", "x"))) is None
    assert states(bus) == ["received", "queued"]


def test_cancel_drops_queued(bus, lis):
    lis.on_status(st("running"))
    lis.on_reply(put_reply(bus, rp("a", "text", "x")))
    assert lis.on_reply(put_reply(bus, rp("a", "cancel"))) is None
    assert states(bus)[-1] == "cancelled"
    assert REPLY not in bus.retained
    assert lis.on_status(st("idle")) is None


def test_newer_text_replaces_queued(bus, lis):
    lis.on_status(st("running"))
    lis.on_reply(put_reply(bus, rp("a", "text", "first")))
    lis.on_reply(put_reply(bus, rp("b", "text", "second")))
    assert states(bus) == ["received", "queued", "received", "queued"]
    assert lis.on_status(st("idle")) == FRAME + "second"
    assert lis.on_status(st("idle")) is None


def test_stop_when_working_sets_flag(bus, lis, tmp_path):
    lis.on_status(st("running"))
    assert lis.on_reply(put_reply(bus, rp("s", "stop"))) is None
    assert stop_flag(tmp_path, SID).read_text() == "s"
    assert states(bus) == ["received", "stopping"]
    assert REPLY not in bus.retained


def test_stop_when_idle_ignored(bus, lis, tmp_path):
    lis.on_status(st("idle"))
    assert lis.on_reply(put_reply(bus, rp("s", "stop"))) is None
    assert states(bus) == ["received", "ignored"]
    assert not stop_flag(tmp_path, SID).exists()


def test_repeat_id_ignored(bus, tmp_path):
    first = Listener(bus, SID, tmp_path)
    first.on_status(st("idle"))
    assert first.on_reply(put_reply(bus, rp("a", "text", "x")))
    again = FakeBus()
    second = Listener(again, SID, tmp_path)
    second.on_status(st("idle"))
    assert second.on_reply(put_reply(again, rp("a", "text", "x"))) is None
    assert states(again) == []


def test_empty_or_junk_reply_ignored(bus, lis):
    lis.on_status(st("idle"))
    for junk in (b"", b"{", b"[]", rp("", "text", "x"), rp("a", "dance", "x"), rp("a", "text")):
        assert lis.on_reply(junk) is None
    assert bus.published == []
