"""Pull the current turn out of a Claude Code transcript (JSONL) for the PSV.

A turn starts at the last prompt a human typed. The PSV shows that prompt and
Claude's latest message only: narration between tool calls, tool calls,
thinking, subagent (sidechain) traffic and system notices are left out.
"""

import json
from pathlib import Path

TAIL_BYTES = 4 * 1024 * 1024
MAX_TEXT_CHARS = 4000
MAX_PROMPT_CHARS = 2000


def _clip(text: str, limit: int) -> str:
    return text if len(text) <= limit else text[:limit] + "…"


def _read_entries(path: Path) -> list[dict]:
    try:
        with open(path, "rb") as f:
            f.seek(0, 2)
            size = f.tell()
            f.seek(max(0, size - TAIL_BYTES))
            data = f.read()
    except OSError:
        return []
    lines = data.split(b"\n")
    if size > TAIL_BYTES:
        lines = lines[1:]  # first line is probably cut in half
    entries = []
    for line in lines:
        try:
            entry = json.loads(line)
        except ValueError:
            continue
        if isinstance(entry, dict):
            entries.append(entry)
    return entries


def _prompt_text(entry: dict) -> str | None:
    """The prompt a human typed, or None if this entry is not one."""
    if entry.get("type") != "user" or entry.get("isSidechain") or entry.get("isMeta"):
        return None
    content = (entry.get("message") or {}).get("content")
    origin = entry.get("origin")
    human = isinstance(origin, dict) and origin.get("kind") == "human"
    if isinstance(content, list) and human:  # text plus attachments
        parts = []
        for block in content:
            if isinstance(block, dict) and block.get("type") == "text" and block.get("text", "").strip():
                parts.append(block["text"].strip())
            elif isinstance(block, dict) and block.get("type") in ("image", "document"):
                parts.append(f"[{block['type']}]")
        text = " ".join(parts)
    elif isinstance(content, str):
        text = content.strip()
    else:
        return None
    if not text:
        return None
    if isinstance(origin, dict):
        return text if human else None
    return None if text.startswith("<") else text


def with_final_message(items: list[dict], text: str) -> list[dict]:
    """Swap in Claude's final reply from the Stop event.

    Stop can fire before the reply reaches the transcript, which would leave
    the prompt alone or an earlier narration line in its place.
    """
    text = text.strip()
    if not items or not text:
        return items
    return [items[0], {"k": "claude", "t": _clip(text, MAX_TEXT_CHARS)}]


def current_turn(path) -> list[dict]:
    entries = _read_entries(Path(path))
    start = next((i for i in range(len(entries) - 1, -1, -1) if _prompt_text(entries[i]) is not None), None)
    if start is None:
        return []
    prompt = {"k": "you", "t": _clip(_prompt_text(entries[start]), MAX_PROMPT_CHARS)}
    latest = None
    for entry in entries[start + 1:]:
        if entry.get("type") != "assistant" or entry.get("isSidechain"):
            continue
        for block in (entry.get("message") or {}).get("content") or []:
            if isinstance(block, dict) and block.get("type") == "text" and block.get("text", "").strip():
                latest = block["text"].strip()
    if latest is None:
        return [prompt]
    return [prompt, {"k": "claude", "t": _clip(latest, MAX_TEXT_CHARS)}]
