from psv_remote.render import render_request


def test_bash():
    t, b = render_request("Bash", {"command": "npm i\nnpm t", "description": "install"})
    assert t == "npm i" and b == "npm i\nnpm t\n\n# install"


def test_edit():
    t, b = render_request("Edit", {"file_path": "/a.py", "old_string": "x", "new_string": "y"})
    assert t == "/a.py" and b == "--- old\nx\n+++ new\ny"


def test_write():
    t, b = render_request("Write", {"file_path": "/a.py", "content": "hi"})
    assert t == "/a.py" and b == "hi"


def test_unknown_tool_json():
    t, b = render_request("mcp__x__y", {"k": "中"})
    assert t == "mcp__x__y" and '"k": "中"' in b


def test_truncate_cjk_no_broken_utf8():
    s = "中" * 30000  # 90000 字节
    _, b = render_request("Write", {"file_path": "/f", "content": s})
    assert b.startswith("【内容已截断，共 90000 字节】\n")
    assert len(b.split("\n", 1)[1].encode()) <= 65536
    b.encode("utf-8")


def test_title_capped():
    t, _ = render_request("Bash", {"command": "x" * 500})
    assert len(t) <= 80
