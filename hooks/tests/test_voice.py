import json
import struct
import subprocess
from pathlib import Path

import pytest

from psv_remote import topics
from psv_remote import voice
from psv_remote.voice import Assembler, Whisper, WhisperError


class Clock:
    def __init__(self):
        self.t = 0.0

    def __call__(self):
        return self.t


def chunk(vid, seq, data):
    return topics.voice_chunk(vid, seq), data


def end(vid, sid="s1", chunks=None, ms=1500, cancel=False):
    body = {"sid": sid, "cancel": True} if cancel else {"sid": sid, "chunks": chunks, "ms": ms}
    return topics.voice_end(vid), json.dumps(body).encode()


def test_topics():
    assert topics.transcriber("h1") == "claude/transcriber/h1"
    assert topics.voice_chunk("ab", 3) == "claude/voice/ab/3"
    assert topics.voice_end("ab") == "claude/voice/ab/end"
    assert topics.voice_result("s1") == "claude/session/s1/voice"
    assert topics.VOICE_ALL == "claude/voice/#"
    assert topics.TRANSCRIBERS == "claude/transcriber/+"


def test_model_rank():
    assert voice.model_rank("/m/ggml-large-v3-turbo-q5_0.bin") == 4
    assert voice.model_rank("ggml-medium.bin") == 3
    assert voice.model_rank("ggml-small.en.bin") == 2
    assert voice.model_rank("ggml-base.bin") == 1
    assert voice.model_rank("ggml-tiny.bin") == 0
    assert voice.model_rank("my-model.bin") == -1


def test_assembler_joins_in_order():
    a = Assembler(Clock())
    assert a.feed(*chunk("v1", 1, b"BB")) is None
    assert a.feed(*chunk("v1", 0, b"AA")) is None
    assert a.feed(*chunk("v1", 2, b"CC")) is None
    job = a.feed(*end("v1", chunks=3))
    assert (job.vid, job.sid, job.pcm, job.error) == ("v1", "s1", b"AABBCC", None)
    assert a.expire() == []  # finished recordings are forgotten


def test_assembler_gap_is_an_error():
    a = Assembler(Clock())
    a.feed(*chunk("v1", 0, b"AA"))
    a.feed(*chunk("v1", 2, b"CC"))
    job = a.feed(*end("v1", chunks=3))
    assert job.pcm is None and job.error == "Audio incomplete — try again"


def test_tail_only_is_an_error():
    a = Assembler(Clock())  # a transcriber that took over mid-recording
    a.feed(*chunk("v1", 5, b"FF"))
    job = a.feed(*end("v1", chunks=6))
    assert job.error == "Audio incomplete — try again"


def test_end_without_chunks_is_an_error():
    job = Assembler(Clock()).feed(*end("v1", chunks=2))
    assert job.error == "Audio incomplete — try again"


def test_assembler_cancel_yields_nothing():
    a = Assembler(Clock())
    a.feed(*chunk("v1", 0, b"AA"))
    assert a.feed(*end("v1", cancel=True)) is None
    assert a.expire() == []


def test_assembler_expires_idle_after_10s():
    clock = Clock()
    a = Assembler(clock)
    a.feed(*chunk("v1", 0, b"AA"))
    clock.t = 9.9
    assert a.expire() == []
    clock.t = 10.1
    assert a.expire() == ["v1"]
    assert a.feed(*end("v1", chunks=1)).error == "Audio incomplete — try again"


def test_malformed_messages_are_ignored():
    a = Assembler(Clock())
    assert a.feed("claude/voice/v1/notanumber", b"x") is None
    assert a.feed(topics.voice_end("v1"), b"not json") is None
    assert a.feed("claude/other", b"x") is None


def test_wav_header(tmp_path):
    pcm = b"\x01\x00" * 100
    path = tmp_path / "a.wav"
    voice.write_wav(path, pcm)
    data = path.read_bytes()
    assert data[:4] == b"RIFF" and data[8:12] == b"WAVE"
    assert struct.unpack_from("<H", data, 22)[0] == 1
    assert struct.unpack_from("<I", data, 24)[0] == 16000
    assert struct.unpack_from("<I", data, 40)[0] == len(pcm)
    assert data[44:] == pcm


class Run:
    def __init__(self, stdout="", returncode=0, exc=None):
        self.stdout, self.returncode, self.exc = stdout, returncode, exc
        self.argv = None
        self.wav_existed = False

    def __call__(self, argv, **kw):
        self.argv = argv
        self.wav_existed = Path(argv[argv.index("-f") + 1]).exists()
        if self.exc:
            raise self.exc
        return subprocess.CompletedProcess(argv, self.returncode, self.stdout, "boom")


def test_whisper_command_and_output():
    run = Run("你好，commit 一下\n")
    w = Whisper("/bin/whisper-cli", "/m/model.bin", "zh", "提示", run=run)
    assert w.transcribe(b"\x00\x00" * 160) == "你好，commit 一下"
    argv = run.argv
    assert argv[0] == "/bin/whisper-cli"
    assert argv[argv.index("-m") + 1] == "/m/model.bin"
    assert argv[argv.index("-l") + 1] == "zh"
    assert argv[argv.index("--prompt") + 1] == "提示"
    assert "-nt" in argv and "-np" in argv
    assert run.wav_existed
    assert not Path(argv[argv.index("-f") + 1]).exists()


def test_whisper_joins_lines():
    w = Whisper("w", "m", "zh", "p", run=Run("先 review\n然后merge\n"))
    assert w.transcribe(b"") == "先 review 然后merge"
    w = Whisper("w", "m", "zh", "p", run=Run("你好\n世界\n"))
    assert w.transcribe(b"") == "你好世界"


def test_whisper_failure_raises():
    run = Run(returncode=1)
    with pytest.raises(WhisperError):
        Whisper("w", "m", "zh", "p", run=run).transcribe(b"")
    assert not Path(run.argv[run.argv.index("-f") + 1]).exists()
    with pytest.raises(WhisperError):
        Whisper("w", "m", "zh", "p", run=Run(exc=subprocess.TimeoutExpired("w", 120))).transcribe(b"")
    with pytest.raises(WhisperError):
        Whisper("w", "m", "zh", "p", run=Run(exc=FileNotFoundError())).transcribe(b"")


def test_benchmark_measures_ms():
    times = iter([1.0, 1.75])
    w = Whisper("w", "m", "zh", "p", run=Run(""), clock=lambda: next(times))
    assert w.benchmark() == 750


def test_find_whisper(tmp_path):
    binary = tmp_path / "whisper-cli"
    binary.write_text("")
    binary.chmod(0o755)
    assert voice.find_whisper(str(binary), which=lambda n: None) == str(binary)
    assert voice.find_whisper(None, which=lambda n: "/usr/bin/whisper-cli") == "/usr/bin/whisper-cli"
    assert voice.find_whisper(str(tmp_path / "missing"), which=lambda n: "/usr/bin/whisper-cli") is None
    assert voice.find_whisper(None, which=lambda n: None, fallbacks=()) is None


def test_availability_messages(tmp_path):
    model = tmp_path / "m.bin"
    assert "whisper-cli not found" in voice.availability(None, str(model))
    assert "Download Voice Model" in voice.availability("/bin/w", str(model))
    model.write_bytes(b"x")
    assert voice.availability("/bin/w", str(model)) is None


def test_defaults():
    assert voice.DEFAULT_MODEL.name == "ggml-large-v3-turbo-q5_0.bin"
    assert voice.DEFAULT_MODEL.parent == Path.home() / ".cache/psv-agent-remote/models"
    assert voice.DEFAULT_LANGUAGE == "auto"  # "zh" turns English speech into Chinese
    assert "English" in voice.DEFAULT_PROMPT


def test_whisper_decodes_utf8_leniently_within_the_vita_budget():
    seen = {}

    def run(argv, **kw):
        seen.update(kw)
        return subprocess.CompletedProcess(argv, 0, "ok", "")
    Whisper("w", "m", "zh", "p", run=run).transcribe(b"")
    assert seen["encoding"] == "utf-8" and seen["errors"] == "replace"
    assert seen["timeout"] <= 25  # the Vita gives up 30 s after release


def test_whisper_temp_file_failure_raises(monkeypatch):
    import tempfile
    monkeypatch.setattr(tempfile, "mkstemp", lambda **kw: (_ for _ in ()).throw(OSError("read-only")))
    with pytest.raises(WhisperError):
        Whisper("w", "m", "zh", "p", run=Run("")).transcribe(b"")
