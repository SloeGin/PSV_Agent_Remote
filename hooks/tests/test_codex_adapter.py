from copy import deepcopy

import pytest

from psv_remote.adapters.codex import COMMAND_APPROVAL, USER_INPUT, remote_request, session_view
from psv_remote.remote import scoped_session_id


def approval(decisions=None):
    return {"id": 7, "method": COMMAND_APPROVAL, "params": {
        "threadId": "native", "turnId": "turn", "itemId": "item", "command": "echo hello",
        "availableDecisions": decisions if decisions is not None else ["accept", "cancel"],
    }}


def question(**overrides):
    return {"id": "q-request", "method": USER_INPUT, "params": {
        "threadId": "native", "turnId": "turn", "itemId": "question", "isBlocking": True,
        "questions": [{"id": "native-question-id", "header": "Runner", "question": "Which runner?",
                       "isOther": True, "isSecret": False,
                       "options": [{"label": "Alpha", "description": "First"}, {"label": "Beta"}],
                       **overrides}],
    }}


def state(requests=None, status="inProgress"):
    return {"id": "native", "cwd": "/project", "turns": [{"turnId": "turn", "status": status,
            "params": {"input": [{"type": "text", "text": "prompt"}]}, "items": []}],
            "requests": requests or [], "threadRuntimeStatus": {"type": "active" if status == "inProgress" else "idle"}}


def test_scoped_id_fits_vita_and_distinguishes_hosts_and_agents():
    sid = scoped_session_id("codex", "host", "thread")
    assert sid == scoped_session_id("codex", "host", "thread")
    assert len(sid.encode()) < 64 and "/" not in sid
    assert sid != scoped_session_id("codex", "other-host", "thread")
    assert sid != scoped_session_id("claude", "host", "thread")


def test_approval_never_turns_rule_amendments_into_session_permissions():
    native = approval(["accept", {"acceptWithExecpolicyAmendment": {"execpolicy_amendment": ["echo"]}}, "cancel"])
    request = remote_request(native)
    assert [o["id"] for o in request.payload["options"]] == ["allow", "deny_stop"]
    assert request.answer({"choice": "allow"}) == {"decision": "accept"}
    assert request.answer({"choice": "deny_stop"}) == {"decision": "cancel"}
    assert request.answer({"choice": "allow_always"}) is None
    assert request.answer({"choice": "deny", "note": "unadvertised"}) is None
    assert request.native == native


def test_advertised_session_and_decline_decisions_are_preserved():
    request = remote_request(approval(["acceptForSession", "decline"]))
    assert request.answer({"choice": "allow_always"}) == {"decision": "acceptForSession"}
    assert request.answer({"choice": "deny"}) == {"decision": "decline"}


def test_native_ids_and_full_option_labels_are_preserved():
    request = remote_request(question())
    assert request.payload["questions"][0]["multi"] is False
    assert request.answer({"answers": [{"selected": [1]}]}) == {
        "answers": {"native-question-id": {"answers": ["Beta"]}}}
    assert request.answer({"answers": [{"other": "Gamma"}]}) == {
        "answers": {"native-question-id": {"answers": ["Gamma"]}}}


@pytest.mark.parametrize("answer", [{"selected": [True]}, {"selected": [-1]}, {"selected": [2]},
                                    {"selected": [0, 1]}, {"selected": [0], "other": "other"}, {}, "Alpha"])
def test_malformed_question_selection_does_not_produce_native_response(answer):
    assert remote_request(question()).answer({"answers": [answer]}) is None


def test_secret_nonblocking_oversized_and_unsupported_requests_fall_back():
    assert remote_request(question(isSecret=True)) is None
    assert remote_request(question(options=[{"label": str(i)} for i in range(5)])) is None
    native = question()
    native["params"]["isBlocking"] = False
    assert remote_request(native) is None
    native = approval()
    native["params"]["networkApprovalContext"] = {"host": "example.com"}
    assert remote_request(native) is None
    native["method"] = "item/fileChange/requestApproval"
    assert remote_request(native) is None
    assert remote_request(question(isOther=False)).answer({"answers": [{"other": "Gamma"}]}) is None


def test_view_uses_island_order_and_ignores_stale_requests():
    raw = state([approval()])
    turn = raw["turns"][0]
    raw["turns"] = []
    older = {"turnId": "older", "status": "completed", "items": []}
    turn["items"] = [{"type": "agentMessage", "phase": "commentary", "text": "Looking"},
                     {"type": "agentMessage", "phase": "final_answer", "text": "Done"}]
    raw["turnHistory"] = {"history": {"entitiesByKey": {"new": turn, "old": older},
                                     "islands": [{"entries": [{"value": "old"}, {"value": "new"}]}]}}
    view = session_view(raw)
    assert view.state == "waiting" and view.active_turn == "turn"
    assert view.items == [{"k": "you", "t": "prompt"}, {"k": "assistant", "t": "Done"}]
    raw["requests"][0]["params"]["turnId"] = "older"
    assert session_view(raw).pending == []


def test_request_change_invalidates_identity_without_mutating_original():
    native = approval()
    request = remote_request(native)
    changed = deepcopy(native)
    changed["params"]["command"] = "different command"
    assert remote_request(changed).key != request.key
    native["params"]["command"] = "mutated source"
    assert request.native["params"]["command"] == "echo hello"
