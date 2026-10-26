#include "test.h"
#include "textlayout.h"

/* ASCII is 10 px wide, any multi-byte character 20 px. */
static int mono(const char *s, int nbytes, void *ud) {
    (void)ud;
    int w = 0;
    for (int i = 0; i < nbytes; i++)
        if (((unsigned char)s[i] & 0xC0) != 0x80) w += ((unsigned char)s[i] < 0x80) ? 10 : 20;
    return w;
}

static void t_hard_newline(void) {
    LineSpan l[8];
    CHECK(layout_wrap("a\nb", 3, 100, mono, 0, l, 8) == 2);
    CHECK(l[1].start == 2 && l[1].len == 1);
}

static void t_crlf(void) {
    LineSpan l[8];
    CHECK(layout_wrap("ab\r\ncd", 6, 100, mono, 0, l, 8) == 2);
    CHECK(l[0].len == 2);
}

static void t_word_wrap(void) {
    LineSpan l[8];
    const char *s = "aaa bbb";
    int n = layout_wrap(s, 7, 50, mono, 0, l, 8);
    CHECK(n == 2);
    CHECK(l[0].len == 4 || l[0].len == 3);
    CHECK(memcmp("bbb", s + l[1].start, 3) == 0);
}

static void t_long_token_breaks_on_char_boundary(void) {
    char s[3 * 100 + 1];
    for (int i = 0; i < 100; i++) memcpy(s + 3 * i, "中", 3);
    s[300] = 0;
    LineSpan l[64];
    int n = layout_wrap(s, 300, 100, mono, 0, l, 64);
    CHECK(n == 20);
    for (int i = 0; i < n; i++) CHECK(l[i].start % 3 == 0 && l[i].len % 3 == 0);
}

static void t_progress_on_too_wide_char(void) {
    LineSpan l[8];
    CHECK(layout_wrap("中", 3, 5, mono, 0, l, 8) == 1);
    CHECK(layout_wrap("中中", 6, 5, mono, 0, l, 8) == 2);
}

static void t_max_lines(void) {
    LineSpan l[2];
    CHECK(layout_wrap("a\nb\nc\nd", 7, 100, mono, 0, l, 2) == 2);
}

static void t_empty(void) {
    LineSpan l[2];
    CHECK(layout_wrap("", 0, 100, mono, 0, l, 2) == 0);
}

static void t_invalid_utf8_progresses(void) {
    LineSpan l[8];
    CHECK(layout_wrap("a\xff\xfe" "b", 4, 100, mono, 0, l, 8) == 1);
}

static void t_pages(void) {
    CHECK(layout_page_count(0, 10) == 1);
    CHECK(layout_page_count(10, 10) == 1);
    CHECK(layout_page_count(21, 10) == 3);
}

int main(void) {
    RUN(t_hard_newline);
    RUN(t_crlf);
    RUN(t_word_wrap);
    RUN(t_long_token_breaks_on_char_boundary);
    RUN(t_progress_on_too_wide_char);
    RUN(t_max_lines);
    RUN(t_empty);
    RUN(t_invalid_utf8_progresses);
    RUN(t_pages);
    return TEST_RESULT();
}
