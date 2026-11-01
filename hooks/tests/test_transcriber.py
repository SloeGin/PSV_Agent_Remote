import json

from fakebus import FakeBus
from psv_remote import topics
from psv_remote.bus import BusError
from psv_remote.config import Config
from psv_remote.transcriber import Transcriber, elect
from psv_remote.voice import WhisperError

MODEL = "/m/ggml-large-v3-turbo-q5_0.bin"


def ann(rank, score, model="ggml-large-v3-turbo-q5_0"):
    return {"v": 1, "model": model, "rank": rank, "score_ms": score}


def test_elect_prefers_model_then_speed_then_host():
    assert elect({"a": ann(4, 900), "b": ann(3, 100)}) == "a"
    assert elect({"a": ann(4, 900), "c": ann(4, 500)}) == "c"
    assert elect({"c": ann(4, 500), "b2": ann(4, 500)}) == "b2"
    assert elect({"x": {"rank": "big"}, "y": ann(0, 50)}) == "y"
    assert elect({}) is None


class FakeWhisper:
    def __init__(self, text="你好", score=700, fail=None):
        self.text, self.score, self.fail = text, score, fail
        self.pcm = []

    def __call__(self, binary, model, language, prompt):
        self.args = (binary, model, language, prompt)
        return self

    def transcribe(self, pcm):
        self.pcm.append(pcm)
        if self.fail:
            raise WhisperError(self.fail)
        return self.text

    def benchmark(self):  # the real one transcribes a silent clip
        if self.fail:
            raise WhisperError(self.fail)
        return self.score


class Clock:
    def __init__(self):
        self.t = 100.0

    def __call__(self):
        return self.t


VOICE = {"enabled": True, "whisper": None, "model": MODEL, "language": "zh", "prompt": "p"}


def make(whisper=None, voice=None, found="/bin/whisper-cli", model_exists=True, bus=None, clock=None):
    bus = bus or FakeBus()
    made = []

    def factory(will=None):
        bus.will = will
        made.append(will)
        return bus

    t = Transcriber(Config(), "h1", {**VOICE, **(voice or {})}, factory,
                    whisper_factory=whisper or FakeWhisper(), clock=clock or Clock(),
                    find=lambda configured: found, model_exists=lambda path: model_exists,
                    start_worker=False)
    return t, bus


def presence(bus, host="h1"):
    raw = bus.retained.get(topics.transcriber(host))
    return json.loads(raw) if raw else None


def start(t):
    t.poll()        # queues the benchmark
    t.work_once()   # runs it
    t.poll()        # connects and announces


def recording(bus, vid="v1", sid="s1", chunks=(b"AA", b"BB")):
    for i, data in enumerate(chunks):
        bus.inbox.append((topics.voice_chunk(vid, i), data))
    bus.inbox.append((topics.voice_end(vid), json.dumps({"sid": sid, "chunks": len(chunks), "ms": 1000}).encode()))


def test_announces_with_will():
    t, bus = make()
    start(t)
    assert bus.will == (topics.transcriber("h1"), b"")
    assert presence(bus) == {"v": 1, "host": "h1", "model": "ggml-large-v3-turbo-q5_0", "rank": 4, "score_ms": 700}
    assert {topics.TRANSCRIBERS, topics.VOICE_ALL} <= set(bus.subscribed)
    assert t.state() == {"state": "active", "message": "this computer transcribes", "leader": "h1"}


def test_unavailable_does_not_announce():
    t, bus = make(found=None)
    start(t)
    assert bus.published == []
    state = t.state()
    assert state["state"] == "unavailable" and "whisper-cli" in state["message"]


def test_model_missing_is_unavailable():
    t, bus = make(model_exists=False)
    start(t)
    assert bus.published == [] and "Download Voice Model" in t.state()["message"]


def test_benchmark_failure_is_unavailable():
    t, bus = make(whisper=FakeWhisper(fail="Transcription failed"))
    start(t)
    assert bus.published == []
    assert t.state()["state"] == "unavailable" and "Transcription failed" in t.state()["message"]


def test_disabled_is_off():
    t, bus = make(voice={"enabled": False})
    start(t)
    assert bus.published == [] and t.state()["state"] == "off"


def test_leader_transcribes_and_publishes_result():
    whisper = FakeWhisper(text="你好")
    t, bus = make(whisper=whisper)
    start(t)
    recording(bus)
    t.poll()
    t.work_once()
    assert whisper.pcm == [b"AABB"]
    assert json.loads(bus.retained[topics.voice_result("s1")]) == {"id": "v1", "text": "你好"}


def test_empty_transcript_is_an_error():
    t, bus = make(whisper=FakeWhisper(text=""))
    start(t)
    recording(bus)
    t.poll()
    t.work_once()
    assert json.loads(bus.retained[topics.voice_result("s1")]) == {"id": "v1", "error": "Didn't catch that — try again"}


def test_standby_ignores_jobs():
    whisper = FakeWhisper()
    t, bus = make(whisper=whisper)
    start(t)
    bus.inbox.append((topics.transcriber("other"), json.dumps(ann(4, 100)).encode()))
    recording(bus)
    t.poll()
    assert t.work_once() is False
    assert whisper.pcm == [] and topics.voice_result("s1") not in bus.retained
    assert t.state() == {"state": "standby", "message": "other transcribes", "leader": "other"}


def test_takeover_after_leader_vanishes():
    t, bus = make()
    start(t)
    bus.inbox.append((topics.transcriber("other"), json.dumps(ann(4, 100)).encode()))
    t.poll()
    assert t.state()["leader"] == "other"
    bus.inbox.append((topics.transcriber("other"), b""))
    t.poll()
    assert t.state()["state"] == "active"


def test_own_presence_cleared_is_announced_again():
    t, bus = make()  # the broker can deliver our previous connection's will late
    start(t)
    bus.retained.pop(topics.transcriber("h1"))
    bus.inbox.append((topics.transcriber("h1"), b""))
    t.poll()
    assert presence(bus)["score_ms"] == 700


def test_whisper_error_becomes_error_result():
    t, bus = make(whisper=FakeWhisper(fail="Transcription failed"))
    t.score = 700  # the benchmark passed earlier; this recording fails
    t.poll()
    recording(bus)
    t.poll()
    t.work_once()
    assert json.loads(bus.retained[topics.voice_result("s1")]) == {"id": "v1", "error": "Transcription failed"}


def test_incomplete_audio_error_result():
    whisper = FakeWhisper()
    t, bus = make(whisper=whisper)
    start(t)
    bus.inbox.append((topics.voice_chunk("v1", 4), b"EE"))  # took over mid-recording
    bus.inbox.append((topics.voice_end("v1"), json.dumps({"sid": "s1", "chunks": 5, "ms": 2500}).encode()))
    t.poll()
    t.work_once()
    assert whisper.pcm == []
    assert json.loads(bus.retained[topics.voice_result("s1")]) == {"id": "v1", "error": "Audio incomplete — try again"}


def test_idle_recording_dropped():
    clock = Clock()
    t, bus = make(clock=clock)
    start(t)
    bus.inbox.append((topics.voice_chunk("v1", 0), b"AA"))
    t.poll()
    clock.t += 10.5
    t.poll()
    assert t.assembler.parts == {}
    assert t.work_once() is False
    assert topics.voice_result("s1") not in bus.retained


def test_unreachable_broker_retries_later():
    clock = Clock()
    attempts = []

    def failing(will=None):
        attempts.append(will)
        raise BusError("unreachable")

    t = Transcriber(Config(), "h1", VOICE, failing, whisper_factory=FakeWhisper(), clock=clock,
                    find=lambda c: "/bin/w", model_exists=lambda p: True, start_worker=False)
    t.poll()
    t.work_once()
    t.poll()
    t.poll()
    assert len(attempts) == 1
    clock.t += 10.1
    t.poll()
    assert len(attempts) == 2
    assert t.state()["state"] == "standby" and "broker" in t.state()["message"]


def test_close_clears_presence():
    t, bus = make()
    start(t)
    t.close()
    assert topics.transcriber("h1") not in bus.retained
    assert bus.closed


def test_settings_reach_whisper():
    whisper = FakeWhisper()
    t, bus = make(whisper=whisper, voice={"language": "en", "prompt": "hello"})
    start(t)
    assert whisper.args == ("/bin/whisper-cli", MODEL, "en", "hello")
