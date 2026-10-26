#include "sessionlist.h"

#include <stdio.h>
#include <string.h>

#define LIST_TOAST_MS 2000

void list_open(SessionList *l, const Model *m) {
    memset(l, 0, sizeof *l);
    l->open = true;
    l->cursor = m->cur > 0 ? m->cur : 0;
}

ListResult list_handle(SessionList *l, Model *m, const Actions *a, uint64_t now, char removed_sid[64]) {
    if (l->cursor >= m->n) l->cursor = m->n > 0 ? m->n - 1 : 0;
    int lines = actions_cursor_lines(a);
    int step = lines ? lines : a->sel_dir;

    if (l->confirming) {
        if (a->back) l->confirming = false;
        else if (a->sel_dir) l->confirm_sel = !l->confirm_sel; /* Keep / Remove sit side by side */
        else if (a->confirm && now - l->confirm_at >= LOCKOUT_MS) {
            l->confirming = false;
            if (l->confirm_sel != 1) return LIST_NONE;
            int idx = -1;
            for (int i = 0; i < m->n; i++)
                if (!strcmp(m->s[i].sid, l->confirm_sid)) idx = i;
            if (idx < 0) return LIST_NONE; /* already gone */
            if (m->s[idx].n_reqs > 0 || m->s[idx].state == ST_WAITING) {
                snprintf(l->toast, sizeof l->toast, "It needs you now; kept");
                l->toast_until_ms = now + LIST_TOAST_MS;
                return LIST_NONE;
            }
            if (removed_sid) snprintf(removed_sid, 64, "%s", m->s[idx].sid);
            model_remove_at(m, idx);
            if (l->cursor >= m->n) l->cursor = m->n > 0 ? m->n - 1 : 0;
            return LIST_REMOVED;
        }
        return LIST_NONE;
    }

    if (a->back || a->sessions) {
        l->open = false;
        return LIST_CLOSED;
    }
    if (step && m->n) {
        l->cursor += step > 0 ? 1 : -1;
        if (l->cursor < 0) l->cursor = 0;
        if (l->cursor >= m->n) l->cursor = m->n - 1;
    }
    if (a->confirm && m->n) {
        model_select(m, l->cursor, now);
        l->open = false;
        return LIST_SWITCHED;
    }
    if (a->square && m->n) {
        const Session *s = &m->s[l->cursor];
        if (s->n_reqs > 0 || s->state == ST_WAITING) {
            snprintf(l->toast, sizeof l->toast, "Answer its request first");
            l->toast_until_ms = now + LIST_TOAST_MS;
            return LIST_NONE;
        }
        l->confirming = true;
        l->confirm_sel = 0;
        l->confirm_at = now;
        snprintf(l->confirm_sid, sizeof l->confirm_sid, "%s", s->sid);
    }
    return LIST_NONE;
}
