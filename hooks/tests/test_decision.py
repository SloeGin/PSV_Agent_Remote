from psv_remote.decision import build_options, decision_output, question_output

SUG = [{"type": "addRules", "rules": [{"toolName": "Bash", "ruleContent": "npm i"}],
        "behavior": "allow", "destination": "localSettings"}]


def test_options_without_suggestions():
    assert [o["id"] for o in build_options(None)] == ["allow", "deny", "deny_note", "deny_stop"]


def test_options_with_suggestions():
    assert [o["id"] for o in build_options(SUG)] == ["allow", "allow_always", "deny", "deny_note", "deny_stop"]


def test_allow():
    assert decision_output("allow", None) == {"hookSpecificOutput": {
        "hookEventName": "PermissionRequest", "decision": {"behavior": "allow"}}}


def test_allow_always_session_destination():
    d = decision_output("allow_always", SUG)["hookSpecificOutput"]["decision"]
    assert d["behavior"] == "allow"
    assert d["updatedPermissions"][0]["destination"] == "session"
    assert SUG[0]["destination"] == "localSettings"


def test_deny_and_deny_stop():
    d = decision_output("deny", None)["hookSpecificOutput"]["decision"]
    assert d == {"behavior": "deny", "message": "用户在 PSV 上拒绝"}
    d = decision_output("deny_stop", None)["hookSpecificOutput"]["decision"]
    assert d["interrupt"] is True and d["behavior"] == "deny"


def test_unknown_choice():
    assert decision_output("yolo", None) is None


def test_deny_with_note():
    d = decision_output("deny_note", None, note="先跑测试")["hookSpecificOutput"]["decision"]
    assert d == {"behavior": "deny", "message": "用户在 PSV 上拒绝，并留言：先跑测试"}
    assert decision_output("deny_note", None, note="  ") is None  # an empty note is not an answer


QS = [{"question": "Which runner?", "header": "Runner", "multiSelect": False,
       "options": [{"label": "Vitest", "description": "fast"}, {"label": "Jest", "description": "existing"}]},
      {"question": "Retry on?", "header": "Retries", "multiSelect": True,
       "options": [{"label": "Timeouts"}, {"label": "5xx"}, {"label": "429"}]}]


def test_question_output():
    out = question_output({"questions": QS}, [{"selected": [1]}, {"selected": [0, 2], "other": "conn resets"}])
    h = out["hookSpecificOutput"]
    assert h["hookEventName"] == "PreToolUse" and h["permissionDecision"] == "allow"
    assert h["updatedInput"]["questions"] == QS
    assert h["updatedInput"]["answers"] == {"Which runner?": "Jest", "Retry on?": "Timeouts, 429, conn resets"}


def test_question_output_other_only():
    out = question_output({"questions": QS[:1]}, [{"selected": [], "other": "Mocha"}])
    assert out["hookSpecificOutput"]["updatedInput"]["answers"] == {"Which runner?": "Mocha"}


def test_question_output_rejects_bad_answers():
    assert question_output({"questions": QS}, [{"selected": [0]}]) is None            # missing one
    assert question_output({"questions": QS}, [{"selected": [5]}, {"selected": [0]}]) is None  # out of range
    assert question_output({"questions": QS}, [{"selected": []}, {"selected": [0]}]) is None   # empty answer
    assert question_output({"questions": QS}, [{"selected": [0, 1]}, {"selected": [0]}]) is None  # 2 on single
    assert question_output({"questions": QS}, "nope") is None
