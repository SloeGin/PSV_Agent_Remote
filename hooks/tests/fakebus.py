from collections.abc import Callable

from psv_remote.bus import Bus, BusError


class FakeBus:
    def __init__(self):
        self.retained: dict[str, bytes] = {}
        self.published: list[tuple[str, bytes, bool]] = []
        self.subscribed: list[str] = []
        self.inbox: list[tuple[str, bytes]] = []  # what next_message hands out, oldest first
        self.responder: Callable[[str], bytes | None] | None = None
        self.closed = False
        self.fail_publish_after_response = False
        self._responded = False

    def read_retained(self, topic: str, wait_s: float) -> bytes | None:
        return self.retained.get(topic)

    def subscribe(self, topic: str) -> None:
        self.subscribed.append(topic)
        if topic in self.retained:  # a broker sends the retained message on subscribe
            self.inbox.append((topic, self.retained[topic]))

    def publish(self, topic: str, payload: bytes, retain: bool = False, qos: int = 0) -> None:
        if self.fail_publish_after_response and self._responded:
            raise BusError("connection lost")
        self.published.append((topic, payload, retain))
        if retain:
            if payload:
                self.retained[topic] = payload
            else:
                self.retained.pop(topic, None)

    def wait_for(self, topic: str, timeout_s: float) -> bytes | None:
        self._responded = True
        return self.responder(topic) if self.responder else None

    def next_message(self, timeout_s: float) -> tuple[str, bytes] | None:
        return self.inbox.pop(0) if self.inbox else None

    def close(self) -> None:
        self.closed = True


def factory_failing() -> Bus:
    raise BusError("unreachable")
