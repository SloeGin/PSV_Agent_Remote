#include "textlayout.h"

#include <stddef.h>

#include "util.h"

static void emit(const char *text, int start, int end, LineSpan *out, int *n) {
    if (end > start && text[end - 1] == '\r') end--;
    out[*n].start = start;
    out[*n].len = end - start;
    (*n)++;
}

int layout_wrap(const char *text, int text_len, int max_w, MeasureFn fn, void *ud, LineSpan *out,
                int max_lines) {
    int n = 0, pos = 0;
    while (pos < text_len && n < max_lines) {
        int start = pos, width = 0, last_space = -1;
        for (;;) {
            if (pos >= text_len) {
                emit(text, start, pos, out, &n);
                break;
            }
            if (text[pos] == '\n') {
                emit(text, start, pos, out, &n);
                pos++;
                break;
            }
            int clen = (int)utf8_seq_len(text + pos, (size_t)(text_len - pos));
            if (clen == 0) clen = 1;
            int cw = fn(text + pos, clen, ud);
            if (width + cw > max_w && pos > start) {
                if (last_space >= start) {
                    emit(text, start, last_space + 1, out, &n);
                    pos = last_space + 1;
                } else {
                    emit(text, start, pos, out, &n);
                }
                break;
            }
            if (text[pos] == ' ') last_space = pos;
            width += cw;
            pos += clen;
        }
    }
    return n;
}

int layout_page_count(int n_lines, int lines_per_page) {
    if (n_lines <= 0 || lines_per_page <= 0) return 1;
    return (n_lines + lines_per_page - 1) / lines_per_page;
}
