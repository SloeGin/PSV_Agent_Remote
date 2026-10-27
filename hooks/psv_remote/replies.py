"""Shared reply validation and durable deduplication, independent of delivery."""

import json
import os
from pathlib import Path

from .pending import _safe


def parse_reply(payload: bytes) -> dict | None:
    try:
        message = json.loads(payload)
    except (ValueError, UnicodeDecodeError):
        return None
    if not isinstance(message, dict):
        return None
    rid, kind, text = message.get("id"), message.get("kind"), message.get("text", "")
    if (not isinstance(rid, str) or not rid or len(rid) > 64
            or kind not in ("text", "stop", "cancel") or not isinstance(text, str)):
        return None
    if kind == "text" and not text.strip():
        return None
    return {"id": rid, "kind": kind, "text": text.strip()}


class ReplyLedger:
    def __init__(self, state_dir: Path, sid: str):
        self.path = Path(state_dir) / f"{_safe(sid)}.replies"

    def seen(self) -> list[str]:
        try:
            return self.path.read_text().split()
        except FileNotFoundError:
            return []

    def mark(self, rid: str) -> None:
        ids = [value for value in self.seen() if value != rid][-31:] + [rid]
        self.path.parent.mkdir(parents=True, exist_ok=True)
        temporary = self.path.with_name(f"{self.path.name}.{os.getpid()}")
        temporary.write_text("\n".join(ids) + "\n")
        os.replace(temporary, self.path)
