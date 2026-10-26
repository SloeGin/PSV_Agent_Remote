#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "input.h"
#include "model.h"

/* SELECT opens a list of every session: ✕ switches to it, □ asks to remove it
 * (a confirm dialog that defaults to Keep), ○ or SELECT closes the list. */
typedef struct {
    bool open;
    int cursor;
    bool confirming;
    int confirm_sel; /* 0 = Keep, 1 = Remove */
    char confirm_sid[64]; /* the session the dialog is about; indices can shift meanwhile */
    uint64_t confirm_at;
    char toast[64];
    uint64_t toast_until_ms;
} SessionList;

typedef enum { LIST_NONE, LIST_CLOSED, LIST_SWITCHED, LIST_REMOVED } ListResult;

void list_open(SessionList *l, const Model *m);
/* removed_sid (may be NULL) receives the removed session's id on LIST_REMOVED. */
ListResult list_handle(SessionList *l, Model *m, const Actions *a, uint64_t now, char removed_sid[64]);
