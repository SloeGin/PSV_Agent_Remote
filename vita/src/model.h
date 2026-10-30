#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "config.h"
#include "reply.h"

typedef enum { ST_UNKNOWN, ST_RUNNING, ST_WAITING, ST_IDLE } SessState;
typedef enum { AGENT_CLAUDE, AGENT_CODEX, AGENT_KIRO, AGENT_UNKNOWN } AgentKind;
typedef enum { CAP_UNKNOWN, CAP_SUPPORTED, CAP_UNSUPPORTED } Capability;
typedef enum { CAP_APPROVAL, CAP_QUESTIONS, CAP_REPLY, CAP_STOP, CAP_COUNT } CapabilityKind;

/* An approval option ("allow", "deny_note", ...). */
typedef struct {
    char id[16];
    char label[48];
} Option;

#define MAX_QUESTIONS 4
#define MAX_QOPTS 4
#define OTHER_BYTES 640 /* 200 keyboard characters of UTF-8 */

typedef struct {
    char label[64];
    char desc[160];
} QOption;

typedef struct {
    char header[32];
    char question[512];
    QOption opts[MAX_QOPTS];
    int n_opts;
    bool multi;
    bool no_other; /* absent allow_other remains compatible with old Claude requests */
} Question;

typedef enum { REQ_APPROVAL, REQ_QUESTION } ReqKind;

typedef struct {
    ReqKind kind;
    char rid[24];
    char tool[64];
    char title[128];
    char cwd[160];
    char *body;           /* approval text; malloc'd, owned by the Request */
    Option opts[5];       /* approval options */
    int n_opts;
    Question *questions;  /* question kind; malloc'd array, owned by the Request */
    int n_questions;
    int timeout_s;
    uint64_t recv_ms;
    char sent_choice[16]; /* approval choice, "answers" or "mac" once sent */
    /* question flow: step == n_questions is the review page */
    int step;
    uint8_t picked[MAX_QUESTIONS]; /* bit i = option i chosen */
    char other[MAX_QUESTIONS][OTHER_BYTES];
} Request;

void request_free(Request *r);

/* Room for the hook's 4000-character last message in UTF-8. */
#define SUMMARY_BYTES 12800

typedef struct {
    SessState state;
    AgentKind agent;
    bool capabilities_present;
    Capability capabilities[CAP_COUNT];
    char project[64];
    char cwd[160];
    char summary[SUMMARY_BYTES];
} StatusMsg;

typedef enum { TURN_YOU, TURN_ASSISTANT, TURN_TOOL, TURN_NOTE } TurnKind;
#define TURN_CLAUDE TURN_ASSISTANT /* legacy source compatibility */

typedef struct {
    TurnKind kind;
    char tool[32];
    char *text; /* malloc'd */
} TurnItem;

#define MAX_TURN_ITEMS 64

void turn_free(TurnItem *items, int n);

#define MAX_SESSIONS 16
#define MAX_REQS 8
#define LOCKOUT_MS 500
#define HIDE_AFTER_MS (12ULL * 3600 * 1000)

enum { MEV_NEW_REQUEST = 1, MEV_IDLE = 2, MEV_WAITING = 4 }; /* WAITING: a session started waiting on a decision */

typedef struct {
    char sid[64];
    char project[64];
    char cwd[160];
    SessState state;
    AgentKind agent;
    bool capabilities_present;
    Capability capabilities[CAP_COUNT];
    char summary[SUMMARY_BYTES];
    unsigned summary_rev; /* bumped on every status update, for render caches */
    TurnItem *turn;       /* this turn's conversation; malloc'd array */
    int n_turn;
    unsigned turn_rev;
    uint64_t idle_since_ms;
    uint64_t last_ms;
    bool flash;
    bool unseen; /* finished while another tab was on screen, not looked at since */
    Request reqs[MAX_REQS]; /* oldest first */
    int n_reqs;
    Reply reply; /* the reply drawer for this session */
} Session;

typedef struct {
    Session s[MAX_SESSIONS];
    int n;
    int cur; /* -1 when there are no sessions */
    int sel; /* -1 when no row is highlighted */
    uint64_t shown_ms;
    char shown_sid[64];
    char shown_rid[24];
    int shown_step;
    bool sheet_hidden; /* the decision sheet is collapsed to its one-line peek */
    Mode mode;
} Model;

void model_init(Model *m, Mode mode);
void model_free(Model *m);
/* Forget every session (the broker changed); the model stays usable. */
void model_clear(Model *m);

/* st == NULL clears (removes) the session. Returns MEV_* bits. */
unsigned model_on_status(Model *m, const char *sid, const StatusMsg *st, uint64_t now);

/* rq == NULL clears that request. Otherwise takes ownership of rq's allocations. */
unsigned model_on_request(Model *m, const char *sid, const char *rid, Request *rq, uint64_t now);

/* Replaces the session's turn; takes ownership of items (NULL/0 clears it). */
void model_on_turn(Model *m, const char *sid, TurnItem *items, int n, uint64_t now);

void model_tick(Model *m, uint64_t now);

Session *model_current(Model *m);
Session *model_find(Model *m, const char *sid);
/* Which reply drawer a session gets; both false while a decision is pending or the state is unknown. */
bool model_reply_idle(const Session *s);
bool model_reply_working(const Session *s);
Capability session_capability(const Session *s, CapabilityKind kind);
const char *agent_name(AgentKind agent);
const Request *model_current_request(const Model *m);
int model_pending_count(const Model *m);
bool model_request_expired(const Request *r, uint64_t now);

void model_switch(Model *m, int dir, uint64_t now);
void model_select(Model *m, int idx, uint64_t now);
void model_remove_at(Model *m, int idx);

/* Rows the highlight moves over for the request on screen. */
typedef enum { ROW_OPTION, ROW_OTHER, ROW_NEXT, ROW_MAC, ROW_SEND, ROW_CHANGE } QRow;
int model_row_count(const Request *r);
QRow model_row_kind(const Request *r, int row, int *option);

void model_move_sel(Model *m, int dir, uint64_t now);
void model_set_sel(Model *m, int idx);

/* □: collapse or expand the decision sheet. Expanding re-arms the lockout. */
void model_toggle_sheet(Model *m, uint64_t now);

typedef enum {
    OUT_NONE,
    OUT_SEND_CHOICE,  /* approval: send choice (deny_note carries the note main collected) */
    OUT_SEND_ANSWERS, /* question: send r->picked / r->other */
    OUT_SEND_MAC,     /* question: let the Mac ask instead */
    OUT_NEED_NOTE,    /* open the keyboard for a deny note, then call model_submit_note */
    OUT_NEED_OTHER,   /* open the keyboard for "Other…", then call model_submit_other */
    OUT_PICK_FIRST,   /* ✕ with nothing highlighted / nothing picked */
} OutcomeKind;

typedef struct {
    OutcomeKind kind;
    char sid[64];
    char rid[24];
    char choice[16];
} Outcome;

/* ✕ on the sheet. Ignored while the sheet is hidden, the request is expired or
 * already answered, or within LOCKOUT_MS of the current page appearing. */
void model_activate(Model *m, uint64_t now, Outcome *out);
/* After the keyboard closes. Both only act if (sid, rid) — taken from the
 * OUT_NEED_* outcome — is still the request on screen, so text typed for a
 * request that went away while the keyboard was open is dropped. */
bool model_submit_note(Model *m, const char *sid, const char *rid, uint64_t now, Outcome *out);
/* Stores the "Other…" text; single-choice questions move on to the next page. */
void model_submit_other(Model *m, const char *sid, const char *rid, const char *text, uint64_t now);
/* ○ on a question: previous page. */
void model_back(Model *m, uint64_t now);

Mode model_toggle_mode(Model *m);
