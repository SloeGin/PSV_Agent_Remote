"""PSV option lists and the hook output for each kind of answer."""

import copy

CHOICES = ("allow", "allow_always", "deny", "deny_note", "deny_stop")
DENY_MESSAGE = "用户在 PSV 上拒绝"
NOTE_CHARS = 1000


def build_options(permission_suggestions: list | None) -> list[dict]:
    options = [{"id": "allow", "label": "Allow"}]
    if permission_suggestions:
        options.append({"id": "allow_always", "label": "Allow for session"})
    options.append({"id": "deny", "label": "Deny"})
    options.append({"id": "deny_note", "label": "Deny with note…"})
    options.append({"id": "deny_stop", "label": "Deny and stop"})
    return options


def decision_output(choice: str, permission_suggestions: list | None, note: str | None = None) -> dict | None:
    if choice == "allow":
        decision = {"behavior": "allow"}
    elif choice == "allow_always":
        rules = copy.deepcopy(permission_suggestions or [])
        for rule in rules:
            rule["destination"] = "session"
        decision = {"behavior": "allow", "updatedPermissions": rules}
    elif choice == "deny":
        decision = {"behavior": "deny", "message": DENY_MESSAGE}
    elif choice == "deny_note":
        note = (note or "").strip()[:NOTE_CHARS] if isinstance(note, str) else ""
        if not note:
            return None
        decision = {"behavior": "deny", "message": f"{DENY_MESSAGE}，并留言：{note}"}
    elif choice == "deny_stop":
        decision = {"behavior": "deny", "message": DENY_MESSAGE, "interrupt": True}
    else:
        return None
    return {"hookSpecificOutput": {"hookEventName": "PermissionRequest", "decision": decision}}


def question_output(tool_input: dict, answers) -> dict | None:
    """Answer AskUserQuestion: echo the questions and add {question: "label, label, other"}."""
    questions = tool_input.get("questions")
    if not isinstance(questions, list) or not isinstance(answers, list) or len(answers) != len(questions):
        return None
    result = {}
    for q, a in zip(questions, answers):
        if not isinstance(a, dict) or not isinstance(q, dict):
            return None
        options = q.get("options") or []
        selected = a.get("selected") or []
        other = a.get("other") if isinstance(a.get("other"), str) else ""
        if not isinstance(selected, list) or any(not isinstance(i, int) or not 0 <= i < len(options) for i in selected):
            return None
        if len(set(selected)) != len(selected) or (not q.get("multiSelect") and len(selected) + bool(other.strip()) > 1):
            return None
        parts = [options[i].get("label", "") for i in sorted(selected)]
        if other.strip():
            parts.append(other.strip()[:NOTE_CHARS])
        if not parts:
            return None
        result[q.get("question", "")] = ", ".join(parts)
    return {"hookSpecificOutput": {
        "hookEventName": "PreToolUse",
        "permissionDecision": "allow",
        "updatedInput": {**tool_input, "answers": result},
    }}
