"""Thin MQTT layer. Handlers depend only on the Bus protocol; tests use a fake."""

import dataclasses
import queue
import socket
import threading
import time
import uuid
from typing import Protocol

import paho.mqtt.client as mqtt

from .config import Config


class BusError(Exception):
    pass


class Bus(Protocol):
    def read_retained(self, topic: str, wait_s: float) -> bytes | None: ...
    def subscribe(self, topic: str) -> None: ...
    def publish(self, topic: str, payload: bytes, retain: bool = False, qos: int = 0) -> None: ...
    def wait_for(self, topic: str, timeout_s: float) -> bytes | None: ...
    # Next message on any subscribed topic; raises BusError once disconnected.
    def next_message(self, timeout_s: float) -> tuple[str, bytes] | None: ...
    def close(self) -> None: ...


def _resolve(host: str, port: int, timeout_s: float) -> str:
    """Resolve to one address within timeout_s; getaddrinfo itself has no timeout."""
    result: list = []

    def run():
        try:
            infos = socket.getaddrinfo(host, port, type=socket.SOCK_STREAM)
            infos.sort(key=lambda i: i[0] != socket.AF_INET)  # prefer IPv4
            result.append(infos[0][4][0])
        except (OSError, IndexError, TypeError) as e:
            result.append(e)

    thread = threading.Thread(target=run, daemon=True)
    thread.start()
    thread.join(timeout_s)
    if not result:
        raise BusError(f"DNS timeout for {host}")
    if isinstance(result[0], Exception):
        raise BusError(f"DNS failed for {host}: {result[0]}")
    return result[0]


class PahoBus:
    def __init__(self, cfg: Config):
        deadline = time.monotonic() + cfg.connect_timeout_s
        self._queues: dict[str, queue.Queue] = {}
        self._all: queue.Queue = queue.Queue()
        self._lock = threading.Lock()
        self._connected = threading.Event()
        self._disconnected = threading.Event()
        self._client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2,
                                   client_id=f"claude-hook-{uuid.uuid4().hex[:8]}")
        self._client.on_connect = self._on_connect
        self._client.on_disconnect = self._on_disconnect
        self._client.on_message = self._on_message
        address = _resolve(cfg.broker_host, cfg.broker_port, cfg.connect_timeout_s)
        self._client.connect_timeout = max(0.1, deadline - time.monotonic())
        try:
            self._client.connect(address, cfg.broker_port, keepalive=30)
        except OSError as e:
            raise BusError(f"connect failed: {e}") from e
        self._client.loop_start()
        if not self._connected.wait(max(0.0, deadline - time.monotonic())):
            self.close()
            raise BusError("no CONNACK")

    def _on_connect(self, client, userdata, flags, reason_code, properties):
        if not reason_code.is_failure:
            self._connected.set()

    def _on_disconnect(self, client, userdata, flags, reason_code, properties):
        # Subscriptions do not survive a reconnect, so a wait can never succeed now.
        self._disconnected.set()

    def _queue(self, topic: str) -> queue.Queue:
        with self._lock:
            return self._queues.setdefault(topic, queue.Queue())

    def _on_message(self, client, userdata, msg):
        self._queue(msg.topic).put(msg.payload)
        self._all.put((msg.topic, msg.payload))

    def subscribe(self, topic: str) -> None:
        self._queue(topic)
        self._client.subscribe(topic, qos=1)

    def wait_for(self, topic: str, timeout_s: float) -> bytes | None:
        q = self._queue(topic)
        deadline = time.monotonic() + timeout_s
        while not self._disconnected.is_set():
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return None
            try:
                return q.get(timeout=min(0.2, remaining))
            except queue.Empty:
                pass
        return None

    def next_message(self, timeout_s: float) -> tuple[str, bytes] | None:
        deadline = time.monotonic() + timeout_s
        while not self._disconnected.is_set():
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return None
            try:
                return self._all.get(timeout=min(0.2, remaining))
            except queue.Empty:
                pass
        raise BusError("disconnected")

    def read_retained(self, topic: str, wait_s: float) -> bytes | None:
        self.subscribe(topic)
        payload = self.wait_for(topic, wait_s)
        return payload or None

    def publish(self, topic: str, payload: bytes, retain: bool = False, qos: int = 0) -> None:
        info = self._client.publish(topic, payload, qos=qos, retain=retain)
        try:
            info.wait_for_publish(1.0)
        except (RuntimeError, ValueError) as e:
            raise BusError(f"publish failed: {e}") from e

    def close(self) -> None:
        self._client.disconnect()
        self._client.loop_stop()


def open_bus(cfg: Config) -> Bus:
    return PahoBus(cfg)


RETRY_PAUSE_S = 0.2


def connect_within(attempt, budget_s: float, per_try_s: float, clock=time.monotonic, sleep=time.sleep):
    """Call attempt(timeout_s) until it returns a bus or budget_s is spent.

    A slow or lossy network can miss one connect timeout and make the next; a
    hook has a few seconds to spare before Claude Code kills it. Raises the
    last BusError when the budget runs out.
    """
    deadline = clock() + budget_s
    while True:
        remaining = deadline - clock()
        try:
            return attempt(min(per_try_s, remaining))
        except BusError:
            if deadline - clock() <= RETRY_PAUSE_S:
                raise
            sleep(RETRY_PAUSE_S)


def open_bus_within(cfg: Config, budget_s: float) -> Bus:
    """open_bus, retrying within budget_s; each try keeps cfg.connect_timeout_s."""
    return connect_within(lambda timeout_s: PahoBus(dataclasses.replace(cfg, connect_timeout_s=timeout_s)),
                          budget_s, cfg.connect_timeout_s)
