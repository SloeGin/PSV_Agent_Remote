"""Claude Code hook entrypoint for PSV Agent Remote.

Reads one hook event from stdin and prints a decision, or nothing. It always
exits 0 and never writes to stderr: silence means Claude Code falls back to
its own permission prompt.
"""

import json
import os
import signal
import sys
import traceback
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

LOG_LIMIT = 1024 * 1024


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


def main() -> None:
    from psv_remote.adapters.claude import connect_budget_s
    from psv_remote.bus import open_bus, open_bus_within
    from psv_remote.config import load_config
    from psv_remote.handlers import handle_event

    event = json.loads(sys.stdin.buffer.read())
    cfg = load_config()
    budget = connect_budget_s(str(event.get("hook_event_name")))
    connect = (lambda: open_bus(cfg)) if budget is None else (lambda: open_bus_within(cfg, budget))
    out = handle_event(event, connect, cfg, os.environ)
    if out is not None:
        sys.stdout.write(json.dumps(out, ensure_ascii=False) + "\n")
        sys.stdout.flush()


def _on_sigterm(signum, frame) -> None:
    # Turn a kill from Claude Code into an exception so cleanup in `finally` runs.
    raise SystemExit(0)


if __name__ == "__main__":
    signal.signal(signal.SIGTERM, _on_sigterm)
    try:
        main()
    except BaseException:
        _log(traceback.format_exc())
    os._exit(0)
