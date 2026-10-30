"""Codex agent profile for the shared bridge.

Keeps the Codex-specific delivery confirmation (turn history + interrupted state)
and the queue-one-reply busy behavior. The generic request/reply/stop lifecycle
lives in bridge.py. `run` is preserved for existing callers (psv_codex.py).
"""

from .adapters.codex import ordered_turns, remote_request, session_view
from .bridge import AgentProfile, Bridge
from .bridge import run as _run


class CodexProfile(AgentProfile):
    name = "codex"
    capabilities = {"approval": True, "questions": True, "reply": True, "stop": True}

    def session_view(self, raw):
        return session_view(raw)

    def remote_request(self, native):
        return remote_request(native)

    def confirm_delivery(self, view, delivery):
        turns = ordered_turns(view.raw)
        turn = next((turn for turn in turns if turn.get("turnId") == delivery.turn_id), None)
        kind = delivery.message["kind"]
        confirmed = turn is not None and (kind == "text" or turn.get("status") == "interrupted")
        if not confirmed:
            return None
        return "delivered" if kind == "text" else "stopped"

    def deliver_busy_reply(self, bridge, message):
        # Queue one text reply; a newer reply replaces it. Delivered when idle.
        rid = message["id"]
        if bridge.queued and bridge.queued["id"] != rid:
            bridge.ledger.mark(bridge.queued["id"])
            bridge._reply_state(bridge.queued["id"], "cancelled")
        bridge.queued = message
        bridge._reply_state(rid, "queued")


def run(bus, native, cfg, host_id):
    return _run(bus, native, cfg, host_id, CodexProfile())


# For tests and callers that constructed the bridge directly.
class CodexBridge(Bridge):
    def __init__(self, bus, native, cfg, host_id, clock=None):
        import time
        super().__init__(bus, native, cfg, host_id, CodexProfile(),
                         clock=clock or time.monotonic)
