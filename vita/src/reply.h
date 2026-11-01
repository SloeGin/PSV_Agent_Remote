#pragma once

/* Replies from the PSV to a session: presets, typed text, 待った!. Kept free of
 * other project headers so model.h and protocol.h can both include it.
 *
 * Each session has a reply drawer: a vertical list of rows, and under them a
 * box holding a draft (Send | Edit | Discard) or a typed reply that is waiting
 * to be delivered (Edit | Discard). The listener on the computer reports each
 * reply's progress, which this module turns into what the drawer shows. */

#include <stdbool.h>
#include <stdint.h>

typedef enum { RK_TEXT, RK_STOP, RK_CANCEL } ReplyKind;

#define REPLY_BYTES 2048         /* a voice transcript can outgrow the keyboard */
#define REPLY_KEYBOARD_CHARS 200 /* what the keyboard can hold, for Edit */
#define REPLY_VOICE_TIMEOUT_MS 30000 /* released, and no transcript by then */
#define REPLY_ACK_MS 10000       /* no word from the listener by then: not delivered */
#define REPLY_FADE_MS 3000       /* how long a final state stays on the peek */

typedef enum { RD_HIDDEN, RD_OPEN, RD_PEEK } ReplyDrawer;
typedef enum {
    RS_NONE, RS_SENDING, RS_QUEUED, RS_DELIVERED, RS_STOPPING, RS_STOPPED,
    RS_IGNORED, RS_CANCELLED, RS_FAILED
} ReplyStatus;
typedef enum { RR_CONTINUE, RR_VERIFY, RR_TYPE, RR_SPEAK, RR_MATTA, RR_SEND, RR_EDIT, RR_DISCARD } ReplyItem;
typedef enum { VS_NONE, VS_RECORDING, VS_TRANSCRIBING } VoiceStatus;
typedef enum { VO_IGNORED, VO_ADDED, VO_HELD, VO_FULL, VO_ERROR } VoiceOutcome;
typedef enum { RB_NONE, RB_DRAFT, RB_QUEUED } ReplyBox;

typedef struct {
    ReplyItem rows[4];
    int n_rows;            /* the vertical list, top to bottom */
    ReplyBox box;          /* the box under the rows, if any */
    ReplyItem box_items[4];
    int n_box;             /* its buttons, left to right */
} ReplyLayout;

typedef struct {
    char draft[REPLY_BYTES];
    bool allow_text, allow_stop; /* resolved session capabilities */
    char sent[REPLY_BYTES]; /* text of the reply in flight */
    bool sent_typed;        /* typed, not a preset: it goes back to the draft if it is not delivered */
    char id[24];
    ReplyKind kind;
    ReplyStatus status;
    bool acked;             /* the listener has answered for this id */
    uint64_t status_ms;
    ReplyDrawer drawer;
    bool editing_sent;      /* the keyboard is open on the queued reply */
    int line, col;          /* highlight: line = row index, or n_rows for the box; -1 = nothing */
    VoiceStatus voice;      /* Speak: recording, or waiting for the transcript */
    char voice_id[24];      /* the recording's id; results for any other are ignored */
    uint64_t voice_ms;      /* recording started, or (transcribing) released */
    char voice_held[REPLY_BYTES]; /* a transcript that arrived while the keyboard was open */
} Reply;

typedef enum { RA_NONE, RA_PUBLISH, RA_OPEN_IME, RA_WITHDRAW, RA_START_VOICE, RA_TOAST } ReplyActKind;
typedef struct {
    ReplyActKind kind;
    ReplyKind rkind;  /* RA_PUBLISH */
    char id[24];      /* RA_PUBLISH, RA_START_VOICE (the recording's id) */
    const char *text; /* RA_PUBLISH (RK_TEXT) and RA_OPEN_IME point into the Reply; RA_TOAST is static */
} ReplyAct;

extern const char *const REPLY_PRESET_CONTINUE;
extern const char *const REPLY_PRESET_VERIFY;

void reply_init(Reply *r);
/* idle/working describe the session; neither (unknown, or a decision pending) means no drawer. */
ReplyLayout reply_layout(const Reply *r, bool idle, bool working);
bool reply_selected(const Reply *r, const ReplyLayout *l, ReplyItem *out);
bool reply_in_flight(const Reply *r); /* a text reply sent and not yet delivered */
bool reply_has_peek(const Reply *r, uint64_t now);

/* □: open, or fold/close. Returns false when this session has no drawer. */
bool reply_square(Reply *r, bool idle, bool working, uint64_t now);
void reply_back(Reply *r, uint64_t now); /* ○ while open: same as the closing □ */
void reply_move(Reply *r, const ReplyLayout *l, int dy, int dx);
void reply_select(Reply *r, int line, int col);

/* ✕ on the highlighted button. new_id names the reply if this sends one. */
ReplyAct reply_activate(Reply *r, bool idle, bool working, const char *new_id, uint64_t now);
/* The keyboard closed; entered is false when it was cancelled. */
ReplyAct reply_ime_done(Reply *r, const char *text, bool entered, const char *new_id, uint64_t now);
void reply_on_state(Reply *r, const char *id, const char *state, uint64_t now);
/* Timers. RA_WITHDRAW: nothing answered in time; clear the retained reply. */
ReplyAct reply_tick(Reply *r, uint64_t now);
/* A decision on this session folds an open drawer out of its way. */
void reply_sync(Reply *r, bool request_pending, uint64_t now);

/* Voice. RR_SPEAK starts recording (RA_START_VOICE); the caller records while ✕
 * is held and then stops: send = false discards it, true waits for the text. */
void reply_voice_stop(Reply *r, bool send, uint64_t now);
/* A transcript (or error) for id. keyboard_open: the keyboard is on this draft,
 * so the text is held until reply_voice_flush. idle/working place the highlight. */
VoiceOutcome reply_on_voice(Reply *r, const char *id, const char *text, bool is_error, bool keyboard_open,
                            bool idle, bool working, uint64_t now);
VoiceOutcome reply_voice_flush(Reply *r, bool idle, bool working);
/* True once when the transcript has not come within REPLY_VOICE_TIMEOUT_MS. */
bool reply_voice_timed_out(Reply *r, uint64_t now);
