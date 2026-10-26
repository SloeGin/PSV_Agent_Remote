"""Merge the PSV Agent Remote hooks into a Claude Code settings.json (or remove them).

Run with the venv's python so the hooks use it:
    hooks/.venv/bin/python hooks/install.py [--settings PATH] [--uninstall]
"""

import argparse
import json
import shlex
import shutil
import sys
import time
from pathlib import Path

MARKER = "psv_hook.py"
EVENTS = ("SessionStart", "UserPromptSubmit", "PermissionRequest", "PreToolUse", "PostToolUse",
          "Stop", "Notification", "SessionEnd")
MATCHERS = {"Notification": "idle_prompt", "PreToolUse": "AskUserQuestion"}
WAITS_FOR_PSV = ("PermissionRequest", "PreToolUse")
DEFAULT_SETTINGS = Path.home() / ".claude" / "settings.json"


def _load(path: Path) -> dict:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError:
        return {}


def _save(path: Path, settings: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists():
        shutil.copy2(path, path.with_name(f"{path.name}.bak-{time.strftime('%Y%m%d%H%M%S')}"))
    path.write_text(json.dumps(settings, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def _strip(settings: dict) -> None:
    hooks = settings.get("hooks", {})
    for event in list(hooks):
        kept = []
        for group in hooks[event]:
            inner = [h for h in group.get("hooks", []) if MARKER not in h.get("command", "")]
            if inner:
                kept.append({**group, "hooks": inner})
        if kept:
            hooks[event] = kept
        else:
            del hooks[event]
    if "hooks" in settings and not hooks:
        del settings["hooks"]


def _timeout(event: str, wait_timeout_s: int) -> int:
    if event in WAITS_FOR_PSV:
        return wait_timeout_s + 10
    if event == "SessionEnd":
        return 3
    return 5


def install(settings_path: Path, python: str, hook: str, wait_timeout_s: int = 120) -> None:
    settings = _load(settings_path)
    _strip(settings)
    hooks = settings.setdefault("hooks", {})
    command = f"{shlex.quote(python)} {shlex.quote(hook)}"
    for event in EVENTS:
        hooks.setdefault(event, []).append({
            "matcher": MATCHERS.get(event, ""),
            "hooks": [{"type": "command", "command": command, "timeout": _timeout(event, wait_timeout_s)}],
        })
    _save(settings_path, settings)


def uninstall(settings_path: Path, hook: str) -> None:
    settings = _load(settings_path)
    _strip(settings)
    _save(settings_path, settings)


def main() -> None:
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from psv_remote.config import load_config

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--settings", type=Path, default=DEFAULT_SETTINGS)
    parser.add_argument("--uninstall", action="store_true")
    args = parser.parse_args()
    hook = str(Path(__file__).resolve().parent / MARKER)
    if args.uninstall:
        uninstall(args.settings, hook)
        print(f"removed PSV hooks from {args.settings}")
    else:
        install(args.settings, sys.executable, hook, int(load_config().wait_timeout_s))
        print(f"installed PSV hooks into {args.settings}")


if __name__ == "__main__":
    main()
