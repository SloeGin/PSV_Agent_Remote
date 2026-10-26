"""Turn a tool call into the (title, body) text shown on the PSV."""

import json

MAX_BODY_BYTES = 65536
MAX_TITLE_CHARS = 80


def _truncate(body: str) -> str:
    raw = body.encode("utf-8")
    if len(raw) <= MAX_BODY_BYTES:
        return body
    kept = raw[:MAX_BODY_BYTES].decode("utf-8", errors="ignore")
    return f"【内容已截断，共 {len(raw)} 字节】\n{kept}"


def render_request(tool_name: str, tool_input: dict) -> tuple[str, str]:
    if tool_name == "Bash":
        command = str(tool_input.get("command", ""))
        title = command.split("\n", 1)[0]
        body = command
        if tool_input.get("description"):
            body += f"\n\n# {tool_input['description']}"
    elif tool_name == "Edit":
        title = str(tool_input.get("file_path", ""))
        body = f"--- old\n{tool_input.get('old_string', '')}\n+++ new\n{tool_input.get('new_string', '')}"
    elif tool_name == "Write":
        title = str(tool_input.get("file_path", ""))
        body = str(tool_input.get("content", ""))
    else:
        title = tool_name
        body = json.dumps(tool_input, ensure_ascii=False, indent=2)
    return title[:MAX_TITLE_CHARS], _truncate(body)
