import fcntl
import hashlib

from fakebus import FakeBus
from psv_remote import kiro_registry as reg
from psv_remote.config import Config
from psv_remote.kiro_bridge import run as kiro_run
from test_kiro_bridge import Native, RemovedOnVita, snap


def hold(tmp_path, key):
    """Hold a bridge's lock the way a running psv_kiro.py does."""
    f = (tmp_path / f"kiro-{key}.bridge.lock").open("a")
    fcntl.flock(f, fcntl.LOCK_EX | fcntl.LOCK_NB)
    return f


def test_lock_key_matches_psv_kiro():
    assert reg.lock_key("host", "/p") == hashlib.sha256(b"host\n/p").hexdigest()[:16]


def test_registry_round_trip(tmp_path):
    key = reg.lock_key("host", "/p")
    lock = hold(tmp_path, key)
    reg.write_entry(tmp_path, key, 4242, "/p", "kiro-abc")
    assert reg.entries(tmp_path) == [{"key": key, "pid": 4242, "cwd": "/p", "sid": "kiro-abc", "starting": False}]
    reg.remove_entry(tmp_path, key)
    assert reg.entries(tmp_path) == []
    lock.close()


def test_starting_until_sid_known(tmp_path):
    key = reg.lock_key("host", "/p")
    lock = hold(tmp_path, key)
    reg.write_entry(tmp_path, key, 4242, "/p")
    assert reg.entries(tmp_path)[0]["starting"] is True
    lock.close()


def test_registry_drops_stale_entries(tmp_path):
    key = reg.lock_key("host", "/p")
    reg.write_entry(tmp_path, key, 4242, "/p", "kiro-abc")  # nobody holds the lock: killed with SIGKILL
    assert reg.entries(tmp_path) == []
    assert not (tmp_path / f"kiro-{key}.bridge.json").exists()


def test_last_error_only_without_a_live_bridge(tmp_path):
    key = reg.lock_key("host", "/p")
    (tmp_path / f"kiro-{key}.log").write_text("starting\nKiro CLI is not logged in; run `kiro-cli login`\n\n")
    assert reg.last_error(tmp_path, key) == "Kiro CLI is not logged in; run `kiro-cli login`"
    lock = hold(tmp_path, key)
    reg.write_entry(tmp_path, key, 4242, "/p", "kiro-abc")
    assert reg.last_error(tmp_path, key) is None  # it is running now: the old log is history
    lock.close()
    assert reg.last_error(tmp_path, reg.lock_key("host", "/other")) is None


def test_on_ready_receives_sid(tmp_path):
    ready = []
    bus = RemovedOnVita()
    kiro_run(lambda: bus, Native(snap(state="idle", requests=[])), Config(state_dir=tmp_path, wait_timeout_s=5),
             "host", sleep=lambda s: None, on_ready=ready.append)
    assert ready == [bus.sid()]
