#include "ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <vita2d.h>

#include "gfx.h"
#include "input.h"
#include "markdown.h"
#include "textlayout.h"
#include "util.h"
#include "view.h"

/* Agent palettes belong to conversations/popups; app pages have their own identity. */
typedef enum { THEME_CLAUDE, THEME_CODEX, THEME_KIRO, THEME_APP } ThemeKind;
typedef struct {
    unsigned bg, panel, bar, sheet, hintbg, line, line2, tabon, text, text2, muted, dim, brand;
} Theme;
static const Theme g_themes[] = {
    {
        .bg = RGBA8(0x17, 0x12, 0x0F, 0xFF),
        .panel = RGBA8(0x1E, 0x18, 0x14, 0xFF),
        .bar = RGBA8(0x21, 0x1A, 0x15, 0xFF),
        .sheet = RGBA8(0x1C, 0x16, 0x12, 0xFF),
        .hintbg = RGBA8(0x14, 0x10, 0x0D, 0xFF),
        .line = RGBA8(0x3A, 0x2F, 0x27, 0xFF),
        .line2 = RGBA8(0x2F, 0x26, 0x20, 0xFF),
        .tabon = RGBA8(0x2E, 0x25, 0x20, 0xFF),
        .text = RGBA8(0xF1, 0xE6, 0xD6, 0xFF),
        .text2 = RGBA8(0xC9, 0xBB, 0xA9, 0xFF),
        .muted = RGBA8(0x9C, 0x8F, 0x80, 0xFF),
        .dim = RGBA8(0x6B, 0x5A, 0x4A, 0xFF),
        .brand = RGBA8(0xFF, 0xB3, 0x5C, 0xFF),
    },
    {
        .bg = RGBA8(0x0D, 0x15, 0x12, 0xFF),
        .panel = RGBA8(0x11, 0x1B, 0x17, 0xFF),
        .bar = RGBA8(0x0F, 0x1E, 0x19, 0xFF),
        .sheet = RGBA8(0x10, 0x19, 0x15, 0xFF),
        .hintbg = RGBA8(0x0A, 0x13, 0x0E, 0xFF),
        .line = RGBA8(0x1F, 0x36, 0x2D, 0xFF),
        .line2 = RGBA8(0x19, 0x2B, 0x24, 0xFF),
        .tabon = RGBA8(0x17, 0x2B, 0x22, 0xFF),
        .text = RGBA8(0xE7, 0xF3, 0xED, 0xFF),
        .text2 = RGBA8(0xBE, 0xD3, 0xC7, 0xFF),
        .muted = RGBA8(0x8D, 0xA9, 0x9A, 0xFF),
        .dim = RGBA8(0x59, 0x74, 0x66, 0xFF),
        .brand = RGBA8(0x10, 0xA3, 0x7F, 0xFF),
    },
    {
        .bg = RGBA8(0x12, 0x0F, 0x17, 0xFF),
        .panel = RGBA8(0x19, 0x14, 0x1F, 0xFF),
        .bar = RGBA8(0x1B, 0x15, 0x24, 0xFF),
        .sheet = RGBA8(0x17, 0x12, 0x21, 0xFF),
        .hintbg = RGBA8(0x0F, 0x0C, 0x15, 0xFF),
        .line = RGBA8(0x32, 0x27, 0x45, 0xFF),
        .line2 = RGBA8(0x26, 0x1D, 0x35, 0xFF),
        .tabon = RGBA8(0x2A, 0x21, 0x40, 0xFF),
        .text = RGBA8(0xEC, 0xE7, 0xF5, 0xFF),
        .text2 = RGBA8(0xC9, 0xBE, 0xDD, 0xFF),
        .muted = RGBA8(0x9A, 0x8D, 0xB0, 0xFF),
        .dim = RGBA8(0x64, 0x58, 0x77, 0xFF),
        .brand = RGBA8(0xA7, 0x8B, 0xFA, 0xFF),
    },
    {
        .bg = RGBA8(0x15, 0x17, 0x0F, 0xFF),
        .panel = RGBA8(0x1C, 0x1F, 0x14, 0xFF),
        .bar = RGBA8(0x20, 0x24, 0x17, 0xFF),
        .sheet = RGBA8(0x19, 0x1D, 0x12, 0xFF),
        .hintbg = RGBA8(0x11, 0x14, 0x0C, 0xFF),
        .line = RGBA8(0x3A, 0x41, 0x29, 0xFF),
        .line2 = RGBA8(0x2C, 0x32, 0x1F, 0xFF),
        .tabon = RGBA8(0x2B, 0x31, 0x20, 0xFF),
        .text = RGBA8(0xE9, 0xEB, 0xD9, 0xFF),
        .text2 = RGBA8(0xC6, 0xC9, 0xAA, 0xFF),
        .muted = RGBA8(0x96, 0x9B, 0x77, 0xFF),
        .dim = RGBA8(0x60, 0x67, 0x47, 0xFF),
        .brand = RGBA8(0xDD, 0xD2, 0x3B, 0xFF),
    },
};
static const Theme *g_theme = &g_themes[THEME_APP];
#define C_BG (g_theme->bg)
#define C_PANEL (g_theme->panel)
#define C_BAR (g_theme->bar)
#define C_SHEET (g_theme->sheet)
#define C_HINTBG (g_theme->hintbg)
#define C_LINE (g_theme->line)
#define C_LINE2 (g_theme->line2)
#define C_TABON (g_theme->tabon)
#define C_TEXT (g_theme->text)
#define C_TEXT2 (g_theme->text2)
#define C_MUTED (g_theme->muted)
#define C_DIM (g_theme->dim)
#define C_BRAND (g_theme->brand)
#define C_SOFA ((unsigned)RGBA8(0xE8, 0x7A, 0x90, 0xFF))
#define C_DESK ((unsigned)RGBA8(0x2E, 0xA9, 0xDF, 0xFF))
#define C_YOU C_SOFA
#define C_YOU_LINE ((unsigned)RGBA8(0x61, 0x33, 0x3C, 0xFF))
#define C_AMBER ((unsigned)RGBA8(0xFF, 0xB3, 0x5C, 0xFF))
#define C_AMBER_DEEP ((unsigned)RGBA8(0x6B, 0x4A, 0x25, 0xFF))
#define C_TEAL ((unsigned)RGBA8(0x5F, 0xC9, 0xB8, 0xFF))
#define C_TEAL_DEEP ((unsigned)RGBA8(0x2D, 0x5B, 0x54, 0xFF))
#define C_ROSE ((unsigned)RGBA8(0xFF, 0x7A, 0x6B, 0xFF))
#define C_DEL ((unsigned)RGBA8(0xFF, 0x9A, 0x8F, 0xFF))
#define C_ADD ((unsigned)RGBA8(0x8F, 0xE3, 0xC4, 0xFF))
#define C_BANNER ((unsigned)RGBA8(0x1F, 0x2D, 0x29, 0xFF))
#define C_SCRIM ((unsigned)RGBA8(0x0A, 0x08, 0x06, 0xB8))

#define TOP_H 44
#define HINT_H 30
#define HINT_Y (SCREEN_H - HINT_H)
#define PAD 16
#define TEXT_SIZE 15 /* UI chrome: tabs, buttons, banner */
#define BODY_SIZE 17 /* reading text: code, conversation */
#define LINE_H 29

/* Turn view */
#define TURN_Y 100
#define TURN_W (SCREEN_W - 2 * PAD - 14)

/* Bottom sheet (expanded geometry; drawn offset by the slide animation) */
#define SHEET_X 10
#define SHEET_W (SCREEN_W - 20)
#define SHEET_TOP 56
#define PEEK_H 46
#define SHEET_PEEK_TOP (HINT_Y - PEEK_H)
#define BODY_TOP (SHEET_TOP + PEEK_H)
#define OPTS_H 64
#define OPTS_Y (HINT_Y - OPTS_H)
#define CODE_X (SHEET_X + 16)
#define CODE_Y (BODY_TOP + 12)
#define CODE_W (SHEET_X + SHEET_W - 28 - CODE_X)
#define CODE_H (OPTS_Y - 24 - CODE_Y)
#define QROW_H 48
#define QROW_GAP 4

#define MAX_LINES 8000
#define MAX_DLINES 2000

typedef struct {
    int x, y, w, h;
} Rect;

#define MAX_HITS 48
typedef struct {
    Rect r;
    Hit hit;
} HitRegion;
static HitRegion g_hits[MAX_HITS];
static int g_n_hits;

/* Approval code: wrap cache */
static char g_code_key[96];
static LineSpan g_code[MAX_LINES];
static unsigned g_code_color[MAX_LINES];
static int g_n_code;
static int g_sheet_max;

/* Turn view: display lines */
typedef enum {
    DL_LABEL_YOU, DL_LABEL_AGENT, DL_YOU, DL_AGENT, DL_TOOL, DL_NOTE, DL_GAP,
    DL_MD, DL_CODE, DL_TABLE_ROW, DL_TABLE_BAR
} DLKind;
enum { DLF_FIRST = 1, DLF_LAST = 2, DLF_ROW_END = 4 }; /* first / last line of a block; last line of a table row */
typedef struct {
    DLKind kind;
    short item;          /* turn item, -1 = the summary */
    signed char doc;     /* DL_MD / DL_CODE: slot in g_md */
    unsigned char flags;
    short block;         /* DL_MD / DL_CODE: block in that doc */
    short h;             /* pixel height */
    int start, len;      /* span in the item's text, or in the doc's text; table rows: row, table */
    short sub;           /* table rows: which wrapped line of the row */
} DLine;

/* Assistant text as markdown */
#define MAX_MD_DOCS 4
#define MD_CODE_SIZE 15
#define MD_HEAD_SIZE 19
#define MD_PILL_PAD 5
#define MD_INDENT 24
#define MD_LINE_H_HEAD 33
#define MD_LINE_H_CODE 25
#define MD_CODE_PAD 8   /* code panel padding above the first / below the last line */
#define MD_CODE_INSET 14
#define MD_GAP_BLOCK 6
#define MD_GAP_HEAD 12
static MdDoc g_md[MAX_MD_DOCS];

/* Tables */
#define MD_CELL_PAD_X 10
#define MD_CELL_PAD_Y 6
#define MD_LINE_H_CELL 26
#define MD_TABLE_BAR_H 10
#define MAX_CELL_SPANS 4096
/* A pane that can be wider than the text area and pans sideways: a table, or a
 * code block (which never wraps, so long lines stay readable). */
typedef struct {
    signed char doc;
    short block;
    bool code;
    int col_w[MD_MAX_COLS]; /* tables only */
    int width;
    bool overflow;
} UiTable;
static UiTable g_tables[UI_MAX_TABLES];
static int g_n_tables;
static bool g_table_vis[UI_MAX_TABLES];
static unsigned g_turn_rev;
static bool g_hint_table; /* an overflowing table is on screen: hint the right stick */
static bool g_hint_reply; /* the session on screen has a reply drawer: hint □ */
static LineSpan g_cell_spans[MAX_CELL_SPANS]; /* wrapped cell lines, relative to the cell */
static int g_n_cell_spans;
static short g_cell_line0[MAX_MD_DOCS][MD_MAX_CELLS], g_cell_nl[MAX_MD_DOCS][MD_MAX_CELLS];
static char g_turn_key[96];
static DLine g_dl[MAX_DLINES];
static int g_n_dl;
static int g_turn_max;
static int g_turn_area_h;

/* Sheet animation */
static float g_sheet_y = SCREEN_H;
static float g_reply_y = SCREEN_H; /* top of the reply drawer, animated */
static char g_sheet_rid[24];

static vita2d_texture *g_agent_icons[3];

bool ui_init(void) {
    vita2d_set_clear_color(C_BG);
    if (!gfx_init()) return false;
    g_agent_icons[AGENT_CLAUDE] = vita2d_load_PNG_file("app0:icons/claude.png");
    g_agent_icons[AGENT_CODEX] = vita2d_load_PNG_file("app0:icons/codex.png");
    g_agent_icons[AGENT_KIRO] = vita2d_load_PNG_file("app0:icons/kiro.png");
    return true; /* Missing optional artwork gets a safe text fallback. */
}

void ui_fini(void) {
    for (int i = 0; i < 3; i++) {
        if (g_agent_icons[i]) vita2d_free_texture(g_agent_icons[i]);
        g_agent_icons[i] = NULL;
    }
}

int ui_turn_max_scroll(void) { return g_turn_max; }
unsigned ui_turn_rev(void) { return g_turn_rev; }
int ui_table_count(void) { return g_n_tables; }
bool ui_table_visible_overflow(int i) { return i >= 0 && i < g_n_tables && g_table_vis[i] && g_tables[i].overflow; }
int ui_table_width(int i) { return i >= 0 && i < g_n_tables ? g_tables[i].width : 0; }
int ui_table_view_w(void) { return TURN_W; }
int ui_sheet_max_scroll(void) { return g_sheet_max; }

/* Later regions sit on top (a dialog over its list), so search backwards. */
Hit ui_hit_test(int x, int y) {
    for (int i = g_n_hits - 1; i >= 0; i--) {
        Rect r = g_hits[i].r;
        if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) return g_hits[i].hit;
    }
    return (Hit){HIT_NONE, -1};
}

static void hit(HitKind kind, int idx, int x, int y, int w, int h) {
    if (g_n_hits < MAX_HITS) g_hits[g_n_hits++] = (HitRegion){{x, y, w, h}, {kind, idx}};
}

/* Drop regions added since `mark` (content that ended up hidden). */
static void hits_truncate(int mark) {
    if (g_n_hits > mark) g_n_hits = mark;
}

static int measure_mono(const char *s, int n, void *ud) { (void)ud; return gfx_char_width(F_MONO, BODY_SIZE, s, n); }
static int measure_sans(const char *s, int n, void *ud) { (void)ud; return gfx_char_width(F_SANS, BODY_SIZE, s, n); }
static int measure_q(const char *s, int n, void *ud) { (void)ud; return gfx_char_width(F_SANS, 19, s, n); }

static void format_ago(char *out, size_t cap, uint64_t ms) {
    unsigned s = (unsigned)(ms / 1000);
    if (s < 60) snprintf(out, cap, "%us ago", s);
    else if (s < 3600) snprintf(out, cap, "%um ago", s / 60);
    else snprintf(out, cap, "%uh ago", s / 3600);
}

/* Draw text cut to max_w, ending in "…" when it does not fit. Returns the x after it. */
static int text_fit(FontFace f, int x, int y, unsigned color, int size, const char *s, int max_w, bool force_ellipsis) {
    int n = (int)strlen(s);
    const char *nl = strchr(s, '\n');
    if (nl) n = (int)(nl - s), force_ellipsis = true;
    int ell = gfx_text_width(f, size, "…", -1);
    if (!force_ellipsis && gfx_text_width(f, size, s, n) <= max_w) return gfx_text(f, x, y, color, size, s, n);
    int w = 0, i = 0;
    while (i < n) {
        int len = (int)utf8_seq_len(s + i, (size_t)(n - i));
        if (len == 0) len = 1;
        int cw = gfx_char_width(f, size, s + i, len);
        if (w + cw + ell > max_w) break;
        w += cw;
        i += len;
    }
    x = gfx_text(f, x, y, color, size, s, i);
    return gfx_text(f, x, y, color, size, "…", -1);
}

/* ---------- chrome ---------- */

static unsigned state_color(SessState s) {
    switch (s) {
    case ST_RUNNING: return C_TEAL;
    case ST_WAITING: return C_AMBER;
    case ST_IDLE: return C_MUTED;
    default: return C_DIM;
    }
}

/* Fades c's alpha 0x50-0xFF and back over ~1.6 s. */
static unsigned breathe(unsigned c, uint64_t now) {
    float t = (float)(now % 1600) / 1600.0f;
    unsigned a = 0x50 + (unsigned)(0xAF * (0.5f - 0.5f * cosf(t * 6.2831853f)));
    return (c & 0x00FFFFFFu) | (a << 24);
}

/* Solid while there is something going on, a ring once finished; the dot
 * breathes while it wants you: a decision, or a finish you haven't looked at. */
static void draw_state_dot(const Session *s, int cx, int cy, int r, unsigned bg, uint64_t now) {
    if (s->n_reqs > 0 || s->state == ST_WAITING) gfx_circle(cx, cy, r, breathe(C_AMBER, now));
    else if (s->state == ST_IDLE && s->unseen) gfx_ring(cx, cy, r, 2, breathe(C_TEAL, now), bg);
    else if (s->state == ST_IDLE) gfx_ring(cx, cy, r, 2, C_MUTED, bg);
    else gfx_circle(cx, cy, r, state_color(s->state));
}

/* Battery at the top right: outline, proportional fill, percentage; a bolt while
 * on power, and the fill breathes while charging. Returns the x left of it. */
static int draw_battery(const UiView *v, int right, uint64_t now) {
    BatteryLevel lv = battery_level(v->battery_pct);
    if (lv == BAT_UNKNOWN) return right;
    unsigned c = lv == BAT_CRITICAL ? C_ROSE : lv == BAT_LOW ? C_AMBER : C_TEAL;
    char pct[8];
    snprintf(pct, sizeof pct, "%d%%", v->battery_pct);
    int x = right - gfx_text_width(F_MONO, 12, pct, -1);
    gfx_text(F_MONO, x, 14, c, 12, pct, -1);
    int bx = x - 8 - 24, by = TOP_H / 2 - 6, bw = 22, bh = 12;
    gfx_round_rect(bx, by, bw, bh, 2, c);
    gfx_round_rect(bx + 1, by + 1, bw - 2, bh - 2, 1, C_BAR);
    gfx_rect(bx + bw, by + 4, 2, 4, c);
    unsigned fill = c;
    if (v->charging && v->battery_pct < 100) fill = breathe(c, now);
    int fw = (bw - 4) * v->battery_pct / 100;
    if (fw > 0) gfx_rect(bx + 2, by + 2, fw, bh - 4, fill);
    if (v->charging || v->power_online) { /* bolt */
        float cx = (float)bx + bw / 2.0f, top = (float)by + 1.5f, bot = (float)(by + bh) - 1.5f;
        gfx_line(cx + 2.5f, top, cx - 2.0f, (top + bot) / 2 + 0.5f, 1.5f, C_TEXT);
        gfx_line(cx - 2.0f, (top + bot) / 2 + 0.5f, cx + 2.0f, (top + bot) / 2 - 0.5f, 1.5f, C_TEXT);
        gfx_line(cx + 2.0f, (top + bot) / 2 - 0.5f, cx - 2.5f, bot, 1.5f, C_TEXT);
    }
    return bx;
}

static void draw_top(const Model *m, const UiView *v, bool show_tabs, uint64_t now) {
    gfx_rect(0, 0, SCREEN_W, TOP_H, C_BAR);
    gfx_rect(0, TOP_H - 1, SCREEN_W, 1, C_LINE);
    int x = gfx_text_spaced(F_MONO_MED, PAD, 13, C_BRAND, 13, 2, "AGENT");
    x = gfx_text_spaced(F_MONO, x + 2, 13, g_theme != &g_themes[THEME_CLAUDE] ? C_BRAND : C_MUTED, 13, 2, "//");
    x = gfx_text_spaced(F_MONO_MED, x + 2, 13, C_BRAND, 13, 2, "REMOTE") + 18;

    const char *net = v->net == NET_UP ? "LINK" : v->net == NET_CONNECTING ? "LINKING" : "OFFLINE";
    if (v->net != NET_UP && v->net_err == NETERR_NO_KEY) net = "NO KEY";
    else if (v->net != NET_UP && v->net_err == NETERR_KEY) net = "KEY REJECTED";
    else if (v->net != NET_UP && v->net_err == NETERR_TLS) net = "TLS ERROR";
    unsigned net_c = v->net == NET_UP ? C_TEAL : v->net == NET_CONNECTING ? C_AMBER : C_ROSE;
    int rx = draw_battery(v, SCREEN_W - PAD, now);
    if (rx < SCREEN_W - PAD) rx -= 16;
    rx -= gfx_text_width(F_MONO, 12, net, -1);
    gfx_text(F_MONO, rx, 14, C_MUTED, 12, net, -1);
    gfx_circle(rx - 10, TOP_H / 2, 4, net_c);
    rx -= 26;
    const char *mode = m->mode == MODE_DESK ? "DESK" : "SOFA";
    int bw = gfx_text_width(F_MONO_MED, 11, mode, -1) + 3 + 14;
    rx -= bw;
    gfx_panel(rx, 12, bw, 20, 5, m->mode == MODE_DESK ? C_DESK : C_SOFA, C_BAR);
    gfx_text_spaced(F_MONO_MED, rx + 7, 15, m->mode == MODE_DESK ? C_DESK : C_SOFA, 11, 1, mode);
    rx -= 10;
    if (!show_tabs) return;
    if (m->n) {
        char pos[16];
        snprintf(pos, sizeof pos, "%d/%d", m->cur + 1, m->n);
        rx -= gfx_text_width(F_MONO, 12, pos, -1);
        gfx_text(F_MONO, rx, 14, C_MUTED, 12, pos, -1);
        rx -= 12;
    }
    int widths[MAX_SESSIONS];
    for (int i = 0; i < m->n; i++) widths[i] = gfx_text_width(F_SANS, TEXT_SIZE, m->s[i].project, -1) + 24 + 17 + 4;
    int avail = rx - x, first = 0, sum = 0;
    for (int i = m->cur; i >= 0; i--) {
        if (sum + widths[i] > avail) break;
        sum += widths[i];
        first = i;
    }
    for (int i = first; i < m->n && x + widths[i] - 4 <= rx; i++) {
        const Session *s = &m->s[i];
        int w = widths[i] - 4;
        bool on = i == m->cur;
        if (on) gfx_panel(x, 6, w, 32, 8, C_LINE, C_TABON);
        if (!(s->flash && (now / 500) % 2)) draw_state_dot(s, x + 16, TOP_H / 2, 5, on ? C_TABON : C_BAR, now);
        gfx_text(F_SANS, x + 29, 12, on ? C_TEXT : C_MUTED, TEXT_SIZE, s->project, -1);
        hit(HIT_TAB, i, x, 0, w, TOP_H);
        x += widths[i];
    }
}

/* ---------- hint strip ---------- */

typedef enum {
    HINT_PLAIN, HINT_PEEK, HINT_APPROVAL, HINT_QUESTION, HINT_LIST, HINT_CONFIRM, HINT_SETTINGS,
    HINT_REPLY, HINT_REPLY_PEEK, HINT_CAPABILITIES
} HintCtx;

static int keycap(int x, const char *label) {
    int w = gfx_text_width(F_MONO_MED, 10, label, -1) + 8;
    gfx_panel(x, HINT_Y + 8, w, 15, 3, C_DIM, C_HINTBG);
    gfx_text(F_MONO_MED, x + 4, HINT_Y + 9, C_TEXT2, 10, label, -1);
    return x + w;
}

static int hint_glyph(int x, Glyph g, const char *label) {
    gfx_glyph(g, x, HINT_Y + 8, 14, C_HINTBG);
    return gfx_text(F_MONO, x + 20, HINT_Y + 8, C_MUTED, 12, label, -1) + 18;
}

static int hint_key(int x, const char *key, const char *label) {
    x = keycap(x, key) + 6;
    return gfx_text(F_MONO, x, HINT_Y + 8, C_MUTED, 12, label, -1) + 18;
}

static int hint_width(const char *key, const char *label) {
    return gfx_text_width(F_MONO_MED, 10, key, -1) + 8 + 6 + gfx_text_width(F_MONO, 12, label, -1);
}

static void draw_hints(HintCtx ctx, Mode mode) {
    gfx_rect(0, HINT_Y, SCREEN_W, HINT_H, C_HINTBG);
    gfx_rect(0, HINT_Y, SCREEN_W, 1, C_LINE2);
    int x = PAD;
    bool tail = true;
    switch (ctx) {
    case HINT_PLAIN:
        if (g_hint_reply) x = hint_glyph(x, GLYPH_SQUARE, "Reply");
        x = hint_key(x, "L-STICK", "Scroll");
        if (g_hint_table) x = hint_key(x, "R-STICK", "Pan");
        x = hint_key(x, "L/R", "Session");
        hint_key(x, "SELECT", "Sessions");
        break;
    case HINT_PEEK:
        x = hint_glyph(x, GLYPH_SQUARE, "Show request");
        x = hint_key(x, "L-STICK", "Scroll");
        if (g_hint_table) x = hint_key(x, "R-STICK", "Pan");
        hint_key(x, "SELECT", "Sessions");
        break;
    case HINT_REPLY:
        x = hint_glyph(x, GLYPH_CROSS, "Choose");
        x = hint_key(x, "UP/DOWN", "Move");
        x = hint_glyph(x, GLYPH_CIRCLE, "Close");
        hint_glyph(x, GLYPH_SQUARE, "Close");
        break;
    case HINT_REPLY_PEEK:
        x = hint_glyph(x, GLYPH_SQUARE, "Reply");
        x = hint_key(x, "L-STICK", "Scroll");
        if (g_hint_table) x = hint_key(x, "R-STICK", "Pan");
        hint_key(x, "SELECT", "Sessions");
        break;
    case HINT_APPROVAL:
        x = hint_glyph(x, GLYPH_CROSS, "Confirm");
        x = hint_key(x, "LEFT/RIGHT", "Choose");
        x = hint_key(x, "L-STICK", "Scroll");
        hint_glyph(x, GLYPH_SQUARE, "Hide");
        break;
    case HINT_QUESTION:
        x = hint_glyph(x, GLYPH_CROSS, "Confirm");
        x = hint_key(x, "UP/DOWN", "Choose");
        x = hint_glyph(x, GLYPH_CIRCLE, "Back");
        hint_glyph(x, GLYPH_SQUARE, "Hide");
        break;
    case HINT_LIST:
        x = hint_glyph(x, GLYPH_CROSS, "Open");
        x = hint_key(x, "UP/DOWN", "Choose");
        x = hint_glyph(x, GLYPH_SQUARE, "Remove");
        x = hint_glyph(x, GLYPH_TRIANGLE, "Capabilities");
        hint_glyph(x, GLYPH_CIRCLE, "Close");
        tail = false;
        break;
    case HINT_CAPABILITIES:
        gfx_text(F_MONO, PAD, HINT_Y + 8, C_MUTED, 12, "Press any button to close", -1);
        tail = false;
        break;
    case HINT_CONFIRM:
        x = hint_glyph(x, GLYPH_CROSS, "Confirm");
        x = hint_key(x, "LEFT/RIGHT", "Choose");
        hint_glyph(x, GLYPH_CIRCLE, "Cancel");
        tail = false;
        break;
    case HINT_SETTINGS:
        x = hint_glyph(x, GLYPH_CROSS, "Select");
        x = hint_glyph(x, GLYPH_SQUARE, "Remove broker");
        hint_glyph(x, GLYPH_CIRCLE, "Back");
        hint_key(SCREEN_W - PAD - hint_width("START", "Close"), "START", "Close");
        return;
    }
    if (!tail) return;
    const char *other = mode == MODE_SOFA ? "Desk mode" : "Sofa mode";
    int right_w = 20 + gfx_text_width(F_MONO, 12, other, -1) + 18 + hint_width("START", "Settings");
    x = SCREEN_W - PAD - right_w;
    x = hint_glyph(x, GLYPH_TRIANGLE, other);
    hint_key(x, "START", "Settings");
}

static void scrollbar(int x, int y, int h, int scroll, int max, int lines_visible, int total) {
    if (max <= 0 || total <= 0) return;
    gfx_round_rect(x, y, 4, h, 2, C_LINE2);
    int thumb = h * lines_visible / (total > lines_visible ? total : lines_visible);
    if (thumb < 24) thumb = 24;
    gfx_round_rect(x, y + (h - thumb) * scroll / max, 4, thumb, 2, C_DIM);
}

/* ---------- turn view ---------- */

static int dl_height(DLKind k) {
    switch (k) {
    case DL_LABEL_YOU:
    case DL_LABEL_AGENT: return 18;
    case DL_TOOL:
    case DL_NOTE: return 26;
    case DL_GAP: return 12;
    default: return LINE_H;
    }
}

static DLine *dl_push(DLKind kind, int item, int start, int len) {
    if (g_n_dl >= MAX_DLINES) return NULL;
    DLine *d = &g_dl[g_n_dl++];
    *d = (DLine){.kind = kind, .item = (short)item, .doc = -1, .h = (short)dl_height(kind), .start = start, .len = len};
    return d;
}

static void dl_gap(int h) {
    DLine *d = dl_push(DL_GAP, -1, 0, 0);
    if (d) d->h = (short)h;
}

typedef enum { MDM_BODY, MDM_HEAD, MDM_TH } MdMode; /* paragraph, heading, table header */

typedef struct {
    const MdDoc *doc;
    MdMode mode;
    int th_start, th_end; /* doc text range of a table's header row (MDM_TH inside it) */
} MdMeasure;

/* Width of one character of a markdown line, styled as it will be drawn. */
static int md_measure(const char *s, int n, void *ud) {
    const MdMeasure *mm = ud;
    int off = (int)(s - mm->doc->text);
    const MdRun *r = md_run_at(mm->doc, off);
    unsigned st = r ? r->style : 0;
    if (st & MD_S_CODE) {
        int w = gfx_char_width(F_MONO, MD_CODE_SIZE, s, n);
        if (off == r->start) w += MD_PILL_PAD;
        if (off + n == r->start + r->len) w += MD_PILL_PAD;
        return w;
    }
    if (mm->mode == MDM_HEAD) return gfx_char_width(F_SANS_MED, MD_HEAD_SIZE, s, n);
    bool th = off >= mm->th_start && off < mm->th_end;
    return gfx_char_width((st & MD_S_BOLD) || th ? F_SANS_MED : F_SANS, BODY_SIZE, s, n);
}

static int measure_code(const char *s, int n, void *ud) { (void)ud; return gfx_char_width(F_MONO, MD_CODE_SIZE, s, n); }

static void md_push(DLKind kind, int slot, int block, int item, int start, int len, int h, unsigned flags) {
    DLine *d = dl_push(kind, item, start, len);
    if (!d) return;
    d->doc = (signed char)slot;
    d->block = (short)block;
    d->h = (short)h;
    d->flags = (unsigned char)flags;
}

static LineSpan g_wrap_tmp[MAX_LINES];

/* Lay out table block b of doc slot: column widths, wrapped cells, one display
 * line per row plus a position bar when it overflows. False when full. */
static bool build_table(int slot, int b, int item) {
    const MdDoc *doc = &g_md[slot];
    const MdBlock *blk = &doc->blocks[b];
    if (g_n_tables >= UI_MAX_TABLES) return false;
    int ti = g_n_tables;
    UiTable *tb = &g_tables[ti];
    tb->doc = (signed char)slot;
    tb->block = (short)b;
    tb->code = false;
    const MdText *h0 = &doc->cells[blk->cell_first], *h1 = &doc->cells[blk->cell_first + blk->n_cols - 1];
    MdMeasure mm = {doc, MDM_BODY, h0->start, h1->start + h1->len};
    tb->width = md_table_widths(doc, blk, TURN_W, 2 * MD_CELL_PAD_X, md_measure, &mm, tb->col_w, &tb->overflow);
    for (int r = 0; r < blk->n_rows; r++) {
        int lines = 1;
        for (int c = 0; c < blk->n_cols; c++) {
            int ci = blk->cell_first + r * blk->n_cols + c;
            const MdText *cell = &doc->cells[ci];
            int inner = tb->col_w[c] - 2 * MD_CELL_PAD_X;
            int n = layout_wrap(doc->text + cell->start, cell->len, inner > 1 ? inner : 1, md_measure, &mm,
                                g_cell_spans + g_n_cell_spans, MAX_CELL_SPANS - g_n_cell_spans);
            if (g_n_cell_spans + n >= MAX_CELL_SPANS) return false;
            g_cell_line0[slot][ci] = (short)g_n_cell_spans;
            g_cell_nl[slot][ci] = (short)n;
            g_n_cell_spans += n;
            if (n > lines) lines = n;
        }
        /* One display line per wrapped line, so scrolling reaches every line of a tall row. */
        for (int k = 0; k < lines; k++) {
            bool top = k == 0, end = k == lines - 1;
            unsigned fl = (r == 0 && top ? DLF_FIRST : 0) | (r == blk->n_rows - 1 && end ? DLF_LAST : 0) |
                          (end ? DLF_ROW_END : 0);
            int before = g_n_dl;
            md_push(DL_TABLE_ROW, slot, b, item, r, ti,
                    MD_LINE_H_CELL + (top ? MD_CELL_PAD_Y : 0) + (end ? MD_CELL_PAD_Y : 0), fl);
            if (g_n_dl > before) g_dl[before].sub = (short)k;
        }
    }
    if (tb->overflow) md_push(DL_TABLE_BAR, slot, b, item, 0, ti, MD_TABLE_BAR_H, 0);
    g_n_tables++;
    return true;
}

/* Display lines for Assistant text as markdown; false = show it plain instead. */
static bool build_md(int slot, int item, const char *t) {
    if (slot >= MAX_MD_DOCS) return false;
    MdDoc *doc = &g_md[slot];
    if (!md_parse(t, (int)strlen(t), doc)) return false;
    int n_dl = g_n_dl, n_tables = g_n_tables, n_spans = g_n_cell_spans;
    for (int b = 0; b < doc->n_blocks; b++) {
        const MdBlock *blk = &doc->blocks[b];
        if (b > 0) dl_gap(blk->kind == MD_HEADING ? MD_GAP_HEAD : MD_GAP_BLOCK);
        const MdText *tx = &blk->text;
        const char *base = doc->text + tx->start;
        int n;
        if (blk->kind == MD_TABLE) {
            if (!build_table(slot, b, item)) { /* out of room: undo, the caller shows it plain */
                g_n_dl = n_dl;
                g_n_tables = n_tables;
                g_n_cell_spans = n_spans;
                return false;
            }
            continue;
        }
        if (blk->kind == MD_CODE) {
            if (g_n_tables >= UI_MAX_TABLES) { /* out of room: undo, the caller shows it plain */
                g_n_dl = n_dl;
                g_n_tables = n_tables;
                g_n_cell_spans = n_spans;
                return false;
            }
            int pi = g_n_tables++;
            UiTable *pane = &g_tables[pi];
            *pane = (UiTable){.doc = (signed char)slot, .block = (short)b, .code = true};
            /* split at newlines only: a huge width never forces a wrap */
            n = layout_wrap(base, tx->len, 1 << 30, measure_code, NULL, g_wrap_tmp, MAX_LINES);
            if (n == 0) g_wrap_tmp[n++] = (LineSpan){0, 0};
            int widest = 0;
            for (int k = 0; k < n; k++) {
                int w = gfx_text_width(F_MONO, MD_CODE_SIZE, base + g_wrap_tmp[k].start, g_wrap_tmp[k].len);
                if (w > widest) widest = w;
                unsigned fl = (k == 0 ? DLF_FIRST : 0) | (k == n - 1 ? DLF_LAST : 0);
                int h = MD_LINE_H_CODE + (fl & DLF_FIRST ? MD_CODE_PAD : 0) + (fl & DLF_LAST ? MD_CODE_PAD : 0);
                int before = g_n_dl;
                md_push(DL_CODE, slot, b, item, tx->start + g_wrap_tmp[k].start, g_wrap_tmp[k].len, h, fl);
                if (g_n_dl > before) g_dl[before].sub = (short)pi;
            }
            pane->width = widest + 2 * MD_CODE_INSET;
            pane->overflow = pane->width > TURN_W;
            if (pane->overflow) md_push(DL_TABLE_BAR, slot, b, item, 0, pi, MD_TABLE_BAR_H, 0);
            continue;
        }
        bool heading = blk->kind == MD_HEADING, list = blk->kind == MD_BULLET || blk->kind == MD_NUMBER;
        int indent = list ? (blk->level + 1) * MD_INDENT : 0;
        MdMeasure mm = {doc, heading ? MDM_HEAD : MDM_BODY, 0, 0};
        n = layout_wrap(base, tx->len, TURN_W - indent, md_measure, &mm, g_wrap_tmp, MAX_LINES);
        if (n == 0) g_wrap_tmp[n++] = (LineSpan){0, 0};
        for (int k = 0; k < n; k++)
            md_push(DL_MD, slot, b, item, tx->start + g_wrap_tmp[k].start, g_wrap_tmp[k].len,
                    heading ? MD_LINE_H_HEAD : LINE_H, k == 0 ? DLF_FIRST : 0);
    }
    return true;
}

static void build_turn(const Session *s) {
    char key[96];
    snprintf(key, sizeof key, "%s:%u:%u", s->sid, s->turn_rev, s->summary_rev);
    if (!strcmp(key, g_turn_key)) return;
    snprintf(g_turn_key, sizeof g_turn_key, "%s", key);
    g_n_dl = 0;
    g_n_tables = 0;
    g_n_cell_spans = 0;
    g_turn_rev++;
    int slot = 0;
    for (int i = 0; i < s->n_turn; i++) {
        const TurnItem *it = &s->turn[i];
        const char *t = it->text ? it->text : "";
        if (it->kind == TURN_TOOL || it->kind == TURN_NOTE) {
            dl_push(it->kind == TURN_TOOL ? DL_TOOL : DL_NOTE, i, 0, (int)strlen(t));
            continue;
        }
        bool you = it->kind == TURN_YOU;
        dl_push(you ? DL_LABEL_YOU : DL_LABEL_AGENT, i, 0, 0);
        if (!you && build_md(slot++, i, t)) {
            dl_push(DL_GAP, i, 0, 0);
            continue;
        }
        int n = layout_wrap(t, (int)strlen(t), TURN_W - (you ? 15 : 0), measure_sans, NULL, g_wrap_tmp, MAX_LINES);
        for (int k = 0; k < n; k++) dl_push(you ? DL_YOU : DL_AGENT, i, g_wrap_tmp[k].start, g_wrap_tmp[k].len);
        dl_push(DL_GAP, i, 0, 0);
    }
    /* Sessions that predate the turn feed still have their last message. */
    if (s->n_turn == 0 && s->summary[0]) {
        dl_push(DL_LABEL_AGENT, -1, 0, 0);
        if (build_md(slot, -1, s->summary)) return;
        int n = layout_wrap(s->summary, (int)strlen(s->summary), TURN_W, measure_sans, NULL, g_wrap_tmp, MAX_LINES);
        for (int k = 0; k < n; k++) dl_push(DL_AGENT, -1, g_wrap_tmp[k].start, g_wrap_tmp[k].len);
    }
}

static bool same_block(const DLine *a, const DLine *b) {
    return a->kind == b->kind && a->doc == b->doc && a->block == b->block && a->item == b->item;
}

/* Top and bottom pixel of the markdown block that display line i (drawn at y) belongs to. */
static void block_extent(int i, int y, int *top, int *bot) {
    int t = y, b = y;
    for (int j = i; j > 0 && same_block(&g_dl[j - 1], &g_dl[i]); j--) t -= g_dl[j - 1].h;
    for (int j = i; j < g_n_dl && same_block(&g_dl[j], &g_dl[i]); j++) b += g_dl[j].h;
    *top = t;
    *bot = b;
}

/* Draw doc text [start, start + len) from x, piece by piece in each run's style. */
static int draw_styled(const MdDoc *doc, int start, int len, int x, int y, MdMode mode) {
    bool heading = mode == MDM_HEAD;
    for (int off = start, end = start + len; off < end;) {
        const MdRun *r = md_run_at(doc, off);
        int stop = r && r->start + r->len < end ? r->start + r->len : end;
        unsigned st = r ? r->style : 0;
        const char *p = doc->text + off;
        int n = stop - off;
        if (st & MD_S_CODE) {
            int lp = off == r->start ? MD_PILL_PAD : 0, rp = stop == r->start + r->len ? MD_PILL_PAD : 0;
            int w = gfx_text_width(F_MONO, MD_CODE_SIZE, p, n) + lp + rp, dy = heading ? 3 : 0;
            gfx_panel(x, y + 4 + dy, w, 22, 4, C_LINE2, C_PANEL);
            gfx_text(F_MONO, x + lp, y + 6 + dy, C_BRAND, MD_CODE_SIZE, p, n);
            x += w;
        } else if (heading) {
            x = gfx_text(F_SANS_MED, x, y + 5, C_BRAND, MD_HEAD_SIZE, p, n);
        } else if (mode == MDM_TH) {
            x = gfx_text(F_SANS_MED, x, y + 3, C_BRAND, BODY_SIZE, p, n);
        } else if (st & MD_S_BOLD) {
            x = gfx_text(F_SANS_MED, x, y + 3, C_TEXT, BODY_SIZE, p, n);
        } else {
            x = gfx_text(F_SANS, x, y + 3, C_TEXT2, BODY_SIZE, p, n);
        }
        off = stop;
    }
    return x;
}

static void draw_md_line(const DLine *d, int y) {
    const MdDoc *doc = &g_md[d->doc];
    const MdBlock *blk = &doc->blocks[d->block];
    bool list = blk->kind == MD_BULLET || blk->kind == MD_NUMBER;
    int x = PAD + (list ? (blk->level + 1) * MD_INDENT : 0);
    if (list && (d->flags & DLF_FIRST))
        gfx_text(F_SANS_MED, PAD + blk->level * MD_INDENT, y + 3, C_BRAND, BODY_SIZE,
                 blk->kind == MD_BULLET ? "•" : blk->marker, -1);
    draw_styled(doc, d->start, d->len, x, y, blk->kind == MD_HEADING ? MDM_HEAD : MDM_BODY);
}

static int table_view_w(const UiTable *tb) { return tb->code || tb->width > TURN_W ? TURN_W : tb->width; }

static void draw_code_line(const DLine *d, int y, int bottom, const UiView *v) {
    int pi = d->sub;
    const UiTable *pane = &g_tables[pi];
    int y0 = y < TURN_Y ? TURN_Y : y, y1 = y + d->h > bottom ? bottom : y + d->h;
    if (y1 > y0) hit(HIT_TABLE, pi, PAD, y0, TURN_W, y1 - y0);
    g_table_vis[pi] = true;
    gfx_clip(PAD + 1, y0, PAD + TURN_W - 1, y1); /* keep the panel's edges */
    int x = PAD + MD_CODE_INSET - (pane->overflow ? v->table_pan[pi] : 0);
    int ty = y + (d->flags & DLF_FIRST ? MD_CODE_PAD : 0) + 5;
    gfx_text(F_MONO, x, ty, C_TEXT, MD_CODE_SIZE, g_md[d->doc].text + d->start, d->len);
    gfx_clip(0, TURN_Y, SCREEN_W, bottom);
}

static void draw_table_row(const DLine *d, int y, int bottom, const UiView *v) {
    int ti = d->len, r = d->start;
    const UiTable *tb = &g_tables[ti];
    const MdDoc *doc = &g_md[tb->doc];
    const MdBlock *blk = &doc->blocks[tb->block];
    int vw = table_view_w(tb);
    int y0 = y < TURN_Y ? TURN_Y : y, y1 = y + d->h > bottom ? bottom : y + d->h;
    if (y1 > y0) hit(HIT_TABLE, ti, PAD, y0, vw, y1 - y0);
    g_table_vis[ti] = true;
    gfx_clip(PAD + 1, y0, PAD + vw - 1, y1); /* keep the frame's edges */
    int x = PAD - (tb->overflow ? v->table_pan[ti] : 0);
    for (int c = 0; c < blk->n_cols; c++) {
        int ci = blk->cell_first + r * blk->n_cols + c;
        const MdText *cell = &doc->cells[ci];
        int k = d->sub;
        if (k < g_cell_nl[tb->doc][ci]) {
            const LineSpan *ls = &g_cell_spans[g_cell_line0[tb->doc][ci] + k];
            draw_styled(doc, cell->start + ls->start, ls->len, x + MD_CELL_PAD_X, y + (k == 0 ? MD_CELL_PAD_Y : 0) - 2,
                        r == 0 ? MDM_TH : MDM_BODY);
        }
        x += tb->col_w[c];
    }
    gfx_clip(0, TURN_Y, SCREEN_W, bottom);
    if ((d->flags & DLF_ROW_END) && !(d->flags & DLF_LAST)) gfx_rect(PAD + 1, y + d->h - 1, vw - 2, 1, C_LINE2);
}

static void draw_table_bar(const DLine *d, int y, const UiView *v) {
    const UiTable *tb = &g_tables[d->len];
    int vw = table_view_w(tb), thumb = vw * vw / tb->width, range = tb->width - vw;
    if (thumb < 24) thumb = 24;
    gfx_round_rect(PAD, y + 4, vw, 3, 1, C_LINE2);
    gfx_round_rect(PAD + (vw - thumb) * v->table_pan[d->len] / (range > 0 ? range : 1), y + 4, thumb, 3, 1,
                   v->table_focus == d->len ? C_BRAND : C_DIM);
}

static void draw_turn(const Model *m, const Session *s, const UiView *v, int bottom, int scroll) {
    memset(g_table_vis, 0, sizeof g_table_vis);
    build_turn(s);
    g_turn_area_h = bottom - TURN_Y;
    /* the furthest scroll that still fills the area */
    int h = 0, max = g_n_dl;
    while (max > 0 && h + g_dl[max - 1].h <= g_turn_area_h) h += g_dl[--max].h;
    g_turn_max = max;
    if (scroll > max) scroll = max;
    if (scroll < 0) scroll = 0;

    vita2d_enable_clipping();
    gfx_clip(0, TURN_Y, SCREEN_W, bottom);
    int y = TURN_Y;
    for (int i = scroll; i < g_n_dl && y < bottom; i++) {
        const DLine *d = &g_dl[i];
        const char *t = d->item >= 0 ? s->turn[d->item].text : s->summary;
        switch (d->kind) {
        case DL_LABEL_YOU: gfx_text_spaced(F_MONO_MED, PAD, y + 2, C_YOU, 11, 1, "YOU"); break;
        case DL_LABEL_AGENT: gfx_text_spaced(F_MONO_MED, PAD, y + 2, C_BRAND, 11, 1, "AGENT"); break;
        case DL_YOU:
            gfx_rect(PAD, y, 3, LINE_H, C_YOU_LINE);
            gfx_text(F_SANS, PAD + 15, y + 3, C_TEXT, BODY_SIZE, t + d->start, d->len);
            break;
        case DL_AGENT: gfx_text(F_SANS, PAD, y + 3, C_TEXT2, BODY_SIZE, t + d->start, d->len); break;
        case DL_TOOL: {
            const TurnItem *it = &s->turn[d->item];
            int x = gfx_text(F_MONO, PAD, y + 4, C_TEAL, 14, "▸ ", -1);
            x = gfx_text(F_MONO, x, y + 4, C_MUTED, 14, it->tool, -1) + 12;
            text_fit(F_MONO, x, y + 4, C_MUTED, 14, t, SCREEN_W - PAD - 14 - x, false);
            break;
        }
        case DL_NOTE: text_fit(F_MONO, PAD, y + 4, C_DIM, 13, t, TURN_W, false); break;
        case DL_GAP: break;
        case DL_MD: draw_md_line(d, y); break;
        case DL_CODE: {
            if (i == scroll || (d->flags & DLF_FIRST)) {
                int top, bot;
                block_extent(i, y, &top, &bot);
                const UiTable *pane = &g_tables[d->sub];
                bool focus = pane->overflow && v->table_focus == d->sub;
                gfx_panel(PAD, top, TURN_W, bot - top, 8, focus ? C_BRAND : C_LINE2, C_PANEL);
            }
            draw_code_line(d, y, bottom, v);
            break;
        }
        case DL_TABLE_ROW: {
            const UiTable *tb = &g_tables[d->len];
            if (i == scroll || (d->flags & DLF_FIRST)) {
                int top, bot;
                block_extent(i, y, &top, &bot);
                bool focus = tb->overflow && v->table_focus == d->len;
                gfx_panel(PAD, top, table_view_w(tb), bot - top, 8, focus ? C_BRAND : C_LINE, C_BG);
            }
            draw_table_row(d, y, bottom, v);
            break;
        }
        case DL_TABLE_BAR:
            g_table_vis[d->len] = true;
            draw_table_bar(d, y, v);
            break;
        }
        y += d->h;
    }
    if (s->state == ST_WAITING && s->n_reqs == 0 && m->mode == MODE_DESK && y + 26 <= bottom) {
        int x = gfx_text(F_MONO, PAD, y + 4, C_TEAL, 14, "Desk mode is on, so the agent asked on your computer. Press ", -1);
        gfx_glyph(GLYPH_TRIANGLE, x, y + 5, 14, C_BG);
        gfx_text(F_MONO, x + 18, y + 4, C_TEAL, 14, " to send decisions here.", -1);
    }
    if (g_n_dl == 0) gfx_text(F_SANS, PAD, TURN_Y + 3, C_MUTED, BODY_SIZE, "Nothing in this turn yet.", -1);
    for (int i = 0; i < g_n_tables; i++)
        if (g_table_vis[i] && g_tables[i].overflow) g_hint_table = true;
    vita2d_disable_clipping();
    scrollbar(SCREEN_W - PAD - 4, TURN_Y, g_turn_area_h, scroll, max, g_turn_area_h / LINE_H, g_n_dl);
}

static void draw_head(const Model *m, const Session *s, uint64_t now) {
    int x;
    if (s->n_reqs > 0 || s->state == ST_WAITING) {
        gfx_circle(PAD + 7, 74, 7, C_AMBER);
        const char *t = s->n_reqs > 0 ? "Needs you" : m->mode == MODE_DESK ? "Waiting on your computer" : "Waiting";
        x = gfx_text(F_SANS, PAD + 24, 54, C_TEXT, 28, t, -1);
        gfx_text(F_MONO, x + 12, 70, C_MUTED, 12, s->project, -1);
    } else if (s->state == ST_RUNNING) {
        for (int i = 0; i < 3; i++) {
            int phase = (int)((now / 100 + (uint64_t)(10 - i * 2)) % 12);
            gfx_rect(PAD + i * 10, 64, 6, 18, RGBA8(0x5F, 0xC9, 0xB8, phase < 4 ? 0xFF : 0x40));
        }
        x = gfx_text(F_SANS, PAD + 42, 54, C_TEXT, 28, "Working", -1);
        gfx_text(F_MONO, x + 12, 70, C_MUTED, 12, s->project, -1);
    } else {
        gfx_ring(PAD + 8, 74, 8, 2, s->state == ST_IDLE ? C_TEAL : C_DIM, C_BG);
        x = gfx_text(F_SANS, PAD + 26, 54, C_TEXT, 28, s->state == ST_IDLE ? "Idle" : "Unknown", -1);
        if (s->state == ST_IDLE) {
            char ago[24], when[40];
            format_ago(ago, sizeof ago, now - s->idle_since_ms);
            snprintf(when, sizeof when, "finished %s", ago);
            gfx_text(F_MONO, x + 12, 70, C_MUTED, 12, when, -1);
        }
    }
}

/* ---------- sheet: approval ---------- */

static bool starts_line(const char *t, int start) { return start == 0 || t[start - 1] == '\n'; }

static void build_code(const Request *r) {
    char key[96];
    snprintf(key, sizeof key, "%s:%p", r->rid, (void *)r->body);
    if (!strcmp(key, g_code_key)) return;
    snprintf(g_code_key, sizeof g_code_key, "%s", key);
    bool bash = !strcmp(r->tool, "Bash"), edit = !strcmp(r->tool, "Edit");
    int prefix = bash ? gfx_text_width(F_MONO, BODY_SIZE, "$ ", -1) : 0;
    const char *t = r->body ? r->body : "";
    g_n_code = layout_wrap(t, (int)strlen(t), CODE_W - 32 - prefix, measure_mono, NULL, g_code, MAX_LINES);
    unsigned color = C_TEXT;
    int section = 0;
    for (int i = 0; i < g_n_code; i++) {
        const char *ls = t + g_code[i].start;
        if (starts_line(t, g_code[i].start)) {
            color = C_TEXT;
            if (edit && !strncmp(ls, "--- old", 7)) section = 1, color = C_TEXT2;
            else if (edit && !strncmp(ls, "+++ new", 7)) section = 2, color = C_TEXT2;
            else if (edit && section == 1) color = C_DEL;
            else if (edit && section == 2) color = C_ADD;
            else if (!strncmp(ls, "# ", 2)) color = C_MUTED;
        }
        g_code_color[i] = color;
    }
}

static void draw_buttons(const char *const *labels, const bool *danger, int n, int sel, int y, uint64_t armed_ms) {
    int gap = 8, w = (SHEET_W - 28 - gap * (n - 1)) / n;
    for (int i = 0; i < n; i++) {
        int x = SHEET_X + 14 + i * (w + gap), h = OPTS_H - 22;
        bool on = i == sel;
        unsigned fill = on ? (danger[i] ? C_ROSE : C_AMBER) : C_SHEET;
        gfx_panel(x, y + 11, w, h, 9, on ? fill : C_LINE, fill);
        int tw = gfx_text_width(F_SANS, 14, labels[i], -1) + (on ? 21 : 0);
        int tx = x + (w - tw) / 2;
        if (on) {
            gfx_glyph(GLYPH_CROSS, tx, y + 11 + (h - 14) / 2, 14, fill);
            tx += 21;
        }
        gfx_text(F_SANS, tx, y + 11 + (h - 14) / 2 - 3, on ? C_BG : C_TEXT2, 14, labels[i], -1);
        if (on && armed_ms < LOCKOUT_MS)
            gfx_rect(x + 2, y + 11 + h - 4, (int)((w - 4) * armed_ms / LOCKOUT_MS), 3, RGBA8(0x17, 0x12, 0x0F, 0x80));
        hit(HIT_OPTION, i, x, y + 11, w, h);
    }
}

static void draw_note(int y, const char *lead, const char *what, unsigned what_c, const char *tail) {
    gfx_glyph(GLYPH_CROSS, SHEET_X + 16, y + 25, 14, C_SHEET);
    int x = gfx_text(F_SANS, SHEET_X + 38, y + 21, C_TEXT2, 15, lead, -1);
    gfx_text(F_SANS_MED, x, y + 21, what_c, 15, what, -1);
    gfx_text(F_MONO, SHEET_X + SHEET_W - 16 - gfx_text_width(F_MONO, 12, tail, -1), y + 24, C_MUTED, 12, tail, -1);
}

static const char *option_label(const Request *r, const char *id) {
    for (int i = 0; i < r->n_opts; i++)
        if (!strcmp(r->opts[i].id, id)) return r->opts[i].label;
    return id;
}

static void draw_approval(const Model *m, const Request *r, int dy, int scroll, uint64_t now) {
    bool expired = model_request_expired(r, now);
    build_code(r);
    int lpp = (CODE_H - 16) / LINE_H;
    g_sheet_max = g_n_code > lpp ? g_n_code - lpp : 0;
    if (scroll > g_sheet_max) scroll = g_sheet_max;
    gfx_panel(CODE_X, CODE_Y + dy, CODE_W, CODE_H, 10, C_LINE2, C_PANEL);
    bool bash = !strcmp(r->tool, "Bash");
    int prefix = bash ? gfx_text_width(F_MONO, BODY_SIZE, "$ ", -1) : 0;
    if (bash && scroll == 0) gfx_text(F_MONO, CODE_X + 16, CODE_Y + dy + 12, C_TEAL, BODY_SIZE, "$", -1);
    for (int i = 0; i < lpp && scroll + i < g_n_code; i++) {
        LineSpan l = g_code[scroll + i];
        gfx_text(F_MONO, CODE_X + 16 + prefix, CODE_Y + dy + 12 + i * LINE_H, expired ? C_DIM : g_code_color[scroll + i],
                 BODY_SIZE, r->body + l.start, l.len);
    }
    scrollbar(SHEET_X + SHEET_W - 18, CODE_Y + dy, CODE_H, scroll, g_sheet_max, lpp, g_n_code);
    if (g_sheet_max > 0) {
        char pg[24];
        snprintf(pg, sizeof pg, "PG %d/%d", scroll / lpp + 1, layout_page_count(g_n_code, lpp));
        gfx_text(F_MONO, SHEET_X + SHEET_W - 16 - gfx_text_width(F_MONO, 11, pg, -1), CODE_Y + dy + CODE_H + 4, C_DIM, 11, pg, -1);
    }
    if (r->cwd[0]) text_fit(F_MONO, CODE_X + 2, CODE_Y + dy + CODE_H + 4, C_DIM, 11, r->cwd, CODE_W - 120, false);

    int y = OPTS_Y + dy;
    gfx_rect(SHEET_X + 1, y, SHEET_W - 2, 1, C_LINE2);
    if (r->sent_choice[0]) {
        bool danger = !strncmp(r->sent_choice, "deny", 4);
        draw_note(y, "Sent · ", option_label(r, r->sent_choice), danger ? C_ROSE : C_AMBER, "waiting for agent…");
        return;
    }
    if (expired) {
        gfx_text(F_SANS, SHEET_X + 16, y + 21, C_MUTED, 15, "This request timed out. Answer it on your computer.", -1);
        return;
    }
    const char *labels[5];
    bool danger[5];
    for (int i = 0; i < r->n_opts && i < 5; i++) {
        labels[i] = r->opts[i].label;
        danger[i] = !strncmp(r->opts[i].id, "deny", 4);
    }
    draw_buttons(labels, danger, r->n_opts, m->sel, y, now - m->shown_ms);
}

/* ---------- sheet: question ---------- */

static void answer_text(const Request *r, int q, char *out, size_t cap) {
    size_t n = 0;
    out[0] = 0;
    for (int i = 0; i < r->questions[q].n_opts && n < cap; i++)
        if (r->picked[q] & (1u << i))
            n += (size_t)snprintf(out + n, cap - n, "%s%s", n ? ", " : "", r->questions[q].opts[i].label);
    if (r->other[q][0] && n < cap) snprintf(out + n, cap - n, "%s“%s”", n ? ", " : "", r->other[q]);
    if (!out[0]) snprintf(out, cap, "—");
}

static void draw_check(int x, int y, bool multi, bool on) {
    if (multi) {
        gfx_round_rect(x, y, 18, 18, 4, on ? C_TEAL : C_DIM);
        if (!on) gfx_round_rect(x + 2, y + 2, 14, 14, 3, C_SHEET);
        else gfx_text(F_MONO_MED, x + 3, y, C_BG, 13, "✓", -1);
    } else {
        gfx_ring(x + 9, y + 9, 9, 2, on ? C_TEAL : C_DIM, C_SHEET);
        if (on) gfx_circle(x + 9, y + 9, 4, C_TEAL);
    }
}

static void draw_question(const Model *m, const Request *r, int dy, uint64_t now) {
    if (r->sent_choice[0]) {
        bool mac = !strcmp(r->sent_choice, "mac");
        draw_note(OPTS_Y + dy, mac ? "" : "Sent · ", mac ? "Answering on computer" : "Answers sent", C_AMBER,
                  mac ? "answer it on your computer" : "waiting for agent…");
        return;
    }
    if (model_request_expired(r, now)) {
        gfx_text(F_SANS, SHEET_X + 16, OPTS_Y + dy + 21, C_MUTED, 15, "This question timed out. Answer it on your computer.", -1);
        return;
    }
    int top = BODY_TOP + dy + 14;
    if (r->step >= r->n_questions) { /* review */
        int y = top;
        for (int q = 0; q < r->n_questions; q++) {
            char hdr[40], ans[600];
            snprintf(hdr, sizeof hdr, "%s", r->questions[q].header[0] ? r->questions[q].header : "ANSWER");
            for (char *p = hdr; *p; p++)
                if (*p >= 'a' && *p <= 'z') *p = (char)(*p - 32);
            answer_text(r, q, ans, sizeof ans);
            gfx_text_spaced(F_MONO_MED, SHEET_X + 18, y + 14, C_MUTED, 11, 1, hdr);
            text_fit(F_SANS, SHEET_X + 160, y + 9, C_TEXT, BODY_SIZE, ans, SHEET_W - 180, false);
            gfx_rect(SHEET_X + 18, y + 44, SHEET_W - 36, 1, C_LINE2);
            y += 46;
        }
        const char *labels[3] = {"Send answers", "Change", "Answer on computer"};
        bool danger[3] = {false, false, false};
        gfx_rect(SHEET_X + 1, OPTS_Y + dy, SHEET_W - 2, 1, C_LINE2);
        draw_buttons(labels, danger, 3, m->sel, OPTS_Y + dy, now - m->shown_ms);
        return;
    }

    const Question *q = &r->questions[r->step];
    LineSpan qs[3];
    int nq = layout_wrap(q->question, (int)strlen(q->question), SHEET_W - 40, measure_q, NULL, qs, 3);
    if (nq > 2) nq = 2;
    for (int i = 0; i < nq; i++) gfx_text(F_SANS, SHEET_X + 18, top + i * 30, C_TEXT, 19, q->question + qs[i].start, qs[i].len);
    int list_top = top + nq * 30 + 10, list_bottom = HINT_Y - 10;
    int rows = model_row_count(r), area = list_bottom - list_top;
    int off = (m->sel + 1) * (QROW_H + QROW_GAP) - area;
    if (off < 0) off = 0;

    /* No clip rectangle here: a second stencil clip inside the sheet's clip
     * crashed the GPU on device. Only rows that fit entirely are drawn. */
    for (int i = 0; i < rows; i++) {
        int y = list_top + i * (QROW_H + QROW_GAP) - off, x = SHEET_X + 14, w = SHEET_W - 28;
        if (y < list_top || y + QROW_H > list_bottom) continue;
        bool on = i == m->sel;
        if (on) gfx_panel(x, y, w, QROW_H, 9, C_LINE, C_TABON);
        int opt;
        QRow kind = model_row_kind(r, i, &opt);
        const char *label = "", *desc = "";
        unsigned lc = C_TEXT;
        char other_desc[OTHER_BYTES + 8];
        switch (kind) {
        case ROW_OPTION:
            label = q->opts[opt].label;
            desc = q->opts[opt].desc;
            draw_check(x + 14, y + 15, q->multi, r->picked[r->step] & (1u << opt));
            break;
        case ROW_OTHER:
            label = "Other…";
            if (r->other[r->step][0]) {
                snprintf(other_desc, sizeof other_desc, "“%s”", r->other[r->step]);
                desc = other_desc;
            } else {
                desc = "Type your own answer";
            }
            draw_check(x + 14, y + 15, q->multi, r->other[r->step][0]);
            break;
        case ROW_NEXT:
            label = r->step == r->n_questions - 1 ? "Review answers" : "Next question";
            lc = C_AMBER;
            break;
        default:
            label = "Answer on computer";
            lc = C_MUTED;
            break;
        }
        int lx = (kind == ROW_OPTION || kind == ROW_OTHER) ? x + 44 : x + 14;
        if (desc[0]) {
            text_fit(F_SANS, lx, y + 4, lc, BODY_SIZE, label, w - 100, false);
            text_fit(F_SANS, lx, y + 27, C_MUTED, 13, desc, w - 100, false);
        } else {
            text_fit(F_SANS, lx, y + 13, lc, BODY_SIZE, label, w - 100, false);
        }
        if (on) {
            gfx_glyph(GLYPH_CROSS, x + w - 32, y + 17, 14, C_TABON);
            uint64_t t = now - m->shown_ms;
            if (t < LOCKOUT_MS) gfx_rect(lx, y + QROW_H - 5, (int)((w - 100) * t / LOCKOUT_MS), 2, C_AMBER);
        }
        hit(HIT_OPTION, i, x, y, w, QROW_H);
    }
}

/* ---------- sheet ---------- */

static void draw_sheet(const Model *m, const Session *s, const Request *r, const UiView *v, uint64_t now) {
    float target = m->sheet_hidden ? SHEET_PEEK_TOP : SHEET_TOP;
    if (strcmp(g_sheet_rid, r->rid)) { /* a new request slides up from the bottom */
        snprintf(g_sheet_rid, sizeof g_sheet_rid, "%s", r->rid);
        if (g_sheet_y < SHEET_PEEK_TOP) g_sheet_y = SCREEN_H;
    }
    g_sheet_y += (target - g_sheet_y) * 0.22f;
    if (fabsf(target - g_sheet_y) < 0.5f) g_sheet_y = target;
    int sy = (int)g_sheet_y, dy = sy - SHEET_TOP;

    vita2d_enable_clipping();
    gfx_clip(0, TOP_H, SCREEN_W, HINT_Y);
    if (!m->sheet_hidden) gfx_rect(0, TOP_H, SCREEN_W, HINT_Y - TOP_H, RGBA8(0x0A, 0x08, 0x06, 0x60));
    gfx_round_rect(SHEET_X, sy, SHEET_W, HINT_Y - sy + 20, 14, C_LINE);
    gfx_round_rect(SHEET_X + 1, sy + 1, SHEET_W - 2, HINT_Y - sy + 20, 13, C_SHEET);
    gfx_round_rect(SCREEN_W / 2 - 18, sy + 5, 36, 3, 1, C_LINE);
    gfx_rect(SHEET_X + 1, sy + PEEK_H, SHEET_W - 2, 1, C_LINE2);

    /* peek row */
    bool expired = model_request_expired(r, now), q = r->kind == REQ_QUESTION;
    char chip[40];
    if (q && r->n_questions > 1 && r->step < r->n_questions) snprintf(chip, sizeof chip, "Q %d/%d", r->step + 1, r->n_questions);
    else if (q) snprintf(chip, sizeof chip, "QUESTION");
    else {
        size_t i = 0;
        for (; r->tool[i] && i + 1 < sizeof chip; i++) chip[i] = (r->tool[i] >= 'a' && r->tool[i] <= 'z') ? (char)(r->tool[i] - 32) : r->tool[i];
        chip[i] = 0;
        if (!chip[0]) snprintf(chip, sizeof chip, "TOOL");
    }
    int cw = gfx_text_width(F_MONO_MED, 11, chip, -1) + (int)strlen(chip) + 15;
    gfx_round_rect(SHEET_X + 16, sy + 14, cw, 20, 4, expired ? C_DIM : q ? C_TEAL : C_AMBER);
    gfx_text_spaced(F_MONO_MED, SHEET_X + 24, sy + 17, C_BG, 11, 1, chip);

    char side[64];
    if (m->sheet_hidden) snprintf(side, sizeof side, "Show");
    else if (expired) snprintf(side, sizeof side, "expired");
    else {
        int left = r->timeout_s - (int)((now - r->recv_ms) / 1000);
        int n = snprintf(side, sizeof side, "%ds left", left > 0 ? left : 0);
        if (s->n_reqs > 1) snprintf(side + n, sizeof side - (size_t)n, " · +%d queued", s->n_reqs - 1);
    }
    int sx = SHEET_X + SHEET_W - 16 - gfx_text_width(F_MONO, 12, side, -1);
    gfx_text(F_MONO, sx, sy + 17, C_MUTED, 12, side, -1);
    if (m->sheet_hidden) gfx_glyph(GLYPH_SQUARE, sx - 20, sy + 17, 14, C_SHEET);

    const char *title;
    char buf[96];
    if (m->sheet_hidden) title = q ? (r->step < r->n_questions ? r->questions[r->step].question : "Review your answers") : r->title;
    else if (q && r->step >= r->n_questions) title = "Review your answers";
    else if (q) {
        snprintf(buf, sizeof buf, "%s%s", r->questions[r->step].header, r->questions[r->step].multi ? " · pick any" : "");
        title = buf;
    } else title = "Needs your approval";
    int tx = SHEET_X + 16 + cw + 10;
    text_fit(F_SANS, tx, sy + 13, C_TEXT, 16, title, sx - 30 - tx, m->sheet_hidden);

    hit(HIT_PEEK, 0, SHEET_X, sy, SHEET_W, PEEK_H);
    int mark = g_n_hits;
    if (sy < SHEET_PEEK_TOP) {
        if (q) draw_question(m, r, dy, now);
        else draw_approval(m, r, dy, v->sheet_scroll, now);
    }
    if (m->sheet_hidden) hits_truncate(mark);
    vita2d_disable_clipping();
}

/* ---------- reply drawer ---------- */

#define RROW_H 44
#define RBOX_BTN_H 38
#define RBOX_TEXT_LINES 2

static LineSpan g_rbox_lines[RBOX_TEXT_LINES + 1];

static const char *reply_item_label(ReplyItem it) {
    switch (it) {
    case RR_CONTINUE: return "Continue";
    case RR_VERIFY: return "Verify it further";
    case RR_TYPE: return "Type…";
    case RR_MATTA: return "待った!";
    case RR_SEND: return "Send";
    case RR_EDIT: return "Edit";
    default: return "Discard";
    }
}

static const char *reply_box_text(const Reply *r, const ReplyLayout *l) {
    return l->box == RB_DRAFT ? r->draft : r->sent;
}

static int reply_box_h(const Reply *r, const ReplyLayout *l, int *n_text) {
    *n_text = 0;
    if (l->box == RB_NONE) return 0;
    const char *t = reply_box_text(r, l);
    int n = layout_wrap(t, (int)strlen(t), SHEET_W - 28 - 28, measure_sans, NULL, g_rbox_lines, RBOX_TEXT_LINES + 1);
    *n_text = n < 1 ? 1 : n > RBOX_TEXT_LINES ? RBOX_TEXT_LINES : n;
    return 6 + 10 + 14 + *n_text * 27 + 8 + RBOX_BTN_H + 10;
}

/* What the one-line peek says: a chip, a line of text and a note on the right. */
static void reply_peek_line(const Reply *r, uint64_t now, const char **chip, unsigned *chip_c, const char **title,
                            const char **side) {
    *chip = "REPLY";
    *chip_c = C_TEAL;
    *side = "";
    *title = r->sent;
    bool stop = r->kind == RK_STOP;
    bool fresh = now - r->status_ms < REPLY_FADE_MS;
    switch (r->status) {
    case RS_SENDING:
        if (stop) *chip = "待った!", *chip_c = C_ROSE, *title = "Sending…";
        else *side = "Sending…";
        return;
    case RS_QUEUED:
        *chip = "QUEUED", *chip_c = C_AMBER, *side = "Sends when this turn ends";
        return;
    case RS_STOPPING:
        *chip = "待った!", *chip_c = C_ROSE, *title = "Stopping current turn…";
        return;
    case RS_FAILED:
        *chip = "NOT CONFIRMED", *chip_c = C_DIM;
        *title = r->draft[0] ? r->draft : stop ? "Stop could not be confirmed" : r->sent;
        *side = r->draft[0] ? "Show" : "";
        return;
    default:
        break;
    }
    if (fresh && r->status == RS_DELIVERED) { *side = "Delivered"; return; }
    if (fresh && r->status == RS_STOPPED) { *chip = "待った!", *chip_c = C_ROSE, *title = "Stopped"; return; }
    if (fresh && r->status == RS_IGNORED) { *chip = "待った!", *chip_c = C_DIM, *title = stop ? "Already idle" : "Reply not sent"; return; }
    if (fresh && r->status == RS_CANCELLED) { *chip = "REPLY", *chip_c = C_DIM, *title = "Reply withdrawn"; return; }
    *chip = "DRAFT";
    *title = r->draft;
    *side = "Show";
}

static void draw_reply_header(int sy, const char *chip, unsigned chip_c, const char *title, bool dim_title,
                              const char *side, bool square) {
    int cw = gfx_text_width(F_MONO_MED, 11, chip, -1) + (int)strlen(chip) + 15;
    gfx_round_rect(SHEET_X + 16, sy + 14, cw, 20, 4, chip_c);
    gfx_text_spaced(F_MONO_MED, SHEET_X + 24, sy + 17, C_BG, 11, 1, chip);
    int sx = SHEET_X + SHEET_W - 16 - gfx_text_width(F_MONO, 12, side, -1);
    gfx_text(F_MONO, sx, sy + 17, C_MUTED, 12, side, -1);
    if (square) gfx_glyph(GLYPH_SQUARE, sx - 20, sy + 17, 14, C_SHEET);
    int tx = SHEET_X + 16 + cw + 10;
    text_fit(F_SANS, tx, sy + 13, dim_title ? C_MUTED : C_TEXT, 16, title, sx - 30 - tx, true);
}

static void draw_reply(const Session *s, uint64_t now) {
    const Reply *r = &s->reply;
    bool idle = model_reply_idle(s), working = model_reply_working(s);
    ReplyLayout l = reply_layout(r, idle, working);
    bool open = r->drawer == RD_OPEN && (l.n_rows > 0 || l.box != RB_NONE);
    bool peek = !open && reply_has_peek(r, now);
    int n_text = 0, box_h = reply_box_h(r, &l, &n_text);
    int open_h = PEEK_H + 8 + l.n_rows * RROW_H + box_h + 10;
    float target = open ? (float)(HINT_Y - open_h) : peek ? (float)SHEET_PEEK_TOP : (float)SCREEN_H;
    g_reply_y += (target - g_reply_y) * 0.22f;
    if (fabsf(target - g_reply_y) < 0.5f) g_reply_y = target;
    int sy = (int)g_reply_y;
    if (sy >= HINT_Y) return;

    vita2d_enable_clipping();
    gfx_clip(0, TOP_H, SCREEN_W, HINT_Y);
    if (open) gfx_rect(0, TOP_H, SCREEN_W, HINT_Y - TOP_H, RGBA8(0x0A, 0x08, 0x06, 0x60));
    gfx_round_rect(SHEET_X, sy, SHEET_W, HINT_Y - sy + 20, 14, C_LINE);
    gfx_round_rect(SHEET_X + 1, sy + 1, SHEET_W - 2, HINT_Y - sy + 20, 13, C_SHEET);
    gfx_round_rect(SCREEN_W / 2 - 18, sy + 5, 36, 3, 1, C_LINE);

    if (!open) {
        const char *chip, *title, *side;
        unsigned chip_c;
        reply_peek_line(r, now, &chip, &chip_c, &title, &side);
        draw_reply_header(sy, chip, chip_c, title, false, side, !strcmp(side, "Show"));
        hit(HIT_REPLY_PEEK, 0, SHEET_X, sy, SHEET_W, PEEK_H);
        vita2d_disable_clipping();
        return;
    }
    bool pending = l.box != RB_NONE;
    draw_reply_header(sy, "REPLY", C_TEAL, idle ? "Reply to agent" : "Agent is working", false,
                      pending ? "Fold" : "Close", true);
    hit(HIT_REPLY_PEEK, 0, SHEET_X, sy, SHEET_W, PEEK_H);
    gfx_rect(SHEET_X + 1, sy + PEEK_H, SHEET_W - 2, 1, C_LINE2);

    int x = SHEET_X + 14, w = SHEET_W - 28, y = sy + PEEK_H + 8;
    for (int i = 0; i < l.n_rows; i++, y += RROW_H) {
        bool on = r->line == i;
        if (on) gfx_panel(x, y, w, RROW_H - 2, 9, C_LINE, C_TABON);
        else if (i > 0 && r->line != i - 1) gfx_rect(x + 8, y, w - 16, 1, C_LINE2);
        ReplyItem it = l.rows[i];
        bool matta = it == RR_MATTA;
        gfx_text(matta ? F_SANS_MED : F_SANS, x + 16, y + 10, matta ? C_ROSE : C_TEXT, BODY_SIZE, reply_item_label(it), -1);
        const char *note = matta ? "stop now" : it == RR_TYPE ? "keyboard" : "";
        int nx = x + w - 16 - gfx_text_width(F_MONO, 12, note, -1) - (on ? 26 : 0);
        gfx_text(F_MONO, nx, y + 14, C_MUTED, 12, note, -1);
        if (on) gfx_glyph(GLYPH_CROSS, x + w - 30, y + 14, 14, C_TABON);
        hit(HIT_REPLY_ROW, i * 4, x, y, w, RROW_H);
    }
    if (pending) {
        bool queued = l.box == RB_QUEUED;
        int by = y + 6, bh = box_h - 6 - 4;
        gfx_panel(x, by, w, bh, 10, queued ? C_AMBER_DEEP : C_LINE2, C_PANEL);
        gfx_text_spaced(F_MONO_MED, x + 14, by + 10, queued ? C_AMBER : C_MUTED, 10, 1,
                        queued ? "QUEUED · SENDS WHEN THIS TURN ENDS" : "DRAFT");
        const char *t = reply_box_text(r, &l);
        int ty = by + 26;
        for (int k = 0; k < n_text; k++, ty += 27) {
            bool last = k == n_text - 1;
            const LineSpan *sp = &g_rbox_lines[k];
            if (last) text_fit(F_SANS, x + 14, ty, C_TEXT, BODY_SIZE, t + sp->start, w - 28, false);
            else gfx_text(F_SANS, x + 14, ty, C_TEXT, BODY_SIZE, t + sp->start, sp->len);
        }
        int gap = 8, bw = (w - 28 - gap * (l.n_box - 1)) / l.n_box, btn_y = ty + 8;
        for (int c = 0; c < l.n_box; c++) {
            int bx = x + 14 + c * (bw + gap);
            bool on = r->line == l.n_rows && r->col == c;
            unsigned fill = l.box_items[c] == RR_DISCARD ? C_ROSE : C_AMBER; /* like Remove in the delete dialog */
            gfx_panel(bx, btn_y, bw, RBOX_BTN_H, 9, on ? fill : C_LINE, on ? fill : C_PANEL);
            const char *lab = reply_item_label(l.box_items[c]);
            int tw = gfx_text_width(F_SANS, 14, lab, -1) + (on ? 21 : 0), tx = bx + (bw - tw) / 2;
            if (on) {
                gfx_glyph(GLYPH_CROSS, tx, btn_y + (RBOX_BTN_H - 14) / 2, 14, fill);
                tx += 21;
            }
            gfx_text(F_SANS, tx, btn_y + (RBOX_BTN_H - 14) / 2 - 3, on ? C_BG : C_TEXT2, 14, lab, -1);
            hit(HIT_REPLY_ROW, l.n_rows * 4 + c, bx, btn_y, bw, RBOX_BTN_H);
        }
    }
    vita2d_disable_clipping();
}

/* ---------- overlays ---------- */

static void draw_overlays(const UiView *v, uint64_t now) {
    if (v->banner_project[0] && now < v->banner_until_ms) {
        gfx_panel(PAD, 52, SCREEN_W - 2 * PAD, 44, 9, C_TEAL_DEEP, C_BANNER);
        gfx_ring(PAD + 22, 74, 6, 2, C_TEAL, C_BANNER);
        int x = gfx_text(F_SANS_MED, PAD + 38, 63, C_TEXT, TEXT_SIZE, v->banner_project, -1);
        gfx_text(F_SANS, x + 8, 63, C_MUTED, TEXT_SIZE, "is idle · finished its turn", -1);
    }
    if (v->toast[0] && now < v->toast_until_ms) {
        int w = gfx_text_width(F_SANS, 14, v->toast, -1) + 32;
        gfx_panel((SCREEN_W - w) / 2, SCREEN_H - 150, w, 40, 9, C_LINE, C_TABON);
        gfx_text(F_SANS, (SCREEN_W - w) / 2 + 16, SCREEN_H - 140, C_TEXT, 14, v->toast, -1);
    }
    gfx_scanlines();
    if (v->dimmed) {
        gfx_rect(0, 0, SCREEN_W, SCREEN_H, RGBA8(0, 0, 0, 0xFF));
        const char *t = "PRESS ANY BUTTON";
        gfx_text_spaced(F_MONO, SCREEN_W - 20 - gfx_text_width(F_MONO, 12, t, -1) - 15, SCREEN_H - 36,
                        RGBA8(0x2B, 0x24, 0x20, 0xFF), 12, 1, t);
    }
}

static const Theme *agent_theme(const Session *s) {
    return &g_themes[!s || s->agent == AGENT_UNKNOWN ? THEME_APP :
                     s->agent == AGENT_CODEX ? THEME_CODEX :
                     s->agent == AGENT_KIRO ? THEME_KIRO : THEME_CLAUDE];
}

static void use_app_theme(void) {
    g_theme = &g_themes[THEME_APP];
    gfx_rect(0, 0, SCREEN_W, SCREEN_H, C_BG);
}

static void use_session_theme(const Model *m) {
    const Session *s = m->cur >= 0 && m->cur < m->n ? &m->s[m->cur] : NULL;
    g_theme = agent_theme(s);
    gfx_rect(0, 0, SCREEN_W, SCREEN_H, C_BG);
}

void ui_draw(const Model *m, const UiView *v, uint64_t now) {
    use_session_theme(m);
    g_n_hits = 0;
    g_hint_table = false;
    g_hint_reply = false;
    draw_top(m, v, true, now);
    const Session *s = m->cur >= 0 ? &m->s[m->cur] : NULL;
    const Request *r = model_current_request(m);
    if (!s) {
        const char *t1 = "No sessions yet";
        const char *t2 = "Connect an agent on your computer to see its sessions here.";
        const char *t3 = "You’ll get a chime when it needs a decision.";
        gfx_text(F_SANS, (SCREEN_W - gfx_text_width(F_SANS, 22, t1, -1)) / 2, 222, C_TEXT, 22, t1, -1);
        gfx_text(F_SANS, (SCREEN_W - gfx_text_width(F_SANS, 14, t2, -1)) / 2, 262, C_MUTED, 14, t2, -1);
        gfx_text(F_SANS, (SCREEN_W - gfx_text_width(F_SANS, 14, t3, -1)) / 2, 284, C_MUTED, 14, t3, -1);
        draw_hints(HINT_PLAIN, m->mode);
        g_sheet_y = SCREEN_H;
        draw_overlays(v, now);
        return;
    }
    draw_head(m, s, now);
    bool reply_open = !r && s->reply.drawer == RD_OPEN && (model_reply_idle(s) || model_reply_working(s));
    bool reply_peek = !r && !reply_open && reply_has_peek(&s->reply, now);
    draw_turn(m, s, v, r || reply_peek ? SHEET_PEEK_TOP - 8 : HINT_Y - 8, v->turn_scroll);
    if (r) {
        g_reply_y = SCREEN_H; /* the decision has the bottom; the reply peek returns after it */
        draw_sheet(m, s, r, v, now);
        draw_hints(m->sheet_hidden ? HINT_PEEK : r->kind == REQ_QUESTION ? HINT_QUESTION : HINT_APPROVAL, m->mode);
    } else {
        g_sheet_y = SCREEN_H;
        g_sheet_rid[0] = 0;
        g_hint_reply = model_reply_idle(s) || model_reply_working(s);
        draw_reply(s, now);
        draw_hints(reply_open ? HINT_REPLY : reply_peek ? HINT_REPLY_PEEK : HINT_PLAIN, m->mode);
    }
    draw_overlays(v, now);
}

/* ---------- session list ---------- */

static void state_label(const Session *s, uint64_t now, char *out, size_t cap) {
    if (s->n_reqs) snprintf(out, cap, "Needs you · %d pending", s->n_reqs);
    else if (s->state == ST_RUNNING) snprintf(out, cap, "Working");
    else if (s->state == ST_WAITING) snprintf(out, cap, "Waiting on your computer");
    else if (s->state == ST_IDLE) {
        char ago[24];
        format_ago(ago, sizeof ago, now - s->idle_since_ms);
        snprintf(out, cap, s->unseen ? "Finished · not seen · %s" : "Idle · %s", ago);
    } else snprintf(out, cap, "Unknown");
}

/* Agent marks are confined to the session list. */
static void draw_agent_icon(AgentKind agent, int cx, int cy) {
    if ((agent == AGENT_CLAUDE || agent == AGENT_CODEX || agent == AGENT_KIRO) && g_agent_icons[agent])
        vita2d_draw_texture(g_agent_icons[agent], cx - 12, cy - 12);
    else gfx_text(F_MONO, cx - 8, cy - 7, C_MUTED, 12,
                  agent == AGENT_CLAUDE ? "CL" : agent == AGENT_CODEX ? "CX" :
                  agent == AGENT_KIRO ? "KI" : "?", -1);
}

static void draw_capabilities(const Model *m, const SessionList *l) {
    const Session *s = NULL;
    for (int i = 0; i < m->n; i++)
        if (!strcmp(m->s[i].sid, l->tooltip_sid)) s = &m->s[i];
    const Theme *parent_theme = g_theme;
    g_theme = agent_theme(s);
    g_n_hits = 0;
    gfx_rect(0, TOP_H, SCREEN_W, HINT_Y - TOP_H, C_SCRIM);
    int w = 760, h = 324, x = (SCREEN_W - w) / 2, y = (HINT_Y - h) / 2;
    gfx_panel(x, y, w, h, 12, C_LINE, C_BAR);
    char title[128];
    snprintf(title, sizeof title, "%s · Capabilities", s ? s->project : "Session ended");
    text_fit(F_SANS_MED, x + 24, y + 20, C_TEXT, 22, title, w - 48, false);
    char intro[100];
    snprintf(intro, sizeof intro, "%s · Available controls for this session.", s ? agent_name(s->agent) : "Agent");
    gfx_text(F_SANS, x + 24, y + 55, C_MUTED, 14, intro, -1);
    static const char *const labels[CAP_COUNT] = {"Approvals", "Questions", "Replies", "Stop"};
    static const char *const descriptions[CAP_COUNT] = {
        "Approve or deny a tool request.", "Answer a question from the agent.",
        "Send a message to continue a session.", "Ask the session to stop its current turn."
    };
    for (int i = 0; i < CAP_COUNT; i++) {
        int ry = y + 96 + i * 42;
        Capability cap = session_capability(s, (CapabilityKind)i);
        const char *value = cap == CAP_SUPPORTED ? "SUPPORTED" :
                            cap == CAP_UNSUPPORTED ? "UNSUPPORTED" : "UNVERIFIED";
        gfx_text(F_SANS_MED, x + 24, ry, C_TEXT, 16, labels[i], -1);
        gfx_text(F_SANS, x + 144, ry + 2, C_MUTED, 13, descriptions[i], -1);
        gfx_text(F_MONO_MED, x + w - 24 - gfx_text_width(F_MONO_MED, 11, value, -1), ry + 4,
                 cap == CAP_SUPPORTED ? C_BRAND : C_MUTED, 11, value, -1);
        gfx_rect(x + 24, ry + 31, w - 48, 1, C_LINE2);
    }
    gfx_text_spaced(F_MONO, x + 24, y + h - 34, C_MUTED, 11, 1, "PRESS ANY BUTTON TO CLOSE");
    g_theme = parent_theme; /* the page and its footer remain application themed */
    draw_hints(HINT_CAPABILITIES, m->mode);
}

void ui_draw_sessions(const Model *m, const SessionList *l, const UiView *v, uint64_t now) {
    use_app_theme();
    g_n_hits = 0;
    draw_top(m, v, false, now);
    char hdr[32];
    snprintf(hdr, sizeof hdr, "SESSIONS · %d", m->n);
    gfx_text_spaced(F_MONO_MED, PAD, 56, C_MUTED, 11, 2, hdr);
    int top = 80, rowh = 62, area = HINT_Y - 8 - top;
    int off = (l->cursor + 1) * rowh - area;
    if (off < 0) off = 0;
    vita2d_enable_clipping();
    gfx_clip(0, top, SCREEN_W, HINT_Y - 4);
    for (int i = 0; i < m->n; i++) {
        const Session *s = &m->s[i];
        int y = top + i * rowh - off;
        if (y + rowh < top || y > HINT_Y) continue;
        bool on = i == l->cursor;
        if (on) gfx_panel(PAD, y, SCREEN_W - 2 * PAD, 58, 9, C_LINE, C_TABON);
        draw_agent_icon(s->agent, PAD + 24, y + 29);
        draw_state_dot(s, PAD + 58, y + 29, 6, on ? C_TABON : C_BG, now);
        int x = gfx_text(F_SANS, PAD + 76, y + 8, C_TEXT, BODY_SIZE, s->project, -1);
        if (i == m->cur) gfx_text(F_MONO, x + 8, y + 12, C_DIM, 11, "· showing", -1);
        text_fit(F_MONO, PAD + 76, y + 34, C_DIM, 11, s->cwd, SCREEN_W - 414, false);
        char st[64];
        state_label(s, now, st, sizeof st);
        gfx_text(F_MONO, SCREEN_W - PAD - 14 - gfx_text_width(F_MONO, 12, st, -1), y + 21, s->n_reqs ? C_AMBER : C_MUTED, 12, st, -1);
        /* only the part inside the list area takes taps */
        int y0 = y < top ? top : y, y1 = y + 58 > HINT_Y - 4 ? HINT_Y - 4 : y + 58;
        if (y1 > y0) hit(HIT_LIST_ROW, i, PAD, y0, SCREEN_W - 2 * PAD, y1 - y0);
    }
    if (m->n == 0) gfx_text(F_SANS, PAD + 14, top + 12, C_MUTED, 16, "No sessions", -1);
    vita2d_disable_clipping();

    const char *confirm_project = NULL;
    bool confirm_kiro = false; /* the Vita is a Kiro session's only interface: removing ends it */
    for (int i = 0; l->confirming && i < m->n; i++)
        if (!strcmp(m->s[i].sid, l->confirm_sid)) {
            confirm_project = m->s[i].project;
            confirm_kiro = m->s[i].agent == AGENT_KIRO;
        }
    if (l->tooltip) {
        draw_capabilities(m, l);
    } else if (confirm_project) {
        g_n_hits = 0; /* the dialog is modal: the list underneath takes no taps */
        gfx_rect(0, 0, SCREEN_W, HINT_Y, C_SCRIM);
        int bw = 540, bh = 168, bx = (SCREEN_W - bw) / 2, by = (SCREEN_H - bh) / 2 - 20;
        gfx_panel(bx, by, bw, bh, 12, C_LINE, C_BAR);
        char t[128];
        snprintf(t, sizeof t, confirm_kiro ? "End the Kiro session in %s?" : "Remove %s from the list?", confirm_project);
        text_fit(F_SANS, bx + 20, by + 18, C_TEXT, 19, t, bw - 40, false);
        if (confirm_kiro) {
            gfx_text(F_SANS, bx + 20, by + 54, C_MUTED, 14, "This ends the conversation on the computer.", -1);
        } else {
            gfx_text(F_SANS, bx + 20, by + 54, C_MUTED, 14, "It comes back the next time the agent reports from that", -1);
            gfx_text(F_SANS, bx + 20, by + 75, C_MUTED, 14, "session. Nothing on your computer changes.", -1);
        }
        const char *labels[2] = {"Keep", confirm_kiro ? "End" : "Remove"};
        for (int i = 0; i < 2; i++) {
            int w = (bw - 50) / 2, x = bx + 20 + i * (w + 10), y = by + bh - 58, h = 42;
            bool on = i == l->confirm_sel;
            unsigned fill = on ? (i == 1 ? C_ROSE : C_BRAND) : C_BAR;
            gfx_panel(x, y, w, h, 9, on ? fill : C_LINE, fill);
            int tw = gfx_text_width(F_SANS, 15, labels[i], -1) + (on ? 21 : 0), tx = x + (w - tw) / 2;
            if (on) {
                gfx_glyph(GLYPH_CROSS, tx, y + 14, 14, fill);
                tx += 21;
            }
            gfx_text(F_SANS, tx, y + 11, on ? C_BG : C_TEXT2, 15, labels[i], -1);
            hit(HIT_DIALOG, i, x, y, w, h);
            uint64_t t2 = now - l->confirm_at;
            if (on && t2 < LOCKOUT_MS) gfx_rect(x + 2, y + h - 4, (int)((w - 4) * t2 / LOCKOUT_MS), 3, RGBA8(0x17, 0x12, 0x0F, 0x80));
        }
        draw_hints(HINT_CONFIRM, m->mode);
    } else {
        draw_hints(HINT_LIST, m->mode);
    }
    UiView ov = *v;
    ov.banner_project[0] = 0;
    if (l->toast[0] && now < l->toast_until_ms) {
        snprintf(ov.toast, sizeof ov.toast, "%s", l->toast);
        ov.toast_until_ms = l->toast_until_ms;
    }
    draw_overlays(&ov, now);
}

/* ---------- settings ---------- */

void ui_draw_settings(const Model *m, const AppConfig *c, const SettingsState *s, int connected_idx,
                      const UiView *v, uint64_t now) {
    use_app_theme();
    g_n_hits = 0;
    draw_top(m, v, false, now);
    gfx_text_spaced(F_MONO_MED, PAD, 56, C_MUTED, 11, 2, "SETTINGS · MQTT BROKERS");

    int n = settings_row_count(c), area_top = 80, area_h = HINT_Y - 8 - area_top;
    int ys[MAX_BROKERS + 5], y = 0;
    for (int i = 0; i < n; i++) {
        SettingsRow row = settings_row_at(c, i);
        if (row == ROW_EXIT) y += 17;
        ys[i] = y;
        y += 44;
        if (row == ROW_ADD) y += 17;
    }
    int offset = 0;
    if (y > area_h) {
        offset = ys[s->cursor] + 22 - area_h / 2;
        if (offset < 0) offset = 0;
        if (offset > y - area_h) offset = y - area_h;
    }
    for (int i = 0; i < n; i++) {
        int ry = area_top + ys[i] - offset;
        if (ry < area_top - 2 || ry + 44 > HINT_Y) continue;
        SettingsRow row = settings_row_at(c, i);
        bool on = i == s->cursor;
        if (on) gfx_panel(PAD, ry, SCREEN_W - 2 * PAD, 44, 8, C_LINE, C_TABON);
        hit(HIT_SETTING, i, PAD, ry, SCREEN_W - 2 * PAD, 44);
        int ty = ry + 12;
        char buf[64];
        switch (row) {
        case ROW_BROKER: {
            snprintf(buf, sizeof buf, "%s:%d", c->brokers[i].host, c->brokers[i].port);
            int bx = gfx_text(F_MONO, PAD + 14, ty, on ? C_TEXT : C_TEXT2, 16, buf, -1);
            if (c->brokers[i].tls) { /* encrypted: stands in for a lock icon */
                gfx_panel(bx + 10, ty + 1, 34, 18, 4, C_TEAL_DEEP, on ? C_TABON : C_BG);
                gfx_text_spaced(F_MONO_MED, bx + 16, ty + 3, C_TEAL, 11, 1, "TLS");
            }
            const char *err = i == connected_idx ? NULL : settings_error_text(net_broker_error(i));
            const char *tag = i == connected_idx ? "● CONNECTED" : err ? err : i == c->active ? "CONNECTING…" : NULL;
            if (tag)
                gfx_text(F_MONO, SCREEN_W - PAD - 14 - gfx_text_width(F_MONO, 12, tag, -1), ty + 3,
                         i == connected_idx ? C_TEAL : err ? C_ROSE : C_AMBER, 12, tag, -1);
            break;
        }
        case ROW_ADD:
            gfx_text(F_SANS, PAD + 14, ty, C_BRAND, 16, "+ Add broker", -1);
            gfx_rect(PAD, ry + 52, SCREEN_W - 2 * PAD, 1, C_LINE2);
            break;
        case ROW_SOUND:
        case ROW_TOUCH:
        case ROW_DIM: {
            const char *label = row == ROW_SOUND ? "Sounds" : row == ROW_TOUCH ? "Front touch" : "Dim screen after";
            if (row == ROW_SOUND) snprintf(buf, sizeof buf, "%s", c->sound ? "ON" : "OFF");
            else if (row == ROW_TOUCH) snprintf(buf, sizeof buf, "%s", c->front_touch ? "ON" : "OFF");
            else if (c->dim_after_s) snprintf(buf, sizeof buf, "%d MIN", c->dim_after_s / 60);
            else snprintf(buf, sizeof buf, "NEVER");
            gfx_text(F_SANS, PAD + 14, ty, on ? C_TEXT : C_TEXT2, 16, label, -1);
            gfx_text(F_MONO, SCREEN_W - PAD - 14 - gfx_text_width(F_MONO, 13, buf, -1), ty + 2, C_BRAND, 13, buf, -1);
            break;
        }
        case ROW_EXIT:
            gfx_rect(PAD, ry - 9, SCREEN_W - 2 * PAD, 1, C_LINE2);
            gfx_text(F_SANS, PAD + 14, ty, C_ROSE, 16, "Exit Agent Remote", -1);
            break;
        }
    }
    draw_hints(HINT_SETTINGS, m->mode);
    UiView ov = *v;
    if (s->toast[0] && now < s->toast_until_ms) {
        snprintf(ov.toast, sizeof ov.toast, "%s", s->toast);
        ov.toast_until_ms = s->toast_until_ms;
    }
    ov.banner_project[0] = 0;
    draw_overlays(&ov, now);
}
