#pragma once

#include <stdbool.h>

/* Drawing helpers on top of vita2d: mixed Latin/CJK text, rounded panels,
 * thick strokes and the PlayStation face-button glyphs. */

typedef enum { F_SANS, F_SANS_MED, F_MONO, F_MONO_MED, F_COUNT } FontFace;
typedef enum { GLYPH_CROSS, GLYPH_CIRCLE, GLYPH_TRIANGLE, GLYPH_SQUARE } Glyph;

bool gfx_init(void);

/* Text in [s, s+n) (n < 0 means NUL-terminated). Characters outside Latin
 * are drawn with the CJK font. y is the top of the line box. */
int gfx_text_width(FontFace f, int size, const char *s, int n);
int gfx_text(FontFace f, int x, int y, unsigned color, int size, const char *s, int n);
/* Same, with extra spacing between characters (for small caps labels). */
int gfx_text_spaced(FontFace f, int x, int y, unsigned color, int size, int spacing, const char *s);

/* Width of one character; used by the line wrapper. */
int gfx_char_width(FontFace f, int size, const char *s, int nbytes);

void gfx_rect(int x, int y, int w, int h, unsigned color);
void gfx_round_rect(int x, int y, int w, int h, int r, unsigned color);
/* Rounded panel with a 1 px border (fill must be opaque). */
void gfx_panel(int x, int y, int w, int h, int r, unsigned border, unsigned fill);
void gfx_dashed_panel(int x, int y, int w, int h, unsigned border);
void gfx_circle(int cx, int cy, int r, unsigned color);
void gfx_ring(int cx, int cy, int r, int thick, unsigned color, unsigned bg);
/* Ring with a see-through middle (drawn as an outline). */
void gfx_ring_outline(int cx, int cy, int r, float thick, unsigned color);
void gfx_line(float x0, float y0, float x1, float y1, float thick, unsigned color);

/* PS face-button glyph in a size x size box at (x, y); bg is what is behind it. */
void gfx_glyph(Glyph g, int x, int y, int size, unsigned bg);

/* Faint CRT scanlines over the whole screen. */
void gfx_scanlines(void);

/* Set the clip rectangle (clipping must be enabled). Always use this instead of
 * vita2d_set_clip_rectangle: it clamps the rectangle to the screen first. */
void gfx_clip(int x0, int y0, int x1, int y1);
