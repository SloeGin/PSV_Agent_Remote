"""Retained updates a hook couldn't deliver, handed to the session's listener.

A hook lives for one event and has a few seconds to reach the broker. When the
network is slow or down, its status and turn would be lost, and the Vita would
keep showing an old state (a session "working" long after it finished). The
hook saves them here instead; the session's background listener, which stays
connected and reconnects on its own, publishes them as soon as it can.
"""

import json
import os
from pathlib import Path

from .bus import BusError
from .pending import _safe


def _path(state_dir: Path, sid: str) -> Path:
    return Path(state_dir) / f"{_safe(sid)}.deferred.json"


def save(state_dir: Path, sid: str, publishes: list[tuple[str, bytes]]) -> None:
    """Replace the saved updates with these (topic, payload) retained publishes."""
    path = _path(state_dir, sid)
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(f"{path.name}.{os.getpid()}")
    tmp.write_text(json.dumps([[topic, payload.decode("utf-8")] for topic, payload in publishes],
                              ensure_ascii=False), encoding="utf-8")
    os.replace(tmp, path)  # a reader never sees half a file


def take(state_dir: Path, sid: str) -> list[tuple[str, bytes]]:
    """Return and remove the saved updates (empty if none or unreadable)."""
    path = _path(state_dir, sid)
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
        path.unlink()
    except FileNotFoundError:
        return []
    except (ValueError, OSError):
        path.unlink(missing_ok=True)
        return []
    return [(topic, payload.encode("utf-8")) for topic, payload in data
            if isinstance(topic, str) and isinstance(payload, str)]


def restore(state_dir: Path, sid: str, publishes: list[tuple[str, bytes]]) -> None:
    """Put back updates that could not be published, unless a hook saved newer ones."""
    if publishes and not _path(state_dir, sid).exists():
        save(state_dir, sid, publishes)


def clear(state_dir: Path, sid: str) -> None:
    _path(state_dir, sid).unlink(missing_ok=True)


class RecordingBus:
    """Stands in for the broker while offline: keeps the retained publishes, in order."""

    def __init__(self):
        self.publishes: list[tuple[str, bytes]] = []

    def publish(self, topic: str, payload: bytes, retain: bool = False, qos: int = 0) -> None:
        if retain:
            self.publishes = [p for p in self.publishes if p[0] != topic] + [(topic, payload)]

    def read_retained(self, topic, wait_s):
        raise BusError("offline")

    def subscribe(self, topic):
        raise BusError("offline")

    def wait_for(self, topic, timeout_s):
        raise BusError("offline")

    def next_message(self, timeout_s):
        raise BusError("offline")

    def close(self):
        pass
