"""Bounded background RPC reader, shared by Codex's two connection strategies."""

import queue
import threading
import uuid


class NativeError(RuntimeError):
    pass


class RpcError(NativeError):
    pass


class RpcTimeout(NativeError):
    pass


class DependencyError(NativeError):
    """A fixed, non-sensitive installation hint safe to show at startup."""


class Peer:
    def __init__(self, read, write, close, ipc=False):
        self.read, self.write, self.close_stream = read, write, close
        self.ipc = ipc
        self.events = queue.Queue(maxsize=256)
        self.pending = {}
        self.lock = threading.Lock()
        self.write_lock = threading.Lock()
        self.error = None
        self.thread = threading.Thread(target=self._reader, daemon=True)
        self.thread.start()

    def send(self, message):
        if self.error:
            raise NativeError("Codex connection is closed") from self.error
        try:
            with self.write_lock:
                self.write(message)
        except Exception as error:
            self._fail(error)
            raise NativeError("Codex write failed; outcome may be unknown") from error

    def call(self, message, timeout=5):
        key = str(uuid.uuid4())
        field = "requestId" if self.ipc else "id"
        inbox = queue.Queue(maxsize=1)
        with self.lock:
            self.pending[key] = inbox
        try:
            self.send({**message, field: key})
            try:
                response = inbox.get(timeout=timeout)
            except queue.Empty as error:
                raise RpcTimeout("Codex RPC timed out; do not replay a control request") from error
            if isinstance(response, Exception):
                raise NativeError("Codex connection lost") from response
            if self.ipc:
                if response.get("resultType") != "success":
                    raise RpcError(str(response.get("error", "IPC request rejected")))
                return response
            if "error" in response:
                error = response["error"]
                raise RpcError(str(error.get("message", "Request rejected")) if isinstance(error, dict) else str(error))
            return response.get("result", {})
        finally:
            with self.lock:
                self.pending.pop(key, None)

    def begin(self, message):
        """Send a request and return (key, inbox) without blocking on the reply.

        For long-lived requests (ACP session/prompt settles only at turn end): the
        caller polls the inbox with poll() from its own loop instead of tying up a
        thread. poll() releases the pending slot once the reply arrives.
        """
        key = str(uuid.uuid4())
        field = "requestId" if self.ipc else "id"
        inbox = queue.Queue(maxsize=1)
        with self.lock:
            self.pending[key] = inbox
        self.send({**message, field: key})
        return key, inbox

    def poll(self, key, inbox):
        """Return the response dict if it has arrived, None if still pending.

        Raises if the connection failed. Releases the pending slot on completion.
        """
        try:
            response = inbox.get_nowait()
        except queue.Empty:
            if self.error:
                self.finish(key)
                raise NativeError("Connection lost") from self.error
            return None
        self.finish(key)
        if isinstance(response, Exception):
            raise NativeError("Connection lost") from response
        if "error" in response:
            error = response["error"]
            raise RpcError(str(error.get("message", "Request rejected")) if isinstance(error, dict) else str(error))
        return response.get("result", {})

    def finish(self, key):
        with self.lock:
            self.pending.pop(key, None)

    def _fail(self, error):
        with self.lock:
            self.error = error
            for inbox in self.pending.values():
                try:
                    inbox.put_nowait(error)
                except queue.Full:
                    pass

    def _reader(self):
        try:
            while True:
                message = self.read()
                if not isinstance(message, dict):
                    raise NativeError("Invalid Codex message")
                if self.ipc and message.get("type") == "client-discovery-request":
                    self.send({"type": "client-discovery-response", "requestId": message["requestId"],
                               "canHandle": False})
                    continue
                response = (message.get("type") == "response" if self.ipc else
                            "method" not in message and "id" in message)
                if response:
                    key = message.get("requestId" if self.ipc else "id")
                    with self.lock:
                        inbox = self.pending.get(key)
                        if inbox is not None:
                            try:
                                inbox.put_nowait(message)
                            except queue.Full:
                                pass
                else:
                    self.events.put_nowait(message)
        except Exception as error:
            self._fail(error)

    def drain(self):
        if self.error:
            raise NativeError("Codex event stream unavailable") from self.error
        messages = []
        while True:
            try:
                messages.append(self.events.get_nowait())
            except queue.Empty:
                return messages

    def close(self):
        self._fail(NativeError("Closed"))
        self.close_stream()
        self.thread.join(timeout=2)
