import json

from install import install, uninstall

OTHER = {"Stop": [{"matcher": "", "hooks": [{"type": "command", "command": "say hi"}]}]}


def test_merges_and_preserves(tmp_path):
    p = tmp_path / "settings.json"
    p.write_text(json.dumps({"model": "x", "hooks": OTHER}))
    install(p, "/py", "/h/psv_hook.py")
    s = json.loads(p.read_text())
    assert s["model"] == "x"
    assert len(s["hooks"]["Stop"]) == 2
    pr = s["hooks"]["PermissionRequest"][0]["hooks"][0]
    assert pr["timeout"] == 130 and pr["command"] == "/py /h/psv_hook.py"
    assert s["hooks"]["Notification"][0]["matcher"] == "idle_prompt"
    assert s["hooks"]["SessionEnd"][0]["hooks"][0]["timeout"] == 3
    assert set(s["hooks"]) == {"SessionStart", "UserPromptSubmit", "PermissionRequest", "PreToolUse",
                               "PostToolUse", "Stop", "Notification", "SessionEnd"}
    pre = s["hooks"]["PreToolUse"][0]
    assert pre["matcher"] == "AskUserQuestion" and pre["hooks"][0]["timeout"] == 130
    assert list(tmp_path.glob("settings.json.bak-*"))


def test_missing_file(tmp_path):
    p = tmp_path / "settings.json"
    install(p, "/py", "/h/psv_hook.py")
    assert "PermissionRequest" in json.loads(p.read_text())["hooks"]


def test_idempotent(tmp_path):
    p = tmp_path / "settings.json"
    p.write_text("{}")
    install(p, "/py", "/h/psv_hook.py")
    install(p, "/py", "/h/psv_hook.py")
    assert len(json.loads(p.read_text())["hooks"]["Stop"]) == 1


def test_uninstall_keeps_others(tmp_path):
    p = tmp_path / "settings.json"
    p.write_text(json.dumps({"hooks": OTHER}))
    install(p, "/py", "/h/psv_hook.py")
    uninstall(p, "/h/psv_hook.py")
    assert json.loads(p.read_text())["hooks"] == OTHER


def _listeners(s, event):
    return [h for g in s["hooks"].get(event, []) for h in g["hooks"] if "psv_listen.py" in h["command"]]


def test_installs_listener(tmp_path):
    p = tmp_path / "settings.json"
    install(p, "/py", "/h/psv_hook.py")
    s = json.loads(p.read_text())
    for event in ("SessionStart", "UserPromptSubmit"):
        (h,) = _listeners(s, event)
        assert h == {"type": "command", "command": "/py /h/psv_listen.py", "asyncRewake": True, "timeout": 43200}
        assert any("psv_hook.py" in x["command"] for g in s["hooks"][event] for x in g["hooks"])
    for event in set(s["hooks"]) - {"SessionStart", "UserPromptSubmit"}:
        assert _listeners(s, event) == []


def test_uninstall_removes_listener(tmp_path):
    p = tmp_path / "settings.json"
    p.write_text(json.dumps({"hooks": OTHER}))
    install(p, "/py", "/h/psv_hook.py")
    uninstall(p, "/h/psv_hook.py")
    s = json.loads(p.read_text())
    assert "psv_listen.py" not in p.read_text()
    assert s["hooks"] == OTHER


def test_reinstall_is_idempotent(tmp_path):
    p = tmp_path / "settings.json"
    install(p, "/py", "/h/psv_hook.py")
    install(p, "/py", "/h/psv_hook.py")
    s = json.loads(p.read_text())
    assert len(_listeners(s, "SessionStart")) == 1 and len(_listeners(s, "UserPromptSubmit")) == 1
