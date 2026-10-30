import os
import http.client
import json
import stat

import pytest

from psv_remote.codex_control import Leases, ManagerControl, configuration_key, request
from psv_remote.config import Config
from psv_remote.vscode_client import manager_arguments


def test_two_windows_release_independently_and_crashed_window_expires():
    clock = [0]
    leases = Leases(True, clock=lambda: clock[0])
    leases.renew("first")
    leases.renew("second")
    leases.release("first")
    clock[0] = 10
    leases.renew("second")
    assert not leases.expired()
    clock[0] = 26
    assert not leases.expired() and not leases.clients
    clock[0] = 35
    assert leases.expired()


def test_persistent_manager_is_not_stopped_by_window_leases():
    clock = [0]
    leases = Leases(False, clock=lambda: clock[0])
    leases.renew("window")
    leases.release("window")
    clock[0] = 1000
    assert not leases.expired()


def test_control_auth_identity_and_private_descriptor(tmp_path):
    cfg = Config(state_dir=tmp_path)
    control = ManagerControl(cfg, "host", "settings", managed=True)
    try:
        assert stat.S_IMODE(control.path.stat().st_mode) == 0o600
        descriptor = json.loads(control.path.read_text())
        connection = http.client.HTTPConnection("127.0.0.1", descriptor["port"])
        connection.request("POST", "/stop", "{}")
        response = connection.getresponse()
        assert response.status == 403
        response.read(); connection.close()
        assert not control.should_stop()
        first = request(cfg, "host", "lease", "window-a")
        second = request(cfg, "host", "lease", "window-b")
        assert first["windows"] == 1 and second["windows"] == 2
        request(cfg, "host", "release", "window-a")
        assert request(cfg, "host")["windows"] == 1
        request(cfg, "host", "stop")
        assert control.should_stop()
    finally:
        control.close()
    assert not control.path.exists()


def test_extension_cannot_stop_persistent_manager(tmp_path):
    cfg = Config(state_dir=tmp_path)
    control = ManagerControl(cfg, "host", "settings", managed=False)
    try:
        with pytest.raises(ValueError): request(cfg, "host", "stop")
        assert not control.should_stop()
    finally:
        control.close()


def test_client_reconstructs_manager_settings_including_paths_with_spaces(tmp_path):
    options = dict(ipc_path=tmp_path / "socket path", database=tmp_path / "index", app_paths=[], cwd=None, limit=4)
    args = manager_arguments("host", options)
    assert "--lease-managed" in args and str(tmp_path / "socket path") in args
    assert configuration_key(Config(), "host", options) == configuration_key(Config(), "host", dict(reversed(list(options.items()))))


# ---- a manager with other settings: take it over when unused, otherwise say what differs ----

from psv_remote.codex_control import configuration_summary  # noqa: E402
from psv_remote.vscode_client import resolve_conflict  # noqa: E402

OPTIONS = dict(ipc_path=None, database=None, app_paths=[], cwd=None, limit=16)


def test_manager_reports_its_settings(tmp_path):
    cfg = Config(state_dir=tmp_path, broker_host="10.0.0.5", broker_port=1883)
    summary = configuration_summary(cfg, "host", OPTIONS)
    assert summary["broker"] == "10.0.0.5:1883" and summary["host ID"] == "host"
    control = ManagerControl(cfg, "host", "key", managed=True, settings=summary)
    try:
        assert request(cfg, "host")["settings"] == summary
    finally:
        control.close()


def mine():
    return configuration_summary(Config(broker_host="10.0.0.5"), "host", OPTIONS)


def theirs(**changes):
    return {**configuration_summary(Config(broker_host="192.168.1.20"), "host", OPTIONS), **changes}


def test_unused_extension_manager_is_replaced():
    state = {"mode": "extension", "windows": 0, "settings": theirs()}
    assert resolve_conflict(state, mine(), replace=False) == ("replace", None)


def test_manager_in_use_by_another_window_is_left_alone_and_explained():
    state = {"mode": "extension", "windows": 1, "settings": theirs()}
    action, message = resolve_conflict(state, mine(), replace=False)
    assert action == "conflict"
    assert "Another VS Code window" in message
    assert "broker 192.168.1.20:1883" in message and "this window uses 10.0.0.5:1883" in message
    assert "Reconnect" in message


def test_reconnect_takes_over_even_when_in_use():
    state = {"mode": "extension", "windows": 2, "settings": theirs()}
    assert resolve_conflict(state, mine(), replace=True) == ("replace", None)


def test_manually_started_manager_is_never_replaced():
    state = {"mode": "persistent", "windows": 0, "settings": theirs()}
    for replace in (False, True):
        action, message = resolve_conflict(state, mine(), replace=replace)
        assert action == "conflict"
        assert "started outside VS Code" in message and "broker 192.168.1.20:1883" in message


def test_older_manager_without_settings_still_gets_a_message():
    action, message = resolve_conflict({"mode": "persistent", "windows": 0}, mine(), replace=False)
    assert action == "conflict" and "different settings" in message


# ---- an unreachable broker is reported, not hidden in per-session bridge logs ----

from psv_remote.bus import BusError  # noqa: E402
from psv_remote.codex_manager import BrokerProbe, broker_problem  # noqa: E402
from psv_remote.vscode_client import running_status  # noqa: E402


def test_unconfigured_machine_is_told_how_to_set_the_broker():
    msg = broker_problem(Config(), configured=False, error="connection refused")
    assert "127.0.0.1:1883" in msg and "no broker is set on this machine" in msg
    assert "~/.config/psv-agent-remote/config.toml" in msg and "Remote settings" in msg


def test_configured_broker_problem_names_the_address():
    msg = broker_problem(Config(broker_host="192.168.1.20"), configured=True, error="timed out")
    assert "192.168.1.20:1883" in msg and "timed out" in msg and "config.toml" not in msg


class FakeConnect:
    def __init__(self, results):
        self.results, self.calls = list(results), 0

    def __call__(self):
        self.calls += 1
        result = self.results.pop(0)
        if isinstance(result, Exception):
            raise result
        return type("Bus", (), {"close": lambda self: None})()


def test_probe_reports_changes_and_backs_off():
    t = [0.0]
    connect = FakeConnect([BusError("connect failed: refused"), BusError("refused"), "ok"])
    probe = BrokerProbe(connect, Config(), configured=False, clock=lambda: t[0])
    assert probe.poll() is True and probe.reachable is False  # changed: now known unreachable
    assert "no broker is set" in probe.message
    t[0] += 5
    assert probe.poll() is False and connect.calls == 1  # not yet: retried every 10 s while failing
    t[0] += 6
    assert probe.poll() is False and connect.calls == 2  # still failing: no change to report
    t[0] += 11
    assert probe.poll() is True and probe.reachable is True and probe.message == ""
    t[0] += 20
    assert connect.calls == 3 and probe.poll() is False and connect.calls == 3  # healthy: every 60 s


def test_manager_state_carries_the_broker_check(tmp_path):
    cfg = Config(state_dir=tmp_path)
    control = ManagerControl(cfg, "host", "key", managed=True)
    try:
        control.update(1, 1, broker={"reachable": False, "message": "Can't reach"})
        assert request(cfg, "host")["broker"] == {"reachable": False, "message": "Can't reach"}
    finally:
        control.close()


def test_status_bar_shows_an_unreachable_broker_instead_of_healthy_sessions():
    state = {"bridges": 2, "discovered": 2, "mode": "extension",
             "broker": {"reachable": False, "message": "Can't reach the MQTT broker at 127.0.0.1:1883"}}
    status = running_status(state, "/tmp/x.log")
    assert status["state"] == "broker" and status["message"].startswith("Can't reach")
    ok = running_status({**state, "broker": {"reachable": True, "message": ""}}, "/tmp/x.log")
    assert ok["state"] == "running" and ok["bridges"] == 2
    assert running_status({"bridges": 0, "discovered": 0, "mode": "extension"}, "/tmp/x.log")["state"] == "running"


# ---- the manager watches the broker for the VS Code panel ----

from psv_remote.codex_manager import BrokerWatch, removed_sessions  # noqa: E402


def status(state, project, agent=None):
    payload = {"state": state, "project": project}
    if agent:
        payload["agent"] = agent
    return json.dumps(payload).encode()


def fed(*messages):
    watch = BrokerWatch(lambda: None)
    for topic, payload in messages:
        watch.feed(topic, payload)
    return watch


def test_watch_lists_sessions_in_order_with_agents():
    watch = fed(("claude/session/a/status", status("idle", "zeta")),
                ("claude/session/b/status", status("running", "beta", "codex")),
                ("claude/session/c/status", status("waiting", "gamma", "kiro")),
                ("claude/session/d/status", status("idle", "alpha")))
    sessions = watch.snapshot({"b"})["sessions"]
    assert [(s["project"], s["state"], s["agent"]) for s in sessions] == [
        ("gamma", "waiting", "kiro"), ("beta", "running", "codex"), ("alpha", "idle", "claude"), ("zeta", "idle", "claude")]
    assert [s["local"] for s in sessions] == [False, True, False, False]


def test_watch_tracks_vita_online_and_mode():
    watch = fed()
    assert watch.snapshot(set())["vita"] == {"online": None, "mode": None}
    watch.feed("claude/psv/online", b"1")
    watch.feed("claude/psv/mode", b"desk")
    assert watch.snapshot(set())["vita"] == {"online": True, "mode": "desk"}
    watch.feed("claude/psv/online", b"0")
    assert watch.snapshot(set())["vita"]["online"] is False


def test_watch_drops_cleared_sessions_but_remembers_their_project():
    watch = fed(("claude/session/a/status", status("idle", "notes")), ("claude/session/a/status", b""))
    assert watch.snapshot(set())["sessions"] == []
    assert watch.project_of("a") == "notes"


def test_watch_ignores_junk():
    watch = fed(("claude/session/a/status", b"{"), ("claude/session/a/turn", status("idle", "x")),
                ("claude/session/b/status", b'"just a string"'))
    assert watch.snapshot(set())["sessions"] == []


def test_watch_caps_at_32():
    watch = fed(*[(f"claude/session/s{i}/status", status("idle", f"p{i:02}")) for i in range(40)])
    assert len(watch.snapshot(set())["sessions"]) == 32


def test_watch_reports_unknown_sessions_while_disconnected():
    def refuse():
        raise BusError("connect failed: refused")
    watch = BrokerWatch(refuse, clock=lambda: 0.0)
    watch.poll()
    assert watch.snapshot(set())["sessions"] is None


def test_removed_lists_only_running_bridges(tmp_path):
    (tmp_path / "codex-a.dismissed").write_text("sig")
    (tmp_path / "codex-gone.dismissed").write_text("sig")  # its bridge isn't running: can't be restored
    watch = fed(("claude/session/codex-a/status", status("idle", "notes")), ("claude/session/codex-a/status", b""))
    assert removed_sessions(tmp_path, {"codex-a", "codex-b"}, watch) == [{"sid": "codex-a", "project": "notes"}]


def test_restore_endpoint_deletes_the_dismissal(tmp_path):
    cfg = Config(state_dir=tmp_path)
    (tmp_path / "codex-a.dismissed").write_text("sig")
    restored = []

    def restore(sid):
        restored.append(sid)
        return sid == "codex-a"

    control = ManagerControl(cfg, "host", "key", managed=True, restore=restore)
    try:
        request_with(cfg, "restore", sid="codex-a")
        assert restored == ["codex-a"]
        with pytest.raises(ValueError):
            request_with(cfg, "restore", sid="codex-unknown")
    finally:
        control.close()


def request_with(cfg, action, **fields):
    descriptor = json.loads((cfg.state_dir / next(p.name for p in cfg.state_dir.glob("codex-*.watch.json"))).read_text())
    connection = http.client.HTTPConnection("127.0.0.1", descriptor["port"], timeout=2)
    try:
        connection.request("POST", "/" + action, json.dumps({"run_id": descriptor["run_id"], **fields}),
                           {"Authorization": "Bearer " + descriptor["token"], "Content-Type": "application/json"})
        response = connection.getresponse()
        response.read()
        if response.status != 200:
            raise ValueError(response.status)
    finally:
        connection.close()


# ---- the VS Code client forwards panel data and takes commands on stdin ----

from psv_remote import kiro_registry  # noqa: E402
from psv_remote.vscode_client import handle_command, kiro_status  # noqa: E402

PANEL_STATE = {"bridges": 1, "discovered": 1, "mode": "extension",
               "vita": {"online": True, "mode": "sofa"},
               "sessions": [{"sid": "a", "agent": "codex", "project": "notes", "state": "idle", "local": True}],
               "removed": [{"sid": "codex-b", "project": "api"}]}


def test_status_carries_panel_fields():
    kiro = {"bridges": [], "errors": {}}
    status = running_status(PANEL_STATE, "/tmp/x.log", kiro)
    assert status["vita"] == PANEL_STATE["vita"] and status["sessions"] == PANEL_STATE["sessions"]
    assert status["removed"] == PANEL_STATE["removed"] and status["kiro"] == kiro


def test_old_manager_status_has_no_panel_fields():
    status = running_status({"bridges": 0, "discovered": 0, "mode": "extension"}, "/tmp/x.log", None)
    assert not {"vita", "sessions", "removed", "kiro"} & set(status)


def test_restore_command_calls_the_manager(tmp_path):
    cfg = Config(state_dir=tmp_path)
    restored = []
    control = ManagerControl(cfg, "host", "key", managed=True, restore=lambda sid: restored.append(sid) or True)
    try:
        handle_command(cfg, "host", '{"cmd": "restore", "sid": "codex-b"}', [])
        assert restored == ["codex-b"]
    finally:
        control.close()


def test_folders_command_reports_failed_kiro_starts(tmp_path):
    cfg = Config(state_dir=tmp_path)
    folders = []
    handle_command(cfg, "host", json.dumps({"cmd": "folders", "paths": ["/p", "/q"]}), folders)
    assert folders == ["/p", "/q"]
    key = kiro_registry.lock_key("host", "/p")
    (tmp_path / f"kiro-{key}.log").write_text("Kiro CLI is not logged in; run `kiro-cli login`\n")
    assert kiro_status(cfg, "host", folders) == {"bridges": [], "errors": {"/p": "Kiro CLI is not logged in; run `kiro-cli login`"}}


def test_bad_command_is_ignored(tmp_path):
    cfg = Config(state_dir=tmp_path)
    folders = ["/keep"]
    for line in ("not json", "[]", '{"cmd": "dance"}', '{"cmd": "folders", "paths": "nope"}',
                 '{"cmd": "restore"}'):
        handle_command(cfg, "host", line, folders)  # no manager running, bad input: nothing raises
    assert folders == ["/keep"]


def test_kiro_start_command_spawns_a_detached_bridge(tmp_path, monkeypatch):
    import psv_remote.vscode_client as vc
    spawned = []

    class Proc:
        pass

    monkeypatch.setattr(vc.subprocess, "Popen", lambda args, **kw: spawned.append((args, kw)) or Proc())
    cfg = Config(state_dir=tmp_path)
    handle_command(cfg, "host", json.dumps({"cmd": "kiro_start", "cwd": "/p"}), [])
    (args, kw), = spawned
    assert args[1].endswith("psv_kiro.py") and args[2:] == ["--cwd", "/p", "--host-id", "host"]
    assert kw["start_new_session"] is True and kw["stdin"] is vc.subprocess.DEVNULL  # outlives VS Code
    key = kiro_registry.lock_key("host", "/p")
    assert kw["stdout"] is kw["stderr"] and (tmp_path / f"kiro-{key}.log").exists()


def test_kiro_start_skipped_when_already_running(tmp_path, monkeypatch):
    import fcntl
    import psv_remote.vscode_client as vc
    spawned = []
    monkeypatch.setattr(vc.subprocess, "Popen", lambda args, **kw: spawned.append(args))
    cfg = Config(state_dir=tmp_path)
    key = kiro_registry.lock_key("host", "/p")
    lock = (tmp_path / f"kiro-{key}.bridge.lock").open("a")
    fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    kiro_registry.write_entry(tmp_path, key, 4242, "/p", "kiro-x")
    handle_command(cfg, "host", json.dumps({"cmd": "kiro_start", "cwd": "/p"}), [])
    assert spawned == []
    lock.close()


def test_kiro_stop_command_terminates_the_registered_bridge(tmp_path, monkeypatch):
    import fcntl
    import signal
    import psv_remote.vscode_client as vc
    killed = []
    monkeypatch.setattr(vc.os, "kill", lambda pid, sig: killed.append((pid, sig)))
    cfg = Config(state_dir=tmp_path)
    key = kiro_registry.lock_key("host", "/p")
    lock = (tmp_path / f"kiro-{key}.bridge.lock").open("a")
    fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    kiro_registry.write_entry(tmp_path, key, 4242, "/p", "kiro-x")
    handle_command(cfg, "host", json.dumps({"cmd": "kiro_stop", "cwd": "/p"}), [])
    handle_command(cfg, "host", json.dumps({"cmd": "kiro_stop", "cwd": "/not-running"}), [])
    assert killed == [(4242, signal.SIGTERM)]
    lock.close()


# ---- review fix: the manager's workers are keyed by thread id, removals by session id ----

from psv_remote.codex_manager import bridged_sessions, make_restore  # noqa: E402
from psv_remote.remote import scoped_session_id  # noqa: E402


def test_removed_and_restore_use_the_bridges_session_ids(tmp_path):
    workers = {"thread-1": object(), "thread-2": object()}  # Supervisor.workers: thread id -> worker
    sid = scoped_session_id("codex", "host", "thread-1")
    (tmp_path / f"{sid}.dismissed").write_text("sig")
    running = bridged_sessions("host", workers)
    assert running == {sid, scoped_session_id("codex", "host", "thread-2")}
    watch = fed((f"claude/session/{sid}/status", status("idle", "notes")), (f"claude/session/{sid}/status", b""))
    assert removed_sessions(tmp_path, running, watch) == [{"sid": sid, "project": "notes"}]
    assert watch.snapshot(running)["sessions"] == []
    restore = make_restore(Config(state_dir=tmp_path), "host", workers)
    assert restore("thread-1") is False  # a raw thread id is not a session
    assert restore(sid) is True and not (tmp_path / f"{sid}.dismissed").exists()


# ---- review fix: a workspace opened through a symlink is the same Kiro folder ----

def linked_folder(tmp_path):
    real = tmp_path / "real-project"
    real.mkdir()
    link = tmp_path / "link-project"
    link.symlink_to(real)
    return str(link), os.path.realpath(real)


def test_kiro_status_matches_folders_through_symlinks(tmp_path):
    import fcntl
    state = tmp_path / "state"
    state.mkdir(mode=0o700)
    cfg = Config(state_dir=state)
    link, real = linked_folder(tmp_path)
    key = kiro_registry.lock_key("host", real)
    lock = (state / f"kiro-{key}.bridge.lock").open("a")
    fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    kiro_registry.write_entry(state, key, 4242, real, "kiro-x")  # psv_kiro registers the resolved path
    bridges = kiro_status(cfg, "host", [link])["bridges"]
    assert [b["cwd"] for b in bridges] == [link]  # reported as the window knows the folder
    lock.close()
    (state / f"kiro-{key}.log").write_text("Kiro CLI is not logged in; run `kiro-cli login`\n")
    assert kiro_status(cfg, "host", [link])["errors"] == {link: "Kiro CLI is not logged in; run `kiro-cli login`"}


def test_kiro_start_and_stop_resolve_the_folder(tmp_path, monkeypatch):
    import fcntl
    import psv_remote.vscode_client as vc
    state = tmp_path / "state"
    state.mkdir(mode=0o700)
    cfg = Config(state_dir=state)
    link, real = linked_folder(tmp_path)
    spawned, killed = [], []
    monkeypatch.setattr(vc.subprocess, "Popen", lambda args, **kw: spawned.append(args))
    monkeypatch.setattr(vc.os, "kill", lambda pid, sig: killed.append(pid))
    handle_command(cfg, "host", json.dumps({"cmd": "kiro_start", "cwd": link}), [])
    assert spawned[0][2:4] == ["--cwd", real]
    key = kiro_registry.lock_key("host", real)
    lock = (state / f"kiro-{key}.bridge.lock").open("a")
    fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    kiro_registry.write_entry(state, key, 4242, real, "kiro-x")
    handle_command(cfg, "host", json.dumps({"cmd": "kiro_start", "cwd": link}), [])
    assert len(spawned) == 1  # already running under its resolved path
    handle_command(cfg, "host", json.dumps({"cmd": "kiro_stop", "cwd": link}), [])
    assert killed == [4242]
    lock.close()


def test_a_failing_command_does_not_stop_the_command_channel():
    import io
    import threading
    from psv_remote.vscode_client import read_commands
    handled = []

    def handle(line):
        handled.append(line.strip())
        if line.startswith("boom"):
            raise OSError("state folder not writable")

    stopped = threading.Event()
    read_commands(io.BytesIO(b"boom\nnext\n"), handle, stopped)
    assert handled == ["boom", "next"] and stopped.is_set()  # EOF still means "this window left"


# ---- review fix: Kiro actions never trust a state folder other users can change ----

def test_kiro_actions_refuse_a_shared_state_folder(tmp_path, monkeypatch):
    import fcntl
    import psv_remote.vscode_client as vc
    shared = tmp_path / "shared"
    shared.mkdir()
    shared.chmod(0o777)  # what another user could have created first in /tmp
    cfg = Config(state_dir=shared)
    spawned, killed = [], []
    monkeypatch.setattr(vc.subprocess, "Popen", lambda args, **kw: spawned.append(args))
    monkeypatch.setattr(vc.os, "kill", lambda pid, sig: killed.append(pid))
    key = kiro_registry.lock_key("host", "/p")
    lock = (shared / f"kiro-{key}.bridge.lock").open("a")
    fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    kiro_registry.write_entry(shared, key, 4242, "/p", "kiro-x")  # a planted entry naming any pid
    handle_command(cfg, "host", json.dumps({"cmd": "kiro_stop", "cwd": "/p"}), [])
    handle_command(cfg, "host", json.dumps({"cmd": "kiro_start", "cwd": "/q"}), [])
    assert killed == [] and spawned == []
    status = kiro_status(cfg, "host", ["/p"])
    assert status["bridges"] == [] and "other users" in status["errors"]["/p"]
    lock.close()


def test_kiro_log_is_not_followed_through_a_symlink(tmp_path, monkeypatch):
    import psv_remote.vscode_client as vc
    state = tmp_path / "state"
    state.mkdir(mode=0o700)
    victim = tmp_path / "victim.txt"
    victim.write_text("precious")
    key = kiro_registry.lock_key("host", "/p")
    (state / f"kiro-{key}.log").symlink_to(victim)
    spawned = []
    monkeypatch.setattr(vc.subprocess, "Popen", lambda args, **kw: spawned.append(args))
    handle_command(Config(state_dir=state), "host", json.dumps({"cmd": "kiro_start", "cwd": "/p"}), [])
    assert victim.read_text() == "precious" and spawned == []
