import os
import time

from psv_remote.pending import PendingTracker


def test_tracker_counts(tmp_path):
    a = PendingTracker(tmp_path, "s1")
    b = PendingTracker(tmp_path, "s1")
    a.add("r1")
    b.add("r2")
    assert a.count() == 2
    a.remove("r1")
    assert b.count() == 1
    b.remove("r2")
    assert a.count() == 0
    a.remove("nope")


def test_tracker_ignores_stale_markers(tmp_path):
    t = PendingTracker(tmp_path, "s1", max_age_s=10)
    t.add("old")
    t.add("new")
    old = time.time() - 60
    os.utime(tmp_path / "s1" / "old", (old, old))
    assert t.count() == 1


def test_tracker_clear(tmp_path):
    t = PendingTracker(tmp_path, "s1")
    t.add("r1")
    t.clear()
    assert t.count() == 0 and not (tmp_path / "s1").exists()
    t.clear()


def test_tracker_sanitizes_sid(tmp_path):
    PendingTracker(tmp_path, "../evil").add("r")
    assert not (tmp_path.parent / "evil").exists()
