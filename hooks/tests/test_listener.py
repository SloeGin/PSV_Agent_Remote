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


def test_stop_drops_queued_text(bus, lis, tmp_path):
    lis.on_status(st("running"))
    lis.on_reply(put_reply(bus, rp("a", "text", "later")))
    lis.on_reply(put_reply(bus, rp("s", "stop")))
    assert stop_flag(tmp_path, SID).read_text() == "s"
    pub = [json.loads(p) for t, p, _ in bus.published if t == STATE and p]
    assert {"id": "a", "state": "cancelled"} in pub
    assert lis.on_status(st("idle")) is None  # the stop must not be undone by the held reply


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


# ---- run(): the loop around Listener ----

from psv_remote.bus import BusError  # noqa: E402
from psv_remote.config import Config  # noqa: E402
from psv_remote.listener import run  # noqa: E402

STATUS = topics.status(SID)


def cfg_for(tmp_path):
    return Config(state_dir=tmp_path)


def pid_file(tmp_path):
    return tmp_path / f"{SID}.listener"


def test_run_delivers_from_inbox(tmp_path):
    bus = FakeBus()
    bus.retained[STATUS] = st("idle")
    bus.retained[REPLY] = rp("a", "text", "Continue.")
    assert run(lambda: bus, SID, cfg_for(tmp_path), 4242, deadline=1e9, sleep=lambda s: None) == FRAME + "Continue."
    assert pid_file(tmp_path).read_text() == "4242"


def test_retires_when_pid_changes(tmp_path):
    bus = FakeBus()
    calls = []

    def clock():
        calls.append(1)
        if len(calls) == 3:
            pid_file(tmp_path).write_text("999")  # a newer listener took over
        return 0.0

    assert run(lambda: bus, SID, cfg_for(tmp_path), 4242, deadline=1e9, clock=clock, sleep=lambda s: None) is None
    assert len(calls) < 10


def test_run_stops_at_deadline(tmp_path):
    bus = FakeBus()
    assert run(lambda: bus, SID, cfg_for(tmp_path), 1, deadline=5.0, clock=lambda: 6.0, sleep=lambda s: None) is None


def test_session_end_stops_listener(tmp_path):
    bus = FakeBus()
    bus.inbox.append((STATUS, b""))
    assert run(lambda: bus, SID, cfg_for(tmp_path), 1, deadline=1e9, sleep=lambda s: None) is None


class DroppingBus(FakeBus):
    def next_message(self, timeout_s):
        raise BusError("disconnected")


def test_reconnects_after_bus_error(tmp_path):
    good = FakeBus()
    good.retained[STATUS] = st("idle")
    good.retained[REPLY] = rp("a", "text", "x")
    buses = [DroppingBus(), good]
    naps = []

    def factory():
        return buses.pop(0)

    assert run(factory, SID, cfg_for(tmp_path), 1, deadline=1e9, sleep=naps.append) == FRAME + "x"
    assert naps == [1]


def test_unreachable_broker_waits_until_deadline(tmp_path):
    t = [0.0]

    def factory():
        raise BusError("unreachable")

    def sleep(s):
        t[0] += s

    assert run(factory, SID, cfg_for(tmp_path), 1, deadline=100.0, clock=lambda: t[0], sleep=sleep) is None
    assert t[0] >= 100.0


# ---- final review fixes ----

class FlakyBus(FakeBus):
    def publish(self, topic, payload, retain=False, qos=0):
        raise BusError("connection lost")


def test_delivers_even_if_reporting_fails(tmp_path):
    lis = Listener(FlakyBus(), SID, tmp_path)
    lis.on_status(st("idle"))
    with pytest.raises(BusError):  # the "received" ack itself fails: nothing is taken
        lis.on_reply(rp("a", "text", "x"))
    lis2 = Listener(FlakyBus(), SID, tmp_path)
    lis2.working = True
    lis2.queued = {"id": "b", "kind": "text", "text": "held"}
    assert lis2.on_status(st("idle")) == FRAME + "held"  # delivery beats reporting


def test_idle_clears_late_stop_flag(bus, lis, tmp_path):
    lis.on_status(st("running"))
    lis.on_reply(put_reply(bus, rp("s", "stop")))
    assert lis.on_status(st("idle")) is None  # the turn ended before another tool ran
    assert not stop_flag(tmp_path, SID).exists()
    assert states(bus)[-1] == "stopped"


def test_retired_listener_does_not_deliver(tmp_path):
    class TakeoverBus(FakeBus):
        def next_message(self, timeout_s):
            got = super().next_message(timeout_s)
            if got and got[0] == REPLY:  # a newer listener took over while this one was waiting
                pid_file(tmp_path).write_text("999")
            return got

    bus = TakeoverBus()
    bus.retained[STATUS] = st("idle")
    bus.retained[REPLY] = rp("a", "text", "x")
    assert run(lambda: bus, SID, cfg_for(tmp_path), 4242, deadline=1e9, sleep=lambda s: None) is None
    assert states(bus) == []
