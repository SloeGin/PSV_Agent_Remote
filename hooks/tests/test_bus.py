import queue
import socket
import threading
import time

import pytest

from psv_remote.bus import BusError, PahoBus
from psv_remote.config import Config


def test_wait_for_returns_early_on_disconnect():
    bus = object.__new__(PahoBus)
    bus._queues = {}
    bus._lock = threading.Lock()
    bus._disconnected = threading.Event()
    threading.Timer(0.1, bus._disconnected.set).start()
    t0 = time.monotonic()
    assert bus.wait_for("t", 5.0) is None
    assert time.monotonic() - t0 < 1.0


def test_wait_for_still_delivers():
    bus = object.__new__(PahoBus)
    bus._queues = {"t": queue.Queue()}
    bus._lock = threading.Lock()
    bus._disconnected = threading.Event()
    bus._queues["t"].put(b"x")
    assert bus.wait_for("t", 1.0) == b"x"


def test_dns_stall_is_bounded(monkeypatch):
    monkeypatch.setattr(socket, "getaddrinfo", lambda *a, **k: time.sleep(10))
    t0 = time.monotonic()
    with pytest.raises(BusError):
        PahoBus(Config(broker_host="stall.example", connect_timeout_s=0.5))
    assert time.monotonic() - t0 < 1.5


def _bare_bus():
    bus = object.__new__(PahoBus)
    bus._queues, bus._lock = {}, threading.Lock()
    bus._all = queue.Queue()
    bus._disconnected = threading.Event()
    return bus


def test_next_message_returns_any_topic():
    bus = _bare_bus()
    bus._on_message(None, None, type("M", (), {"topic": "a/b", "payload": b"x"})())
    assert bus.next_message(1.0) == ("a/b", b"x")
    assert bus.next_message(0.05) is None


def test_next_message_raises_after_disconnect():
    bus = _bare_bus()
    bus._disconnected.set()
    with pytest.raises(BusError):
        bus.next_message(1.0)


def test_reply_topics():
    from psv_remote import topics
    assert topics.reply("s1") == "claude/session/s1/reply"
    assert topics.reply_state("s1") == "claude/session/s1/reply_state"
