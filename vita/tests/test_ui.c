/* Exercise the real UI with a recording renderer, without a Vita GPU/font. */
#include <stdlib.h>
#include <vita2d.h>

#include "gfx.h"
#include "protocol.h"
#include "ui.h"
#include "util.h"
#include "test.h"

typedef struct { int x, y, w, h; unsigned color; char text[256]; } Draw;
static Draw draws[2048];
static int n_draws, n_icon_lines;
static int icon_draws, icon_loads, icon_frees;
static unsigned icon_pool_used;
static bool missing_icons;
static vita2d_texture icons[2] = {{AGENT_CLAUDE}, {AGENT_CODEX}};

vita2d_texture *vita2d_load_PNG_file(const char *path) {
    icon_loads++;
    if (missing_icons) return NULL;
    if (!strcmp(path, "app0:icons/claude.png")) return &icons[0];
    CHECK_STR(path, "app0:icons/codex.png");
    return &icons[1];
}

void vita2d_free_texture(vita2d_texture *texture) {
    CHECK(texture == &icons[0] || texture == &icons[1]);
    icon_frees++;
}

void vita2d_draw_texture(const vita2d_texture *texture, float x, float y) {
    CHECK(texture == &icons[0] || texture == &icons[1]);
    CHECK(x >= 0 && x + 24 <= SCREEN_W);
    icon_draws++;
    icon_pool_used += 4 * 20; /* SDK texture quad: 4 texture vertices */
}

static void record(int x, int y, int w, int h, unsigned color, const char *s, int n) {
    if (n_draws >= 2048) abort();
    Draw *d = &draws[n_draws++];
    *d = (Draw){.x=x, .y=y, .w=w, .h=h, .color=color};
    if (s) snprintf(d->text, sizeof d->text, "%.*s", n < 0 ? (int)strlen(s) : n, s);
}

bool gfx_init(void) { return true; }
int gfx_char_width(FontFace f, int size, const char *s, int nbytes) { return size / 2; }
int gfx_text_width(FontFace f, int size, const char *s, int n) {
    if (n < 0) n = (int)strlen(s);
    int w = 0;
    for (int i = 0; i < n;) {
        unsigned len = utf8_seq_len(s + i, (size_t)(n - i));
        i += len ? (int)len : 1;
        w += size / 2;
    }
    return w;
}
int gfx_text(FontFace f, int x, int y, unsigned c, int size, const char *s, int n) {
    int w = gfx_text_width(f, size, s, n);
    record(x, y, w, size, c, s, n);
    return x + w;
}
int gfx_text_spaced(FontFace f, int x, int y, unsigned c, int size, int spacing, const char *s) {
    return gfx_text(f, x, y, c, size, s, -1);
}
void gfx_rect(int x, int y, int w, int h, unsigned c) { record(x,y,w,h,c,NULL,0); }
void gfx_round_rect(int x, int y, int w, int h, int r, unsigned c) { gfx_rect(x,y,w,h,c); }
void gfx_panel(int x, int y, int w, int h, int r, unsigned border, unsigned fill) { gfx_rect(x,y,w,h,fill); }
void gfx_dashed_panel(int x, int y, int w, int h, unsigned c) { gfx_rect(x,y,w,h,c); }
void gfx_circle(int cx, int cy, int r, unsigned c) {}
void gfx_ring(int cx, int cy, int r, int thick, unsigned c, unsigned bg) {}
void gfx_ring_outline(int cx, int cy, int r, float thick, unsigned c) {}
void gfx_line(float x0, float y0, float x1, float y1, float thick, unsigned c) { n_icon_lines++; }
void gfx_glyph(Glyph g, int x, int y, int size, unsigned bg) {}
void gfx_scanlines(void) {}
void gfx_clip(int x0, int y0, int x1, int y1) {}
void vita2d_set_clear_color(unsigned color) {}
void vita2d_enable_clipping(void) {}
void vita2d_disable_clipping(void) {}
NetError net_broker_error(int index) { return NETERR_NONE; }

static bool text_is(const char *text, unsigned color) {
    for (int i = 0; i < n_draws; i++)
        if (!strcmp(draws[i].text, text) && draws[i].color == color) return true;
    return false;
}

static void session(Model *m, const char *sid, const char *status) {
    StatusMsg msg;
    CHECK(protocol_parse_status(status, strlen(status), &msg));
    model_on_status(m, sid, &msg, 0);
    const char *turn = "{\"items\":[{\"k\":\"you\",\"t\":\"prompt\"},"
                       "{\"k\":\"assistant\",\"t\":\"# Heading\\n\\nordinary `inline` **strong**\"}]}";
    TurnItem *items;
    int n;
    CHECK(protocol_parse_turn(turn, strlen(turn), &items, &n));
    model_on_turn(m, sid, items, n, 0);
}

static void t_theme_switch_preserves_semantic_colors_and_body(void) {
    Model m;
    model_init(&m, MODE_SOFA);
    session(&m, "claude-ui", "{\"state\":\"idle\"}");
    session(&m, "codex-ui", "{\"state\":\"idle\",\"agent\":\"codex\"}");
    UiView v = {.net=NET_UP, .battery_pct=-1};
    for (int agent = 0; agent < 2; agent++) {
        model_select(&m, agent, 0);
        n_draws = n_icon_lines = icon_draws = 0;
        ui_draw(&m, &v, 0);
        unsigned brand = agent ? RGBA8(0x10,0xa3,0x7f,255) : RGBA8(0xff,0xb3,0x5c,255);
        CHECK(text_is("AGENT", brand) && text_is("REMOTE", brand));
        CHECK(text_is("Heading", brand) && text_is("inline", brand));
        CHECK(text_is("YOU", RGBA8(0xe8,0x7a,0x90,255)));
        CHECK(text_is("SOFA", RGBA8(0xe8,0x7a,0x90,255)));
        CHECK(text_is("ordinary ", agent ? RGBA8(0xbe,0xd3,0xc7,255) : RGBA8(0xc9,0xbb,0xa9,255)));
        CHECK(text_is("strong", agent ? RGBA8(0xe7,0xf3,0xed,255) : RGBA8(0xf1,0xe6,0xd6,255)));
        CHECK(n_icon_lines == 0 && icon_draws == 0); /* no Agent icon on the main page */
        CHECK(draws[0].color == (agent ? RGBA8(0x0d,0x15,0x12,255) : RGBA8(0x17,0x12,0x0f,255)));
        bool you_line = false;
        for (int i = 0; i < n_draws; i++)
            if (draws[i].w == 3 && draws[i].color == RGBA8(0x61,0x33,0x3c,255)) you_line = true;
        CHECK(you_line);
    }
    m.mode = MODE_DESK;
    n_draws = 0;
    ui_draw(&m, &v, 0);
    CHECK(text_is("DESK", RGBA8(0x2e,0xa9,0xdf,255)));
    model_free(&m);
}

static void t_list_icons_and_modal_capabilities(void) {
    Model m;
    model_init(&m, MODE_SOFA);
    session(&m, "claude-list", "{\"state\":\"idle\"}");
    session(&m, "codex-list", "{\"state\":\"idle\",\"agent\":\"codex\","
            "\"capabilities\":{\"approval\":true,\"reply\":false}}");
    SessionList l;
    list_open(&l, &m);
    l.cursor = 1;
    UiView v = {.net=NET_UP, .battery_pct=-1};
    n_draws = n_icon_lines = icon_draws = 0;
    ui_draw_sessions(&m, &l, &v, 0);
    CHECK(n_icon_lines == 0 && icon_draws == 2);
    l.tooltip = true;
    snprintf(l.tooltip_sid, sizeof l.tooltip_sid, "%s", m.s[1].sid);
    n_draws = 0;
    ui_draw_sessions(&m, &l, &v, 0);
    unsigned muted = RGBA8(0x8d,0xa9,0x9a,255);
    CHECK(text_is("Codex · Available controls for this session.", muted));
    CHECK(text_is("SUPPORTED", RGBA8(0x10,0xa3,0x7f,255)));
    CHECK(text_is("UNSUPPORTED", muted) && text_is("UNVERIFIED", muted));
    CHECK(ui_hit_test(50,110).kind == HIT_NONE); /* no tap through to a row */
    CHECK(m.cur == 0 && m.mode == MODE_SOFA);
    model_free(&m);
}

static bool rect_is(int x, int y, int w, int h, unsigned color) {
    for (int i = 0; i < n_draws; i++) {
        const Draw *d = &draws[i];
        if (!d->text[0] && d->x == x && d->y == y && d->w == w && d->h == h && d->color == color) return true;
    }
    return false;
}

static void t_app_pages_and_highlighted_agent_popup_are_independent(void) {
    Model m;
    model_init(&m, MODE_SOFA);
    session(&m, "claude-app", "{\"state\":\"idle\"}");
    session(&m, "codex-app", "{\"state\":\"idle\",\"agent\":\"codex\",\"capabilities\":{\"reply\":true}}");
    UiView v = {.net=NET_UP, .battery_pct=-1};
    AppConfig cfg;
    config_defaults(&cfg);
    cfg.n_brokers = 0; /* all application settings fit on screen */
    SettingsState settings = {0};
    unsigned app = RGBA8(0xdd,0xd2,0x3b,255), bg = RGBA8(0x15,0x17,0x0f,255);
    for (int current = 0; current < 2; current++) {
        model_select(&m, current, 0);
        SessionList l;
        list_open(&l, &m);
        n_draws = 0;
        ui_draw_sessions(&m, &l, &v, 0);
        CHECK(text_is("AGENT", app) && text_is("REMOTE", app) && text_is("//", app));
        CHECK(rect_is(0, 0, SCREEN_W, SCREEN_H, bg));
        n_draws = 0;
        ui_draw_settings(&m, &cfg, &settings, -1, &v, 0);
        CHECK(text_is("AGENT", app) && text_is("+ Add broker", app));
        CHECK(text_is("ON", app));
        CHECK(rect_is(0, 0, SCREEN_W, SCREEN_H, bg));
        for (int highlighted = 0; highlighted < 2; highlighted++) {
            l.tooltip = true;
            l.cursor = highlighted;
            snprintf(l.tooltip_sid, sizeof l.tooltip_sid, "%s", m.s[highlighted].sid);
            n_draws = 0;
            ui_draw_sessions(&m, &l, &v, 0);
            CHECK(text_is("AGENT", app)); /* title bar stays application colored */
            unsigned brand = highlighted ? RGBA8(0x10,0xa3,0x7f,255) : RGBA8(0xff,0xb3,0x5c,255);
            unsigned panel = highlighted ? RGBA8(0x0f,0x1e,0x19,255) : RGBA8(0x21,0x1a,0x15,255);
            CHECK(text_is("SUPPORTED", brand));
            CHECK(rect_is(100, 95, 760, 324, panel));
            CHECK(rect_is(0, 514, SCREEN_W, 30, RGBA8(0x11,0x14,0x0c,255)));
            CHECK(m.cur == current && m.mode == MODE_SOFA);
        }
        n_draws = 0;
        ui_draw(&m, &v, 0);
        unsigned brand = current ? RGBA8(0x10,0xa3,0x7f,255) : RGBA8(0xff,0xb3,0x5c,255);
        CHECK(text_is("AGENT", brand)); /* returning to the conversation restores its Agent */
        CHECK(text_is("YOU", RGBA8(0xe8,0x7a,0x90,255)));
    }
    model_free(&m);
}

static void t_full_codex_list_fits_frame_pool(void) {
    Model m;
    model_init(&m, MODE_SOFA);
    for (int i = 0; i < MAX_SESSIONS; i++) {
        char sid[24];
        snprintf(sid, sizeof sid, "codex-budget-%d", i);
        session(&m, sid, "{\"state\":\"idle\",\"agent\":\"codex\"}");
    }
    UiView v = {.net=NET_UP, .battery_pct=-1};
    SessionList l;
    list_open(&l, &m);
    /* Exercise every scroll position and mixed lists, then the modal too. */
    for (int mix = 0; mix < 2; mix++) {
        if (mix) for (int i = 0; i < m.n; i += 2) m.s[i].agent = AGENT_CLAUDE;
        for (int row = 0; row < m.n; row++) {
            l.cursor = row;
            for (int modal = 0; modal < 2; modal++) {
                n_draws = n_icon_lines = icon_draws = 0;
                icon_pool_used = 0;
                l.tooltip = modal;
                snprintf(l.tooltip_sid, sizeof l.tooltip_sid, "%s", m.s[row].sid);
                ui_draw_sessions(&m, &l, &v, 0);
                CHECK(icon_draws > 0 && n_icon_lines == 0);
                CHECK(icon_pool_used < 1024); /* <1 KiB, formerly >0.8 MiB per icon */
                CHECK(icon_loads == 2); /* never decode/allocate textures per frame */
            }
        }
    }
    model_free(&m);
}

static void t_missing_artwork_falls_back_without_vector_drawing(void) {
    ui_fini();
    CHECK(icon_frees == 2);
    missing_icons = true;
    CHECK(ui_init());
    Model m;
    model_init(&m, MODE_SOFA);
    session(&m, "missing-icon", "{\"state\":\"idle\",\"agent\":\"codex\"}");
    SessionList l;
    list_open(&l, &m);
    UiView v = {.net=NET_UP, .battery_pct=-1};
    n_draws = n_icon_lines = icon_draws = 0;
    ui_draw_sessions(&m, &l, &v, 0);
    CHECK(icon_draws == 0 && n_icon_lines == 0);
    CHECK(text_is("CX", RGBA8(0x96,0x9b,0x77,255)));
    model_free(&m);
    ui_fini();
    CHECK(icon_frees == 2); /* NULL texture is never freed or drawn */
}

int main(void) {
    CHECK(ui_init());
    RUN(t_theme_switch_preserves_semantic_colors_and_body);
    RUN(t_list_icons_and_modal_capabilities);
    RUN(t_app_pages_and_highlighted_agent_popup_are_independent);
    RUN(t_full_codex_list_fits_frame_pool);
    RUN(t_missing_artwork_falls_back_without_vector_drawing);
    return TEST_RESULT();
}
