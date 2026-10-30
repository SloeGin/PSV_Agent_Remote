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
