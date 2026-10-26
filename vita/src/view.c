#include "view.h"

#include <stdio.h>
#include <string.h>

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

ClipRect clip_rect(int x0, int y0, int x1, int y1, int w, int h) {
    ClipRect r = {clampi(x0, 0, w), clampi(y0, 0, h), clampi(x1, 0, w), clampi(y1, 0, h)};
    if (r.x1 < r.x0) r.x1 = r.x0;
    if (r.y1 < r.y0) r.y1 = r.y0;
    return r;
}

int scroll_update(ScrollState *s, const char *sid, const char *rid, int delta, int line_count,
                  int lines_per_page) {
    if (strcmp(s->sid, sid) || strcmp(s->rid, rid)) {
        snprintf(s->sid, sizeof s->sid, "%s", sid);
        snprintf(s->rid, sizeof s->rid, "%s", rid);
        s->line = 0;
        delta = 0; /* input aimed at the previous request does not carry over */
    }
    int max_line = line_count - lines_per_page;
    s->line += delta;
    if (s->line > max_line) s->line = max_line;
    if (s->line < 0) s->line = 0;
    return s->line;
}

int table_focus_pick(const bool *visible, int n_tables, int current, int dir) {
    bool cur_ok = current >= 0 && current < n_tables && visible[current];
    if (dir == 0 || !cur_ok) {
        if (cur_ok) return current;
        for (int i = 0; i < n_tables; i++)
            if (visible[i]) return i;
        return -1;
    }
    for (int i = current + dir; i >= 0 && i < n_tables; i += dir)
        if (visible[i]) return i;
    return current;
}

int table_pan_clamp(int pan, int table_w, int view_w) {
    int max = table_w > view_w ? table_w - view_w : 0;
    return pan < 0 ? 0 : pan > max ? max : pan;
}

BatteryLevel battery_level(int percent) {
    if (percent < 0 || percent > 100) return BAT_UNKNOWN;
    if (percent <= 10) return BAT_CRITICAL;
    if (percent <= 30) return BAT_LOW;
    return BAT_OK;
}

PowerChange power_watch(PowerWatch *w, bool online) {
    bool was_seen = w->seen, was = w->online;
    w->seen = true;
    w->online = online;
    if (!was_seen || was == online) return POWER_NO_CHANGE;
    return online ? POWER_TO_DESK : POWER_TO_SOFA;
}
