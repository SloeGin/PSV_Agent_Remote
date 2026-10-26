#pragma once

#include <stdbool.h>

typedef struct {
    int line;
    char sid[64];
    char rid[24];
} ScrollState;

/* Apply delta to the scroll position for the request (sid, rid) on screen.
 * Jumps back to the top whenever a different request is shown, even with no
 * input that frame, then clamps to [0, line_count - lines_per_page]. */
int scroll_update(ScrollState *s, const char *sid, const char *rid, int delta, int line_count,
                  int lines_per_page);

typedef struct {
    int x0, y0, x1, y1;
} ClipRect;

/* A clip rectangle clamped to a w x h screen with x0 <= x1 and y0 <= y1.
 * vita2d draws the clip rectangle into the stencil buffer, and an inverted or
 * off-screen one (a sliding sheet's list starting below the display) crashes
 * the GPU. An empty result clips everything away. */
ClipRect clip_rect(int x0, int y0, int x1, int y1, int w, int h);

/* Which overflowing table the right stick pans. visible[i]: table i overflows
 * and is at least partly on screen. dir 0 keeps current while it is visible,
 * else takes the first visible one (-1 if none); dir +1/-1 steps to the next /
 * previous visible table and stays put at the ends. */
int table_focus_pick(const bool *visible, int n_tables, int current, int dir);

/* Clamp a table's pan offset to [0, max(0, table_w - view_w)]. */
int table_pan_clamp(int pan, int table_w, int view_w);

/* Battery colour band: teal above 30%, amber at 30% and below, red at 10% and
 * below. UNKNOWN (outside 0-100, e.g. a scePower error) hides the indicator. */
typedef enum { BAT_UNKNOWN, BAT_OK, BAT_LOW, BAT_CRITICAL } BatteryLevel;
BatteryLevel battery_level(int percent);

/* Plugging in switches to desk mode, unplugging to sofa mode. Only the moment
 * the power state changes counts, so a manual △ toggle stays until the next
 * plug or unplug, and the first sample never switches. */
typedef struct {
    bool seen, online;
} PowerWatch;
typedef enum { POWER_NO_CHANGE, POWER_TO_DESK, POWER_TO_SOFA } PowerChange;
PowerChange power_watch(PowerWatch *w, bool online);
