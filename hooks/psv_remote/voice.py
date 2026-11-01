"""Voice replies: reassembling the Vita's audio chunks and running whisper.cpp.

The Vita streams 16 kHz mono 16-bit PCM in numbered chunks
(claude/voice/<vid>/<seq>) and closes the recording with
claude/voice/<vid>/end. A recording with a missing chunk is reported, never
transcribed in pieces.
"""

import json
import os
import re
import shutil
import subprocess
import tempfile
import time
import wave
from dataclasses import dataclass
from pathlib import Path

RATE = 16000
IDLE_S = 10.0  # a recording with nothing new for this long is dropped
TIMEOUT_S = 25.0  # the Vita gives up 30 s after release; a stuck job must not block the next
INCOMPLETE = "Audio incomplete — try again"

DEFAULT_MODEL = Path.home() / ".cache/psv-agent-remote/models/ggml-large-v3-turbo-q5_0.bin"
DEFAULT_LANGUAGE = "auto"  # "zh" writes English speech out as Chinese; auto still keeps mixed speech mixed
# Simplified Chinese with English words in it: keeps whisper from translating
# the English or drifting into Traditional characters.
DEFAULT_PROMPT = "以下是普通话的句子，中间夹杂一些English单词，比如commit、push、review、merge。"

_FALLBACKS = ("/opt/homebrew/bin/whisper-cli", "/usr/local/bin/whisper-cli")
_RANKS = (("large", 4), ("medium", 3), ("small", 2), ("base", 1), ("tiny", 0))
_CJK = re.compile(r"[　-〿㐀-鿿豈-﫿＀-￯]")


def model_rank(path: str) -> int:
    """How capable a ggml model is, from its file name; -1 when unrecognized."""
    name = Path(path).name.lower()
    for word, rank in _RANKS:
        if word in name:
            return rank
    return -1


def find_whisper(configured: str | None, which=shutil.which, fallbacks=_FALLBACKS) -> str | None:
    if configured:
        path = os.path.expanduser(configured)
        return path if os.access(path, os.X_OK) else None
    found = which("whisper-cli")
    if found:
        return found
    # VS Code started from the Dock may not have Homebrew on PATH.
    return next((path for path in fallbacks if os.access(path, os.X_OK)), None)


def availability(binary: str | None, model: str) -> str | None:
    """Why this machine can't transcribe, or None."""
    if not binary:
        return "whisper-cli not found — see the README"
    if not Path(model).is_file():
        return "Voice model missing — run PSV Agent Remote: Download Voice Model"
    return None


def write_wav(path: Path, pcm: bytes) -> None:
    with wave.open(str(path), "wb") as out:
        out.setnchannels(1)
        out.setsampwidth(2)
        out.setframerate(RATE)
        out.writeframes(pcm)


@dataclass
class Job:
    vid: str
    sid: str
    pcm: bytes | None
    error: str | None


class Assembler:
    def __init__(self, clock=time.monotonic):
        self.clock = clock
        self.parts: dict[str, dict[int, bytes]] = {}
        self.seen: dict[str, float] = {}

    def feed(self, topic: str, payload: bytes) -> Job | None:
        parts = topic.split("/")
        if len(parts) != 4 or parts[:2] != ["claude", "voice"]:
            return None
        vid, last = parts[2], parts[3]
        if last == "end":
            return self._end(vid, payload)
        if not last.isdigit():
            return None
        self.parts.setdefault(vid, {})[int(last)] = payload
        self.seen[vid] = self.clock()
        return None

    def _end(self, vid: str, payload: bytes) -> Job | None:
        try:
            data = json.loads(payload)
        except (ValueError, UnicodeDecodeError):
            return None
        sid = data.get("sid") if isinstance(data, dict) else None
        if not isinstance(sid, str) or not sid or any(c in sid for c in "/+#"):
            return None  # not a session id we could publish a result to
        parts = self.parts.pop(vid, {})
        self.seen.pop(vid, None)
        if data.get("cancel"):
            return None
        count = data.get("chunks")
        if not isinstance(count, int) or count <= 0 or set(parts) != set(range(count)):
            return Job(vid, sid, None, INCOMPLETE)
        return Job(vid, sid, b"".join(parts[i] for i in range(count)), None)

    def expire(self) -> list[str]:
        now = self.clock()
        gone = [vid for vid, seen in self.seen.items() if now - seen > IDLE_S]
        for vid in gone:
            self.parts.pop(vid, None)
            self.seen.pop(vid, None)
        return gone


class WhisperError(Exception):
    pass


def _join(lines: list[str]) -> str:
    text = ""
    for line in lines:
        if text and not (_CJK.match(text[-1]) and _CJK.match(line[0])):
            text += " "
        text += line
    return text


class Whisper:
    def __init__(self, binary: str, model: str, language: str, prompt: str,
                 run=subprocess.run, clock=time.monotonic):
        self.binary, self.model, self.language, self.prompt = binary, model, language, prompt
        self.run, self.clock = run, clock

    def transcribe(self, pcm: bytes) -> str:
        wav = None
        try:
            fd, name = tempfile.mkstemp(prefix="psv-voice-", suffix=".wav")
            os.close(fd)
            wav = Path(name)
            write_wav(wav, pcm)
            argv = [self.binary, "-m", self.model, "-f", str(wav), "-l", self.language,
                    "--prompt", self.prompt, "-nt", "-np"]
            try:
                # whisper may split a character across segment lines; never fail on decoding
                done = self.run(argv, capture_output=True, text=True, encoding="utf-8", errors="replace",
                                timeout=TIMEOUT_S)
            except subprocess.TimeoutExpired as e:
                raise WhisperError("Transcription took too long") from e
            except OSError as e:
                raise WhisperError(f"Couldn't run whisper-cli: {e}") from e
            if done.returncode != 0:
                raise WhisperError("Transcription failed")
            return _join([line.strip() for line in done.stdout.splitlines() if line.strip()])
        except OSError as e:
            raise WhisperError("Couldn't write the recording to a temporary file") from e
        finally:
            if wav is not None:
                wav.unlink(missing_ok=True)

    def benchmark(self) -> int:
        """Milliseconds to transcribe one second of silence (comparable across machines)."""
        start = self.clock()
        self.transcribe(b"\x00\x00" * RATE)
        return round((self.clock() - start) * 1000)
