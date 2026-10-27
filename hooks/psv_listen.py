"""Background listener for replies from the PSV (an asyncRewake hook).

Claude Code starts it on SessionStart and UserPromptSubmit. It waits for a
reply for this session; to deliver one it writes the text to stderr and exits
2, which wakes the session. Every other ending, errors included, exits 0.
"""

import json
import os
import signal
import sys
import time
import traceback
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

LOG_LIMIT = 1024 * 1024
LIFETIME_S = 43200 - 60  # just inside the hook's 12 h timeout


def _log(text: str) -> None:
    try:
        log = Path.home() / ".cache" / "psv-agent-remote" / "hook.log"
        log.parent.mkdir(parents=True, exist_ok=True)
        if log.exists() and log.stat().st_size > LOG_LIMIT:
            log.write_bytes(log.read_bytes()[-LOG_LIMIT // 2:])
        with log.open("a", encoding="utf-8") as f:
            f.write(text)
    except Exception:
        pass


def main() -> str | None:
    from psv_remote.bus import open_bus
    from psv_remote.config import load_config
    from psv_remote.listener import run

    event = json.loads(sys.stdin.buffer.read())
    sid = event.get("session_id")
    cfg = load_config()
    if not cfg.enabled or not isinstance(sid, str) or not sid:
        return None
    lifetime = float(os.environ.get("PSV_LISTEN_MAX_S") or LIFETIME_S)
    return run(lambda: open_bus(cfg), sid, cfg, os.getpid(), time.monotonic() + lifetime)


def _on_sigterm(signum, frame) -> None:
    raise SystemExit(0)


if __name__ == "__main__":
    signal.signal(signal.SIGTERM, _on_sigterm)
    text = None
    try:
        text = main()
    except BaseException:
        _log(traceback.format_exc())
    if text:
        sys.stderr.write(text + "\n")
        sys.stderr.flush()
        os._exit(2)
    os._exit(0)
