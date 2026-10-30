"""Kiro agent profile for the shared bridge (standalone `kiro-cli acp` session).

The bridge owns the session (acp_connection.AcpConnection spawns `kiro-cli acp`),
so the Vita is its only user interface. Differences from Codex, rooted in the
verified ACP behavior:
- Questions are not supported over ACP, so the capability is off.
- Delivery is confirmed from the ACP snapshot's turn state, not a turn history.
- A busy reply steers the active turn (_session/steer) instead of being queued.
- A deny-and-stop answers the permission as a reject, then cancels the turn.

The generic lifecycle lives in bridge.py. `run` is the entry point for a single
Kiro ACP session; unlike codex_bridge.run it survives broker outages, because
ending the bridge would end the session it owns.
"""

import time

from .adapters.kiro import remote_request, session_view
from .bridge import AgentProfile, Delivery, run_reconnecting
from .rpc import NativeError


class KiroProfile(AgentProfile):
    name = "kiro"
    capabilities = {"approval": True, "questions": False, "reply": True, "stop": True}
    has_native_fallback = False  # the Vita is this session's only approval window
    dismissable = False  # ...and its only interface: hiding it would strand the session

    def session_view(self, raw):
        return session_view(raw)

    def remote_request(self, native):
        return remote_request(native)

    def confirm_delivery(self, view, delivery):
        kind = delivery.message["kind"]
        if kind == "text":
            # A prompt/steer is confirmed once the turn it started is running or
            # has produced output; snapshot state reflects session/update.
            if view.active_turn == delivery.turn_id or view.state == "running":
                return "delivered"
            if view.items:
                return "delivered"
            return None
        # stop: confirmed when the turn it targeted is no longer active.
        if view.active_turn != delivery.turn_id and view.state != "running":
            return "stopped"
        return None

    def deliver_busy_reply(self, bridge, message):
        # Steer the active turn; delivery is then confirmed like a text turn.
        rid = message["id"]
        if bridge.queued and bridge.queued["id"] != rid:
            bridge.ledger.mark(bridge.queued["id"])
            bridge._reply_state(bridge.queued["id"], "cancelled")
            bridge.queued = None
        turn = bridge.native.steer(message["text"], "psv-" + rid)
        if not isinstance(turn, str) or not turn:
            # No active turn to steer (it just ended). Fall back to a fresh turn.
            bridge._send(message)
            return
        bridge.ledger.mark(rid)
        bridge.delivery = Delivery(message, turn, bridge.clock() + 10)
        bridge._reply_state(rid, "steering")

    def after_respond(self, bridge, request, message):
        # Deny-and-stop: the permission was answered as a plain reject; now
        # interrupt the active turn so the agent actually halts.
        if message.get("choice") != "deny_stop":
            return
        if bridge.view.active_turn:
            try:
                bridge.native.stop(bridge.view.active_turn)
            except NativeError:
                pass


def run(bus_factory, native, cfg, host_id, sleep=time.sleep):
    # The bridge owns this session, so a broker outage reconnects instead of ending it.
    return run_reconnecting(bus_factory, native, cfg, host_id, KiroProfile(), sleep=sleep)
