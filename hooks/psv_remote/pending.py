"""Per-session marker files for permission requests that are still waiting.

Each hook invocation is its own process, so parallel requests in one session
coordinate through the filesystem: the last one to finish resets the status.
Markers older than max_age_s belong to a hook that was killed and are ignored.
"""

import re
import shutil
import time
from pathlib import Path


def _safe(name: str) -> str:
    return re.sub(r"[^A-Za-z0-9_-]", "_", name) or "_"


class PendingTracker:
    def __init__(self, state_dir: Path, sid: str, max_age_s: float | None = None):
        self._dir = Path(state_dir) / _safe(sid)
        self._max_age_s = max_age_s

    def add(self, rid: str) -> None:
        self._dir.mkdir(parents=True, exist_ok=True)
        (self._dir / _safe(rid)).touch()

    def remove(self, rid: str) -> None:
        (self._dir / _safe(rid)).unlink(missing_ok=True)

    def count(self) -> int:
        cutoff = time.time() - self._max_age_s if self._max_age_s is not None else None
        n = 0
        try:
            for marker in self._dir.iterdir():
                try:
                    if cutoff is None or marker.stat().st_mtime >= cutoff:
                        n += 1
                except FileNotFoundError:
                    pass
        except FileNotFoundError:
            pass
        return n

    def clear(self) -> None:
        shutil.rmtree(self._dir, ignore_errors=True)
