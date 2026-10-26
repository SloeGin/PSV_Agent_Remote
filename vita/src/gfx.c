#include "gfx.h"

#include <math.h>
#include <string.h>
#include <vita2d.h>

#include "input.h"
#include "util.h"
#include "view.h"

static const char *const FONT_FILES[F_COUNT] = {
    "app0:fonts/sans.ttf", "app0:fonts/sans-medium.ttf", "app0:fonts/mono.ttf", "app0:fonts/mono-medium.ttf"};
#define CJK_FONT_FILE "app0:fonts/cjk.otf"

#define MAX_SIZE 64
#define CJK F_COUNT

/* vita2d caches each glyph bitmap at the first size it is rendered and scales
 * it for every other size, which blurs text. One font instance per size keeps
 * every glyph rendered at exactly the size it is drawn. */
static vita2d_font *g_fonts[F_COUNT + 1][MAX_SIZE];
static short g_ascii_w[F_COUNT][MAX_SIZE][128]; /* [face][size][char], 0 = not measured yet */
static short g_cjk_w[MAX_SIZE];


static int clamp_size(int size) { return size < 1 || size >= MAX_SIZE ? 15 : size; }

static vita2d_font *font_at(int face, int size) {
    size = clamp_size(size);
    if (!g_fonts[face][size]) {
        g_fonts[face][size] = vita2d_load_font_file(face == CJK ? CJK_FONT_FILE : FONT_FILES[face]);
    }
    return g_fonts[face][size];
}

bool gfx_init(void) {
    bool ok = font_at(CJK, 15) != NULL;
    for (int i = 0; i < F_COUNT; i++) ok &= font_at(i, 15) != NULL;
    return ok;
}

static unsigned decode(const char *s, int n, int *len) {
    const unsigned char *u = (const unsigned char *)s;
    int l = (int)utf8_seq_len(s, (size_t)n);
    if (l == 0) {
        *len = 1;
        return '?';
    }
    *len = l;
    if (l == 1) return u[0];
    if (l == 2) return ((u[0] & 0x1Fu) << 6) | (u[1] & 0x3Fu);
    if (l == 3) return ((u[0] & 0x0Fu) << 12) | ((u[1] & 0x3Fu) << 6) | (u[2] & 0x3Fu);
    return ((u[0] & 0x07u) << 18) | ((u[1] & 0x3Fu) << 12) | ((u[2] & 0x3Fu) << 6) | (u[3] & 0x3Fu);
}

static bool is_latin(unsigned cp) {
    return cp < 0x100 || (cp >= 0x2010 && cp < 0x2028) || cp == 0x2212 || (cp >= 0x2190 && cp < 0x2200) || cp == 0x25CF;
}

static vita2d_font *font_for(FontFace f, int size, unsigned cp) { return font_at(is_latin(cp) ? (int)f : CJK, size); }

static int raw_width(vita2d_font *font, int size, const char *s, int n) {
    char buf[512];
    if (n > (int)sizeof buf - 1) n = (int)sizeof buf - 1;
    memcpy(buf, s, (size_t)n);
    buf[n] = 0;
    return vita2d_font_text_width(font, (unsigned)size, buf);
}

int gfx_char_width(FontFace f, int size, const char *s, int nbytes) {
    int len;
    unsigned cp = decode(s, nbytes, &len);
    size = clamp_size(size);
    if (cp == '\t') return 4 * gfx_char_width(f, size, " ", 1);
    if (cp < 128) {
        if (!g_ascii_w[f][size][cp]) g_ascii_w[f][size][cp] = (short)raw_width(font_at(f, size), size, s, 1);
        return g_ascii_w[f][size][cp];
    }
    if (!is_latin(cp) && cp >= 0x2E80) { /* CJK and full-width forms share one advance */
        if (!g_cjk_w[size]) g_cjk_w[size] = (short)raw_width(font_at(CJK, size), size, "中", 3);
        return g_cjk_w[size];
    }
    return raw_width(font_for(f, size, cp), size, s, len);
}

int gfx_text_width(FontFace f, int size, const char *s, int n) {
    if (n < 0) n = (int)strlen(s);
    int w = 0;
    for (int i = 0; i < n;) {
        int len;
        decode(s + i, n - i, &len);
        w += gfx_char_width(f, size, s + i, len);
        i += len;
    }
    return w;
}

/* Draw runs of same-font characters; returns the x after the text. */
int gfx_text(FontFace f, int x, int y, unsigned color, int size, const char *s, int n) {
    if (n < 0) n = (int)strlen(s);
    char buf[512];
    int i = 0;
    while (i < n) {
        int len;
        unsigned cp = decode(s + i, n - i, &len);
        vita2d_font *font = font_for(f, size, cp);
        int start = i, blen = 0;
        while (i < n) {
            unsigned c = decode(s + i, n - i, &len);
            if (font_for(f, size, c) != font || blen + len >= (int)sizeof buf) break;
            if (c == '\t') {
                memcpy(buf + blen, "    ", 4);
                blen += 4;
            } else {
                memcpy(buf + blen, s + i, (size_t)len);
                blen += len;
            }
            i += len;
        }
        buf[blen] = 0;
        vita2d_font_draw_text(font, x, y + size, color, (unsigned)size, buf);
        x += gfx_text_width(f, size, s + start, i - start);
    }
    return x;
}

int gfx_text_spaced(FontFace f, int x, int y, unsigned color, int size, int spacing, const char *s) {
    int n = (int)strlen(s);
    for (int i = 0; i < n;) {
        int len;
        decode(s + i, n - i, &len);
        x = gfx_text(f, x, y, color, size, s + i, len) + spacing;
        i += len;
    }
    return x - spacing;
}

void gfx_rect(int x, int y, int w, int h, unsigned color) {
    if (w > 0 && h > 0) vita2d_draw_rectangle((float)x, (float)y, (float)w, (float)h, color);
}

void gfx_circle(int cx, int cy, int r, unsigned color) { vita2d_draw_fill_circle((float)cx, (float)cy, (float)r, color); }

void gfx_round_rect(int x, int y, int w, int h, int r, unsigned color) {
    if (r * 2 > h) r = h / 2;
    if (r * 2 > w) r = w / 2;
    if (r <= 0) {
        gfx_rect(x, y, w, h, color);
        return;
    }
    gfx_rect(x + r, y, w - 2 * r, h, color);
    gfx_rect(x, y + r, r, h - 2 * r, color);
    gfx_rect(x + w - r, y + r, r, h - 2 * r, color);
    gfx_circle(x + r, y + r, r, color);
    gfx_circle(x + w - r - 1, y + r, r, color);
    gfx_circle(x + r, y + h - r - 1, r, color);
    gfx_circle(x + w - r - 1, y + h - r - 1, r, color);
}

void gfx_panel(int x, int y, int w, int h, int r, unsigned border, unsigned fill) {
    gfx_round_rect(x, y, w, h, r, border);
    gfx_round_rect(x + 1, y + 1, w - 2, h - 2, r > 1 ? r - 1 : 0, fill);
}

void gfx_dashed_panel(int x, int y, int w, int h, unsigned border) {
    for (int i = 0; i < w; i += 8) {
        int seg = i + 4 < w ? 4 : w - i;
        gfx_rect(x + i, y, seg, 1, border);
        gfx_rect(x + i, y + h - 1, seg, 1, border);
    }
    for (int i = 0; i < h; i += 8) {
        int seg = i + 4 < h ? 4 : h - i;
        gfx_rect(x, y + i, 1, seg, border);
        gfx_rect(x + w - 1, y + i, 1, seg, border);
    }
}

void gfx_ring(int cx, int cy, int r, int thick, unsigned color, unsigned bg) {
    gfx_circle(cx, cy, r, color);
    gfx_circle(cx, cy, r - thick, bg);
}

void gfx_ring_outline(int cx, int cy, int r, float thick, unsigned color) {
    int steps = r * 6;
    for (int i = 0; i < steps; i++) {
        float a = 6.2831853f * (float)i / (float)steps;
        vita2d_draw_fill_circle(cx + r * cosf(a), cy + r * sinf(a), thick / 2, color);
    }
}

void gfx_line(float x0, float y0, float x1, float y1, float thick, unsigned color) {
    float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
    int steps = (int)(len * 2) + 1;
    for (int i = 0; i <= steps; i++) {
        float t = (float)i / (float)steps;
        vita2d_draw_fill_circle(x0 + dx * t, y0 + dy * t, thick / 2, color);
    }
}

#define C_CROSS RGBA8(0x7A, 0xA7, 0xFF, 0xFF)
#define C_CIRCLE RGBA8(0xFF, 0x6B, 0x6B, 0xFF)
#define C_TRIANGLE RGBA8(0x3F, 0xD0, 0xA6, 0xFF)
#define C_SQUARE RGBA8(0xE4, 0x8B, 0xD2, 0xFF)

void gfx_glyph(Glyph g, int x, int y, int size, unsigned bg) {
    float k = size / 14.0f, t = 2.2f * k;
    switch (g) {
    case GLYPH_CROSS:
        gfx_line(x + 3 * k, y + 3 * k, x + 11 * k, y + 11 * k, t, C_CROSS);
        gfx_line(x + 11 * k, y + 3 * k, x + 3 * k, y + 11 * k, t, C_CROSS);
        break;
    case GLYPH_CIRCLE:
        gfx_ring(x + (int)(7 * k), y + (int)(7 * k), (int)(5.9f * k), (int)(t + 0.5f), C_CIRCLE, bg);
        break;
    case GLYPH_TRIANGLE:
        gfx_line(x + 7 * k, y + 2.4f * k, x + 12.2f * k, y + 11.3f * k, t, C_TRIANGLE);
        gfx_line(x + 12.2f * k, y + 11.3f * k, x + 1.8f * k, y + 11.3f * k, t, C_TRIANGLE);
        gfx_line(x + 1.8f * k, y + 11.3f * k, x + 7 * k, y + 2.4f * k, t, C_TRIANGLE);
        break;
    case GLYPH_SQUARE: {
        int s0 = (int)(1.5f * k), s1 = (int)(12.5f * k), th = (int)(t + 0.5f);
        gfx_rect(x + s0, y + s0, s1 - s0, th, C_SQUARE);
        gfx_rect(x + s0, y + s1 - th, s1 - s0, th, C_SQUARE);
        gfx_rect(x + s0, y + s0, th, s1 - s0, C_SQUARE);
        gfx_rect(x + s1 - th, y + s0, th, s1 - s0, C_SQUARE);
        break;
    }
    }
}

void gfx_scanlines(void) {
    for (int y = 0; y < SCREEN_H; y += 3) gfx_rect(0, y, SCREEN_W, 1, RGBA8(0xFF, 0xFF, 0xFF, 0x05));
}

void gfx_clip(int x0, int y0, int x1, int y1) {
    ClipRect r = clip_rect(x0, y0, x1, y1, SCREEN_W, SCREEN_H);
    vita2d_set_clip_rectangle(r.x0, r.y0, r.x1, r.y1);
}
