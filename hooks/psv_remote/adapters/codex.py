"""Codex native state and decisions translated into the remote wire protocol."""

import hashlib
import json
from copy import deepcopy
from dataclasses import dataclass, field

from ..remote import encode

COMMAND_APPROVAL = "item/commandExecution/requestApproval"
USER_INPUT = "item/tool/requestUserInput"


def ordered_turns(state: dict) -> list[dict]:
    history = state.get("turnHistory", {}).get("history", {})
    entities = history.get("entitiesByKey")
    if isinstance(entities, dict):
        keys = [entry.get("value") for island in history.get("islands", [])
                for entry in island.get("entries", [])]
        # The frontend's island ordering, not dictionary insertion order, defines
        # chronology when older pages arrive after the live tail.
        if keys:
            return [entities[key] for key in keys if key in entities]
        return sorted(entities.values(), key=lambda turn: turn.get("turnStartedAtMs") or 0)
    return state.get("turns", [])


def _text(content) -> str:
    if isinstance(content, str):
        return content
    if not isinstance(content, list):
        return ""
    return "\n".join(block["text"] for block in content if isinstance(block, dict)
                     and block.get("type") == "text" and isinstance(block.get("text"), str))


@dataclass
class View:
    native_id: str
    cwd: str
    state: str
    active_turn: str | None
    items: list[dict]
    pending: list[dict]
    raw: dict = field(repr=False)


def session_view(state: dict) -> View:
    turns = ordered_turns(state)
    active = [turn for turn in turns if turn.get("status") == "inProgress" and turn.get("turnId")]
    active_id = active[-1]["turnId"] if active else None
    pending = [request for request in state.get("requests", []) if isinstance(request, dict)
               and isinstance(request.get("params"), dict)
               and request["params"].get("threadId") == state["id"]
               and request["params"].get("turnId") == active_id and active_id is not None]
    runtime = state.get("threadRuntimeStatus", {}).get("type")
    status = "waiting" if pending else "running" if active or runtime == "active" else "idle"
    items = []
    if turns:
        turn = turns[-1]
        prompt = _text(turn.get("params", {}).get("input"))
        for item in turn.get("items", []):
            if item.get("type") == "userMessage":
                prompt = _text(item.get("content")) or prompt
        if prompt:
            items.append({"k": "you", "t": prompt[:2000]})
        messages = [item for item in turn.get("items", []) if item.get("type") == "agentMessage"
                    and isinstance(item.get("text"), str) and item["text"].strip()]
        final = [item for item in messages if item.get("phase") == "final_answer"]
        if messages:
            text = (final or messages)[-1]["text"]
            items.append({"k": "assistant", "t": text[:4000] + ("…" if len(text) > 4000 else "")})
    return View(state["id"], state.get("cwd", ""), status, active_id, items, pending, state)


def request_key(request: dict) -> str:
    # Include the entire native payload: same-ID questions or commands that change
    # must invalidate a remote selection made against the old request.
    data = json.dumps(request, sort_keys=True, ensure_ascii=False, allow_nan=False)
    return hashlib.sha256(data.encode()).hexdigest()


@dataclass(frozen=True)
class Request:
    native: dict
    payload: dict
    decisions: dict[str, str] = field(default_factory=dict)

    @property
    def key(self) -> str:
        return request_key(self.native)

    @property
    def native_timeout_s(self) -> float | None:
        """When Codex resolves the request by itself (autoResolutionMs), if it says."""
        ms = self.native["params"].get("autoResolutionMs")
        return ms / 1000 if type(ms) is int and ms > 0 else None

    def answer(self, message: dict) -> dict | None:
        if self.native["method"] == COMMAND_APPROVAL:
            choice = message.get("choice")
            decision = self.decisions.get(choice) if isinstance(choice, str) else None
            return {"decision": decision} if decision else None
        answers = message.get("answers")
        questions = self.native["params"]["questions"]
        if not isinstance(answers, list) or len(answers) != len(questions):
            return None
        result = {}
        for question, answer in zip(questions, answers):
            if not isinstance(answer, dict):
                return None
            selected, other = answer.get("selected", []), answer.get("other", "")
            if (not isinstance(selected, list) or not isinstance(other, str)
                    or len(selected) + bool(other.strip()) != 1):
                return None
            if selected:
                index = selected[0]
                if type(index) is not int or not 0 <= index < len(question["options"]):
                    return None
                text = question["options"][index]["label"]
            else:
                if not question.get("isOther") or len(other.strip()) > 1000:
                    return None
                text = other.strip()
            result[question["id"]] = {"answers": [text]}
        return {"answers": result}


def remote_request(native: dict) -> Request | None:
    """Only represent requests that fit the Vita UI without dropping choices."""
    if (type(native.get("id")) not in (str, int) or not isinstance(native.get("params"), dict)):
        return None
    params, method = native["params"], native.get("method")
    if method == COMMAND_APPROVAL:
        command = params.get("command")
        if not isinstance(command, str) or not command.strip() or params.get("networkApprovalContext"):
            return None  # network/file/permission approvals stay on the computer
        available = params.get("availableDecisions")
        if available is None:
            available = ["accept", "decline", "cancel"]
        if not isinstance(available, list):
            return None
        decisions, options = {}, []
        for wire, native_choice, label in (
            ("allow", "accept", "Allow"),
            ("allow_always", "acceptForSession", "Allow for session"),
            ("deny", "decline", "Deny"),
            ("deny_stop", "cancel", "Deny and stop"),
        ):
            if native_choice in available:
                decisions[wire] = native_choice
                options.append({"id": wire, "label": label})
        if not options:
            return None
        body = command
        if isinstance(params.get("reason"), str) and params["reason"]:
            body += "\n\n" + params["reason"]
        raw = body.encode()[:65000]
        return Request(deepcopy(native), {"kind": "approval", "tool": "Bash",
                       "title": command.splitlines()[0][:80], "body": raw.decode("utf-8", errors="ignore"),
                       "options": options}, decisions)
    if method != USER_INPUT or params.get("isBlocking") is not True:
        return None
    questions = params.get("questions")
    if not isinstance(questions, list) or not 1 <= len(questions) <= 4:
        return None
    converted, ids = [], set()
    for question in questions:
        if not isinstance(question, dict) or question.get("isSecret"):
            return None
        qid, options = question.get("id"), question.get("options")
        if (not isinstance(qid, str) or not qid or qid in ids
                or not isinstance(question.get("question"), str)
                or not isinstance(options, list) or not 1 <= len(options) <= 4
                or any(not isinstance(option, dict) or not isinstance(option.get("label"), str)
                       or not option["label"] for option in options)):
            return None
        ids.add(qid)
        converted.append({"header": str(question.get("header", "")), "question": question["question"],
                          "multi": False, "allow_other": bool(question.get("isOther")),
                          "options": [{"label": option["label"],
                                       "description": str(option.get("description") or "")} for option in options]})
    payload = {"kind": "question", "tool": "QUESTION", "title": questions[0]["question"],
               "questions": converted}
    # Keep the whole outgoing JSON within the Vita parser's payload limit.
    return Request(deepcopy(native), payload) if len(encode(payload)) < 120000 else None
