#include "input.h"

#include <stdlib.h>
#include <string.h>

#define LSTICK_DEADZONE 40
#define RSTICK_DEADZONE 60
#define SCROLL_REPEAT_MS 120
#define SEL_REPEAT_MS 300
#define DRAG_THRESHOLD_PX 12
#define PAN_MAX_PX 18

void input_init(InputState *s) {
    memset(s, 0, sizeof *s);
    s->front_enabled = true;
}

void input_set_front_enabled(InputState *s, bool on) {
    s->front_enabled = on;
    s->front.down = false;
}

/* Fire once when a direction starts, then every period_ms while it is held. */
static int repeat(int dir, int *held_dir, uint64_t *next, uint64_t now, uint64_t period_ms) {
    if (dir == 0) {
        *held_dir = 0;
        return 0;
    }
    if (dir != *held_dir || now >= *next) {
        *held_dir = dir;
        *next = now + period_ms;
        return dir;
    }
    return 0;
}

static int clampi(int v, int lo, int hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

/* Drag like a phone: past a small threshold the touch locks to one axis.
 * Vertically, every LINE_PX of finger travel scrolls one line (finger up = read
 * further down); sideways (front panel only) it pans pixel by pixel. Returns
 * true for a touch that was released without dragging. */
static bool drag(TouchDrag *d, bool down, int x, int y, bool sideways_ok, Actions *a) {
    bool tap = false;
    if (down && !d->down) {
        d->start_x = x;
        d->start_y = y;
        d->dragging = d->sideways = false;
    } else if (down) {
        int dx = x - d->start_x, dy = y - d->start_y;
        if (!d->dragging && sideways_ok && abs(dx) > DRAG_THRESHOLD_PX && abs(dx) > abs(dy)) {
            d->dragging = d->sideways = true;
            d->anchor_x = d->start_x + (dx > 0 ? DRAG_THRESHOLD_PX : -DRAG_THRESHOLD_PX);
        } else if (!d->dragging && abs(dy) > DRAG_THRESHOLD_PX) {
            d->dragging = true;
            d->anchor_y = d->start_y + (dy > 0 ? DRAG_THRESHOLD_PX : -DRAG_THRESHOLD_PX);
        }
        if (d->sideways) {
            a->touch_pan_px += d->anchor_x - x;
            a->touch_x = d->start_x;
            a->touch_y = d->start_y;
            d->anchor_x = x;
        } else if (d->dragging) {
            while (d->anchor_y - y >= LINE_PX) a->scroll_lines++, d->anchor_y -= LINE_PX;
            while (y - d->anchor_y >= LINE_PX) a->scroll_lines--, d->anchor_y += LINE_PX;
        }
    } else if (d->down && !d->dragging) {
        tap = true;
    }
    d->down = down;
    return tap;
}

Actions input_map(InputState *s, const InputFrame *f) {
    Actions a;
    memset(&a, 0, sizeof a);
    uint32_t pressed = f->buttons & ~s->prev_buttons;
    s->prev_buttons = f->buttons;

    if (pressed & BTN_L) a.session_dir = -1;
    if (pressed & BTN_R) a.session_dir = 1;
    a.confirm = pressed & BTN_CROSS;
    a.square = pressed & BTN_SQUARE;
    a.toggle_mode = pressed & BTN_TRIANGLE;
    a.settings = pressed & BTN_START;
    a.back = pressed & BTN_CIRCLE;
    a.sessions = pressed & BTN_SELECT;

    /* Left stick: scroll or choose, whichever way it is pushed further. */
    int lx = (int)f->lx - 128, ly = (int)f->ly - 128;
    bool lvert = abs(ly) >= abs(lx);
    int vdir = !lvert ? 0 : ly > LSTICK_DEADZONE ? 1 : ly < -LSTICK_DEADZONE ? -1 : 0;
    if (f->buttons & BTN_DOWN) vdir = 1;
    if (f->buttons & BTN_UP) vdir = -1;
    a.scroll_lines = repeat(vdir, &s->scroll_dir, &s->scroll_next, f->now_ms, SCROLL_REPEAT_MS);
    int hdir = lvert ? 0 : lx > LSTICK_DEADZONE ? 1 : lx < -LSTICK_DEADZONE ? -1 : 0;
    if (f->buttons & BTN_RIGHT) hdir = 1;
    if (f->buttons & BTN_LEFT) hdir = -1;
    a.sel_dir = repeat(hdir, &s->sel_dir, &s->sel_next, f->now_ms, SEL_REPEAT_MS);

    /* Right stick: pan the focused table sideways, switch tables up/down. */
    int rx = (int)f->rx - 128, ry = (int)f->ry - 128;
    if (abs(rx) > RSTICK_DEADZONE) {
        int px = (abs(rx) - RSTICK_DEADZONE) * PAN_MAX_PX / (127 - RSTICK_DEADZONE);
        if (px > PAN_MAX_PX) px = PAN_MAX_PX;
        a.pan_px = rx > 0 ? px : -px;
    }
    int tdir = ry > RSTICK_DEADZONE ? 1 : ry < -RSTICK_DEADZONE ? -1 : 0;
    a.table_dir = repeat(tdir, &s->table_dir, &s->table_next, f->now_ms, SEL_REPEAT_MS);

    /* Rear panel: scrolling only. Front panel: drag scrolls, tap presses. */
    int rear_y = (clampi(f->rear_y, REAR_Y_MIN, REAR_Y_MAX) - REAR_Y_MIN) * (SCREEN_H - 1) / (REAR_Y_MAX - REAR_Y_MIN);
    drag(&s->rear, f->rear_down, 0, rear_y, false, &a);
    bool front_tap = drag(&s->front, s->front_enabled && f->front_down, f->front_x, f->front_y, true, &a);
    if (front_tap) {
        a.tap = true;
        a.tap_x = s->front.start_x;
        a.tap_y = s->front.start_y;
    }
    return a;
}

bool actions_any(const Actions *a) {
    return a->session_dir || a->scroll_lines || a->sel_dir || a->confirm || a->square || a->toggle_mode || a->sessions ||
           a->settings || a->back || a->quit || a->tap || a->pan_px || a->table_dir || a->touch_pan_px;
}
