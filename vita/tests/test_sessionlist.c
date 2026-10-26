#include "sessionlist.h"
#include "test.h"

static Actions A(void) {
    Actions a;
    memset(&a, 0, sizeof a);
    return a;
}

static StatusMsg st(SessState s) {
    StatusMsg m;
    memset(&m, 0, sizeof m);
    m.state = s;
    return m;
}

static void three_sessions(Model *m) {
    model_init(m, MODE_SOFA);
    StatusMsg i = st(ST_IDLE), r = st(ST_RUNNING), w = st(ST_WAITING);
    model_on_status(m, "A", &i, 0);
    model_on_status(m, "B", &r, 0);
    model_on_status(m, "C", &w, 0);
}

static void t_open_switch_close(void) {
    Model m;
    three_sessions(&m);
    SessionList l;
    list_open(&l, &m);
    CHECK(l.open && l.cursor == 0);
    Actions a = A();
    a.scroll_lines = 1;
    list_handle(&l, &m, &a, 0, NULL);
    CHECK(l.cursor == 1);
    a = A();
    a.confirm = true;
    CHECK(list_handle(&l, &m, &a, 0, NULL) == LIST_SWITCHED);
    CHECK(!l.open && m.cur == 1);
    list_open(&l, &m);
    CHECK(l.cursor == 1);
    a = A();
    a.sessions = true;
    CHECK(list_handle(&l, &m, &a, 0, NULL) == LIST_CLOSED && !l.open);
    model_free(&m);
}

static void t_remove_needs_confirmation(void) {
    Model m;
    three_sessions(&m);
    SessionList l;
    list_open(&l, &m);
    Actions a = A();
    a.square = true;
    CHECK(list_handle(&l, &m, &a, 1000, NULL) == LIST_NONE);
    CHECK(l.confirming && l.confirm_sel == 0); /* Keep is the default */
    a = A();
    a.confirm = true;
    CHECK(list_handle(&l, &m, &a, 1100, NULL) == LIST_NONE); /* lockout */
    CHECK(list_handle(&l, &m, &a, 1600, NULL) == LIST_NONE);
    CHECK(!l.confirming && m.n == 3);                         /* kept */
    a = A();
    a.square = true;
    list_handle(&l, &m, &a, 2000, NULL);
    a = A();
    a.sel_dir = 1;
    list_handle(&l, &m, &a, 2100, NULL);
    CHECK(l.confirm_sel == 1);
    a = A();
    a.confirm = true;
    char sid[64] = "";
    CHECK(list_handle(&l, &m, &a, 2600, sid) == LIST_REMOVED);
    CHECK_STR(sid, "A");
    CHECK(m.n == 2 && l.open && !l.confirming);
    model_free(&m);
}

static void t_cancel_and_blocked(void) {
    Model m;
    three_sessions(&m);
    SessionList l;
    list_open(&l, &m);
    Actions a = A();
    a.square = true;
    list_handle(&l, &m, &a, 0, NULL);
    a = A();
    a.back = true;
    list_handle(&l, &m, &a, 100, NULL);
    CHECK(!l.confirming && l.open);
    l.cursor = 2; /* waiting session */
    a = A();
    a.square = true;
    list_handle(&l, &m, &a, 200, NULL);
    CHECK(!l.confirming && l.toast[0]);
    model_free(&m);
}

static void t_cursor_clamped_after_removal(void) {
    Model m;
    three_sessions(&m);
    SessionList l;
    list_open(&l, &m);
    l.cursor = 2;
    model_remove_at(&m, 2);
    Actions a = A();
    list_handle(&l, &m, &a, 0, NULL);
    CHECK(l.cursor == 1);
    model_free(&m);
}

static void t_remove_targets_the_confirmed_session(void) {
    Model m;
    model_init(&m, MODE_SOFA);
    StatusMsg i = st(ST_IDLE);
    model_on_status(&m, "A", &i, 0);
    model_on_status(&m, "B", &i, 0);
    model_on_status(&m, "C", &i, 0);
    model_on_status(&m, "D", &i, 0);
    SessionList l;
    list_open(&l, &m);
    l.cursor = 2; /* C */
    Actions a = A();
    a.square = true;
    list_handle(&l, &m, &a, 0, NULL);
    model_on_status(&m, "A", NULL, 100); /* A ends while the dialog is open: indices shift */
    a = A();
    a.sel_dir = 1;
    list_handle(&l, &m, &a, 200, NULL);
    a = A();
    a.confirm = true;
    char sid[64] = "";
    CHECK(list_handle(&l, &m, &a, 700, sid) == LIST_REMOVED);
    CHECK_STR(sid, "C");
    for (int k = 0; k < m.n; k++) CHECK(strcmp(m.s[k].sid, "C") != 0);
    CHECK(m.n == 2);
    model_free(&m);
}

static void t_remove_rechecks_waiting(void) {
    Model m;
    model_init(&m, MODE_SOFA);
    StatusMsg i = st(ST_IDLE), w = st(ST_WAITING);
    model_on_status(&m, "A", &i, 0);
    SessionList l;
    list_open(&l, &m);
    Actions a = A();
    a.square = true;
    list_handle(&l, &m, &a, 0, NULL);
    model_on_status(&m, "A", &w, 100); /* became waiting while the dialog was open */
    a = A();
    a.sel_dir = 1;
    list_handle(&l, &m, &a, 200, NULL);
    a = A();
    a.confirm = true;
    CHECK(list_handle(&l, &m, &a, 700, NULL) == LIST_NONE);
    CHECK(m.n == 1);
    model_free(&m);
}

static void t_dialog_only_switches_horizontally(void) {
    Model m;
    three_sessions(&m);
    SessionList l;
    list_open(&l, &m);
    Actions a = A();
    a.square = true;
    list_handle(&l, &m, &a, 0, NULL);
    a = A();
    a.scroll_lines = 1; /* left stick / d-pad up-down: Keep and Remove sit side by side */
    list_handle(&l, &m, &a, 100, NULL);
    CHECK(l.confirm_sel == 0);
    a = A();
    a.sel_dir = 1;
    list_handle(&l, &m, &a, 200, NULL);
    CHECK(l.confirm_sel == 1);
    model_free(&m);
}

int main(void) {
    RUN(t_dialog_only_switches_horizontally);
    RUN(t_remove_targets_the_confirmed_session);
    RUN(t_remove_rechecks_waiting);
    RUN(t_open_switch_close);
    RUN(t_remove_needs_confirmation);
    RUN(t_cancel_and_blocked);
    RUN(t_cursor_clamped_after_removal);
    return TEST_RESULT();
}
