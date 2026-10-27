#include <stdlib.h>

#include "model.h"
#include "protocol.h"
#include "sessionlist.h"
#include "test.h"

static void status(Model *m, const char *sid, const char *json) {
    StatusMsg msg;
    CHECK(protocol_parse_status(json, strlen(json), &msg));
    model_on_status(m, sid, &msg, 0);
}

static void t_legacy_and_codex_capabilities(void) {
    Model m;
    model_init(&m, MODE_SOFA);
    status(&m, "claude", "{\"state\":\"idle\"}");
    CHECK(m.s[0].agent == AGENT_CLAUDE && model_reply_idle(&m.s[0]));
    for (int i = 0; i < CAP_COUNT; i++) CHECK(session_capability(&m.s[0], i) == CAP_SUPPORTED);
    status(&m, "codex", "{\"state\":\"running\",\"agent\":\"codex\","
           "\"capabilities\":{\"approval\":true,\"questions\":null,\"reply\":false,\"stop\":true}}");
    Session *s = &m.s[1];
    CHECK(s->agent == AGENT_CODEX && model_reply_working(s));
    CHECK(session_capability(s, CAP_APPROVAL) == CAP_SUPPORTED);
    CHECK(session_capability(s, CAP_QUESTIONS) == CAP_UNKNOWN);
    CHECK(session_capability(s, CAP_REPLY) == CAP_UNSUPPORTED);
    ReplyLayout layout = reply_layout(&s->reply, false, true);
    CHECK(layout.n_rows == 1 && layout.rows[0] == RR_MATTA);
    status(&m, "codex", "{\"state\":\"idle\",\"agent\":\"codex\"}");
    CHECK(!model_reply_idle(s) && session_capability(s, CAP_STOP) == CAP_UNKNOWN);
    status(&m, "future", "{\"state\":\"idle\",\"agent\":\"future-agent\"}");
    CHECK(m.s[2].agent == AGENT_UNKNOWN && !model_reply_idle(&m.s[2]));
    model_free(&m);
}

static void t_reply_without_stop_capability(void) {
    Model m;
    model_init(&m, MODE_SOFA);
    status(&m, "codex", "{\"state\":\"running\",\"agent\":\"codex\","
           "\"capabilities\":{\"reply\":true,\"stop\":false}}");
    ReplyLayout l = reply_layout(&m.s[0].reply, false, true);
    CHECK(l.n_rows == 1 && l.rows[0] == RR_TYPE);
    m.s[0].reply.editing_sent = true;
    status(&m, "codex", "{\"state\":\"idle\",\"agent\":\"codex\"}");
    ReplyAct a = reply_ime_done(&m.s[0].reply, "keep my words", true, "late", 100);
    CHECK(a.kind == RA_NONE);
    CHECK_STR(m.s[0].reply.draft, "keep my words");
    model_free(&m);
}

static void t_assistant_and_legacy_turns(void) {
    const char *json = "{\"items\":[{\"k\":\"claude\",\"t\":\"old\"},"
                       "{\"k\":\"assistant\",\"t\":\"new\"}]}";
    TurnItem *items;
    int n;
    CHECK(protocol_parse_turn(json, strlen(json), &items, &n));
    CHECK(n == 2 && items[0].kind == TURN_ASSISTANT && items[1].kind == TURN_ASSISTANT);
    CHECK_STR(items[0].text, "old");
    CHECK_STR(items[1].text, "new");
    turn_free(items, n);
}

static void t_question_without_other(void) {
    const char *json = "{\"rid\":\"q\",\"title\":\"Choose\",\"kind\":\"question\",\"questions\":["
        "{\"question\":\"Pick\",\"allow_other\":false,\"options\":[{\"label\":\"A\"}]},"
        "{\"question\":\"Pick again\",\"multi\":true,\"allow_other\":false,\"options\":[{\"label\":\"B\"}]}]}";
    Request request = {0};
    CHECK(protocol_parse_request(json, strlen(json), &request));
    Model m;
    model_init(&m, MODE_SOFA);
    model_on_request(&m, "codex", "q", &request, 0);
    Request *r = &m.s[0].reqs[0];
    int option;
    CHECK(model_row_count(r) == 2 && model_row_kind(r, 1, &option) == ROW_MAC);
    model_submit_other(&m, "codex", "q", "not offered", 600);
    CHECK(r->step == 0 && !r->other[0][0]);
    m.sel = 0;
    Outcome out;
    model_activate(&m, 600, &out);
    CHECK(r->step == 1 && model_row_count(r) == 3);
    CHECK(model_row_kind(r, 1, &option) == ROW_NEXT);
    CHECK(model_row_kind(r, 2, &option) == ROW_MAC);
    model_free(&m);
}

static void t_failed_reply_preserves_draft(void) {
    Reply r;
    reply_init(&r);
    snprintf(r.id, sizeof r.id, "mine");
    snprintf(r.sent, sizeof r.sent, "my text");
    r.sent_typed = true;
    r.status = RS_QUEUED;
    reply_on_state(&r, "other", "failed", 0);
    CHECK(r.status == RS_QUEUED);
    reply_on_state(&r, "mine", "failed", 0);
    CHECK(r.status == RS_FAILED && !reply_in_flight(&r));
    CHECK_STR(r.draft, "my text");
    CHECK(reply_has_peek(&r, 20000));
    r.status = RS_SENDING;
    r.acked = false;
    reply_on_state(&r, "mine", "unknown", 0);
    CHECK(reply_tick(&r, REPLY_ACK_MS + 1).kind == RA_WITHDRAW);
}

static void t_capability_tooltip_consumes_press_and_release(void) {
    Model m;
    model_init(&m, MODE_SOFA);
    status(&m, "a", "{\"state\":\"idle\"}");
    status(&m, "b", "{\"state\":\"idle\",\"agent\":\"codex\"}");
    SessionList l;
    list_open(&l, &m);
    l.cursor = 1; /* highlighted, not currently displayed */
    InputState input;
    input_init(&input);
    InputFrame f = {.lx=128, .ly=128, .rx=128, .ry=128, .buttons=BTN_TRIANGLE};
    Actions a = input_map(&input, &f);
    list_handle(&l, &m, &a, 0, NULL);
    CHECK(l.tooltip && m.mode == MODE_SOFA);
    CHECK_STR(l.tooltip_sid, "b");
    a = input_map(&input, &f);
    list_handle(&l, &m, &a, 1, NULL);
    CHECK(l.tooltip); /* holding the opening button does not dismiss */
    f.buttons = 0;
    a = input_map(&input, &f);
    list_handle(&l, &m, &a, 2, NULL);
    f.buttons = BTN_CROSS | BTN_SELECT | BTN_SQUARE | BTN_TRIANGLE;
    a = input_map(&input, &f);
    list_handle(&l, &m, &a, 3, NULL);
    CHECK(!l.tooltip && l.open && !l.confirming && m.cur == 0 && m.mode == MODE_SOFA);
    a.confirm = true; /* even a repeated action while held must be swallowed */
    list_handle(&l, &m, &a, 4, NULL);
    CHECK(l.open && m.cur == 0);
    f.buttons = 0;
    a = input_map(&input, &f);
    list_handle(&l, &m, &a, 5, NULL);
    f.buttons = BTN_CROSS;
    a = input_map(&input, &f);
    CHECK(list_handle(&l, &m, &a, 6, NULL) == LIST_SWITCHED && m.cur == 1);
    model_free(&m);
}

int main(void) {
    RUN(t_legacy_and_codex_capabilities);
    RUN(t_reply_without_stop_capability);
    RUN(t_assistant_and_legacy_turns);
    RUN(t_question_without_other);
    RUN(t_failed_reply_preserves_draft);
    RUN(t_capability_tooltip_consumes_press_and_release);
    return TEST_RESULT();
}
