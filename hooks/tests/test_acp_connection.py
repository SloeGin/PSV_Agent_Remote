
import pytest

from psv_remote.acp_connection import AcpConnection, _classify_startup, _content_text
from psv_remote.rpc import NativeError, RpcError


class FakePeer:
    """Stands in for the JSON-RPC Peer: queued events, recorded sends/calls."""

    def __init__(self, events=()):
        self.events = list(events)
        self.sent = []
        self.closed = False

    def drain(self):
        events, self.events = self.events, []
        return events

    def send(self, message):
        self.sent.append(message)

    def call(self, message, timeout=5):  # unused in these tests
        raise AssertionError("no call expected")

    def begin(self, message):
        # Record the prompt request; it never settles in these unit tests.
        self.sent.append(message)
        return "prompt-key", object()

    def poll(self, key, inbox):
        return None  # the turn stays outstanding

    def finish(self, key):
        pass

    def close(self):
        self.closed = True


def conn(events=(), session_id="s1"):
    """An AcpConnection wired to a fake peer, bypassing process spawn/handshake."""
    c = AcpConnection.__new__(AcpConnection)
    c.cfg = None
    c.cwd = "/project"
    c.session_id = session_id
    c.mode_id = "vibe"
    c._turn_items = []
    c._turn_id = None
    c._turn_active = False
    c._prompt_key = None
    c._prompt_inbox = None
    c._echoed_user = None
    c._permissions = {}
    c._process = None
    c.peer = FakePeer(events)
    return c


def update(kind, **fields):
    return {"method": "session/update",
            "params": {"sessionId": "s1", "update": {"sessionUpdate": kind, **fields}}}


def permission_request(request_id="perm-1", session="s1"):
    return {"method": "session/request_permission", "id": request_id,
            "params": {"sessionId": session, "toolCall": {"toolCallId": "c1", "title": "Write File"},
                       "options": [{"optionId": "accept", "kind": "allow_once"},
                                   {"optionId": "reject", "kind": "reject_once"}]}}


# ---- replica from the update stream ----

def test_message_chunks_accumulate_into_turn_items():
    c = conn([
        update("user_message_chunk", content={"type": "text", "text": "do it"}),
        update("agent_message_chunk", content={"type": "text", "text": "on "}),
        update("agent_message_chunk", content={"type": "text", "text": "it"}),
        update("tool_call", title="Write File"),
    ])
    snap = c.snapshot()
    assert snap["items"] == [
        {"k": "you", "t": "do it"},
        {"k": "assistant", "t": "on it"},
        {"k": "tool", "t": "Write File"},
    ]


def test_lifecycle_updates_drive_turn_active_and_state():
    c = conn([update("session_info_update", _meta={"kiro": {"kind": "turn_start"}})])
    assert c.snapshot()["state"] == "running"
    c.peer.events = [update("session_info_update", _meta={"kiro": {"kind": "turn_end"}})]
    assert c.snapshot()["state"] == "idle"


def test_start_echoes_the_user_message_into_the_turn():
    c = conn()
    c.start("write the file", "psv-1")
    items = c.snapshot()["items"]
    assert items[0] == {"k": "you", "t": "write the file"}


def test_streamed_user_echo_is_not_duplicated():
    c = conn()
    c.start("do the thing", "psv-1")
    # Kiro replays the same message as a user_message_chunk; it must not double.
    c.peer.events = [update("user_message_chunk", content={"type": "text", "text": "do the thing"}),
                     update("agent_message_chunk", content={"type": "text", "text": "ok"})]
    items = c.snapshot()["items"]
    assert items == [{"k": "you", "t": "do the thing"}, {"k": "assistant", "t": "ok"}]


def test_steer_echoes_into_the_conversation():
    c = conn()
    c._turn_active, c._turn_id, c._turn_items = True, "turn-x", [{"k": "assistant", "t": "working"}]
    c.steer("actually use vitest", "psv-2")
    assert {"k": "you", "t": "actually use vitest"} in c.snapshot()["items"]


def test_turn_start_after_prompt_keeps_the_user_message():
    # Real Kiro order: the turn's session_info_update(turn_start) arrives after
    # the prompt was sent. It must not wipe the echoed "you" line.
    c = conn()
    c.start("write the file", "psv-1")
    c.peer.events = [update("session_info_update", _meta={"kiro": {"kind": "turn_start"}}),
                     update("agent_message_chunk", content={"type": "text", "text": "done"})]
    assert c.snapshot()["items"] == [{"k": "you", "t": "write the file"},
                                     {"k": "assistant", "t": "done"}]


def test_a_new_prompt_replaces_the_previous_turn():
    c = conn()
    c._turn_items = [{"k": "you", "t": "old"}, {"k": "assistant", "t": "old reply"}]
    c.start("new", "psv-2")
    assert c.snapshot()["items"] == [{"k": "you", "t": "new"}]


def test_updates_for_other_sessions_are_ignored():
    c = conn([{"method": "session/update", "params": {"sessionId": "other",
              "update": {"sessionUpdate": "agent_message_chunk",
                         "content": {"type": "text", "text": "leak"}}}}])
    assert c.snapshot()["items"] == []


# ---- permission requests ----

def test_permission_request_is_recorded_not_answered():
    c = conn([permission_request()])
    snap = c.snapshot()
    assert snap["state"] == "waiting"
    assert [r["id"] for r in snap["requests"]] == ["perm-1"]
    # Nothing was sent back yet; the bridge answers via respond().
    assert c.peer.sent == []


def test_respond_sends_outcome_and_clears_pending():
    c = conn([permission_request()])
    c.snapshot()  # pump records the request
    request = {"id": "perm-1", "params": {}}
    c.respond(request, {"outcome": {"outcome": "selected", "optionId": "reject"}})
    assert c.peer.sent[-1] == {"id": "perm-1",
                               "result": {"outcome": {"outcome": "selected", "optionId": "reject"}}}
    assert c.snapshot()["requests"] == []


def test_respond_forwards_consent_meta():
    c = conn([permission_request()])
    c.snapshot()
    c.respond({"id": "perm-1"}, {"outcome": {"outcome": "selected", "optionId": "accept"},
                                 "_meta": {"kiro": {"consent": {"scope": "workspace"}}}})
    assert c.peer.sent[-1]["result"]["_meta"] == {"kiro": {"consent": {"scope": "workspace"}}}


def test_respond_to_unknown_request_raises():
    c = conn()
    with pytest.raises(RpcError):
        c.respond({"id": "gone"}, {"outcome": {"outcome": "cancelled"}})


def test_auth_request_is_refused_not_hung():
    c = conn([{"method": "_kiro/auth/getAccessToken", "id": "auth-9", "params": {}}])
    c.snapshot()
    refusal = next(m for m in c.peer.sent if m.get("id") == "auth-9")
    assert refusal["error"]["code"] == -32601
    assert "result" not in refusal


# ---- reply / stop ----

def test_start_sends_prompt_when_idle_and_returns_turn_id():
    c = conn()
    turn = c.start("hello", "psv-abc")
    assert turn == "psv-abc"
    prompt = c.peer.sent[-1]
    assert prompt["method"] == "session/prompt"
    assert prompt["params"]["sessionId"] == "s1"
    assert prompt["params"]["prompt"] == [{"type": "text", "text": "hello"}]
    assert c.snapshot()["state"] == "running"


def test_start_refused_when_a_turn_is_active():
    c = conn([update("session_info_update", _meta={"kiro": {"kind": "turn_start"}})])
    c.snapshot()
    assert c.start("hello", "psv-abc") is None
    assert not any(m.get("method") == "session/prompt" for m in c.peer.sent)


def test_steer_only_during_active_turn():
    c = conn()
    assert c.steer("adjust", "psv-s") is None  # idle: nothing to steer
    c._turn_active, c._turn_id = True, "turn-x"
    assert c.steer("adjust", "psv-s") == "turn-x"
    assert c.peer.sent[-1]["method"] == "_session/steer"


def test_stop_cancels_only_the_matching_active_turn():
    c = conn()
    c._turn_active, c._turn_id = True, "turn-x"
    assert c.stop("turn-y") is None  # mismatched turn id
    assert c.stop("turn-x") == "turn-x"
    assert c.peer.sent[-1] == {"method": "session/cancel", "params": {"sessionId": "s1"}}


# ---- helpers ----

def test_classify_startup_detects_login_and_flags():
    assert "login" in _classify_startup(b"error: You are not logged in").lower()
    assert "flags" in _classify_startup(b"unexpected argument '--agent-engine=v3'").lower()
    assert _classify_startup(b"some other failure") is None


def test_content_text_handles_blocks_and_lists():
    assert _content_text("plain") == "plain"
    assert _content_text({"type": "text", "text": "x"}) == "x"
    assert _content_text({"type": "image"}) == ""
    assert _content_text([{"type": "text", "text": "a"}, {"type": "text", "text": "b"}]) == "ab"


# ---- review fixes: pending permissions never outlive their turn ----

class SettlingPeer(FakePeer):
    """A peer whose prompt request has settled (the turn ended)."""

    def poll(self, key, inbox):
        return {"stopReason": "end_turn"}


def test_stop_cancels_pending_permissions():
    c = conn([permission_request("perm-1")])
    c.start("go", "m1")
    assert c.snapshot()["state"] == "waiting"
    assert c.stop("m1") == "m1"
    cancels = [m for m in c.peer.sent if m.get("id") == "perm-1"]
    assert cancels == [{"id": "perm-1", "result": {"outcome": {"outcome": "cancelled"}}}]
    snap = c.snapshot()
    assert snap["requests"] == [] and snap["state"] != "waiting"


def test_turn_end_drops_unanswered_permissions():
    c = conn()
    c.start("go", "m1")
    c.peer = SettlingPeer([permission_request("perm-2")])
    snap = c.snapshot()
    assert snap["requests"] == [] and snap["state"] == "idle"
    assert {"id": "perm-2", "result": {"outcome": {"outcome": "cancelled"}}} in c.peer.sent


def test_stderr_goes_to_a_file_not_a_pipe(monkeypatch):
    import subprocess
    seen = {}

    class FakeProcess:
        def __init__(self, command, **kwargs):
            seen.update(kwargs)
            kwargs["stderr"].write(b"error: Please log in first\n")
            kwargs["stderr"].flush()

        def terminate(self):
            pass

        def wait(self, timeout=None):
            return 0

    monkeypatch.setattr(subprocess, "Popen", FakeProcess)
    c = AcpConnection.__new__(AcpConnection)
    c._process = c._spawn("kiro-cli", "v3", None)
    # A pipe nobody reads fills up and blocks kiro-cli; a file never does.
    assert seen["stderr"] is not subprocess.PIPE and hasattr(seen["stderr"], "seek")
    assert str(c._start_error("fallback")) == "Kiro CLI is not logged in; run `kiro-cli login`"
