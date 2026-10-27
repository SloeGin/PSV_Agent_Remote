#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "config.h"
#include "model.h"
#include "net.h"
#include "sessionlist.h"
#include "settings.h"

#define UI_MAX_TABLES 16

typedef struct {
    char banner_project[64]; /* "<project> is idle" banner */
    uint64_t banner_until_ms;
    char toast[96];
    uint64_t toast_until_ms;
    bool dimmed;
    NetStatus net;
    NetError net_err; /* the active broker's last failure while not connected */
    int battery_pct;      /* 0-100, or -1 when unknown */
    bool charging;        /* battery is being charged */
    bool power_online;    /* on external power */
    int turn_scroll;  /* display line at the top of the turn view */
    int sheet_scroll; /* line at the top of the approval code box */
    int table_focus;  /* the overflowing table the right stick pans, or -1 */
    int table_pan[UI_MAX_TABLES]; /* pixels each table is shifted left */
} UiView;

bool ui_init(void);

/* Call between vita2d_start_drawing and vita2d_end_drawing. */
void ui_draw(const Model *m, const UiView *v, uint64_t now);
void ui_draw_settings(const Model *m, const AppConfig *c, const SettingsState *s, int connected_idx,
                      const UiView *v, uint64_t now);
void ui_draw_sessions(const Model *m, const SessionList *l, const UiView *v, uint64_t now);

/* What a front-panel tap at (x, y) lands on, from the last draw. */
typedef enum {
    HIT_NONE, HIT_TAB, HIT_PEEK, HIT_OPTION, HIT_LIST_ROW, HIT_DIALOG, HIT_SETTING, HIT_TABLE,
    HIT_REPLY_ROW, /* index = line * 4 + col */
    HIT_REPLY_PEEK
} HitKind;
typedef struct {
    HitKind kind;
    int index; /* session, option/row, list row, dialog button or settings row */
} Hit;
Hit ui_hit_test(int x, int y);

/* Tables in the turn view, as laid out and drawn by the last ui_draw. Their
 * numbering starts over whenever ui_turn_rev() changes. */
unsigned ui_turn_rev(void);
int ui_table_count(void);
bool ui_table_visible_overflow(int i); /* overflows and is at least partly on screen */
int ui_table_width(int i);
int ui_table_view_w(void);

/* Scroll limits measured by the last ui_draw. */
int ui_turn_max_scroll(void);
int ui_sheet_max_scroll(void);
