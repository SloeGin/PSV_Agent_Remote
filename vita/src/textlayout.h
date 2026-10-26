#pragma once

/* Pixel width of s[0..nbytes). */
typedef int (*MeasureFn)(const char *s, int nbytes, void *ud);

typedef struct {
    int start;
    int len;
} LineSpan;

/* Wrap UTF-8 text to max_w pixels: hard breaks at '\n', soft breaks after the
 * last space, otherwise between characters. Returns the number of lines. */
int layout_wrap(const char *text, int text_len, int max_w, MeasureFn fn, void *ud, LineSpan *out,
                int max_lines);

int layout_page_count(int n_lines, int lines_per_page);
