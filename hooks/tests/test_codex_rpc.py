import queue
import threading

import pytest

from psv_remote.codex_rpc import NativeError, Peer, RpcError


class Stream:
    def __init__(self, ipc=False):
        self.incoming, self.outgoing = queue.Queue(), queue.Queue()
        self.peer = Peer(self.read, self.outgoing.put, lambda: self.incoming.put(None), ipc=ipc)

    def read(self):
        value = self.incoming.get(timeout=2)
        if isinstance(value, Exception):
            raise value
        return value

    def exchange(self, response):
        def answer():
            sent = self.outgoing.get(timeout=1)
            self.incoming.put(response(sent))
        worker = threading.Thread(target=answer)
        worker.start()
        return worker


def test_server_request_with_same_id_does_not_complete_client_rpc():
    stream = Stream()
    def reply(sent):
        stream.incoming.put({"id": sent["id"], "method": "native/approval", "params": {}})
        return {"id": sent["id"], "result": {"ok": True}}
    worker = stream.exchange(reply)
    try:
        assert stream.peer.call({"method": "read"}) == {"ok": True}
        assert stream.peer.drain()[0]["method"] == "native/approval"
    finally:
        worker.join()
        stream.peer.close()


def test_disconnect_wakes_waiter_without_replaying_control():
    stream = Stream()
    worker = stream.exchange(lambda sent: OSError("disconnected"))
    try:
        with pytest.raises(NativeError):
            stream.peer.call({"method": "turn/start"})
        assert stream.outgoing.empty()
    finally:
        worker.join()
        stream.peer.close()


def test_rejected_native_call_is_distinct_from_unknown_outcome():
    stream = Stream()
    worker = stream.exchange(lambda sent: {"id": sent["id"], "error": {"message": "stale"}})
    try:
        with pytest.raises(RpcError):
            stream.peer.call({"method": "turn/interrupt"})
    finally:
        worker.join()
        stream.peer.close()


def test_ipc_discovery_never_claims_frontend_ownership():
    stream = Stream(ipc=True)
    try:
        stream.incoming.put({"type": "client-discovery-request", "requestId": "discovery"})
        assert stream.outgoing.get(timeout=1) == {
            "type": "client-discovery-response", "requestId": "discovery", "canHandle": False}
    finally:
        stream.peer.close()
