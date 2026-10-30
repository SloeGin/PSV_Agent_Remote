"""Which Kiro bridges are running on this machine, for the VS Code panel.

Each psv_kiro.py holds kiro-<key>.bridge.lock while it runs (the authority on
"running") and keeps kiro-<key>.bridge.json next to it: its pid, working
directory and, once the Kiro session exists, the session id. Bridges started
from VS Code log to kiro-<key>.log. An entry whose lock nobody holds belongs to
a bridge that died without cleaning up; listing removes it.
"""

import fcntl
import hashlib
import json
import os
from pathlib import Path


def lock_key(host_id: str, cwd: str) -> str:
    return hashlib.sha256(f"{host_id}\n{cwd}".encode()).hexdigest()[:16]


def _entry_path(state_dir: Path, key: str) -> Path:
    return Path(state_dir) / f"kiro-{key}.bridge.json"


def _held(state_dir: Path, key: str) -> bool:
    path = Path(state_dir) / f"kiro-{key}.bridge.lock"
    try:
        with path.open("a") as lock:
            try:
                fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except BlockingIOError:
                return True
            fcntl.flock(lock, fcntl.LOCK_UN)
            return False
    except OSError:
        return False


def write_entry(state_dir: Path, key: str, pid: int, cwd: str, sid: str | None = None) -> None:
    path = _entry_path(state_dir, key)
    tmp = path.with_name(f"{path.name}.{os.getpid()}")
    tmp.write_text(json.dumps({"pid": pid, "cwd": cwd, "sid": sid}))
    os.replace(tmp, path)


def remove_entry(state_dir: Path, key: str) -> None:
    _entry_path(state_dir, key).unlink(missing_ok=True)


def entries(state_dir: Path) -> list[dict]:
    """Running bridges, oldest first by working directory; stale entries are deleted."""
    found = []
    for path in sorted(Path(state_dir).glob("kiro-*.bridge.json")):
        key = path.name[len("kiro-"):-len(".bridge.json")]
        if not _held(state_dir, key):
            path.unlink(missing_ok=True)
            continue
        try:
            data = json.loads(path.read_text())
        except (OSError, ValueError):
            continue
        if not isinstance(data, dict) or not isinstance(data.get("cwd"), str):
            continue
        found.append({"key": key, "pid": data.get("pid"), "cwd": data["cwd"], "sid": data.get("sid"),
                      "starting": data.get("sid") is None})
    return sorted(found, key=lambda e: e["cwd"])


def last_error(state_dir: Path, key: str) -> str | None:
    """Why a start failed: the log's last line, when no bridge for this key is running."""
    if _held(state_dir, key):
        return None
    try:
        lines = [line.strip() for line in (Path(state_dir) / f"kiro-{key}.log").read_text().splitlines()]
    except (OSError, UnicodeDecodeError):
        return None
    lines = [line for line in lines if line]
    return lines[-1][:200] if lines else None
