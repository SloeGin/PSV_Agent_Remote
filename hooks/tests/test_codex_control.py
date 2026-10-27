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
