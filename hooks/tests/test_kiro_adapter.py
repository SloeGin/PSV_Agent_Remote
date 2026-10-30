
from psv_remote.adapters.kiro import remote_request, request_key, session_view
from psv_remote.remote import scoped_session_id

FOUR_OPTIONS = [
    {"optionId": "accept", "name": "Allow", "kind": "allow_once"},
    {"optionId": "always-accept", "name": "Always allow", "kind": "allow_always"},
    {"optionId": "reject", "name": "Deny", "kind": "reject_once"},
    {"optionId": "always-reject", "name": "Always deny", "kind": "reject_always"},
]


def permission(options=None, consent=None, title="Write File"):
    params = {
        "sessionId": "s1",
        "toolCall": {"toolCallId": "call-1", "title": title, "status": "pending"},
        "options": options if options is not None else FOUR_OPTIONS,
    }
    if consent is not None:
        params["_meta"] = {"kiro": {"consent": consent}}
    return {"id": "perm-1", "params": params}


def snapshot(state="running", requests=None, items=None, active="turn-1"):
    return {
        "id": "native-thread", "cwd": "/project", "state": state,
        "activeTurnId": active if state == "running" else None,
        "items": items or [{"k": "you", "t": "hi"}, {"k": "assistant", "t": "hello"}],
        "modeId": "vibe",
        "requests": requests or [],
    }


def test_scoped_id_fits_vita_and_distinguishes_agents():
    sid = scoped_session_id("kiro", "host", "thread")
    assert len(sid.encode()) < 64 and "/" not in sid
    assert sid != scoped_session_id("codex", "host", "thread")


def test_session_view_reads_snapshot():
    view = session_view(snapshot(requests=[permission()]))
    assert view.native_id == "native-thread"
    assert view.cwd == "/project"
    assert view.state == "running"
    assert view.active_turn == "turn-1"
    assert [i["k"] for i in view.items] == ["you", "assistant"]
    assert len(view.pending) == 1


def test_idle_snapshot_has_no_active_turn():
    view = session_view(snapshot(state="idle", active=None))
    assert view.state == "idle" and view.active_turn is None


def test_session_view_publishes_only_the_last_exchange_bounded():
    # A long history must never reach the Vita: only the last you + last
    # assistant, each truncated, like Claude/Codex.
    big = []
    for n in range(300):
        big.append({"k": "you", "t": f"q{n}"})
        big.append({"k": "assistant", "t": f"a{n}"})
        big.append({"k": "tool", "t": f"tool{n}"})
    big.append({"k": "you", "t": "X" * 5000})
    big.append({"k": "assistant", "t": "Y" * 9000})
    view = session_view(snapshot(items=big, state="idle", active=None))
    assert [i["k"] for i in view.items] == ["you", "assistant"]
    assert view.items[0]["t"] == "X" * 2000           # you capped at 2000
    assert view.items[1]["t"] == "Y" * 4000 + "…"      # assistant capped at 4000 + ellipsis
    # No tool lines, no older turns leak through.
    assert all("tool" not in i["t"] for i in view.items)


def test_four_option_approval_maps_every_psv_choice():
    request = remote_request(permission(consent={"capability": "fs_write",
                                                 "resource": "psv_probe_marker.txt",
                                                 "workspaceRoot": "/project"}))
    assert [o["id"] for o in request.payload["options"]] == ["allow", "allow_always", "deny", "deny_stop"]
    # allow_always is a persistent Kiro rule, so it must not read "for session".
    assert [o["label"] for o in request.payload["options"]] == [
        "Allow", "Always allow", "Deny", "Deny and stop"]
    assert request.answer({"choice": "allow"}) == {
        "outcome": {"outcome": "selected", "optionId": "accept"}}
    assert request.answer({"choice": "deny"}) == {
        "outcome": {"outcome": "selected", "optionId": "reject"}}
    # deny_stop reuses the reject option; the bridge sends session/cancel separately.
    assert request.answer({"choice": "deny_stop"}) == {
        "outcome": {"outcome": "selected", "optionId": "reject"}}


def test_allow_always_echoes_canonical_consent():
    request = remote_request(permission(consent={"capability": "fs_write",
                                                 "resource": "out.txt", "workspaceRoot": "/project"}))
    assert request.answer({"choice": "allow_always"}) == {
        "outcome": {"outcome": "selected", "optionId": "always-accept"},
        "_meta": {"kiro": {"consent": {"resource": "out.txt", "workspaceRoot": "/project", "scope": "workspace"}}}}


def test_allow_always_without_consent_still_answers():
    request = remote_request(permission())  # no _meta.kiro.consent
    assert request.answer({"choice": "allow_always"}) == {
        "outcome": {"outcome": "selected", "optionId": "always-accept"}}


def test_deny_note_denies_and_requires_text():
    request = remote_request(permission())
    assert request.answer({"choice": "deny_note", "note": "run tests first"}) == {
        "outcome": {"outcome": "selected", "optionId": "reject"}}
    assert request.answer({"choice": "deny_note", "note": "  "}) is None
    assert request.answer({"choice": "deny_note"}) is None


def test_options_absent_from_agent_are_never_offered():
    # Only allow_once and reject_once offered: no allow_always row, no consent echo.
    request = remote_request(permission(options=[
        {"optionId": "accept", "name": "Allow", "kind": "allow_once"},
        {"optionId": "reject", "name": "Deny", "kind": "reject_once"},
    ]))
    assert [o["id"] for o in request.payload["options"]] == ["allow", "deny", "deny_stop"]
    assert request.answer({"choice": "allow_always"}) is None


def test_requests_without_usable_options_are_skipped():
    assert remote_request({"id": "x", "params": {"options": []}}) is None
    assert remote_request({"id": "x", "params": {}}) is None
    assert remote_request({"params": {"options": FOUR_OPTIONS}}) is None  # no id
    # Only an allow with no deny is still actionable; only a reject_always is not.
    assert remote_request(permission(options=[
        {"optionId": "always-reject", "name": "Always deny", "kind": "reject_always"}])) is None


def test_key_changes_when_the_request_content_changes():
    a = permission()
    b = permission(title="Delete File")
    assert request_key(a) != request_key(b)
    assert request_key(a) == request_key(permission())


def test_body_includes_resource_and_truncates():
    request = remote_request(permission(consent={"capability": "fs_write", "resource": "big.txt"}))
    assert "Write File" in request.payload["body"]
    assert "big.txt" in request.payload["body"]
    assert request.payload["tool"] == "fs_write"
