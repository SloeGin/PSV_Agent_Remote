#include "model.h"

#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void request_free(Request *r) {
    free(r->body);
    free(r->questions);
    r->body = NULL;
    r->questions = NULL;
    r->n_questions = 0;
}

void turn_free(TurnItem *items, int n) {
    for (int i = 0; i < n; i++) free(items[i].text);
    free(items);
}

static void free_session(Session *s) {
    for (int i = 0; i < s->n_reqs; i++) request_free(&s->reqs[i]);
    s->n_reqs = 0;
    turn_free(s->turn, s->n_turn);
    s->turn = NULL;
    s->n_turn = 0;
}

void model_init(Model *m, Mode mode) {
    memset(m, 0, sizeof *m);
    m->cur = -1;
    m->sel = -1;
    m->mode = mode;
}

void model_free(Model *m) {
    for (int i = 0; i < m->n; i++) free_session(&m->s[i]);
    m->n = 0;
    m->cur = -1;
}

void model_clear(Model *m) {
    model_free(m);
    m->sel = -1;
    m->shown_sid[0] = m->shown_rid[0] = 0;
    m->sheet_hidden = false;
}

static int find(const Model *m, const char *sid) {
    for (int i = 0; i < m->n; i++)
        if (!strcmp(m->s[i].sid, sid)) return i;
    return -1;
}

/* Reset the selection and lockout clock whenever a different page is shown;
 * a request that was not on screen before opens its sheet expanded. */
static void sync_view(Model *m, uint64_t now) {
    if (m->cur >= 0) m->s[m->cur].unseen = false; /* on screen counts as seen */
    const char *sid = m->cur >= 0 ? m->s[m->cur].sid : "";
    const Request *r = model_current_request(m);
    const char *rid = r ? r->rid : "";
    int step = r ? r->step : 0;
    bool new_request = strcmp(sid, m->shown_sid) || strcmp(rid, m->shown_rid);
    if (new_request || step != m->shown_step) {
        snprintf(m->shown_sid, sizeof m->shown_sid, "%s", sid);
        snprintf(m->shown_rid, sizeof m->shown_rid, "%s", rid);
        m->shown_step = step;
        m->shown_ms = now;
        m->sel = -1;
        if (new_request) m->sheet_hidden = false;
    }
}

static void remove_session(Model *m, int i) {
    free_session(&m->s[i]);
    memmove(&m->s[i], &m->s[i + 1], sizeof(Session) * (size_t)(m->n - i - 1));
    m->n--;
    if (m->n == 0) m->cur = -1;
    else if (m->cur > i || m->cur >= m->n) m->cur--;
}

/* Least recently updated session, preferring ones with nothing pending. */
static int eviction_victim(const Model *m) {
    int best = -1;
    for (int pass = 0; pass < 2 && best < 0; pass++)
        for (int i = 0; i < m->n; i++)
            if ((pass == 1 || m->s[i].n_reqs == 0) && (best < 0 || m->s[i].last_ms < m->s[best].last_ms))
                best = i;
    return best;
}

static int get_or_create(Model *m, const char *sid, SessState initial, uint64_t now, bool *created) {
    int i = find(m, sid);
    *created = i < 0;
    if (i >= 0) return i;
    if (m->n == MAX_SESSIONS) remove_session(m, eviction_victim(m));
    i = m->n++;
    Session *s = &m->s[i];
    memset(s, 0, sizeof *s);
    snprintf(s->sid, sizeof s->sid, "%s", sid);
    snprintf(s->project, sizeof s->project, "?");
    reply_init(&s->reply);
    s->state = initial;
    s->last_ms = now;
    if (m->cur < 0) m->cur = i;
    return i;
}

unsigned model_on_status(Model *m, const char *sid, const StatusMsg *st, uint64_t now) {
    if (!st) {
        int i = find(m, sid);
        if (i >= 0) remove_session(m, i);
        sync_view(m, now);
        return 0;
    }
    bool created;
    int i = get_or_create(m, sid, st->state, now, &created);
    Session *s = &m->s[i];
    unsigned ev = (!created && s->state != ST_IDLE && st->state == ST_IDLE) ? MEV_IDLE : 0;
    /* Not for a session first seen already waiting: that is a retained status on (re)connect. */
    if (!created && s->state != ST_WAITING && st->state == ST_WAITING) ev |= MEV_WAITING;
    if (st->state == ST_IDLE && (created || s->state != ST_IDLE)) s->idle_since_ms = now;
    if (ev & MEV_IDLE) s->unseen = true;
    else if (st->state != ST_IDLE) s->unseen = false;
    s->state = st->state;
    s->agent = st->agent;
    s->capabilities_present = st->capabilities_present;
    memcpy(s->capabilities, st->capabilities, sizeof s->capabilities);
    s->reply.allow_text = session_capability(s, CAP_REPLY) == CAP_SUPPORTED;
    s->reply.allow_stop = session_capability(s, CAP_STOP) == CAP_SUPPORTED;
    if (st->project[0]) snprintf(s->project, sizeof s->project, "%s", st->project);
    if (st->cwd[0]) snprintf(s->cwd, sizeof s->cwd, "%s", st->cwd);
    snprintf(s->summary, sizeof s->summary, "%s", st->summary);
    s->summary_rev++;
    s->last_ms = now;
    sync_view(m, now);
    return ev;
}

unsigned model_on_request(Model *m, const char *sid, const char *rid, Request *rq, uint64_t now) {
    if (!rq) {
        int i = find(m, sid);
        if (i < 0) return 0;
        Session *s = &m->s[i];
        for (int k = 0; k < s->n_reqs; k++) {
            if (strcmp(s->reqs[k].rid, rid)) continue;
            request_free(&s->reqs[k]);
            memmove(&s->reqs[k], &s->reqs[k + 1], sizeof(Request) * (size_t)(s->n_reqs - k - 1));
            s->n_reqs--;
            break;
        }
        sync_view(m, now);
        return 0;
    }

    Session *cur = m->cur >= 0 ? &m->s[m->cur] : NULL;
    bool cur_busy = cur && cur->n_reqs > 0;
    bool created;
    int i = get_or_create(m, sid, ST_WAITING, now, &created);
    Session *s = &m->s[i];
    s->last_ms = now;
    snprintf(rq->rid, sizeof rq->rid, "%s", rid);

    for (int k = 0; k < s->n_reqs; k++) {
        if (strcmp(s->reqs[k].rid, rid)) continue;
        /* Re-delivered (e.g. retained after reconnect): keep what is on screen,
         * including the expiry clock and any answers in progress. */
        request_free(rq);
        sync_view(m, now);
        return 0;
    }

    if (s->n_reqs == MAX_REQS) {
        request_free(&s->reqs[0]);
        memmove(&s->reqs[0], &s->reqs[1], sizeof(Request) * (MAX_REQS - 1));
        s->n_reqs--;
    }
    rq->recv_ms = now;
    rq->sent_choice[0] = '\0';
    s->reqs[s->n_reqs++] = *rq;

    if (!cur_busy) m->cur = i;
    else if (m->cur != i) s->flash = true;
    sync_view(m, now);
    return MEV_NEW_REQUEST;
}

void model_on_turn(Model *m, const char *sid, TurnItem *items, int n, uint64_t now) {
    int i = find(m, sid);
    if (i < 0 && !items) return;
    bool created;
    if (i < 0) i = get_or_create(m, sid, ST_RUNNING, now, &created);
    Session *s = &m->s[i];
    turn_free(s->turn, s->n_turn);
    s->turn = items;
    s->n_turn = items ? n : 0;
    s->turn_rev++;
    s->last_ms = now;
    sync_view(m, now);
}

void model_tick(Model *m, uint64_t now) {
    for (int i = m->n - 1; i >= 0; i--)
        if (m->s[i].n_reqs == 0 && now - m->s[i].last_ms > HIDE_AFTER_MS) remove_session(m, i);
    sync_view(m, now);
}

Session *model_current(Model *m) {
    return m->cur >= 0 ? &m->s[m->cur] : NULL;
}

Session *model_find(Model *m, const char *sid) {
    int i = find(m, sid);
    return i >= 0 ? &m->s[i] : NULL;
}

const char *agent_name(AgentKind agent) {
    return agent == AGENT_CODEX ? "Codex" : agent == AGENT_CLAUDE ? "Claude Code" :
           agent == AGENT_KIRO ? "Kiro" : "Agent";
}

Capability session_capability(const Session *s, CapabilityKind kind) {
    if (!s || (unsigned)kind >= CAP_COUNT) return CAP_UNKNOWN;
    if (!s->capabilities_present && s->agent == AGENT_CLAUDE) return CAP_SUPPORTED;
    return s->capabilities[kind];
}

bool model_reply_idle(const Session *s) {
    return s->n_reqs == 0 && s->state == ST_IDLE && s->reply.allow_text;
}

bool model_reply_working(const Session *s) {
    return s->n_reqs == 0 && (s->state == ST_RUNNING || s->state == ST_WAITING) &&
           (s->reply.allow_text || s->reply.allow_stop);
}

const Request *model_current_request(const Model *m) {
    if (m->cur < 0 || m->s[m->cur].n_reqs == 0) return NULL;
    return &m->s[m->cur].reqs[0];
}

int model_pending_count(const Model *m) {
    return m->cur >= 0 ? m->s[m->cur].n_reqs : 0;
}

bool model_request_expired(const Request *r, uint64_t now) {
    return now > r->recv_ms + (uint64_t)r->timeout_s * 1000;
}

void model_switch(Model *m, int dir, uint64_t now) {
    if (m->n == 0 || dir == 0) return;
    m->cur = ((m->cur + (dir > 0 ? 1 : -1)) % m->n + m->n) % m->n;
    m->s[m->cur].flash = false;
    sync_view(m, now);
}

void model_select(Model *m, int idx, uint64_t now) {
    if (idx < 0 || idx >= m->n) return;
    m->cur = idx;
    m->s[idx].flash = false;
    sync_view(m, now);
}

void model_remove_at(Model *m, int idx) {
    if (idx >= 0 && idx < m->n) remove_session(m, idx);
    if (m->cur >= 0) m->s[m->cur].unseen = false;
}

/* ---------- rows on the sheet ---------- */

int model_row_count(const Request *r) {
    if (!r) return 0;
    if (r->kind == REQ_APPROVAL) return r->n_opts;
    if (r->step >= r->n_questions) return 3; /* Send, Change, Answer on Mac */
    const Question *q = &r->questions[r->step];
    return q->n_opts + (!q->no_other ? 1 : 0) + (q->multi ? 1 : 0) + 1;
}

QRow model_row_kind(const Request *r, int row, int *option) {
    *option = -1;
    if (r->kind == REQ_APPROVAL) {
        *option = row;
        return ROW_OPTION;
    }
    if (r->step >= r->n_questions) return row == 0 ? ROW_SEND : row == 1 ? ROW_CHANGE : ROW_MAC;
    const Question *q = &r->questions[r->step];
    if (row < q->n_opts) {
        *option = row;
        return ROW_OPTION;
    }
    row -= q->n_opts;
    if (!q->no_other) {
        if (row == 0) return ROW_OTHER;
        row--;
    }
    if (q->multi && row == 0) return ROW_NEXT;
    return ROW_MAC;
}

static Request *live_request(Model *m, uint64_t now) {
    if (m->cur < 0 || m->s[m->cur].n_reqs == 0 || m->sheet_hidden) return NULL;
    Request *r = &m->s[m->cur].reqs[0];
    if (r->sent_choice[0] || model_request_expired(r, now)) return NULL;
    return r;
}

void model_move_sel(Model *m, int dir, uint64_t now) {
    sync_view(m, now);
    Request *r = live_request(m, now);
    int n = model_row_count(r);
    if (!r || n == 0 || dir == 0) return;
    if (m->sel < 0) m->sel = dir > 0 ? 0 : n - 1;
    else m->sel += dir > 0 ? 1 : -1;
    if (m->sel < 0) m->sel = 0;
    if (m->sel >= n) m->sel = n - 1;
}

void model_set_sel(Model *m, int idx) {
    const Request *r = model_current_request(m);
    if (idx == -1 || (r && !m->sheet_hidden && idx >= 0 && idx < model_row_count(r))) m->sel = idx;
}

void model_toggle_sheet(Model *m, uint64_t now) {
    if (!model_current_request(m)) return;
    m->sheet_hidden = !m->sheet_hidden;
    m->sel = -1;
    if (!m->sheet_hidden) m->shown_ms = now;
}

/* ---------- ✕ / ○ ---------- */

static void fill_outcome(Model *m, const Request *r, OutcomeKind kind, Outcome *out) {
    out->kind = kind;
    snprintf(out->sid, sizeof out->sid, "%s", m->s[m->cur].sid);
    snprintf(out->rid, sizeof out->rid, "%s", r->rid);
    snprintf(out->choice, sizeof out->choice, "%s", r->sent_choice);
}

static void next_page(Request *r) {
    if (r->step < r->n_questions) r->step++;
}

void model_activate(Model *m, uint64_t now, Outcome *out) {
    memset(out, 0, sizeof *out);
    sync_view(m, now);
    Request *r = live_request(m, now);
    if (!r) return;
    if (m->sel < 0) {
        out->kind = OUT_PICK_FIRST;
        return;
    }
    if (now - m->shown_ms < LOCKOUT_MS) return;
    int opt;
    QRow row = model_row_kind(r, m->sel, &opt);
    if (r->kind == REQ_APPROVAL) {
        if (!strcmp(r->opts[opt].id, "deny_note")) {
            fill_outcome(m, r, OUT_NEED_NOTE, out);
            return;
        }
        memcpy(r->sent_choice, r->opts[opt].id, sizeof r->sent_choice);
        fill_outcome(m, r, OUT_SEND_CHOICE, out);
        return;
    }
    int q = r->step;
    switch (row) {
    case ROW_OPTION:
        if (r->questions[q].multi) {
            r->picked[q] ^= (uint8_t)(1u << opt);
        } else {
            r->picked[q] = (uint8_t)(1u << opt);
            r->other[q][0] = '\0';
            next_page(r);
        }
        break;
    case ROW_OTHER:
        fill_outcome(m, r, OUT_NEED_OTHER, out);
        return;
    case ROW_NEXT:
        if (!r->picked[q] && !r->other[q][0]) {
            out->kind = OUT_PICK_FIRST;
            return;
        }
        next_page(r);
        break;
    case ROW_MAC:
        snprintf(r->sent_choice, sizeof r->sent_choice, "mac");
        fill_outcome(m, r, OUT_SEND_MAC, out);
        return;
    case ROW_SEND:
        snprintf(r->sent_choice, sizeof r->sent_choice, "answers");
        fill_outcome(m, r, OUT_SEND_ANSWERS, out);
        return;
    case ROW_CHANGE:
        r->step = r->n_questions - 1;
        break;
    }
    sync_view(m, now);
}

/* The live request on screen, if it is (sid, rid). */
static Request *same_request(Model *m, const char *sid, const char *rid, uint64_t now) {
    Request *r = live_request(m, now);
    if (!r || strcmp(m->s[m->cur].sid, sid) || strcmp(r->rid, rid)) return NULL;
    return r;
}

bool model_submit_note(Model *m, const char *sid, const char *rid, uint64_t now, Outcome *out) {
    memset(out, 0, sizeof *out);
    Request *r = same_request(m, sid, rid, now);
    if (!r || r->kind != REQ_APPROVAL) return false;
    snprintf(r->sent_choice, sizeof r->sent_choice, "deny_note");
    fill_outcome(m, r, OUT_SEND_CHOICE, out);
    return true;
}

void model_submit_other(Model *m, const char *sid, const char *rid, const char *text, uint64_t now) {
    Request *r = same_request(m, sid, rid, now);
    if (!r || r->kind != REQ_QUESTION || r->step >= r->n_questions || !text[0]) return;
    int q = r->step;
    if (r->questions[q].no_other) return;
    utf8_copy(r->other[q], OTHER_BYTES, text, strlen(text));
    if (!r->questions[q].multi) {
        r->picked[q] = 0;
        next_page(r);
    }
    sync_view(m, now);
}

void model_back(Model *m, uint64_t now) {
    Request *r = live_request(m, now);
    if (!r || r->kind != REQ_QUESTION || r->step == 0) return;
    r->step--;
    sync_view(m, now);
}

Mode model_toggle_mode(Model *m) {
    m->mode = m->mode == MODE_SOFA ? MODE_DESK : MODE_SOFA;
    return m->mode;
}
