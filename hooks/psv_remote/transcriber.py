"""The voice transcriber each bridge manager runs, and how machines pick one.

Every machine that can transcribe announces itself, retained, on
claude/transcriber/<host> with its model and a speed score, and leaves an empty
last-will so the announcement disappears when it dies. Every machine applies
elect() to the same announcements, so they agree on one winner without talking
to each other; only the winner transcribes. Whisper runs on a worker thread
(the benchmark too: on a slow machine it takes seconds) so the manager loop
never waits on it.
"""

import json
import queue
import threading
import time
from pathlib import Path

from . import topics
from .bus import BusError
from .voice import (DEFAULT_LANGUAGE, DEFAULT_MODEL, DEFAULT_PROMPT, Assembler, Whisper, WhisperError,
                    availability, find_whisper, model_rank)

NOTHING_HEARD = "Didn't catch that — try again"
_PREFIX = "claude/transcriber/"


def elect(announcements: dict[str, dict]) -> str | None:
    """The winner: the most capable model, then the fastest, then the lowest host id."""
    best = None
    for host, data in announcements.items():
        if not isinstance(data, dict):
            continue
        rank, score = data.get("rank"), data.get("score_ms")
        if not isinstance(rank, int) or isinstance(rank, bool) or not isinstance(score, (int, float)):
            continue
        key = (-rank, score, host)
        if best is None or key < best:
            best = key
    return best[2] if best else None


class Transcriber:
    RETRY_S = 10

    def __init__(self, cfg, host_id, voice: dict, bus_factory, whisper_factory=Whisper, clock=time.monotonic,
                 find=find_whisper, model_exists=lambda path: Path(path).is_file(), start_worker=True):
        self.host_id, self.bus_factory, self.clock = host_id, bus_factory, clock
        self.enabled = voice.get("enabled", True)
        model = voice.get("model") or str(DEFAULT_MODEL)
        binary = find(voice.get("whisper"))
        self.problem = None
        if self.enabled and not binary:
            self.problem = availability(None, model)
        elif self.enabled and not model_exists(model):
            self.problem = availability(binary, "")  # "" is never a file: the missing-model message
        self.whisper = None if not self.enabled or self.problem else whisper_factory(
            binary, model, voice.get("language") or DEFAULT_LANGUAGE, voice.get("prompt") or DEFAULT_PROMPT)
        self.model_name, self.rank = Path(model).stem, model_rank(model)
        self.score: int | None = None
        self.benchmarking = False
        self.bus, self.retry_at, self.down = None, 0.0, False
        self.announcements: dict[str, dict] = {}
        self.assembler = Assembler(clock)
        self.work: queue.Queue = queue.Queue()
        self.worker = None
        if start_worker and self.whisper is not None:
            self.worker = threading.Thread(target=self._run, daemon=True, name="psv-transcriber")
            self.worker.start()

    # ---- worker ----

    def _run(self):
        while (item := self.work.get()) is not None:
            self._run_one(item)

    @staticmethod
    def _run_one(item):
        """One queued item; nothing it raises may end the worker (the machine would keep winning, silently)."""
        try:
            item()
        except Exception as e:  # last resort; it goes to the manager log
            print(f"Voice transcriber: {e!r}", flush=True)

    def work_once(self) -> bool:
        """Run one queued item on this thread (tests); False when nothing was queued."""
        try:
            item = self.work.get_nowait()
        except queue.Empty:
            return False
        if item is not None:
            self._run_one(item)
        return True

    def _benchmark(self):
        try:
            self.score = self.whisper.benchmark()
        except WhisperError as e:
            self.problem = str(e)
        except Exception as e:
            self.problem = f"Couldn't run whisper: {e}"
        self.benchmarking = False

    def _transcribe(self, job):
        if job.error:
            result = {"id": job.vid, "error": job.error}
        else:
            try:
                text = self.whisper.transcribe(job.pcm)
                result = {"id": job.vid, "text": text} if text else {"id": job.vid, "error": NOTHING_HEARD}
            except WhisperError as e:
                result = {"id": job.vid, "error": str(e)}
            except Exception:
                result = {"id": job.vid, "error": "Transcription failed"}
        bus = self.bus
        if bus is None:
            return  # disconnected meanwhile; the Vita times out
        try:
            bus.publish(topics.voice_result(job.sid), json.dumps(result, ensure_ascii=False).encode(),
                        retain=True, qos=1)
        except (BusError, ValueError):  # paho raises ValueError for a topic it rejects
            pass

    # ---- manager loop ----

    def _usable(self) -> bool:
        return self.enabled and self.whisper is not None and self.problem is None

    def poll(self) -> None:
        """Connect when due, announce, and handle what arrived; never blocks for long."""
        if not self._usable():
            return
        if self.score is None:
            if not self.benchmarking:
                self.benchmarking = True
                self.work.put(self._benchmark)
            return
        if self.bus is None:
            if self.clock() < self.retry_at:
                return
            try:
                self.bus = self.bus_factory(will=(topics.transcriber(self.host_id), b""))
                self.bus.subscribe(topics.TRANSCRIBERS)
                self.bus.subscribe(topics.VOICE_ALL)
                self._announce()
                self.down = False
            except BusError:
                self._drop()
                return
        try:
            for _ in range(500):
                message = self.bus.next_message(0.01)
                if message is None:
                    break
                self._feed(*message)
        except BusError:
            self._drop()
            return
        self.assembler.expire()

    def _announce(self):
        mine = {"v": 1, "host": self.host_id, "model": self.model_name, "rank": self.rank, "score_ms": self.score}
        self.announcements[self.host_id] = mine
        self.bus.publish(topics.transcriber(self.host_id), json.dumps(mine).encode(), retain=True, qos=1)

    def _feed(self, topic: str, payload: bytes):
        if topic.startswith(_PREFIX):
            host = topic[len(_PREFIX):]
            if host == self.host_id:
                if not payload:
                    self._announce()  # a late will from our previous connection
                return
            if not payload:
                self.announcements.pop(host, None)
                return
            try:
                data = json.loads(payload)
            except (ValueError, UnicodeDecodeError):
                return
            if isinstance(data, dict):
                self.announcements[host] = data
        elif topic.startswith("claude/voice/"):
            job = self.assembler.feed(topic, payload)
            if job is not None and elect(self.announcements) == self.host_id:
                self.work.put(lambda job=job: self._transcribe(job))

    def _drop(self):
        if self.bus is not None:
            try:
                self.bus.close()
            except Exception:
                pass
        self.bus, self.retry_at, self.down = None, self.clock() + self.RETRY_S, True
        self.announcements = {}  # retained announcements come again on the next subscription

    def state(self) -> dict:
        if not self.enabled:
            return {"state": "off", "message": "off", "leader": None}
        if self.problem:
            return {"state": "unavailable", "message": self.problem, "leader": None}
        if self.score is None:
            return {"state": "standby", "message": "measuring this computer's speed", "leader": None}
        if self.bus is None:
            return {"state": "standby", "message": "can't reach the broker", "leader": None}
        leader = elect(self.announcements)
        if leader == self.host_id:
            return {"state": "active", "message": "this computer transcribes", "leader": leader}
        return {"state": "standby", "message": f"{leader} transcribes", "leader": leader}

    def close(self) -> None:
        if self.bus is not None:
            try:
                self.bus.publish(topics.transcriber(self.host_id), b"", retain=True, qos=1)
            except BusError:
                pass
            try:
                self.bus.close()
            except Exception:
                pass
            self.bus = None
        if self.worker is not None:
            self.work.put(None)
