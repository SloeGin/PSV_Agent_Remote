"""Replies from the PSV: a background listener hands them to the session.

Claude Code runs psv_listen.py as an asyncRewake hook. It waits for a reply on
claude/session/<sid>/reply and, when it can deliver one, exits 2 with the text
on stderr, which wakes the session. Typed replies wait while the agent works;
a stop reply leaves a flag file that the PostToolUse hook turns into a hard stop.
"""

import json
import time
from collections.abc import Callable
from pathlib import Path

from . import topics
from .bus import Bus, BusError
from .config import Config
from .pending import _safe

FRAME = "Message from the user, sent from their PS Vita remote: "
KINDS = {"text", "stop", "cancel"}
SEEN_MAX = 32


def stop_flag(state_dir: Path, sid: str) -> Path:
    """Exists while a stop is pending; holds the stop reply's id."""
    return Path(state_dir) / f"{_safe(sid)}.stop"


def pid_file(state_dir: Path, sid: str) -> Path:
    """Holds the pid of the session's current listener; older ones retire."""
    return Path(state_dir) / f"{_safe(sid)}.listener"


def _seen_file(state_dir: Path, sid: str) -> Path:
    return Path(state_dir) / f"{_safe(sid)}.replies"


def _parse(payload: bytes) -> dict | None:
    try:
        msg = json.loads(payload)
    except (ValueError, UnicodeDecodeError):
        return None
    if not isinstance(msg, dict):
        return None
    rid, kind, text = msg.get("id"), msg.get("kind"), msg.get("text", "")
    if not isinstance(rid, str) or not rid or len(rid) > 64 or kind not in KINDS or not isinstance(text, str):
        return None
    if kind == "text" and not text.strip():
        return None
    return {"id": rid, "kind": kind, "text": text.strip()}


class Listener:
    def __init__(self, bus: Bus, sid: str, state_dir: Path):
        self.bus = bus
        self._sid = sid
        self._dir = Path(state_dir)
        self.working: bool | None = None  # None until the first status arrives
        self.ended = False                # the session's status was cleared
        self.queued: dict | None = None

    # ---- bookkeeping ----

    def _seen(self) -> list[str]:
        try:
            return _seen_file(self._dir, self._sid).read_text().split()
        except FileNotFoundError:
            return []

    def _mark_seen(self, rid: str) -> None:
        ids = [i for i in self._seen() if i != rid][-(SEEN_MAX - 1):] + [rid]
        self._dir.mkdir(parents=True, exist_ok=True)
        _seen_file(self._dir, self._sid).write_text("\n".join(ids) + "\n")

    def _state(self, rid: str, state: str) -> None:
        payload = json.dumps({"id": rid, "state": state}).encode()
        self.bus.publish(topics.reply_state(self._sid), payload, retain=True, qos=1)

    def _clear_reply(self) -> None:
        self.bus.publish(topics.reply(self._sid), b"", retain=True, qos=1)

    def _deliver(self, msg: dict) -> str:
        self.queued = None
        self._mark_seen(msg["id"])
        self._state(msg["id"], "delivered")
        self._clear_reply()
        turn = {"items": [{"k": "you", "t": msg["text"][:2000]}], "ts": int(time.time())}
        self.bus.publish(topics.turn(self._sid), json.dumps(turn, ensure_ascii=False).encode(), retain=True)
        return FRAME + msg["text"]

    # ---- events ----

    def on_status(self, payload: bytes) -> str | None:
        if not payload:
            self.ended = True
            return None
        try:
            state = json.loads(payload).get("state")
        except (ValueError, UnicodeDecodeError, AttributeError):
            return None
        if state not in ("idle", "running", "waiting"):
            return None
        self.working = state != "idle"
        if not self.working and self.queued:
            return self._deliver(self.queued)
        return None

    def on_reply(self, payload: bytes) -> str | None:
        msg = _parse(payload) if payload else None
        if msg is None:
            return None
        if msg["kind"] == "cancel":
            if self.queued and self.queued["id"] == msg["id"]:
                self.queued = None
                self._mark_seen(msg["id"])
                self._state(msg["id"], "cancelled")
                self._clear_reply()
            return None
        if msg["id"] in self._seen():
            return None
        self._state(msg["id"], "received")
        if msg["kind"] == "stop":
            self._mark_seen(msg["id"])
            if self.working:
                if self.queued:  # delivering it after the stop would start the agent again
                    self._mark_seen(self.queued["id"])
                    self._state(self.queued["id"], "cancelled")
                    self.queued = None
                self._dir.mkdir(parents=True, exist_ok=True)
                stop_flag(self._dir, self._sid).write_text(msg["id"])
                self._state(msg["id"], "stopping")
            else:
                self._state(msg["id"], "ignored")
            self._clear_reply()
            return None
        if self.working is False:
            return self._deliver(msg)
        self.queued = msg  # a newer reply replaces a held one
        self._state(msg["id"], "queued")
        return None


BACKOFF_MAX_S = 30


def run(bus_factory: Callable[[], Bus], sid: str, cfg: Config, pid: int, deadline: float,
        clock: Callable[[], float] = time.monotonic, sleep: Callable[[float], None] = time.sleep) -> str | None:
    """Listen until a reply can be delivered (returns its framed text) or this
    listener should go: replaced by a newer one, past the deadline, or the
    session ended (returns None). Broker trouble is retried with backoff."""
    state_dir = Path(cfg.state_dir)
    state_dir.mkdir(parents=True, exist_ok=True)
    pid_file(state_dir, sid).write_text(str(pid))
    lis: Listener | None = None
    backoff = 1
    while clock() < deadline:
        try:
            bus = bus_factory()
        except BusError:
            sleep(min(backoff, max(0.0, deadline - clock())))
            backoff = min(backoff * 2, BACKOFF_MAX_S)
            continue
        if lis is None:
            lis = Listener(bus, sid, state_dir)
        lis.bus = bus
        try:
            bus.subscribe(topics.status(sid))
            bus.subscribe(topics.reply(sid))
            while clock() < deadline:
                if pid_file(state_dir, sid).read_text().strip() != str(pid):
                    return None
                got = bus.next_message(1.0)
                if got is None:
                    continue
                topic, payload = got
                text = lis.on_status(payload) if topic == topics.status(sid) else lis.on_reply(payload)
                if text is not None:
                    return text
                if lis.ended:
                    return None
                backoff = 1
        except (BusError, FileNotFoundError):
            sleep(min(backoff, max(0.0, deadline - clock())))
            backoff = min(backoff * 2, BACKOFF_MAX_S)
        finally:
            try:
                bus.close()
            except Exception:
                pass
    return None
