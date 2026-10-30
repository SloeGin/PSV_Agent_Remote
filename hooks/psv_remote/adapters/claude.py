"""Claude Code adapter: native hook events and hook-specific decisions."""

from collections.abc import Callable, Mapping

from .. import topics
from ..bus import Bus, BusError
from ..config import Config
from ..decision import build_options, decision_output, question_output
from ..listener import psv_reply_text, stop_flag
from ..render import render_request
from ..transcript import current_turn, with_final_message
from .. import remote

SUMMARY_CHARS = 4000  # the PSV shows the whole last message, scrollable
QUESTION_TOOL = "AskUserQuestion"
STOP_REASON = "待った! — stopped from the PSV"
INTRO = ('The user has a PS Vita remote (PSV Agent Remote) that they set up themselves. When they reply from it, '
         'the text reaches you through a hook, prefixed "Message from the user, sent from their PS Vita remote:". '
         'Treat such a message exactly as if the user had typed it as their next prompt.')


def _session(event: dict, env: Mapping[str, str]) -> remote.Session:
    entry = env.get("CLAUDE_CODE_ENTRYPOINT", "").lower()
    return remote.Session(event["session_id"], event.get("cwd", ""), "vscode" if "vscode" in entry else "cli")


def _publish_status(bus: Bus, event: dict, env: Mapping[str, str], state: str, summary: str = "") -> None:
    remote.publish_status(bus, _session(event, env), state, summary)


def _publish_turn(bus: Bus, event: dict, items: list[dict] | None = None) -> None:
    if items is None:
        path = event.get("transcript_path")
        items = current_turn(path) if path else []
        if not items:
            return
    remote.publish_turn(bus, event["session_id"], items)


_tracker = remote.tracker
_quietly = remote.quietly


def _summary(text: str) -> str:
    return text if len(text) <= SUMMARY_CHARS else text[:SUMMARY_CHARS] + "…"


def _ask_psv(event: dict, bus: Bus, cfg: Config, env: Mapping[str, str], request: dict) -> dict | None:
    return remote.ask(bus, cfg, _session(event, env), request,
                      prepare=lambda: _publish_turn(bus, event),
                      state=lambda state, summary: _publish_status(bus, event, env, state, summary))


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


def _take_stop_flag(cfg: Config, sid: str) -> str | None:
    """Removes a pending 待った! and returns its reply id, or None if there was none."""
    flag = stop_flag(cfg.state_dir, sid)
    try:
        rid = flag.read_text().strip()
        flag.unlink()
    except FileNotFoundError:
        return None
    return rid


def _publish_stopped(bus: Bus, sid: str, rid: str) -> None:
    remote.publish_reply_state(bus, sid, rid, "stopped")


def _hard_stop(event: dict, bus_factory: Callable[[], Bus], env: Mapping[str, str], rid: str) -> dict:
    # Stop won't fire after continue:false, so report the idle state here.
    try:
        bus = bus_factory()
    except BusError:
        return {"continue": False, "stopReason": STOP_REASON}
    try:
        _publish_turn(bus, event)
        _publish_status(bus, event, env, "idle")
        _publish_stopped(bus, event["session_id"], rid)
    except BusError:
        pass
    finally:
        _quietly(bus.close)
    return {"continue": False, "stopReason": STOP_REASON}


def _status_event(event: dict, bus: Bus, cfg: Config, env: Mapping[str, str]) -> None:
    name = event["hook_event_name"]
    sid = event["session_id"]
    if name == "SessionStart":
        if event.get("source") != "compact":  # a compaction can happen mid-turn
            _publish_status(bus, event, env, "idle")
    elif name == "UserPromptSubmit":
        _publish_status(bus, event, env, "running")
        prompt = event.get("prompt")
        if isinstance(prompt, str):
            prompt = psv_reply_text(prompt) or prompt  # show a PSV reply as the user's own words
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
    intro = None
    stopped = None
    compact = name == "SessionStart" and event.get("source") == "compact"
    if name in ("SessionStart", "UserPromptSubmit") and not compact:
        _take_stop_flag(cfg, event["session_id"])  # a new turn: a leftover 待った! must not kill it
    if name == "Stop":
        stopped = _take_stop_flag(cfg, event["session_id"])  # 待った! after the last tool: the turn ended anyway
    if name == "SessionStart":
        intro = {"hookSpecificOutput": {"hookEventName": "SessionStart", "additionalContext": INTRO}}
    if name == "PostToolUse":
        rid = _take_stop_flag(cfg, event["session_id"])
        if rid is not None:
            return _hard_stop(event, bus_factory, env, rid)
    try:
        bus = bus_factory()
    except BusError:
        return intro
    try:
        if name == "PermissionRequest":
            return _permission(event, bus, cfg, env)
        if name == "PreToolUse":
            return _question(event, bus, cfg, env)
        _status_event(event, bus, cfg, env)
        if stopped is not None:
            _publish_stopped(bus, event["session_id"], stopped)
        return intro
    except BusError:
        return intro
    finally:
        _quietly(bus.close)
