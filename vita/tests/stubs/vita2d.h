#pragma once

/* Minimal vita2d API used by ui.c; tests record drawing and texture lifetimes. */
#define RGBA8(r,g,b,a) (((unsigned)(a) << 24) | ((unsigned)(b) << 16) | ((unsigned)(g) << 8) | (unsigned)(r))
void vita2d_set_clear_color(unsigned color);
void vita2d_enable_clipping(void);
void vita2d_disable_clipping(void);
typedef struct { int agent; } vita2d_texture;
vita2d_texture *vita2d_load_PNG_file(const char *path);
void vita2d_free_texture(vita2d_texture *texture);
void vita2d_draw_texture(const vita2d_texture *texture, float x, float y);
