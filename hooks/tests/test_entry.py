import json
import os
import subprocess
import sys
import time
from pathlib import Path

HOOK = Path(__file__).resolve().parent.parent / "psv_hook.py"


def run_hook(stdin: bytes, env: dict | None = None, tmp: Path | None = None):
    full_env = {**os.environ, "HOME": str(tmp) if tmp else os.environ["HOME"], **(env or {})}
    return subprocess.run([sys.executable, str(HOOK)], input=stdin, env=full_env,
                          capture_output=True, timeout=10)


def test_garbage_stdin_is_silent(tmp_path):
    r = run_hook(b"not json", tmp=tmp_path)
    assert r.returncode == 0 and r.stdout == b"" and r.stderr == b""
    assert (tmp_path / ".cache" / "psv-agent-remote" / "hook.log").exists()


def test_unreachable_broker_falls_back_fast(tmp_path):
    ev = {"hook_event_name": "PermissionRequest", "session_id": "s", "cwd": "/x",
          "tool_name": "Bash", "tool_input": {"command": "ls"}}
    t0 = time.monotonic()
    r = run_hook(json.dumps(ev).encode(), tmp=tmp_path,
                 env={"PSV_REMOTE_HOST": "127.0.0.1", "PSV_REMOTE_PORT": "1",
                      "PSV_REMOTE_STATE_DIR": str(tmp_path / "state")})
    assert r.returncode == 0 and r.stdout == b"" and time.monotonic() - t0 < 4
