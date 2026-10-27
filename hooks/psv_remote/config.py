"""Configuration: defaults < ~/.config/psv-agent-remote/config.toml < environment.

Before the rename to PSV Agent Remote the file lived in ~/.config/psv-claude-remote/;
it is still read when the new one does not exist.
"""

import dataclasses
import os
import tomllib
from collections.abc import Mapping
from dataclasses import dataclass
from pathlib import Path

DEFAULT_PATH = Path.home() / ".config" / "psv-agent-remote" / "config.toml"
LEGACY_PATH = Path.home() / ".config" / "psv-claude-remote" / "config.toml"

_ENV = {
    "PSV_REMOTE_HOST": "broker_host",
    "PSV_REMOTE_PORT": "broker_port",
    "PSV_REMOTE_TIMEOUT": "wait_timeout_s",
    "PSV_REMOTE_STATE_DIR": "state_dir",
    "PSV_REMOTE_ENABLED": "enabled",
}


@dataclass(frozen=True)
class Config:
    broker_host: str = "127.0.0.1"
    broker_port: int = 1883
    wait_timeout_s: float = 120.0
    connect_timeout_s: float = 2.0
    retained_wait_s: float = 0.5
    state_dir: Path = Path("/tmp/psv-agent-remote")
    enabled: bool = True


def _coerce(field: str, value):
    kind = Config.__dataclass_fields__[field].type
    if kind is bool:
        if isinstance(value, str):
            return value.strip().lower() not in ("0", "false", "no", "off", "")
        return bool(value)
    if kind is Path:
        return Path(value)
    return kind(value)


def load_config(env: Mapping[str, str] = os.environ, path: Path | None = None,
                legacy_path: Path | None = None) -> Config:
    if path is None:
        path, legacy_path = DEFAULT_PATH, LEGACY_PATH
    if legacy_path is not None and not path.exists() and legacy_path.exists():
        path = legacy_path
    values: dict = {}
    try:
        with open(path, "rb") as f:
            data = tomllib.load(f)
        for key, value in data.items():
            if key in Config.__dataclass_fields__:
                values[key] = _coerce(key, value)
    except FileNotFoundError:
        pass
    for var, field in _ENV.items():
        if var in env:
            values[field] = _coerce(field, env[var])
    return dataclasses.replace(Config(), **values)
