"""Kiro ACP state and permission decisions translated into the remote wire protocol.

Mirrors adapters/codex.py: session_view() reads the connection's snapshot into a
View the bridge understands, and remote_request()/Request map a Kiro
session/request_permission into a PSV approval sheet and back into an ACP outcome.

Kiro has no structured-question callback, so only approvals are represented;
questions stay on the computer, as documented in docs/kiro-integration.md.
"""

import hashlib
import json
from copy import deepcopy
from dataclasses import dataclass, field

# PSV wire choice -> (Kiro option kind, PSV option label). The bridge only ever
# sends a choice the agent actually offered; the mapping is resolved against the
# request's own options[], never synthesized.
_KIND_FOR_CHOICE = {
    "allow": "allow_once",
    "allow_always": "allow_always",
    "deny": "reject_once",
}
_LABEL = {
    "allow": "Allow",
    # Kiro's allow_always persists a workspace rule that outlives the session,
    # so it is not labelled "Allow for session" like Claude/Codex. Kiro's own
    # wording is used instead.
    "allow_always": "Always allow",
    "deny": "Deny",
    "deny_stop": "Deny and stop",
}


@dataclass
class View:
    native_id: str
    cwd: str
    state: str
    active_turn: str | None
    items: list[dict]
    pending: list[dict]
    raw: dict = field(repr=False)


# Match the Claude/Codex contract: the Vita shows the last exchange only, with
# the same per-item caps, so a long history never reaches the device.
YOU_CHARS = 2000
ASSISTANT_CHARS = 4000


def session_view(state: dict) -> View:
    """Read the AcpConnection snapshot into the bridge's View.

    Emits at most the last user prompt and the last assistant message of the
    current turn (truncated), mirroring adapters/codex.session_view. Tool lines
    and older turns are not published; this bounds what the Vita must hold.
    """
    raw_items = [item for item in state.get("items", [])
                 if isinstance(item, dict) and item.get("k") and isinstance(item.get("t"), str)]
    last_you = next((i["t"] for i in reversed(raw_items) if i["k"] == "you"), None)
    last_assistant = next((i["t"] for i in reversed(raw_items) if i["k"] == "assistant"), None)
    items = []
    if last_you:
        items.append({"k": "you", "t": last_you[:YOU_CHARS]})
    if last_assistant:
        text = last_assistant[:ASSISTANT_CHARS] + ("…" if len(last_assistant) > ASSISTANT_CHARS else "")
        items.append({"k": "assistant", "t": text})
    pending = [request for request in state.get("requests", [])
               if isinstance(request, dict) and isinstance(request.get("params"), dict)]
    return View(
        native_id=state.get("id") or "",
        cwd=state.get("cwd", ""),
        state=state.get("state", "idle"),
        active_turn=state.get("activeTurnId"),
        items=items,
        pending=pending,
        raw=state,
    )


def request_key(request: dict) -> str:
    """Stable identity for a permission request, invalidated if its content changes."""
    data = json.dumps(request, sort_keys=True, ensure_ascii=False, allow_nan=False)
    return hashlib.sha256(data.encode()).hexdigest()


@dataclass(frozen=True)
class Request:
    native: dict           # the recorded {"id", "params"} permission request
    payload: dict          # the PSV approval sheet
    options: dict[str, dict] = field(default_factory=dict)  # PSV choice -> ACP option

    @property
    def key(self) -> str:
        return request_key(self.native)

    @property
    def native_timeout_s(self) -> float | None:
        return None  # Kiro waits for an answer indefinitely

    def answer(self, message: dict) -> dict | None:
        """Translate a PSV decision into an ACP permission outcome.

        deny_stop denies here; the bridge issues session/cancel separately, like
        Codex. deny_note denies; the bridge sends the note as input. A plain
        allow/allow_always/deny returns the agent's own optionId.
        """
        choice = message.get("choice")
        if not isinstance(choice, str):
            return None
        if choice == "deny_note":
            note = message.get("note")
            if not isinstance(note, str) or not note.strip():
                return None
            choice = "deny"  # reject; the note is delivered as a follow-up message
        option = self.options.get("deny" if choice == "deny_stop" else choice)
        if option is None:
            return None
        outcome = {"outcome": {"outcome": "selected", "optionId": option["optionId"]}}
        if choice == "allow_always":
            consent = _consent_echo(self.native)
            if consent is not None:
                outcome["_meta"] = {"kiro": {"consent": consent}}
        return outcome


def _consent_echo(native: dict) -> dict | None:
    """Return canonical consent metadata for a persistent choice, if offered."""
    consent = ((native.get("params") or {}).get("_meta") or {}).get("kiro", {}).get("consent")
    if not isinstance(consent, dict):
        return None
    echo = {}
    if isinstance(consent.get("resource"), str):
        echo["resource"] = consent["resource"]
    if isinstance(consent.get("workspaceRoot"), str):
        echo["workspaceRoot"] = consent["workspaceRoot"]
        echo.setdefault("scope", "workspace")
    return echo or None


def remote_request(native: dict) -> Request | None:
    """Represent a Kiro permission request as a PSV approval, or None to skip it.

    Only session/request_permission with a usable options[] becomes a sheet. The
    PSV offers Allow / Always allow / Deny / Deny and stop, each only when
    the agent supplied the corresponding option kind.
    """
    if type(native.get("id")) not in (str, int):
        return None
    params = native.get("params")
    if not isinstance(params, dict):
        return None
    agent_options = params.get("options")
    tool_call = params.get("toolCall") if isinstance(params.get("toolCall"), dict) else {}
    if not isinstance(agent_options, list) or not agent_options:
        return None

    # Index the agent's options by kind; keep the first of each kind.
    by_kind: dict[str, dict] = {}
    for option in agent_options:
        if not isinstance(option, dict):
            return None
        oid, kind = option.get("optionId"), option.get("kind")
        if not isinstance(oid, str) or not oid or not isinstance(kind, str):
            return None
        by_kind.setdefault(kind, option)

    options: list[dict] = []
    resolved: dict[str, dict] = {}

    def offer(choice: str, kind: str):
        option = by_kind.get(kind)
        if option is not None:
            resolved[choice] = option
            options.append({"id": choice, "label": _LABEL[choice]})

    offer("allow", "allow_once")
    offer("allow_always", "allow_always")
    offer("deny", "reject_once")
    # Deny and stop reuses the reject_once option plus a separate session/cancel.
    if "deny" in resolved:
        resolved["deny_stop"] = resolved["deny"]
        options.append({"id": "deny_stop", "label": _LABEL["deny_stop"]})

    if not any(choice in resolved for choice in ("allow", "deny")):
        return None  # nothing actionable to show

    title = tool_call.get("title") if isinstance(tool_call.get("title"), str) else "Permission request"
    body = _body(params, tool_call)
    payload = {"kind": "approval", "tool": _tool_name(params, tool_call),
               "title": title[:80], "body": body, "options": options}
    return Request(deepcopy(native), payload, resolved)


def _tool_name(params: dict, tool_call: dict) -> str:
    consent = (params.get("_meta") or {}).get("kiro", {}).get("consent")
    if isinstance(consent, dict) and isinstance(consent.get("capability"), str):
        return consent["capability"]
    return "Tool"


def _body(params: dict, tool_call: dict) -> str:
    consent = (params.get("_meta") or {}).get("kiro", {}).get("consent")
    resource = consent.get("resource") if isinstance(consent, dict) else None
    parts = []
    if isinstance(tool_call.get("title"), str):
        parts.append(tool_call["title"])
    if isinstance(resource, str) and resource and resource not in parts:
        parts.append(resource)
    body = "\n\n".join(parts) if parts else "Kiro is requesting permission."
    return body.encode()[:65000].decode("utf-8", errors="ignore")
