#include <stdlib.h>

#include "model.h"
#include "util.h"
#include "test.h"

static Request mkreq(const char *rid) {
    Request q;
    memset(&q, 0, sizeof q);
    snprintf(q.rid, sizeof q.rid, "%s", rid);
    snprintf(q.tool, sizeof q.tool, "Bash");
    snprintf(q.title, sizeof q.title, "title-%s", rid);
    q.body = strdup("body");
    snprintf(q.opts[0].id, sizeof q.opts[0].id, "allow");
    snprintf(q.opts[1].id, sizeof q.opts[1].id, "deny");
    snprintf(q.opts[2].id, sizeof q.opts[2].id, "deny_stop");
    q.n_opts = 3;
    q.timeout_s = 120;
    return q;
}

static StatusMsg st(SessState s, const char *project) {
    StatusMsg m;
    memset(&m, 0, sizeof m);
    m.state = s;
    snprintf(m.project, sizeof m.project, "%s", project);
    return m;
}

static OutcomeKind act(Model *m, uint64_t now, Outcome *o) {
    model_activate(m, now, o);
    return o->kind;
}

static Request mkq(const char *rid, bool multi2) {
    Request q;
    memset(&q, 0, sizeof q);
    snprintf(q.rid, sizeof q.rid, "%s", rid);
    q.kind = REQ_QUESTION;
    q.timeout_s = 120;
    q.n_questions = 2;
    q.questions = calloc(2, sizeof(Question));
    q.questions[0].n_opts = 2;
    snprintf(q.questions[0].opts[0].label, 64, "Vitest");
    snprintf(q.questions[0].opts[1].label, 64, "Jest");
    q.questions[1].n_opts = 3;
    q.questions[1].multi = multi2;
    return q;
}

/* ---- Task 4: data ---- */

static void t_new_request_jumps_when_idle(void) {
    Model m;
    model_init(&m, MODE_SOFA);
    StatusMsg a = st(ST_IDLE, "a"), b = st(ST_RUNNING, "b");
    model_on_status(&m, "A", &a, 0);
    model_on_status(&m, "B", &b, 0);
    CHECK_STR(model_current(&m)->sid, "A");
    Request q = mkreq("r1");
    CHECK(model_on_request(&m, "B", "r1", &q, 10) & MEV_NEW_REQUEST);
    CHECK_STR(model_current(&m)->sid, "B");
    model_free(&m);
}

static void t_new_request_flashes_when_busy(void) {
    Model m;
    model_init(&m, MODE_SOFA);
    Request q1 = mkreq("r1"), q2 = mkreq("r2");
    model_on_request(&m, "A", "r1", &q1, 0);
    CHECK(model_on_request(&m, "B", "r2", &q2, 5) & MEV_NEW_REQUEST);
    CHECK_STR(model_current(&m)->sid, "A");
    CHECK(m.s[1].flash);
    model_free(&m);
}

static void t_unknown_session_created_waiting(void) {
    Model m;
    model_init(&m, MODE_SOFA);
    Request q = mkreq("r1");
    model_on_request(&m, "A", "r1", &q, 0);
    CHECK(m.n == 1 && m.s[0].state == ST_WAITING);
    CHECK_STR(m.s[0].project, "?");
    model_free(&m);
}

static void t_duplicate_request_idempotent(void) {
    Model m;
    model_init(&m, MODE_SOFA);
    Request q1 = mkreq("r1"), q2 = mkreq("r1");
    model_on_request(&m, "A", "r1", &q1, 0);
    CHECK((model_on_request(&m, "A", "r1", &q2, 5) & MEV_NEW_REQUEST) == 0);
    CHECK(model_pending_count(&m) == 1);
    CHECK(model_current_request(&m)->recv_ms == 0); /* expiry clock not reset */
    model_free(&m);
}

static void t_clear_request(void) {
    Model m;
    model_init(&m, MODE_SOFA);
    Request q = mkreq("r1");
    model_on_request(&m, "A", "r1", &q, 0);
    model_on_request(&m, "A", "r1", NULL, 1);
    CHECK(model_pending_count(&m) == 0);
    CHECK(model_current_request(&m) == NULL);
    model_on_request(&m, "A", "nope", NULL, 2);
    model_on_request(&m, "Z", "nope", NULL, 2);
    CHECK(m.n == 1);
    model_free(&m);
}

static void t_queue_order(void) {
    Model m;
    model_init(&m, MODE_SOFA);
    Request q1 = mkreq("r1"), q2 = mkreq("r2");
    model_on_request(&m, "A", "r1", &q1, 0);
    model_on_request(&m, "A", "r2", &q2, 5);
    CHECK_STR(model_current_request(&m)->rid, "r1");
    CHECK(model_pending_count(&m) == 2);
    model_on_request(&m, "A", "r1", NULL, 6);
    CHECK_STR(model_current_request(&m)->rid, "r2");
    model_free(&m);
}

static void t_expiry_by_local_time(void) {
    Model m;
    model_init(&m, MODE_SOFA);
    Request q = mkreq("r1");
    model_on_request(&m, "A", "r1", &q, 1000);
    CHECK(!model_request_expired(model_current_request(&m), 1000 + 119999));
    CHECK(model_request_expired(model_current_request(&m), 1000 + 120001));
    model_free(&m);
}

static void t_idle_event_only_on_transition(void) {
    Model m;
    model_init(&m, MODE_SOFA);
    StatusMsg i = st(ST_IDLE, "a"), r = st(ST_RUNNING, "a");
    CHECK((model_on_status(&m, "A", &i, 0) & MEV_IDLE) == 0);
    CHECK((model_on_status(&m, "A", &i, 1) & MEV_IDLE) == 0);
    model_on_status(&m, "A", &r, 2);
    CHECK(model_on_status(&m, "A", &i, 3) & MEV_IDLE);
    model_free(&m);
}

static void t_hide_after_12h(void) {
    Model m;
    model_init(&m, MODE_SOFA);
    StatusMsg i = st(ST_IDLE, "a");
    model_on_status(&m, "A", &i, 0);
    model_tick(&m, HIDE_AFTER_MS);
    CHECK(m.n == 1);
    model_tick(&m, HIDE_AFTER_MS + 1);
    CHECK(m.n == 0 && m.cur == -1);
    CHECK(model_current(&m) == NULL);
    model_free(&m);
}

static void t_hide_keeps_sessions_with_requests(void) {
    Model m;
    model_init(&m, MODE_SOFA);
    Request q = mkreq("r1");
    model_on_request(&m, "A", "r1", &q, 0);
    model_tick(&m, HIDE_AFTER_MS + 1);
    CHECK(m.n == 1);
    model_free(&m);
}

static void t_capacity(void) {
    Model m;
    model_init(&m, MODE_SOFA);
    char sid[16];
    for (int i = 0; i < MAX_SESSIONS + 1; i++) {
        snprintf(sid, sizeof sid, "S%d", i);
        StatusMsg r = st(ST_RUNNING, sid);
        model_on_status(&m, sid, &r, (uint64_t)i);
    }
    CHECK(m.n == MAX_SESSIONS);
    for (int i = 0; i < m.n; i++) CHECK(strcmp(m.s[i].sid, "S0") != 0); /* oldest evicted */
    char rid[16];
    for (int i = 0; i < MAX_REQS + 1; i++) {
        snprintf(rid, sizeof rid, "r%d", i);
        Request q = mkreq(rid);
        model_on_request(&m, "S5", rid, &q, 100 + (uint64_t)i);
    }
    int idx = -1;
    for (int i = 0; i < m.n; i++)
        if (!strcmp(m.s[i].sid, "S5")) idx = i;
    CHECK(idx >= 0 && m.s[idx].n_reqs == MAX_REQS);
    CHECK_STR(m.s[idx].reqs[0].rid, "r1"); /* r0 dropped */
    model_free(&m);
}

static void t_status_clear_frees(void) {
    Model m;
    model_init(&m, MODE_SOFA);
    Request q = mkreq("r1");
    model_on_request(&m, "A", "r1", &q, 0);
    StatusMsg b = st(ST_IDLE, "b");
    model_on_status(&m, "B", &b, 0);
    model_on_status(&m, "A", NULL, 1);
    CHECK(m.n == 1);
    CHECK_STR(model_current(&m)->sid, "B");
    model_free(&m);
}

/* ---- Task 5: interaction ---- */

static void t_confirm_requires_selection_and_lockout(void) {
    Model m;
    model_init(&m, MODE_SOFA);
    Request q = mkreq("r1");
    model_on_request(&m, "A", "r1", &q, 1000);
    Outcome o;
    CHECK(act(&m, 2000, &o) == OUT_PICK_FIRST);
    model_move_sel(&m, +1, 1100);
    CHECK(act(&m, 1400, &o) == OUT_NONE); /* within 500 ms of showing */
    CHECK(act(&m, 1600, &o) == OUT_SEND_CHOICE);
    CHECK_STR(o.choice, "allow");
    CHECK_STR(o.rid, "r1");
    CHECK_STR(o.sid, "A");
    CHECK(act(&m, 1700, &o) == OUT_NONE); /* already sent */
    model_free(&m);
}

static void t_sel_resets_when_request_changes(void) {
    Model m;
    model_init(&m, MODE_SOFA);
    Request q1 = mkreq("r1"), q2 = mkreq("r2");
    model_on_request(&m, "A", "r1", &q1, 0);
    model_on_request(&m, "A", "r2", &q2, 10);
    model_move_sel(&m, +1, 600);
    CHECK(m.sel == 0);
    model_on_request(&m, "A", "r1", NULL, 700);
    CHECK(m.sel == -1);
    CHECK(m.shown_ms == 700);
    CHECK_STR(m.shown_rid, "r2");
    Outcome o;
    model_move_sel(&m, +1, 800);
    CHECK(act(&m, 1100, &o) == OUT_NONE); /* lockout restarts at 700 */
    CHECK(act(&m, 1200, &o) == OUT_SEND_CHOICE);
    model_free(&m);
}

static void t_confirm_rejects_expired(void) {
    Model m;
    model_init(&m, MODE_SOFA);
    Request q = mkreq("r1");
    model_on_request(&m, "A", "r1", &q, 0);
    model_move_sel(&m, +1, 1000);
    Outcome o;
    CHECK(act(&m, 121000, &o) == OUT_NONE);
    model_free(&m);
}

static void t_sheet_hide_blocks_and_rearms(void) {
    Model m;
    model_init(&m, MODE_SOFA);
    Request q = mkreq("r1");
    model_on_request(&m, "A", "r1", &q, 0);
    model_move_sel(&m, +1, 600);
    model_toggle_sheet(&m, 700);
    CHECK(m.sheet_hidden && m.sel == -1);
    Outcome o;
    model_move_sel(&m, +1, 800);
    CHECK(m.sel == -1);                    /* hidden: no selection */
    CHECK(act(&m, 900, &o) == OUT_NONE);   /* hidden: ✕ does nothing */
    model_toggle_sheet(&m, 1000);
    CHECK(!m.sheet_hidden && m.shown_ms == 1000);
    model_move_sel(&m, +1, 1100);
    CHECK(act(&m, 1400, &o) == OUT_NONE);  /* re-armed lockout */
    CHECK(act(&m, 1501, &o) == OUT_SEND_CHOICE);
    model_free(&m);
}

static void t_new_request_unhides(void) {
    Model m;
    model_init(&m, MODE_SOFA);
    Request q1 = mkreq("r1"), q2 = mkreq("r2");
    model_on_request(&m, "A", "r1", &q1, 0);
    model_toggle_sheet(&m, 10);
    model_on_request(&m, "A", "r2", &q2, 20);
    CHECK(m.sheet_hidden);                 /* r1 is still the one shown */
    model_on_request(&m, "A", "r1", NULL, 30);
    CHECK(!m.sheet_hidden);                /* r2 comes up expanded */
    model_free(&m);
}

static void t_deny_note_flow(void) {
    Model m;
    model_init(&m, MODE_SOFA);
    Request q = mkreq("r1");
    snprintf(q.opts[2].id, sizeof q.opts[2].id, "deny_note");
    model_on_request(&m, "A", "r1", &q, 0);
    m.sel = 2;
    Outcome o;
    CHECK(act(&m, 600, &o) == OUT_NEED_NOTE);
    CHECK(!model_current_request(&m)->sent_choice[0]);
    CHECK(model_submit_note(&m, "A", "r1", 700, &o) && o.kind == OUT_SEND_CHOICE);
    CHECK_STR(o.choice, "deny_note");
    model_free(&m);
}

static void t_question_single_then_multi_then_review(void) {
    Model m;
    model_init(&m, MODE_SOFA);
    Request q = mkq("r1", true);
    model_on_request(&m, "A", "r1", &q, 0);
    const Request *r = model_current_request(&m);
    CHECK(model_row_count(r) == 2 + 1 + 1);           /* 2 options, Other, Mac */
    Outcome o;
    m.sel = 1;
    CHECK(act(&m, 600, &o) == OUT_NONE);              /* pick Jest, move on */
    CHECK(r->step == 1 && r->picked[0] == 0x2);
    CHECK(m.sel == -1 && m.shown_ms == 600);          /* new page: re-armed */
    CHECK(model_row_count(r) == 3 + 1 + 1 + 1);       /* 3 options, Other, Next, Mac */
    m.sel = 4;                                         /* Next with nothing picked */
    CHECK(act(&m, 1200, &o) == OUT_PICK_FIRST);
    m.sel = 0;
    act(&m, 1200, &o);
    m.sel = 2;
    act(&m, 1200, &o);
    CHECK(r->picked[1] == 0x5 && r->step == 1);       /* multi: toggles, stays */
    m.sel = 3;
    CHECK(act(&m, 1200, &o) == OUT_NEED_OTHER);
    model_submit_other(&m, "A", "r1", "conn resets", 1300);
    CHECK_STR(r->other[1], "conn resets");
    CHECK(r->step == 1);                               /* multi keeps the page */
    m.sel = 4;
    act(&m, 1300, &o);
    CHECK(r->step == 2 && model_row_count(r) == 3);    /* review */
    m.sel = 1;
    CHECK(act(&m, 1900, &o) == OUT_NONE && r->step == 1); /* Change */
    model_back(&m, 2000);
    CHECK(r->step == 0);
    model_back(&m, 2100);
    CHECK(r->step == 0);
    model_free(&m);
}

static void t_question_send_and_mac(void) {
    Model m;
    model_init(&m, MODE_SOFA);
    Request q = mkq("r1", false);
    model_on_request(&m, "A", "r1", &q, 0);
    const Request *r = model_current_request(&m);
    Outcome o;
    m.sel = 2;                                         /* Other on a single-choice question */
    CHECK(act(&m, 600, &o) == OUT_NEED_OTHER);
    model_submit_other(&m, "A", "r1", "Mocha", 700);
    CHECK(r->step == 1 && r->picked[0] == 0);
    m.sel = 0;
    act(&m, 1300, &o);
    CHECK(r->step == 2);
    m.sel = 0;
    CHECK(act(&m, 1900, &o) == OUT_SEND_ANSWERS);
    CHECK_STR(o.choice, "answers");
    model_free(&m);

    model_init(&m, MODE_SOFA);
    Request q2 = mkq("r2", false);
    model_on_request(&m, "A", "r2", &q2, 0);
    m.sel = 3;                                         /* Answer on Mac */
    CHECK(act(&m, 600, &o) == OUT_SEND_MAC);
    model_free(&m);
}

static void t_keyboard_text_goes_only_to_its_request(void) {
    Model m;
    model_init(&m, MODE_SOFA);
    Request a = mkreq("rA"), b = mkreq("rB");
    snprintf(a.opts[2].id, sizeof a.opts[2].id, "deny_note");
    model_on_request(&m, "S", "rA", &a, 0);
    model_on_request(&m, "S", "rB", &b, 0);
    m.sel = 2;
    Outcome o;
    CHECK(act(&m, 600, &o) == OUT_NEED_NOTE);
    model_on_request(&m, "S", "rA", NULL, 700);           /* A answered on the Mac while typing */
    CHECK(!model_submit_note(&m, "S", "rA", 800, &o));    /* must not deny B */
    CHECK(!model_current_request(&m)->sent_choice[0]);
    model_free(&m);

    model_init(&m, MODE_SOFA);
    Request q1 = mkq("q1", false), q2 = mkq("q2", false);
    model_on_request(&m, "S", "q1", &q1, 0);
    model_on_request(&m, "S", "q2", &q2, 0);
    model_on_request(&m, "S", "q1", NULL, 10);
    model_submit_other(&m, "S", "q1", "Mocha", 20);       /* q2 is current now: ignore */
    CHECK(model_current_request(&m)->step == 0 && !model_current_request(&m)->other[0][0]);
    model_free(&m);
}

static void t_long_other_stays_valid_utf8(void) {
    Model m;
    model_init(&m, MODE_SOFA);
    Request q = mkq("q1", true);
    model_on_request(&m, "S", "q1", &q, 0);
    char text[700] = "";
    for (int i = 0; i < 200; i++) strcat(text, "中");  /* 600 bytes */
    strcat(text, "abc");
    model_submit_other(&m, "S", "q1", text, 600);
    const char *o = model_current_request(&m)->other[0];
    CHECK(utf8_valid(o, strlen(o)));
    CHECK(strlen(o) >= 600);                             /* 200 CJK characters fit */
    model_free(&m);
}

static void t_redelivery_keeps_answers(void) {
    Model m;
    model_init(&m, MODE_SOFA);
    Request q = mkq("r1", false), again = mkq("r1", false);
    model_on_request(&m, "A", "r1", &q, 0);
    Outcome o;
    m.sel = 0;
    act(&m, 600, &o);
    CHECK(model_on_request(&m, "A", "r1", &again, 700) == 0);
    CHECK(model_current_request(&m)->step == 1);
    model_free(&m);
}

static void t_turn_owned_and_replaced(void) {
    Model m;
    model_init(&m, MODE_SOFA);
    TurnItem *a = calloc(1, sizeof(TurnItem));
    a[0].text = strdup("hi");
    model_on_turn(&m, "A", a, 1, 0);
    CHECK(m.n == 1 && m.s[0].n_turn == 1);
    unsigned rev = m.s[0].turn_rev;
    TurnItem *b = calloc(2, sizeof(TurnItem));
    b[0].text = strdup("x");
    b[1].text = strdup("y");
    model_on_turn(&m, "A", b, 2, 1);
    CHECK(m.s[0].n_turn == 2 && m.s[0].turn_rev != rev);
    model_on_turn(&m, "A", NULL, 0, 2);
    CHECK(m.s[0].n_turn == 0);
    model_on_turn(&m, "Z", NULL, 0, 3);                /* clearing an unknown session creates nothing */
    CHECK(m.n == 1);
    model_free(&m);
}

static void t_switch_clears_flash(void) {
    Model m;
    model_init(&m, MODE_SOFA);
    Request q1 = mkreq("r1"), q2 = mkreq("r2");
    model_on_request(&m, "A", "r1", &q1, 0);
    model_on_request(&m, "B", "r2", &q2, 0);
    CHECK(m.s[1].flash);
    model_switch(&m, +1, 10);
    CHECK_STR(model_current(&m)->sid, "B");
    CHECK(!m.s[1].flash);
    model_switch(&m, +1, 20); /* wraps */
    CHECK_STR(model_current(&m)->sid, "A");
    model_switch(&m, -1, 30);
    CHECK_STR(model_current(&m)->sid, "B");
    model_free(&m);
}

static void t_remove_and_select(void) {
    Model m;
    model_init(&m, MODE_SOFA);
    StatusMsg a = st(ST_IDLE, "a"), b = st(ST_IDLE, "b");
    model_on_status(&m, "A", &a, 0);
    model_on_status(&m, "B", &b, 0);
    model_select(&m, 1, 1);
    CHECK_STR(model_current(&m)->sid, "B");
    model_remove_at(&m, 1);
    CHECK(m.n == 1 && m.cur == 0);
    model_free(&m);
}

static void t_move_sel_bounds(void) {
    Model m;
    model_init(&m, MODE_SOFA);
    Request q = mkreq("r1");
    model_on_request(&m, "A", "r1", &q, 0);
    model_move_sel(&m, +1, 1);
    CHECK(m.sel == 0);
    model_move_sel(&m, +1, 2);
    model_move_sel(&m, +1, 3);
    model_move_sel(&m, +1, 4);
    CHECK(m.sel == 2);
    model_set_sel(&m, -1);
    model_move_sel(&m, -1, 5);
    CHECK(m.sel == 2);
    model_set_sel(&m, 7);
    CHECK(m.sel == 2); /* out of range ignored */
    model_set_sel(&m, 1);
    CHECK(m.sel == 1);
    model_free(&m);
}

static void t_move_sel_without_request(void) {
    Model m;
    model_init(&m, MODE_SOFA);
    model_move_sel(&m, +1, 0);
    CHECK(m.sel == -1);
    model_free(&m);
}

static void t_toggle_mode(void) {
    Model m;
    model_init(&m, MODE_SOFA);
    CHECK(model_toggle_mode(&m) == MODE_DESK);
    CHECK(model_toggle_mode(&m) == MODE_SOFA);
    model_free(&m);
}

static void t_cwd_kept(void) {
    Model m;
    model_init(&m, MODE_SOFA);
    StatusMsg a = st(ST_IDLE, "a");
    snprintf(a.cwd, sizeof a.cwd, "/x/a");
    model_on_status(&m, "A", &a, 0);
    CHECK_STR(m.s[0].cwd, "/x/a");
    model_free(&m);
}

static void t_summary_rev_changes(void) {
    Model m;
    model_init(&m, MODE_SOFA);
    StatusMsg a = st(ST_IDLE, "a");
    model_on_status(&m, "A", &a, 0);
    unsigned rev = m.s[0].summary_rev;
    model_on_status(&m, "A", &a, 1);
    CHECK(m.s[0].summary_rev != rev);
    model_free(&m);
}

static void t_idle_since(void) {
    Model m;
    model_init(&m, MODE_SOFA);
    StatusMsg i = st(ST_IDLE, "a"), r = st(ST_RUNNING, "a");
    model_on_status(&m, "A", &i, 100);
    CHECK(m.s[0].idle_since_ms == 100);
    model_on_status(&m, "A", &i, 200); /* still idle: keeps the original time */
    CHECK(m.s[0].idle_since_ms == 100);
    model_on_status(&m, "A", &r, 300);
    model_on_status(&m, "A", &i, 400);
    CHECK(m.s[0].idle_since_ms == 400);
    model_free(&m);
}

int main(void) {
    RUN(t_idle_since);
    RUN(t_summary_rev_changes);
    RUN(t_cwd_kept);
    RUN(t_confirm_requires_selection_and_lockout);
    RUN(t_sel_resets_when_request_changes);
    RUN(t_confirm_rejects_expired);
    RUN(t_switch_clears_flash);
    RUN(t_remove_and_select);
    RUN(t_sheet_hide_blocks_and_rearms);
    RUN(t_new_request_unhides);
    RUN(t_deny_note_flow);
    RUN(t_question_single_then_multi_then_review);
    RUN(t_question_send_and_mac);
    RUN(t_redelivery_keeps_answers);
    RUN(t_keyboard_text_goes_only_to_its_request);
    RUN(t_long_other_stays_valid_utf8);
    RUN(t_turn_owned_and_replaced);
    RUN(t_move_sel_bounds);
    RUN(t_move_sel_without_request);
    RUN(t_toggle_mode);
    RUN(t_new_request_jumps_when_idle);
    RUN(t_new_request_flashes_when_busy);
    RUN(t_unknown_session_created_waiting);
    RUN(t_duplicate_request_idempotent);
    RUN(t_clear_request);
    RUN(t_queue_order);
    RUN(t_expiry_by_local_time);
    RUN(t_idle_event_only_on_transition);
    RUN(t_hide_after_12h);
    RUN(t_hide_keeps_sessions_with_requests);
    RUN(t_capacity);
    RUN(t_status_clear_frees);
    return TEST_RESULT();
}
