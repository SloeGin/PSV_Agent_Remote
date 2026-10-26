import json

from psv_remote.transcript import MAX_TEXT_CHARS, current_turn


def write(path, entries):
    path.write_text("\n".join(json.dumps(e, ensure_ascii=False) for e in entries) + "\n", encoding="utf-8")


def user(text, kind="human", **kw):
    return {"type": "user", "message": {"role": "user", "content": text}, "origin": {"kind": kind}, **kw}


def assistant(*blocks, **kw):
    return {"type": "assistant", "message": {"role": "assistant", "content": list(blocks)}, **kw}


def text(t):
    return {"type": "text", "text": t}


def tool(name, **inp):
    return {"type": "tool_use", "name": name, "input": inp}


def result():
    return {"type": "user", "message": {"role": "user", "content": [{"type": "tool_result", "content": "ok"}]}}


def test_turn_starts_at_last_human_prompt(tmp_path):
    p = tmp_path / "t.jsonl"
    write(p, [
        user("old prompt"), assistant(text("old answer")),
        user("把提问页做出来"),
        assistant({"type": "thinking", "thinking": "hmm"}),
        assistant(text("好的，我先读一下 ui.c。")),
        assistant(tool("Read", file_path="/x/vita/src/ui.c")), result(),
        user("[Subagent hand-back] report", kind="peer"),
        assistant(tool("Bash", command="npm ci\nnpm test", description="install")), result(),
        assistant(text("Sidechain chatter"), isSidechain=True),
        assistant(text("测试通过了。")),
    ])
    # only the prompt and Claude's latest message: no narration in between, no tool calls
    assert current_turn(p) == [
        {"k": "you", "t": "把提问页做出来"},
        {"k": "claude", "t": "测试通过了。"},
    ]


def test_turn_before_any_reply(tmp_path):
    p = tmp_path / "t.jsonl"
    write(p, [user("go"), assistant(tool("Bash", command="ls"))])
    assert current_turn(p) == [{"k": "you", "t": "go"}]


def test_missing_or_garbage_file(tmp_path):
    assert current_turn(tmp_path / "nope.jsonl") == []
    p = tmp_path / "g.jsonl"
    p.write_text("not json\n{\"type\": \"assistant\"}\n", encoding="utf-8")
    assert current_turn(p) == []


def test_prompt_without_origin_falls_back(tmp_path):
    p = tmp_path / "t.jsonl"
    write(p, [{"type": "user", "message": {"content": "hello"}}, assistant(text("hi"))])
    assert current_turn(p)[0] == {"k": "you", "t": "hello"}


def test_long_reply_clipped(tmp_path):
    p = tmp_path / "t.jsonl"
    write(p, [user("prompt"), assistant(text("x" * (MAX_TEXT_CHARS + 500)))])
    turn = current_turn(p)
    assert len(turn[1]["t"]) == MAX_TEXT_CHARS + 1


def test_prompt_with_attachment_starts_the_turn(tmp_path):
    p = tmp_path / "t.jsonl"
    image_prompt = {"type": "user", "origin": {"kind": "human"}, "message": {"role": "user", "content": [
        {"type": "text", "text": "看看这张截图"}, {"type": "image", "source": {"type": "base64", "data": "xx"}}]}}
    write(p, [user("old prompt"), assistant(text("old answer")), image_prompt, assistant(text("看到了"))])
    assert current_turn(p) == [{"k": "you", "t": "看看这张截图 [image]"}, {"k": "claude", "t": "看到了"}]
