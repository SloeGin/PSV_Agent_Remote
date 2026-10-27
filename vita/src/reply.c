#include "reply.h"

#include <stdio.h>
#include <string.h>

const char *const REPLY_PRESET_CONTINUE = "Continue.";
const char *const REPLY_PRESET_VERIFY = "Verify your work further before you finish.";

void reply_init(Reply *r) {
    memset(r, 0, sizeof *r);
    r->allow_text = r->allow_stop = true;
    r->line = -1;
}

bool reply_in_flight(const Reply *r) {
    return r->kind == RK_TEXT && (r->status == RS_SENDING || r->status == RS_QUEUED);
}

static bool final_state(ReplyStatus s) {
    return s == RS_DELIVERED || s == RS_STOPPED || s == RS_IGNORED || s == RS_CANCELLED;
}

bool reply_has_peek(const Reply *r, uint64_t now) {
    if (r->draft[0] || r->status == RS_SENDING || r->status == RS_QUEUED || r->status == RS_STOPPING ||
        r->status == RS_FAILED)
        return true;
    return final_state(r->status) && now - r->status_ms < REPLY_FADE_MS;
}

ReplyLayout reply_layout(const Reply *r, bool idle, bool working) {
    ReplyLayout l;
    memset(&l, 0, sizeof l);
    if (!idle && !working) return l;
    if (r->allow_text && r->draft[0]) {
        l.box = RB_DRAFT;
        l.box_items[l.n_box++] = RR_SEND;
        l.box_items[l.n_box++] = RR_EDIT;
        l.box_items[l.n_box++] = RR_DISCARD;
    } else if (r->allow_text && reply_in_flight(r) && r->sent_typed) {
        l.box = RB_QUEUED;
        l.box_items[l.n_box++] = RR_EDIT;
        l.box_items[l.n_box++] = RR_DISCARD;
    }
    if (idle && r->allow_text) {
        l.rows[l.n_rows++] = RR_CONTINUE;
        l.rows[l.n_rows++] = RR_VERIFY;
    } else if (working && r->allow_stop) {
        l.rows[l.n_rows++] = RR_MATTA;
    }
    if (r->allow_text && l.box == RB_NONE) l.rows[l.n_rows++] = RR_TYPE;
    return l;
}

static int n_lines(const ReplyLayout *l) { return l->n_rows + (l->box != RB_NONE ? 1 : 0); }

bool reply_selected(const Reply *r, const ReplyLayout *l, ReplyItem *out) {
    if (r->line < 0 || r->line >= n_lines(l)) return false;
    if (r->line < l->n_rows) *out = l->rows[r->line];
    else if (r->col >= 0 && r->col < l->n_box) *out = l->box_items[r->col];
    else return false;
    return true;
}

void reply_move(Reply *r, const ReplyLayout *l, int dy, int dx) {
    int n = n_lines(l);
    if (n == 0) return;
    if (dy) {
        int line = r->line < 0 ? (dy > 0 ? 0 : n - 1) : r->line + dy;
        if (line < 0) line = 0;
        if (line >= n) line = n - 1;
        if (line != r->line) r->col = 0;
        r->line = line;
    }
    if (dx && r->line == l->n_rows && l->n_box > 0) {
        int col = r->col + dx;
        r->col = col < 0 ? 0 : col >= l->n_box ? l->n_box - 1 : col;
    }
}

void reply_select(Reply *r, int line, int col) {
    r->line = line;
    r->col = col;
}

/* Close the drawer, leaving a peek while a draft or a typed reply is pending. */
static void fold(Reply *r) {
    r->drawer = r->draft[0] || (reply_in_flight(r) && r->sent_typed) ? RD_PEEK : RD_HIDDEN;
}

bool reply_square(Reply *r, bool idle, bool working, uint64_t now) {
    (void)now;
    if (!idle && !working) return false;
    if (r->drawer == RD_OPEN) {
        fold(r);
    } else {
        r->drawer = RD_OPEN;
        r->line = -1;
        r->col = 0;
    }
    return true;
}

void reply_back(Reply *r, uint64_t now) {
    (void)now;
    if (r->drawer == RD_OPEN) fold(r);
}

static void set_status(Reply *r, ReplyStatus s, uint64_t now) {
    r->status = s;
    r->status_ms = now;
}

/* Start tracking a new reply; it replaces whatever was in flight. */
static ReplyAct send(Reply *r, ReplyKind kind, const char *text, bool typed, const char *new_id, uint64_t now) {
    ReplyAct a = {RA_PUBLISH, kind, "", NULL};
    snprintf(r->id, sizeof r->id, "%s", new_id);
    r->kind = kind;
    if (text != r->sent) snprintf(r->sent, sizeof r->sent, "%s", text ? text : "");
    r->sent_typed = typed;
    r->acked = false;
    set_status(r, RS_SENDING, now);
    snprintf(a.id, sizeof a.id, "%s", r->id);
    a.text = r->sent;
    return a;
}

ReplyAct reply_activate(Reply *r, bool idle, bool working, const char *new_id, uint64_t now) {
    ReplyAct none = {RA_NONE, RK_TEXT, "", NULL};
    ReplyLayout l = reply_layout(r, idle, working);
    ReplyItem it;
    if (!reply_selected(r, &l, &it)) return none;
    ReplyAct a = none;
    switch (it) {
    case RR_CONTINUE:
    case RR_VERIFY:
        a = send(r, RK_TEXT, it == RR_CONTINUE ? REPLY_PRESET_CONTINUE : REPLY_PRESET_VERIFY, false, new_id, now);
        fold(r);
        break;
    case RR_TYPE:
        r->editing_sent = false;
        a.kind = RA_OPEN_IME;
        a.text = r->draft;
        break;
    case RR_EDIT:
        r->editing_sent = l.box == RB_QUEUED;
        a.kind = RA_OPEN_IME;
        a.text = r->editing_sent ? r->sent : r->draft;
        break;
    case RR_SEND: {
        char text[REPLY_BYTES];
        snprintf(text, sizeof text, "%s", r->draft);
        r->draft[0] = 0;
        a = send(r, RK_TEXT, text, true, new_id, now);
        fold(r);
        break;
    }
    case RR_DISCARD:
        if (l.box == RB_QUEUED) {
            a.kind = RA_PUBLISH;
            a.rkind = RK_CANCEL;
            snprintf(a.id, sizeof a.id, "%s", r->id);
            r->sent_typed = false;
            set_status(r, RS_CANCELLED, now);
        } else {
            r->draft[0] = 0;
        }
        fold(r);
        break;
    case RR_MATTA:
        /* The listener drops a queued reply when it stops the agent; keep the words. */
        if (reply_in_flight(r) && r->sent_typed && !r->draft[0]) snprintf(r->draft, sizeof r->draft, "%s", r->sent);
        a = send(r, RK_STOP, "", false, new_id, now);
        fold(r);
        break;
    }
    return a;
}

ReplyAct reply_ime_done(Reply *r, const char *text, bool entered, const char *new_id, uint64_t now) {
    ReplyAct none = {RA_NONE, RK_TEXT, "", NULL};
    bool editing_sent = r->editing_sent;
    r->editing_sent = false;
    if (!entered) return none;
    if (!r->allow_text) {
        if (!r->draft[0]) snprintf(r->draft, sizeof r->draft, "%s", text);
        return none;
    }
    if (editing_sent) {
        if (!text[0] || !reply_in_flight(r)) return none;
        return send(r, RK_TEXT, text, true, new_id, now); /* the listener keeps only the newest */
    }
    snprintf(r->draft, sizeof r->draft, "%s", text);
    return none;
}

void reply_on_state(Reply *r, const char *id, const char *state, uint64_t now) {
    if (!r->id[0] || strcmp(id, r->id)) return;
    if (strcmp(state, "received") && strcmp(state, "queued") && strcmp(state, "delivered") &&
        strcmp(state, "stopping") && strcmp(state, "stopped") && strcmp(state, "ignored") &&
        strcmp(state, "cancelled") && strcmp(state, "failed")) return;
    r->acked = true;
    if (!strcmp(state, "queued")) set_status(r, RS_QUEUED, now);
    else if (!strcmp(state, "delivered")) set_status(r, RS_DELIVERED, now);
    else if (!strcmp(state, "stopping")) set_status(r, RS_STOPPING, now);
    else if (!strcmp(state, "stopped")) set_status(r, RS_STOPPED, now);
    else if (!strcmp(state, "ignored")) set_status(r, RS_IGNORED, now);
    else if (!strcmp(state, "cancelled")) set_status(r, RS_CANCELLED, now);
    else if (!strcmp(state, "failed")) {
        set_status(r, RS_FAILED, now);
        if (r->sent_typed && !r->draft[0]) snprintf(r->draft, sizeof r->draft, "%s", r->sent);
    }
}

ReplyAct reply_tick(Reply *r, uint64_t now) {
    ReplyAct a = {RA_NONE, RK_TEXT, "", NULL};
    if (r->status == RS_SENDING && !r->acked && now - r->status_ms > REPLY_ACK_MS) {
        set_status(r, RS_FAILED, now);
        if (r->sent_typed && !r->draft[0]) snprintf(r->draft, sizeof r->draft, "%s", r->sent);
        a.kind = RA_WITHDRAW;
    } else if (final_state(r->status) && now - r->status_ms >= REPLY_FADE_MS) {
        set_status(r, RS_NONE, now);
    }
    return a;
}

void reply_sync(Reply *r, bool request_pending, uint64_t now) {
    (void)now;
    if (request_pending && r->drawer == RD_OPEN) fold(r);
}
