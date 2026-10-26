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
