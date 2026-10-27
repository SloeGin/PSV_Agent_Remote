from psv_remote import topics
from psv_remote.config import load_config


def test_defaults(tmp_path):
    c = load_config(env={}, path=tmp_path / "missing.toml")
    assert c.broker_host == "127.0.0.1" and c.broker_port == 1883
    assert c.wait_timeout_s == 120.0 and c.enabled is True


def test_toml_then_env_override(tmp_path):
    p = tmp_path / "c.toml"
    p.write_text('broker_host = "a"\nwait_timeout_s = 30\n')
    c = load_config(env={"PSV_REMOTE_HOST": "b", "PSV_REMOTE_ENABLED": "0"}, path=p)
    assert c.broker_host == "b" and c.wait_timeout_s == 30 and c.enabled is False


def test_topics():
    assert topics.request("s1", "r1") == "claude/session/s1/request/r1"
    assert topics.response("s1", "r1") == "claude/session/s1/response/r1"
    assert topics.status("s1") == "claude/session/s1/status"


def test_falls_back_to_pre_rename_config(tmp_path):
    old = tmp_path / "old.toml"
    old.write_text('broker_host = "127.0.0.1"\n')
    c = load_config(env={}, path=tmp_path / "missing.toml", legacy_path=old)
    assert c.broker_host == "127.0.0.1"


def test_new_config_wins_over_old(tmp_path):
    new, old = tmp_path / "new.toml", tmp_path / "old.toml"
    new.write_text('broker_host = "10.0.0.1"\n')
    old.write_text('broker_host = "127.0.0.1"\n')
    assert load_config(env={}, path=new, legacy_path=old).broker_host == "10.0.0.1"


def test_new_default_paths():
    from psv_remote import config
    assert config.DEFAULT_PATH.parent.name == "psv-agent-remote"
    assert config.LEGACY_PATH.parent.name == "psv-claude-remote"
    assert config.Config().state_dir.name == "psv-agent-remote"
