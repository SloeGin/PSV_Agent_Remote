"""MQTT topic names shared with the PSV app."""

ONLINE = "claude/psv/online"
MODE = "claude/psv/mode"


def status(sid: str) -> str:
    return f"claude/session/{sid}/status"


def request(sid: str, rid: str) -> str:
    return f"claude/session/{sid}/request/{rid}"


def response(sid: str, rid: str) -> str:
    return f"claude/session/{sid}/response/{rid}"


def turn(sid: str) -> str:
    return f"claude/session/{sid}/turn"


def reply(sid: str) -> str:
    return f"claude/session/{sid}/reply"


def reply_state(sid: str) -> str:
    return f"claude/session/{sid}/reply_state"
