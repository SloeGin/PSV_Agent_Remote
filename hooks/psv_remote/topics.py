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


# Voice replies. Audio stays outside claude/session/#, which the Vita subscribes
# to whole; only the (small) result goes back under the session.
VOICE_ALL = "claude/voice/#"
TRANSCRIBERS = "claude/transcriber/+"


def transcriber(host_id: str) -> str:
    return f"claude/transcriber/{host_id}"


def voice_chunk(vid: str, seq: int) -> str:
    return f"claude/voice/{vid}/{seq}"


def voice_end(vid: str) -> str:
    return f"claude/voice/{vid}/end"


def voice_result(sid: str) -> str:
    return f"claude/session/{sid}/voice"
