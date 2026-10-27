#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "protocol.h"
#include "test.h"

static void t_topics(void) {
    char s[64], r[24];
    CHECK(protocol_parse_topic("claude/session/abc/status", s, r) == TOPIC_STATUS);
    CHECK_STR(s, "abc");
    CHECK(protocol_parse_topic("claude/session/abc/request/r1", s, r) == TOPIC_REQUEST);
    CHECK_STR(s, "abc");
    CHECK_STR(r, "r1");
    CHECK(protocol_parse_topic("claude/session/abc/response/r1", s, r) == TOPIC_OTHER);
    CHECK(protocol_parse_topic("claude/session//status", s, r) == TOPIC_OTHER);
    CHECK(protocol_parse_topic("claude/session/abc/request/", s, r) == TOPIC_OTHER);
    CHECK(protocol_parse_topic("claude/session/abc/request/r1/x", s, r) == TOPIC_OTHER);
    CHECK(protocol_parse_topic("claude/psv/online", s, r) == TOPIC_OTHER);
    char long_sid[200] = "claude/session/";
    memset(long_sid + 15, 'a', 100);
    strcpy(long_sid + 115, "/status");
    CHECK(protocol_parse_topic(long_sid, s, r) == TOPIC_OTHER);
}

static void t_status(void) {
    StatusMsg m;
    const char *j = "{\"state\":\"idle\",\"project\":\"proj\",\"summary\":\"完成\",\"ts\":1}";
    CHECK(protocol_parse_status(j, strlen(j), &m));
    CHECK(m.state == ST_IDLE);
    CHECK_STR(m.project, "proj");
    CHECK_STR(m.summary, "完成");
    const char *w = "{\"state\":\"waiting\"}";
    CHECK(protocol_parse_status(w, strlen(w), &m) && m.state == ST_WAITING);
    CHECK_STR(m.project, "");
    const char *c = "{\"state\":\"idle\",\"cwd\":\"/Users/me/proj\"}";
    CHECK(protocol_parse_status(c, strlen(c), &m));
    CHECK_STR(m.cwd, "/Users/me/proj");
}

static void t_request(void) {
    Request q = {0};
    const char *j =
        "{\"rid\":\"r1\",\"tool\":\"Bash\",\"title\":\"ls\",\"body\":\"ls -la\",\"timeout_s\":120,"
        "\"options\":[{\"id\":\"allow\",\"label\":\"允许\"},{\"id\":\"evil\",\"label\":\"x\"},"
        "{\"id\":\"deny\",\"label\":\"拒绝\"}]}";
    CHECK(protocol_parse_request(j, strlen(j), &q));
    CHECK(q.n_opts == 2);
    CHECK_STR(q.opts[0].label, "允许");
    CHECK_STR(q.opts[1].id, "deny");
    CHECK_STR(q.body, "ls -la");
    CHECK(q.timeout_s == 120);
    CHECK_STR(q.cwd, "");
    free(q.body);
    const char *w = "{\"rid\":\"r1\",\"title\":\"t\",\"body\":\"b\",\"cwd\":\"/Users/me/proj\"}";
    CHECK(protocol_parse_request(w, strlen(w), &q));
    CHECK_STR(q.cwd, "/Users/me/proj");
    free(q.body);
}

static void t_request_timeout_default(void) {
    Request q = {0};
    const char *j = "{\"rid\":\"r1\",\"title\":\"t\",\"body\":\"b\",\"timeout_s\":0.1}";
    CHECK(protocol_parse_request(j, strlen(j), &q) && q.timeout_s == 120);
    free(q.body);
    const char *big = "{\"rid\":\"r1\",\"title\":\"t\",\"body\":\"b\",\"timeout_s\":1e300}";
    CHECK(protocol_parse_request(big, strlen(big), &q) && q.timeout_s == 120);
    free(q.body);
}

static void t_rejects_garbage(void) {
    Request q = {0};
    StatusMsg m;
    CHECK(!protocol_parse_request("{", 1, &q));
    CHECK(!protocol_parse_request("[]", 2, &q));
    CHECK(!protocol_parse_status("\xff\xfe", 2, &m));
    CHECK(!protocol_parse_status("{\"state\":\"bogus\"}", 17, &m));
    CHECK(!protocol_parse_request("{\"rid\":\"r\"}", 11, &q));
    CHECK(q.body == NULL);
    CHECK(!protocol_parse_request("{\"rid\":1,\"title\":\"t\",\"body\":\"b\"}", 32, &q));
    CHECK(q.body == NULL);
}

static void t_rejects_oversize(void) {
    size_t n = 131073;
    char *big = malloc(n);
    memset(big, ' ', n);
    big[0] = '{';
    big[n - 1] = '}';
    Request q = {0};
    CHECK(!protocol_parse_request(big, n, &q));
    free(big);
}

static void t_body_with_nul_len(void) {
    /* payload not NUL-terminated: parser must respect len */
    char buf[64];
    const char *j = "{\"rid\":\"r\",\"title\":\"t\",\"body\":\"b\"}";
    size_t n = strlen(j);
    memcpy(buf, j, n);
    memset(buf + n, 'x', sizeof buf - n);
    Request q = {0};
    CHECK(protocol_parse_request(buf, n, &q));
    free(q.body);
}

static void t_response(void) {
    char b[128];
    CHECK(protocol_build_response("r1", "allow", b, sizeof b) > 0);
    CHECK_STR(b, "{\"rid\":\"r1\",\"choice\":\"allow\"}");
    CHECK(protocol_build_response("r1", "allow", b, 10) < 0);
}

static void t_offline_flag(void) {
    CHECK(protocol_is_offline_flag("claude/psv/online", (const uint8_t *)"0", 1));
    CHECK(!protocol_is_offline_flag("claude/psv/online", (const uint8_t *)"1", 1));
    CHECK(!protocol_is_offline_flag("claude/psv/online", (const uint8_t *)"", 0));
    CHECK(!protocol_is_offline_flag("claude/psv/mode", (const uint8_t *)"0", 1));
}

static void t_long_summary_kept(void) {
    static char j[12000];
    static StatusMsg m;
    size_t n = (size_t)snprintf(j, sizeof j, "{\"state\":\"idle\",\"summary\":\"");
    for (int i = 0; i < 3000; i++) n += (size_t)snprintf(j + n, sizeof j - n, "完");
    snprintf(j + n, sizeof j - n, "\"}");
    CHECK(protocol_parse_status(j, strlen(j), &m));
    CHECK(strlen(m.summary) == 9000);
}

static void t_turn_topic(void) {
    char s[64], r[24];
    CHECK(protocol_parse_topic("claude/session/abc/turn", s, r) == TOPIC_TURN);
    CHECK_STR(s, "abc");
}

static void t_turn(void) {
    const char *j = "{\"items\":[{\"k\":\"you\",\"t\":\"做个提问页\"},{\"k\":\"claude\",\"t\":\"好的\"},"
                    "{\"k\":\"tool\",\"n\":\"Read\",\"t\":\"ui.c\"},{\"k\":\"bogus\",\"t\":\"x\"},{\"k\":\"note\",\"t\":\"…\"}],\"ts\":1}";
    TurnItem *items = NULL;
    int n = 0;
    CHECK(protocol_parse_turn(j, strlen(j), &items, &n));
    CHECK(n == 4);
    CHECK(items[0].kind == TURN_YOU);
    CHECK_STR(items[0].text, "做个提问页");
    CHECK(items[2].kind == TURN_TOOL);
    CHECK_STR(items[2].tool, "Read");
    CHECK(items[3].kind == TURN_NOTE);
    turn_free(items, n);
    CHECK(!protocol_parse_turn("{\"items\":3}", 11, &items, &n));
    CHECK(!protocol_parse_turn("[", 1, &items, &n));
}

static void t_question_request(void) {
    const char *j =
        "{\"rid\":\"r1\",\"kind\":\"question\",\"tool\":\"AskUserQuestion\",\"title\":\"Which?\",\"timeout_s\":120,"
        "\"questions\":[{\"header\":\"Runner\",\"question\":\"Which runner?\",\"multi\":false,"
        "\"options\":[{\"label\":\"Vitest\",\"description\":\"fast\"},{\"label\":\"Jest\",\"description\":\"\"}]},"
        "{\"header\":\"Retries\",\"question\":\"Retry on?\",\"multi\":true,\"options\":[{\"label\":\"5xx\"}]}]}";
    Request q;
    memset(&q, 0, sizeof q);
    CHECK(protocol_parse_request(j, strlen(j), &q));
    CHECK(q.kind == REQ_QUESTION && q.n_questions == 2);
    CHECK_STR(q.questions[0].opts[0].desc, "fast");
    CHECK(q.questions[1].multi && q.questions[1].n_opts == 1);
    CHECK(q.body == NULL);
    request_free(&q);
    const char *empty = "{\"rid\":\"r1\",\"kind\":\"question\",\"title\":\"t\",\"questions\":[]}";
    CHECK(!protocol_parse_request(empty, strlen(empty), &q));
    CHECK(q.questions == NULL);
}

static void t_build_approval_escapes_note(void) {
    char b[256];
    CHECK(protocol_build_approval("r1", "deny_note", "say \"hi\"\n先跑测试", b, sizeof b) > 0);
    CHECK_STR(b, "{\"rid\":\"r1\",\"choice\":\"deny_note\",\"note\":\"say \\\"hi\\\"\\n先跑测试\"}");
    CHECK(protocol_build_approval("r1", "allow", NULL, b, sizeof b) > 0);
    CHECK_STR(b, "{\"rid\":\"r1\",\"choice\":\"allow\"}");
}

static void t_build_answers(void) {
    Request r;
    memset(&r, 0, sizeof r);
    snprintf(r.rid, sizeof r.rid, "r9");
    r.kind = REQ_QUESTION;
    r.n_questions = 2;
    r.picked[0] = 0x2;          /* option 1 */
    r.picked[1] = 0x5;          /* options 0 and 2 */
    snprintf(r.other[1], OTHER_BYTES, "conn resets");
    char b[256];
    CHECK(protocol_build_answers(&r, b, sizeof b) > 0);
    CHECK_STR(b, "{\"rid\":\"r9\",\"answers\":[{\"selected\":[1]},{\"selected\":[0,2],\"other\":\"conn resets\"}]}");
    CHECK(protocol_build_mac("r9", b, sizeof b) > 0);
    CHECK_STR(b, "{\"rid\":\"r9\",\"choice\":\"mac\"}");
    CHECK(protocol_build_mac("r9", b, 8) < 0);
}

static void t_reply_topics(void) {
    char sid[64], rid[24];
    CHECK(protocol_parse_topic("claude/session/abc/reply_state", sid, rid) == TOPIC_REPLY_STATE);
    CHECK_STR(sid, "abc");
    CHECK(protocol_parse_topic("claude/session/abc/reply", sid, rid) == TOPIC_OTHER);
}

static void t_build_reply(void) {
    char buf[256];
    int n = protocol_build_reply("id1", RK_TEXT, "say \"hi\" 待った!", buf, sizeof buf);
    CHECK(n > 0);
    cJSON *o = cJSON_ParseWithLength(buf, (size_t)n);
    CHECK(o != NULL);
    CHECK_STR(cJSON_GetObjectItem(o, "id")->valuestring, "id1");
    CHECK_STR(cJSON_GetObjectItem(o, "kind")->valuestring, "text");
    CHECK_STR(cJSON_GetObjectItem(o, "text")->valuestring, "say \"hi\" 待った!");
    cJSON_Delete(o);
    n = protocol_build_reply("id2", RK_STOP, NULL, buf, sizeof buf);
    o = cJSON_ParseWithLength(buf, (size_t)n);
    CHECK_STR(cJSON_GetObjectItem(o, "kind")->valuestring, "stop");
    CHECK(cJSON_GetObjectItem(o, "text") == NULL);
    cJSON_Delete(o);
    n = protocol_build_reply("id3", RK_CANCEL, "ignored", buf, sizeof buf);
    o = cJSON_ParseWithLength(buf, (size_t)n);
    CHECK_STR(cJSON_GetObjectItem(o, "kind")->valuestring, "cancel");
    CHECK(cJSON_GetObjectItem(o, "text") == NULL);
    cJSON_Delete(o);
    CHECK(protocol_build_reply("id1", RK_TEXT, "a long enough text", buf, 10) == -1);
}

static void t_parse_reply_state(void) {
    char id[24], state[16];
    const char *ok = "{\"id\":\"a\",\"state\":\"queued\"}";
    CHECK(protocol_parse_reply_state(ok, strlen(ok), id, state));
    CHECK_STR(id, "a");
    CHECK_STR(state, "queued");
    const char *bad[] = {"{\"id\":\"a\"}", "{\"state\":\"queued\"}", "{\"id\":1,\"state\":\"queued\"}",
                         "{\"id\":\"0123456789012345678901234\",\"state\":\"queued\"}",
                         "{\"id\":\"a\",\"state\":\"a-much-too-long-state\"}", "[]", ""};
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) CHECK(!protocol_parse_reply_state(bad[i], strlen(bad[i]), id, state));
}

int main(void) {
    RUN(t_turn_topic);
    RUN(t_reply_topics);
    RUN(t_build_reply);
    RUN(t_parse_reply_state);
    RUN(t_turn);
    RUN(t_question_request);
    RUN(t_build_approval_escapes_note);
    RUN(t_build_answers);
    RUN(t_offline_flag);
    RUN(t_long_summary_kept);
    RUN(t_topics);
    RUN(t_status);
    RUN(t_request);
    RUN(t_request_timeout_default);
    RUN(t_rejects_garbage);
    RUN(t_rejects_oversize);
    RUN(t_body_with_nul_len);
    RUN(t_response);
    return TEST_RESULT();
}
