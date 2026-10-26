"""Hook event handlers. Return the JSON to print, or None to stay silent."""

import json
import os
import time
import uuid
from collections.abc import Callable, Mapping

from . import topics
from .bus import Bus, BusError
from .config import Config
from .decision import build_options, decision_output, question_output
from .pending import PendingTracker
from .render import render_request
from .transcript import current_turn, with_final_message

SUMMARY_CHARS = 4000  # the PSV shows the whole last message, scrollable
QUESTION_TOOL = "AskUserQuestion"


def _dump(obj) -> bytes:
    return json.dumps(obj, ensure_ascii=False).encode("utf-8")


def _publish_status(bus: Bus, event: dict, env: Mapping[str, str], state: str, summary: str = "") -> None:
    cwd = event.get("cwd", "")
    entry = env.get("CLAUDE_CODE_ENTRYPOINT", "").lower()
    payload = {
        "state": state,
        "project": os.path.basename(cwd.rstrip("/")) or cwd,
        "cwd": cwd,
        "client": "vscode" if "vscode" in entry else "cli",
        "summary": summary,
        "ts": int(time.time()),
    }
    bus.publish(topics.status(event["session_id"]), _dump(payload), retain=True)


def _publish_turn(bus: Bus, event: dict, items: list[dict] | None = None) -> None:
    if items is None:
        path = event.get("transcript_path")
        items = current_turn(path) if path else []
        if not items:
            return  # nothing readable: keep whatever the PSV already shows
    bus.publish(topics.turn(event["session_id"]), _dump({"items": items, "ts": int(time.time())}), retain=True)


def _tracker(cfg: Config, sid: str) -> PendingTracker:
    # A marker outliving the hook's own timeout was left by a killed hook.
    return PendingTracker(cfg.state_dir, sid, max_age_s=cfg.wait_timeout_s + 10)


def _summary(text: str) -> str:
    return text if len(text) <= SUMMARY_CHARS else text[:SUMMARY_CHARS] + "…"


def _quietly(fn, *args, **kwargs) -> None:
    try:
        fn(*args, **kwargs)
    except BusError:
        pass


def _ask_psv(event: dict, bus: Bus, cfg: Config, env: Mapping[str, str], request: dict) -> dict | None:
    """Publish a request to the PSV and wait for the matching response message.

    Returns the response dict, or None for offline / desk mode / timeout / junk,
    in which case the caller stays silent and Claude Code asks on the Mac.
    """
    sid = event["session_id"]
    if bus.read_retained(topics.ONLINE, cfg.retained_wait_s) != b"1":
        return None
    _quietly(_publish_turn, bus, event)
    if bus.read_retained(topics.MODE, cfg.retained_wait_s) == b"desk":
        _publish_status(bus, event, env, "waiting", f"请在电脑上处理：{request['title']}")
        return None

    rid = uuid.uuid4().hex[:12]
    tracker = _tracker(cfg, sid)
    tracker.add(rid)
    msg = None
    try:
        bus.subscribe(topics.response(sid, rid))
        payload = {**request, "rid": rid, "timeout_s": cfg.wait_timeout_s, "cwd": event.get("cwd", ""),
                   "ts": int(time.time())}
        bus.publish(topics.request(sid, rid), _dump(payload), retain=True)
        _publish_status(bus, event, env, "waiting", request["title"])
        raw = bus.wait_for(topics.response(sid, rid), cfg.wait_timeout_s)
        try:
            msg = json.loads(raw) if raw else None
        except ValueError:
            msg = None
    finally:
        # Cleanup is best effort: a dropped connection must not lose the answer.
        tracker.remove(rid)
        _quietly(bus.publish, topics.request(sid, rid), b"", retain=True)
        if tracker.count() == 0:
            _quietly(_publish_status, bus, event, env, "running")
    return msg if isinstance(msg, dict) and msg.get("rid") == rid else None


def _permission(event: dict, bus: Bus, cfg: Config, env: Mapping[str, str]) -> dict | None:
    if event.get("tool_name") == QUESTION_TOOL:
        return None  # questions are answered from PreToolUse
    title, body = render_request(event.get("tool_name", ""), event.get("tool_input") or {})
    suggestions = event.get("permission_suggestions")
    request = {"kind": "approval", "tool": event.get("tool_name", ""), "title": title, "body": body,
               "options": build_options(suggestions)}
    msg = _ask_psv(event, bus, cfg, env, request)
    if not msg or not isinstance(msg.get("choice"), str):
        return None
    return decision_output(msg["choice"], suggestions, msg.get("note"))


def _question(event: dict, bus: Bus, cfg: Config, env: Mapping[str, str]) -> dict | None:
    tool_input = event.get("tool_input") or {}
    questions = tool_input.get("questions")
    if not isinstance(questions, list) or not questions:
        return None
    request = {
        "kind": "question",
        "tool": QUESTION_TOOL,
        "title": str(questions[0].get("question", "")),
        "questions": [{
            "header": str(q.get("header", "")),
            "question": str(q.get("question", "")),
            "multi": bool(q.get("multiSelect")),
            "options": [{"label": str(o.get("label", "")), "description": str(o.get("description", ""))}
                        for o in (q.get("options") or [])],
        } for q in questions],
    }
    msg = _ask_psv(event, bus, cfg, env, request)
    if not msg or msg.get("choice") == "mac":
        return None
    return question_output(tool_input, msg.get("answers"))


def _status_event(event: dict, bus: Bus, cfg: Config, env: Mapping[str, str]) -> None:
    name = event["hook_event_name"]
    sid = event["session_id"]
    if name == "SessionStart":
        _publish_status(bus, event, env, "idle")
    elif name == "UserPromptSubmit":
        _publish_status(bus, event, env, "running")
        prompt = event.get("prompt")
        if isinstance(prompt, str) and prompt.strip():
            _publish_turn(bus, event, [{"k": "you", "t": prompt.strip()[:2000]}])
    elif name == "PostToolUse":
        _publish_turn(bus, event)
        if _tracker(cfg, sid).count() == 0:
            _publish_status(bus, event, env, "running")
    elif name == "Stop":
        path = event.get("transcript_path")
        items = with_final_message(current_turn(path) if path else [], event.get("last_assistant_message") or "")
        if items:
            _publish_turn(bus, event, items)
        _publish_status(bus, event, env, "idle", _summary(event.get("last_assistant_message") or ""))
    elif name == "Notification":
        _publish_status(bus, event, env, "idle")
    elif name == "SessionEnd":
        _tracker(cfg, sid).clear()
        bus.publish(topics.status(sid), b"", retain=True)
        bus.publish(topics.turn(sid), b"", retain=True)


_STATUS_EVENTS = {"SessionStart", "UserPromptSubmit", "PostToolUse", "Stop", "Notification", "SessionEnd"}


def _wants_bus(event: dict) -> bool:
    name = event.get("hook_event_name")
    if name == "Notification":
        return event.get("notification_type") == "idle_prompt"
    if name == "PreToolUse":
        return event.get("tool_name") == QUESTION_TOOL
    return name == "PermissionRequest" or name in _STATUS_EVENTS


def handle_event(event: dict, bus_factory: Callable[[], Bus], cfg: Config,
                 env: Mapping[str, str]) -> dict | None:
    name = event.get("hook_event_name")
    if not cfg.enabled or not event.get("session_id") or not _wants_bus(event):
        return None
    try:
        bus = bus_factory()
    except BusError:
        return None
    try:
        if name == "PermissionRequest":
            return _permission(event, bus, cfg, env)
        if name == "PreToolUse":
            return _question(event, bus, cfg, env)
        _status_event(event, bus, cfg, env)
        return None
    except BusError:
        return None
    finally:
        _quietly(bus.close)
