from copy import deepcopy

import pytest

from psv_remote.codex_connections import AppServer, FrontendIPC
from psv_remote.codex_rpc import NativeError, RpcError
from psv_remote.codex_state import Replica
from test_codex_adapter import approval, state


class Peer:
    def __init__(self, events=()):
        self.events = list(events)
        self.calls = []

    def drain(self):
        events, self.events = self.events, []
        return events

    def call(self, message):
        self.calls.append(message)
        if message["method"] == "thread/read":
            return {"thread": {"cwd": "/test", "status": {"type": "active"}}}
        if message["method"] == "thread/turns/list":
            return {"data": [{"id": "turn", "status": "inProgress", "items": []}]}
        raise AssertionError(message)


def app(events=()):
    native = AppServer.__new__(AppServer)
    native.peer, native.thread_id = Peer(events), "native"
    native.requests, native.metadata, native.state, native.last_fetch = {}, {}, {}, 0
    return native


def test_app_server_filters_native_requests_by_exact_thread_and_resolution():
    request = approval()
    foreign = deepcopy(request)
    foreign["id"], foreign["params"]["threadId"] = "foreign", "elsewhere"
    native = app([foreign, request])
    assert native.snapshot()["requests"] == [request]
    native.peer.events = [{"method": "serverRequest/resolved", "params": {
        "threadId": "native", "requestId": request["id"]}}]
    assert native.snapshot()["requests"] == []


def test_app_server_revalidates_pending_request_before_answering():
    native = app()
    with pytest.raises(RpcError):
        native.respond(approval(), {"decision": "accept"})
    assert all(call["method"] in ("thread/read", "thread/turns/list") for call in native.peer.calls)


def test_app_server_does_not_interrupt_a_different_turn():
    native = app()
    assert native.stop("previous-turn") is None
    assert all(call["method"] != "turn/interrupt" for call in native.peer.calls)


def frontend(events):
    native = FrontendIPC.__new__(FrontendIPC)
    native.thread_id, native.owner = "native", "owner"
    native.peer = Peer(events)
    native.replica = Replica("native")
    native.replica.apply({"type": "snapshot", "revision": 1, "conversationState": state()})
    return native


@pytest.mark.parametrize("source,host,thread", [
    ("foreign", "local", "native"), ("owner", "other-host", "native"),
    ("owner", "local", "other-thread")])
def test_frontend_ignores_state_from_other_owner_host_or_thread(source, host, thread):
    native = frontend([{"method": "thread-stream-state-changed", "sourceClientId": source,
                        "version": 999, "params": {"hostId": host, "conversationId": thread}}])
    assert native.snapshot()["id"] == "native"


def test_frontend_disables_controls_when_stream_version_changes():
    native = frontend([{"method": "thread-stream-state-changed", "sourceClientId": "owner",
                        "version": 12, "params": {"hostId": "local", "conversationId": "native"}}])
    with pytest.raises(NativeError):
        native.snapshot()
