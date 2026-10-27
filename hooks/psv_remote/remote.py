"""Shared MQTT messages and request lifecycle; no agent-native API semantics."""

import hashlib
import json
import os
import time
import uuid
from dataclasses import dataclass
from collections.abc import Callable

from . import topics
from .bus import Bus, BusError
from .config import Config
from .pending import PendingTracker


def encode(value) -> bytes:
    return json.dumps(value, ensure_ascii=False).encode("utf-8")


def quietly(function, *args, **kwargs):
    try:
        return function(*args, **kwargs)
    except BusError:
        return None


def scoped_session_id(agent: str, host: str, native_id: str) -> str:
    """One MQTT segment, below the Vita's 64-byte SID buffer limit."""
    digest = hashlib.sha256(encode([agent, host, native_id])).hexdigest()[:40]
    return f"{agent}-{digest}"


@dataclass(frozen=True)
class Session:
    sid: str
    cwd: str
    client: str = "cli"
    agent: str | None = None  # omitted for the existing Claude wire contract


def publish_status(bus: Bus, session: Session, state: str, summary: str = "",
                   capabilities: dict | None = None) -> None:
    payload = {"state": state, "project": os.path.basename(session.cwd.rstrip("/")) or session.cwd,
               "cwd": session.cwd, "client": session.client, "summary": summary, "ts": int(time.time())}
    if session.agent is not None:
        payload["agent"] = session.agent
    if capabilities is not None:
        payload["capabilities"] = capabilities
    bus.publish(topics.status(session.sid), encode(payload), retain=True)


def publish_turn(bus: Bus, sid: str, items: list[dict]) -> None:
    bus.publish(topics.turn(sid), encode({"items": items, "ts": int(time.time())}), retain=True)


def publish_reply_state(bus: Bus, sid: str, reply_id: str, state: str) -> None:
    bus.publish(topics.reply_state(sid), encode({"id": reply_id, "state": state}), retain=True, qos=1)


def tracker(cfg: Config, sid: str) -> PendingTracker:
    return PendingTracker(cfg.state_dir, sid, max_age_s=cfg.wait_timeout_s + 10)


def ask(bus: Bus, cfg: Config, session: Session, request: dict,
        prepare: Callable[[], None], state: Callable[[str, str], None]) -> dict | None:
    """Synchronous hook request. An unavailable remote leaves the native UI in charge."""
    if bus.read_retained(topics.ONLINE, cfg.retained_wait_s) != b"1":
        return None
    quietly(prepare)
    if bus.read_retained(topics.MODE, cfg.retained_wait_s) == b"desk":
        state("waiting", f"请在电脑上处理：{request['title']}")
        return None
    rid = uuid.uuid4().hex[:12]
    pending = tracker(cfg, session.sid)
    pending.add(rid)
    message = None
    try:
        bus.subscribe(topics.response(session.sid, rid))
        payload = {**request, "rid": rid, "timeout_s": cfg.wait_timeout_s,
                   "cwd": session.cwd, "ts": int(time.time())}
        bus.publish(topics.request(session.sid, rid), encode(payload), retain=True)
        state("waiting", request["title"])
        raw = bus.wait_for(topics.response(session.sid, rid), cfg.wait_timeout_s)
        try:
            message = json.loads(raw) if raw else None
        except ValueError:
            message = None
    finally:
        pending.remove(rid)
        quietly(bus.publish, topics.request(session.sid, rid), b"", retain=True)
        if pending.count() == 0:
            quietly(state, "running", "")
    return message if isinstance(message, dict) and message.get("rid") == rid else None
