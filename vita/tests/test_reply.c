#include "reply.h"
#include "test.h"

#define IDLE true, false
#define WORKING false, true

static Reply fresh(void) {
    Reply r;
    reply_init(&r);
    return r;
}

static void set_draft(Reply *r, const char *t) { snprintf(r->draft, sizeof r->draft, "%s", t); }

/* Highlight an item by name in the current layout. */
static void pick(Reply *r, bool idle, bool working, ReplyItem want) {
    ReplyLayout l = reply_layout(r, idle, working);
    for (int i = 0; i < l.n_rows; i++)
        if (l.rows[i] == want) return reply_select(r, i, 0);
    for (int i = 0; i < l.n_box; i++)
        if (l.box_items[i] == want) return reply_select(r, l.n_rows, i);
    CHECK(!"item not in layout");
}

/* A typed reply, sent and acknowledged as queued while the session works. */
static Reply queued(const char *text) {
    Reply r = fresh();
    set_draft(&r, text);
    r.drawer = RD_OPEN;
    pick(&r, WORKING, RR_SEND);
    reply_activate(&r, WORKING, "q1", 0);
    reply_on_state(&r, "q1", "received", 100);
    reply_on_state(&r, "q1", "queued", 100);
    return r;
}

static void t_reply_layout(void) {
    Reply r = fresh();
    ReplyLayout l = reply_layout(&r, IDLE);
    CHECK(l.n_rows == 3 && l.rows[0] == RR_CONTINUE && l.rows[1] == RR_VERIFY && l.rows[2] == RR_TYPE);
    CHECK(l.box == RB_NONE && l.n_box == 0);

    set_draft(&r, "hi");
    l = reply_layout(&r, IDLE);
    CHECK(l.n_rows == 2 && l.rows[0] == RR_CONTINUE && l.rows[1] == RR_VERIFY);
    CHECK(l.box == RB_DRAFT && l.n_box == 3);
    CHECK(l.box_items[0] == RR_SEND && l.box_items[1] == RR_EDIT && l.box_items[2] == RR_DISCARD);

    r = fresh();
    l = reply_layout(&r, WORKING);
    CHECK(l.n_rows == 2 && l.rows[0] == RR_MATTA && l.rows[1] == RR_TYPE && l.box == RB_NONE);
    set_draft(&r, "hi");
    l = reply_layout(&r, WORKING);
    CHECK(l.n_rows == 1 && l.rows[0] == RR_MATTA && l.box == RB_DRAFT);

    r = queued("later");
    l = reply_layout(&r, WORKING);
    CHECK(l.n_rows == 1 && l.rows[0] == RR_MATTA);
    CHECK(l.box == RB_QUEUED && l.n_box == 2 && l.box_items[0] == RR_EDIT && l.box_items[1] == RR_DISCARD);
    l = reply_layout(&r, IDLE);
    CHECK(l.n_rows == 2 && l.rows[0] == RR_CONTINUE && l.box == RB_QUEUED);

    r = fresh(); /* a preset in flight with a draft: the box shows the draft */
    set_draft(&r, "mine");
    pick(&r, IDLE, RR_CONTINUE);
    reply_activate(&r, IDLE, "p1", 0);
    CHECK(reply_layout(&r, IDLE).box == RB_DRAFT);

    l = reply_layout(&r, false, false);
    CHECK(l.n_rows == 0 && l.box == RB_NONE);
}

static void t_reply_move(void) {
    Reply r = fresh();
    set_draft(&r, "hi");
    ReplyLayout l = reply_layout(&r, IDLE); /* Continue, Verify, [Send Edit Discard] */
    reply_move(&r, &l, 1, 0);
    CHECK(r.line == 0);
    r.line = -1;
    reply_move(&r, &l, -1, 0);
    CHECK(r.line == 2 && r.col == 0);
    reply_move(&r, &l, 0, 1);
    CHECK(r.col == 1);
    reply_move(&r, &l, 0, 5);
    CHECK(r.col == 2);
    reply_move(&r, &l, 0, -9);
    CHECK(r.col == 0);
    reply_move(&r, &l, -1, 0);
    CHECK(r.line == 1);
    reply_move(&r, &l, 0, 1); /* left/right do nothing on a row */
    CHECK(r.line == 1);
    reply_move(&r, &l, -5, 0);
    CHECK(r.line == 0);
    reply_move(&r, &l, 1, 0);
    reply_move(&r, &l, 1, 0); /* down from the last row enters the box on its first button */
    CHECK(r.line == 2 && r.col == 0);
    reply_move(&r, &l, 3, 0);
    CHECK(r.line == 2);
    ReplyItem it;
    CHECK(reply_selected(&r, &l, &it) && it == RR_SEND);
}

static void t_reply_square_cycle(void) {
    Reply r = fresh();
    CHECK(reply_square(&r, IDLE, 0) && r.drawer == RD_OPEN && r.line == -1);
    r.line = 1;
    CHECK(reply_square(&r, IDLE, 0) && r.drawer == RD_HIDDEN);
    set_draft(&r, "hi");
    reply_square(&r, IDLE, 0);
    CHECK(r.drawer == RD_OPEN && r.line == -1);
    reply_square(&r, IDLE, 0);
    CHECK(r.drawer == RD_PEEK);
    reply_square(&r, IDLE, 0);
    CHECK(r.drawer == RD_OPEN);
    reply_back(&r, 0);
    CHECK(r.drawer == RD_PEEK);
    r.draft[0] = 0;
    reply_square(&r, IDLE, 0);
    reply_back(&r, 0);
    CHECK(r.drawer == RD_HIDDEN);
}

static void t_reply_square_no_drawer(void) {
    Reply r = fresh();
    CHECK(!reply_square(&r, false, false, 0));
    CHECK(r.drawer == RD_HIDDEN);
}

static void t_reply_preset_publishes(void) {
    Reply r = fresh();
    reply_square(&r, IDLE, 0);
    pick(&r, IDLE, RR_CONTINUE);
    ReplyAct a = reply_activate(&r, IDLE, "a", 5);
    CHECK(a.kind == RA_PUBLISH && a.rkind == RK_TEXT);
    CHECK_STR(a.id, "a");
    CHECK_STR(a.text, "Continue.");
    CHECK(r.status == RS_SENDING && r.drawer == RD_HIDDEN && reply_has_peek(&r, 5));

    r = fresh();
    set_draft(&r, "keep me");
    reply_square(&r, IDLE, 0);
    pick(&r, IDLE, RR_VERIFY);
    a = reply_activate(&r, IDLE, "b", 5);
    CHECK_STR(a.text, "Verify your work further before you finish.");
    CHECK_STR(r.draft, "keep me");
    CHECK(r.drawer == RD_PEEK);
}

static void t_reply_activate_nothing_selected(void) {
    Reply r = fresh();
    reply_square(&r, IDLE, 0);
    CHECK(reply_activate(&r, IDLE, "a", 0).kind == RA_NONE);
    CHECK(r.status == RS_NONE);
}

static void t_reply_type_and_return(void) {
    Reply r = fresh();
    reply_square(&r, IDLE, 0);
    pick(&r, IDLE, RR_TYPE);
    ReplyAct a = reply_activate(&r, IDLE, "x", 0);
    CHECK(a.kind == RA_OPEN_IME);
    CHECK_STR(a.text, "");
    CHECK(reply_ime_done(&r, "hi", true, "x", 0).kind == RA_NONE);
    CHECK_STR(r.draft, "hi");
    CHECK(r.drawer == RD_OPEN);
    pick(&r, IDLE, RR_EDIT);
    a = reply_activate(&r, IDLE, "x", 0);
    CHECK(a.kind == RA_OPEN_IME);
    CHECK_STR(a.text, "hi");
    reply_ime_done(&r, "x", false, "x", 0);
    CHECK_STR(r.draft, "hi");
}

static void t_reply_send_draft(void) {
    Reply r = fresh();
    set_draft(&r, "also the percentage");
    reply_square(&r, IDLE, 0);
    pick(&r, IDLE, RR_SEND);
    ReplyAct a = reply_activate(&r, IDLE, "d1", 1000);
    CHECK(a.kind == RA_PUBLISH && a.rkind == RK_TEXT);
    CHECK_STR(a.text, "also the percentage");
    CHECK(r.draft[0] == 0 && r.sent_typed);
    reply_on_state(&r, "d1", "delivered", 2000);
    CHECK(r.status == RS_DELIVERED && reply_has_peek(&r, 4000));
    reply_tick(&r, 5001);
    CHECK(r.status == RS_NONE && !reply_has_peek(&r, 5001));
}

static void t_reply_timeout_withdraws(void) {
    Reply r = fresh();
    set_draft(&r, "typed");
    pick(&r, IDLE, RR_SEND);
    reply_activate(&r, IDLE, "t1", 0);
    CHECK(reply_tick(&r, 10000).kind == RA_NONE);
    ReplyAct a = reply_tick(&r, 10001);
    CHECK(a.kind == RA_WITHDRAW && r.status == RS_FAILED);
    CHECK_STR(r.draft, "typed");
    CHECK(reply_has_peek(&r, 99999));

    r = fresh();
    set_draft(&r, "mine");
    pick(&r, IDLE, RR_CONTINUE);
    reply_activate(&r, IDLE, "t2", 0);
    CHECK(reply_tick(&r, 10001).kind == RA_WITHDRAW);
    CHECK_STR(r.draft, "mine");
}

static void t_reply_any_state_stops_timer(void) {
    Reply r = fresh();
    pick(&r, IDLE, RR_CONTINUE);
    reply_activate(&r, IDLE, "a", 0);
    reply_on_state(&r, "other", "delivered", 1000); /* not ours */
    CHECK(r.status == RS_SENDING);
    reply_on_state(&r, "a", "received", 9000);
    CHECK(reply_tick(&r, 11000).kind == RA_NONE && r.status == RS_SENDING);
    reply_on_state(&r, "a", "queued", 11000);
    CHECK(r.status == RS_QUEUED && reply_in_flight(&r));
}

static void t_reply_matta(void) {
    Reply r = fresh();
    reply_square(&r, WORKING, 0);
    pick(&r, WORKING, RR_MATTA);
    ReplyAct a = reply_activate(&r, WORKING, "m1", 0);
    CHECK(a.kind == RA_PUBLISH && a.rkind == RK_STOP);
    CHECK_STR(a.id, "m1");
    CHECK(r.drawer == RD_HIDDEN && reply_has_peek(&r, 0));
    reply_on_state(&r, "m1", "stopping", 10);
    CHECK(r.status == RS_STOPPING);
    reply_on_state(&r, "m1", "stopped", 20);
    CHECK(r.status == RS_STOPPED);
    reply_on_state(&r, "m1", "ignored", 30);
    CHECK(r.status == RS_IGNORED);
}

static void t_reply_matta_returns_queued_to_draft(void) {
    Reply r = queued("after the turn");
    pick(&r, WORKING, RR_MATTA);
    reply_activate(&r, WORKING, "m2", 0);
    CHECK_STR(r.draft, "after the turn"); /* the listener drops it; keep it for resending */
    CHECK(!reply_in_flight(&r));
}

static void t_reply_edit_queued(void) {
    Reply r = queued("old words");
    pick(&r, WORKING, RR_EDIT);
    ReplyAct a = reply_activate(&r, WORKING, "x", 0);
    CHECK(a.kind == RA_OPEN_IME && r.editing_sent);
    CHECK_STR(a.text, "old words");
    a = reply_ime_done(&r, "new words", true, "q2", 200);
    CHECK(a.kind == RA_PUBLISH && a.rkind == RK_TEXT);
    CHECK_STR(a.id, "q2");
    CHECK_STR(a.text, "new words");
    CHECK(r.status == RS_SENDING && !r.editing_sent && r.draft[0] == 0);

    r = queued("stay");
    pick(&r, WORKING, RR_EDIT);
    reply_activate(&r, WORKING, "x", 0);
    CHECK(reply_ime_done(&r, "ignored", false, "q3", 0).kind == RA_NONE);
    CHECK(r.status == RS_QUEUED);
    CHECK_STR(r.sent, "stay");
    CHECK_STR(r.id, "q1");
}

static void t_reply_discard_queued(void) {
    Reply r = queued("drop me");
    pick(&r, WORKING, RR_DISCARD);
    ReplyAct a = reply_activate(&r, WORKING, "x", 300);
    CHECK(a.kind == RA_PUBLISH && a.rkind == RK_CANCEL);
    CHECK_STR(a.id, "q1");
    reply_on_state(&r, "q1", "cancelled", 400);
    CHECK(r.status == RS_CANCELLED && r.draft[0] == 0 && !reply_in_flight(&r));
    CHECK(reply_layout(&r, WORKING).box == RB_NONE);
}

static void t_reply_discard_draft(void) {
    Reply r = fresh();
    set_draft(&r, "nope");
    reply_square(&r, IDLE, 0);
    pick(&r, IDLE, RR_DISCARD);
    CHECK(reply_activate(&r, IDLE, "x", 0).kind == RA_NONE);
    CHECK(r.draft[0] == 0 && r.drawer == RD_HIDDEN);
}

static void t_reply_sync_yields_to_request(void) {
    Reply r = fresh();
    set_draft(&r, "wait");
    reply_square(&r, IDLE, 0);
    reply_sync(&r, true, 0);
    CHECK(r.drawer == RD_PEEK && reply_has_peek(&r, 0));
    reply_sync(&r, false, 0);
    CHECK(r.drawer == RD_PEEK);
    r.draft[0] = 0;
    r.drawer = RD_OPEN;
    reply_sync(&r, true, 0);
    CHECK(r.drawer == RD_HIDDEN);
}

static void t_reply_new_reply_replaces(void) {
    Reply r = fresh();
    pick(&r, IDLE, RR_CONTINUE);
    reply_activate(&r, IDLE, "a", 0);
    pick(&r, IDLE, RR_VERIFY);
    reply_activate(&r, IDLE, "b", 10);
    CHECK_STR(r.id, "b");
    CHECK_STR(r.sent, "Verify your work further before you finish.");
    reply_on_state(&r, "a", "delivered", 20);
    CHECK(r.status == RS_SENDING);
}

static void t_reply_drag_steps(void) {
    int acc = 0;
    CHECK(reply_drag_steps(&acc, -30) == 0); /* finger moved right, not far enough yet */
    CHECK(reply_drag_steps(&acc, -40) == 1); /* 70 px right: one button right */
    CHECK(reply_drag_steps(&acc, -130) == 2);
    CHECK(reply_drag_steps(&acc, 200) == -3); /* finger moved left */
    acc = 0;
    CHECK(reply_drag_steps(&acc, 59) == 0 && reply_drag_steps(&acc, 1) == -1);
}

int main(void) {
    RUN(t_reply_layout);
    RUN(t_reply_move);
    RUN(t_reply_drag_steps);
    RUN(t_reply_square_cycle);
    RUN(t_reply_square_no_drawer);
    RUN(t_reply_preset_publishes);
    RUN(t_reply_activate_nothing_selected);
    RUN(t_reply_type_and_return);
    RUN(t_reply_send_draft);
    RUN(t_reply_timeout_withdraws);
    RUN(t_reply_any_state_stops_timer);
    RUN(t_reply_matta);
    RUN(t_reply_matta_returns_queued_to_draft);
    RUN(t_reply_edit_queued);
    RUN(t_reply_discard_queued);
    RUN(t_reply_discard_draft);
    RUN(t_reply_sync_yields_to_request);
    RUN(t_reply_new_reply_replaces);
    return TEST_RESULT();
}
