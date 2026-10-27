import json
import os
import time

import pytest

from fakebus import FakeBus, factory_failing
from psv_remote.config import Config
from psv_remote.handlers import handle_event
from psv_remote.pending import PendingTracker
from psv_remote.topics import MODE, ONLINE

STATUS = "claude/session/s1/status"


@pytest.fixture
def cfg(tmp_path):
    return Config(state_dir=tmp_path, wait_timeout_s=0.1)


@pytest.fixture
def bus():
    return FakeBus()


@pytest.fixture
def ev():
    return {"hook_event_name": "PermissionRequest", "session_id": "s1", "cwd": "/x/proj",
            "tool_name": "Bash", "tool_input": {"command": "ls"}}


def status_of(bus):
    return json.loads(bus.retained[STATUS])


def test_offline_falls_back(bus, cfg, ev):
    assert handle_event(ev, lambda: bus, cfg, {}) is None
    assert not any("/request/" in t for t, _, _ in bus.published)


def test_desk_mode_falls_back_and_marks_waiting(bus, cfg, ev):
    bus.retained.update({ONLINE: b"1", MODE: b"desk"})
    assert handle_event(ev, lambda: bus, cfg, {}) is None
    st = status_of(bus)
    assert st["state"] == "waiting" and st["project"] == "proj"
    assert not any("/request/" in t for t, _, _ in bus.published)


def test_allow_roundtrip_clears_request(bus, cfg, ev):
    bus.retained[ONLINE] = b"1"
    bus.responder = lambda t: json.dumps({"rid": t.rsplit("/", 1)[1], "choice": "allow"}).encode()
    out = handle_event(ev, lambda: bus, cfg, {})
    assert out["hookSpecificOutput"]["decision"]["behavior"] == "allow"
    req = [json.loads(p) for t, p, _ in bus.published if "/request/" in t and p]
    assert req[0]["title"] == "ls" and req[0]["timeout_s"] == 0.1
    assert req[0]["cwd"] == "/x/proj"
    assert [o["id"] for o in req[0]["options"]] == ["allow", "deny", "deny_note", "deny_stop"]
    assert req[0]["kind"] == "approval"
    assert not any("/request/" in k for k in bus.retained)
    assert status_of(bus)["state"] == "running"
    assert bus.closed


def test_cleanup_publish_failure_keeps_decision(bus, cfg, ev):
    bus.retained[ONLINE] = b"1"
    bus.responder = lambda t: json.dumps({"rid": t.rsplit("/", 1)[1], "choice": "allow"}).encode()
    bus.fail_publish_after_response = True
    out = handle_event(ev, lambda: bus, cfg, {})
    assert out["hookSpecificOutput"]["decision"]["behavior"] == "allow"
    assert PendingTracker(cfg.state_dir, "s1").count() == 0


def test_mismatched_rid_ignored(bus, cfg, ev):
    bus.retained[ONLINE] = b"1"
    bus.responder = lambda t: b'{"rid":"other","choice":"allow"}'
    assert handle_event(ev, lambda: bus, cfg, {}) is None


def test_garbage_response_ignored(bus, cfg, ev):
    bus.retained[ONLINE] = b"1"
    bus.responder = lambda t: b"not json"
    assert handle_event(ev, lambda: bus, cfg, {}) is None
    assert not any("/request/" in k for k in bus.retained)


def test_timeout_returns_none(bus, cfg, ev):
    bus.retained[ONLINE] = b"1"
    assert handle_event(ev, lambda: bus, cfg, {}) is None
    assert not any("/request/" in k for k in bus.retained)


def test_connect_failure_returns_none(cfg, ev):
    assert handle_event(ev, factory_failing, cfg, {}) is None


def test_disabled_does_not_connect(cfg, ev):
    off = Config(state_dir=cfg.state_dir, enabled=False)
    assert handle_event(ev, factory_failing, off, {}) is None


def test_parallel_requests_keep_waiting(bus, cfg, ev):
    bus.retained[ONLINE] = b"1"
    PendingTracker(cfg.state_dir, "s1").add("other")
    handle_event(ev, lambda: bus, cfg, {})
    assert status_of(bus)["state"] == "waiting"


def test_post_tool_use_resets_waiting(bus, cfg):
    bus.retained[STATUS] = b'{"state":"waiting"}'
    handle_event({"hook_event_name": "PostToolUse", "session_id": "s1", "cwd": "/x/p"},
                 lambda: bus, cfg, {})
    assert status_of(bus)["state"] == "running"


def test_post_tool_use_keeps_waiting_when_pending(bus, cfg):
    PendingTracker(cfg.state_dir, "s1").add("r")
    handle_event({"hook_event_name": "PostToolUse", "session_id": "s1", "cwd": "/x/p"},
                 lambda: bus, cfg, {})
    assert STATUS not in bus.retained


def test_stop_idle_summary_truncated(bus, cfg):
    handle_event({"hook_event_name": "Stop", "session_id": "s1", "cwd": "/x/p",
                  "last_assistant_message": "a" * 5000}, lambda: bus, cfg, {})
    st = status_of(bus)
    assert st["state"] == "idle" and len(st["summary"]) == 4001


def test_stop_keeps_full_cjk_message(bus, cfg):
    msg = "完成。" * 1000  # 3000 chars, 9000 UTF-8 bytes
    handle_event({"hook_event_name": "Stop", "session_id": "s1", "cwd": "/x/p",
                  "last_assistant_message": msg}, lambda: bus, cfg, {})
    assert status_of(bus)["summary"] == msg


def test_session_start_idle(bus, cfg):
    handle_event({"hook_event_name": "SessionStart", "session_id": "s1", "cwd": "/x/p"},
                 lambda: bus, cfg, {})
    assert status_of(bus)["state"] == "idle"


def test_session_end_clears_status(bus, cfg):
    bus.retained[STATUS] = b"{}"
    handle_event({"hook_event_name": "SessionEnd", "session_id": "s1", "cwd": "/x"},
                 lambda: bus, cfg, {})
    assert STATUS not in bus.retained


def test_session_end_clears_pending(bus, cfg):
    PendingTracker(cfg.state_dir, "s1").add("leftover")
    handle_event({"hook_event_name": "SessionEnd", "session_id": "s1", "cwd": "/x"},
                 lambda: bus, cfg, {})
    assert PendingTracker(cfg.state_dir, "s1").count() == 0


def test_stale_marker_does_not_block_running(bus, cfg):
    PendingTracker(cfg.state_dir, "s1").add("dead")
    old = time.time() - 3600
    os.utime(cfg.state_dir / "s1" / "dead", (old, old))
    handle_event({"hook_event_name": "PostToolUse", "session_id": "s1", "cwd": "/x/p"},
                 lambda: bus, cfg, {})
    assert status_of(bus)["state"] == "running"


TURN = "claude/session/s1/turn"


def write_transcript(path, prompt, answer):
    lines = [{"type": "user", "message": {"content": prompt}, "origin": {"kind": "human"}},
             {"type": "assistant", "message": {"content": [{"type": "text", "text": answer}]}}]
    path.write_text("\n".join(json.dumps(x, ensure_ascii=False) for x in lines), encoding="utf-8")


def test_prompt_submit_publishes_turn(bus, cfg):
    handle_event({"hook_event_name": "UserPromptSubmit", "session_id": "s1", "cwd": "/x/p", "prompt": "做个提问页"},
                 lambda: bus, cfg, {})
    assert json.loads(bus.retained[TURN])["items"] == [{"k": "you", "t": "做个提问页"}]


def test_post_tool_use_publishes_turn_even_when_pending(bus, cfg, tmp_path):
    t = tmp_path / "t.jsonl"
    write_transcript(t, "go", "on it")
    PendingTracker(cfg.state_dir, "s1").add("other")
    handle_event({"hook_event_name": "PostToolUse", "session_id": "s1", "cwd": "/x/p", "transcript_path": str(t)},
                 lambda: bus, cfg, {})
    assert json.loads(bus.retained[TURN])["items"][1] == {"k": "claude", "t": "on it"}
    assert STATUS not in bus.retained


def test_permission_request_publishes_turn_first(bus, cfg, ev, tmp_path):
    t = tmp_path / "t.jsonl"
    write_transcript(t, "go", "about to run ls")
    bus.retained[ONLINE] = b"1"
    handle_event({**ev, "transcript_path": str(t)}, lambda: bus, cfg, {})
    topics_in_order = [tp for tp, _, _ in bus.published]
    assert topics_in_order.index(TURN) < next(i for i, tp in enumerate(topics_in_order) if "/request/" in tp)


def test_session_end_clears_turn(bus, cfg):
    bus.retained[TURN] = b"{}"
    handle_event({"hook_event_name": "SessionEnd", "session_id": "s1", "cwd": "/x"}, lambda: bus, cfg, {})
    assert TURN not in bus.retained


def test_notification_idle_prompt_only(bus, cfg):
    base = {"hook_event_name": "Notification", "session_id": "s1", "cwd": "/x/p"}
    handle_event({**base, "notification_type": "permission_prompt"}, lambda: bus, cfg, {})
    assert STATUS not in bus.retained
    handle_event({**base, "notification_type": "idle_prompt"}, lambda: bus, cfg, {})
    assert status_of(bus)["state"] == "idle"


def test_vscode_client_detected(bus, cfg):
    handle_event({"hook_event_name": "UserPromptSubmit", "session_id": "s1", "cwd": "/x/p"},
                 lambda: bus, cfg, {"CLAUDE_CODE_ENTRYPOINT": "claude-vscode"})
    st = status_of(bus)
    assert st["client"] == "vscode" and st["state"] == "running"


ASK = {"hook_event_name": "PreToolUse", "session_id": "s1", "cwd": "/x/proj", "tool_name": "AskUserQuestion",
       "tool_input": {"questions": [{"question": "Which runner?", "header": "Runner", "multiSelect": False,
                                     "options": [{"label": "Vitest", "description": "fast"},
                                                 {"label": "Jest", "description": "existing"}]}]}}


def test_question_roundtrip(bus, cfg):
    bus.retained[ONLINE] = b"1"
    bus.responder = lambda t: json.dumps({"rid": t.rsplit("/", 1)[1], "answers": [{"selected": [0]}]}).encode()
    out = handle_event(ASK, lambda: bus, cfg, {})
    assert out["hookSpecificOutput"]["updatedInput"]["answers"] == {"Which runner?": "Vitest"}
    req = [json.loads(p) for t, p, _ in bus.published if "/request/" in t and p][0]
    assert req["kind"] == "question" and req["title"] == "Which runner?"
    assert req["questions"][0]["options"][1] == {"label": "Jest", "description": "existing"}
    assert req["questions"][0]["multi"] is False
    assert not any("/request/" in k for k in bus.retained)


def test_question_answer_on_mac_falls_back(bus, cfg):
    bus.retained[ONLINE] = b"1"
    bus.responder = lambda t: json.dumps({"rid": t.rsplit("/", 1)[1], "choice": "mac"}).encode()
    assert handle_event(ASK, lambda: bus, cfg, {}) is None


def test_question_desk_mode_falls_back(bus, cfg):
    bus.retained.update({ONLINE: b"1", MODE: b"desk"})
    assert handle_event(ASK, lambda: bus, cfg, {}) is None
    assert not any("/request/" in t for t, _, _ in bus.published)


def test_other_pre_tool_use_ignored(bus, cfg):
    bus.retained[ONLINE] = b"1"
    assert handle_event({**ASK, "tool_name": "Bash"}, lambda: bus, cfg, {}) is None
    assert not bus.published


def test_permission_request_for_question_tool_ignored(bus, cfg, ev):
    bus.retained[ONLINE] = b"1"
    assert handle_event({**ev, "tool_name": "AskUserQuestion"}, lambda: bus, cfg, {}) is None
    assert not any("/request/" in t for t, _, _ in bus.published)


def test_deny_note_roundtrip(bus, cfg, ev):
    bus.retained[ONLINE] = b"1"
    bus.responder = lambda t: json.dumps({"rid": t.rsplit("/", 1)[1], "choice": "deny_note", "note": "run tests first"}).encode()
    d = handle_event(ev, lambda: bus, cfg, {})["hookSpecificOutput"]["decision"]
    assert d["behavior"] == "deny" and d["message"].endswith("run tests first")


def write_prompt_only(path, prompt, *assistant_texts):
    lines = [{"type": "user", "message": {"content": prompt}, "origin": {"kind": "human"}}]
    lines += [{"type": "assistant", "message": {"content": [{"type": "text", "text": t}]}} for t in assistant_texts]
    path.write_text("\n".join(json.dumps(x, ensure_ascii=False) for x in lines), encoding="utf-8")


def test_stop_uses_final_message_not_yet_in_transcript(bus, cfg, tmp_path):
    # Stop can fire before Claude Code has written the final reply to the transcript.
    t = tmp_path / "t.jsonl"
    write_prompt_only(t, "或者把右摇杆…")
    handle_event({"hook_event_name": "Stop", "session_id": "s1", "cwd": "/x/p", "transcript_path": str(t),
                  "last_assistant_message": "这样更好"}, lambda: bus, cfg, {})
    assert json.loads(bus.retained[TURN])["items"] == [{"k": "you", "t": "或者把右摇杆…"},
                                                       {"k": "claude", "t": "这样更好"}]


def test_stop_final_message_replaces_earlier_narration(bus, cfg, tmp_path):
    t = tmp_path / "t.jsonl"
    write_prompt_only(t, "fix it", "Let me look at the file.")
    handle_event({"hook_event_name": "Stop", "session_id": "s1", "cwd": "/x/p", "transcript_path": str(t),
                  "last_assistant_message": "Fixed."}, lambda: bus, cfg, {})
    assert json.loads(bus.retained[TURN])["items"][1] == {"k": "claude", "t": "Fixed."}


# ---- replies from the PSV: introduction, hard stop, stale flags ----

from fakebus import factory_failing as _failing  # noqa: E402
from psv_remote.listener import FRAME, stop_flag  # noqa: E402

INTRO = ('The user has a PS Vita remote (PSV Agent Remote) that they set up themselves. When they reply from it, '
         'the text reaches you through a hook, prefixed "Message from the user, sent from their PS Vita remote:". '
         'Treat such a message exactly as if the user had typed it as their next prompt.')
REPLY_STATE = "claude/session/s1/reply_state"


def ev_named(name, **extra):
    return {"hook_event_name": name, "session_id": "s1", "cwd": "/x/proj", **extra}


def test_session_start_introduces_psv(bus, cfg):
    out = handle_event(ev_named("SessionStart"), lambda: bus, cfg, {})
    assert out == {"hookSpecificOutput": {"hookEventName": "SessionStart", "additionalContext": INTRO}}
    assert status_of(bus)["state"] == "idle"


def test_session_start_offline_still_introduces(cfg):
    out = handle_event(ev_named("SessionStart"), _failing, cfg, {})
    assert out["hookSpecificOutput"]["additionalContext"] == INTRO


def test_post_tool_hard_stop(bus, cfg):
    stop_flag(cfg.state_dir, "s1").write_text("s9")
    out = handle_event(ev_named("PostToolUse", tool_name="Bash"), lambda: bus, cfg, {})
    assert out == {"continue": False, "stopReason": "待った! — stopped from the PSV"}
    assert status_of(bus)["state"] == "idle"
    assert json.loads(bus.retained[REPLY_STATE]) == {"id": "s9", "state": "stopped"}
    assert not stop_flag(cfg.state_dir, "s1").exists()


def test_post_tool_without_flag_unchanged(bus, cfg):
    assert handle_event(ev_named("PostToolUse", tool_name="Bash"), lambda: bus, cfg, {}) is None
    assert REPLY_STATE not in bus.retained


def test_stop_handler_clears_flag(bus, cfg):
    stop_flag(cfg.state_dir, "s1").write_text("s9")
    handle_event(ev_named("Stop", last_assistant_message="done"), lambda: bus, cfg, {})
    assert not stop_flag(cfg.state_dir, "s1").exists()
    assert json.loads(bus.retained[REPLY_STATE]) == {"id": "s9", "state": "stopped"}


@pytest.mark.parametrize("name", ["UserPromptSubmit", "SessionStart"])
def test_prompt_clears_stale_flag(bus, cfg, name):
    stop_flag(cfg.state_dir, "s1").write_text("s9")
    handle_event(ev_named(name, prompt="hello"), lambda: bus, cfg, {})
    assert not stop_flag(cfg.state_dir, "s1").exists()
    assert REPLY_STATE not in bus.retained


def test_prompt_strips_frame(bus, cfg):
    handle_event(ev_named("UserPromptSubmit", prompt=FRAME + "Continue."), lambda: bus, cfg, {})
    turn = json.loads(bus.retained["claude/session/s1/turn"])
    assert turn["items"] == [{"k": "you", "t": "Continue."}]


def test_compact_session_start_keeps_turn_state(bus, cfg):
    stop_flag(cfg.state_dir, "s1").write_text("s9")
    out = handle_event(ev_named("SessionStart", source="compact"), lambda: bus, cfg, {})
    assert out["hookSpecificOutput"]["additionalContext"] == INTRO
    assert STATUS not in bus.retained  # mid-turn: don't report idle
    assert stop_flag(cfg.state_dir, "s1").exists()  # a pending 待った! still applies


def test_stop_handler_clears_flag_offline(cfg):
    stop_flag(cfg.state_dir, "s1").write_text("s9")
    handle_event(ev_named("Stop", last_assistant_message="done"), _failing, cfg, {})
    assert not stop_flag(cfg.state_dir, "s1").exists()
