#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Same bit values as SceCtrl, so host tests can use them. */
enum {
    BTN_SELECT = 0x1,
    BTN_START = 0x8,
    BTN_UP = 0x10,
    BTN_RIGHT = 0x20,
    BTN_DOWN = 0x40,
    BTN_LEFT = 0x80,
    BTN_L = 0x100,
    BTN_R = 0x200,
    BTN_TRIANGLE = 0x1000,
    BTN_CIRCLE = 0x2000,
    BTN_CROSS = 0x4000,
    BTN_SQUARE = 0x8000,
};

#define SCREEN_W 960
#define SCREEN_H 544
#define LINE_PX 28

/* Active area of the rear panel in raw touch units (x spans 0-1919). */
#define REAR_X_MAX 1919
#define REAR_Y_MIN 108
#define REAR_Y_MAX 889

typedef struct {
    uint32_t buttons;
    uint8_t lx, ly, rx, ry;
    bool rear_down;
    int rear_x, rear_y; /* raw rear panel coordinates; the rear only scrolls */
    bool front_down;
    int front_x, front_y; /* already scaled to 960x544 */
    uint64_t now_ms;
} InputFrame;

typedef struct {
    int session_dir;
    int scroll_lines; /* positive = content moves up (read further down) */
    int sel_dir;
    bool confirm, square, toggle_mode, settings, sessions, back, quit; /* quit comes from the settings page */
    bool tap; /* front panel only */
    int tap_x, tap_y;
    int pan_px;          /* right stick left/right this frame; + reveals what is to the right */
    int table_dir;       /* right stick up/down: switch the table it pans */
    int touch_pan_px;    /* front sideways drag this frame; + = finger moved left */
    int touch_x, touch_y; /* where that drag started (front or rear, in screen pixels) */
    int rear_lines;       /* the part of scroll_lines that came from the rear panel */
} Actions;

typedef struct {
    bool down, dragging, sideways; /* a drag locks to the axis it first moved along */
    int start_x, start_y, anchor_x, anchor_y;
} TouchDrag;

typedef struct {
    uint32_t prev_buttons;
    int scroll_dir;
    uint64_t scroll_next;
    int sel_dir;
    uint64_t sel_next;
    int table_dir;
    uint64_t table_next;
    TouchDrag front, rear;
    bool front_enabled;
} InputState;

void input_init(InputState *s);
Actions input_map(InputState *s, const InputFrame *f);

/* Settings switch: with the front panel off it neither scrolls nor taps. */
void input_set_front_enabled(InputState *s, bool on);

/* Lines to move a highlight or cursor by. Text follows scroll_lines on either
 * panel, but a highlight follows the finger on the back the other way round:
 * finger up on the rear panel moves it up, as if pointing through the Vita. */
int actions_cursor_lines(const Actions *a);

/* True if the frame produced anything to act on (a tap fires on a frame with no input). */
bool actions_any(const Actions *a);
