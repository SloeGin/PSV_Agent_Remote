#include <stdio.h>
#include <stdlib.h>

#include "markdown.h"
#include "test.h"

static MdDoc doc;

static const char *T(const MdText *t) {
    static char buf[MD_MAX_TEXT + 1];
    memcpy(buf, doc.text + t->start, (size_t)t->len);
    buf[t->len] = 0;
    return buf;
}

static bool parse(const char *s) { return md_parse(s, (int)strlen(s), &doc); }

static const MdRun *run(const MdText *t, int i) { return &doc.runs[t->run_first + i]; }

static const char *R(const MdText *t, int i) {
    static char buf[MD_MAX_TEXT + 1];
    const MdRun *r = run(t, i);
    memcpy(buf, doc.text + r->start, (size_t)r->len);
    buf[r->len] = 0;
    return buf;
}

static const MdText *cell(const MdBlock *b, int row, int col) { return &doc.cells[b->cell_first + row * b->n_cols + col]; }

static void t_plain_paragraph(void) {
    CHECK(parse("hello\nworld"));
    CHECK(doc.n_blocks == 1 && doc.blocks[0].kind == MD_PARA);
    CHECK_STR(T(&doc.blocks[0].text), "hello\nworld");
    CHECK(doc.blocks[0].text.n_runs == 1 && run(&doc.blocks[0].text, 0)->style == 0);
}

static void t_bold_and_code(void) {
    CHECK(parse("a **b** `c`"));
    const MdText *t = &doc.blocks[0].text;
    CHECK_STR(T(t), "a b c");
    CHECK(t->n_runs == 4);
    CHECK_STR(R(t, 0), "a ");
    CHECK(run(t, 0)->style == 0);
    CHECK_STR(R(t, 1), "b");
    CHECK(run(t, 1)->style == MD_S_BOLD);
    CHECK_STR(R(t, 2), " ");
    CHECK_STR(R(t, 3), "c");
    CHECK(run(t, 3)->style == MD_S_CODE);
}

static void t_code_keeps_stars(void) {
    CHECK(parse("`**x**`"));
    const MdText *t = &doc.blocks[0].text;
    CHECK_STR(T(t), "**x**");
    CHECK(t->n_runs == 1 && run(t, 0)->style == MD_S_CODE);
}

static void t_unpaired_markers_literal(void) {
    const char *cases[] = {"a ** b", "x `y", "** **", "a**", "`"};
    for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
        CHECK(parse(cases[i]));
        const MdText *t = &doc.blocks[0].text;
        CHECK_STR(T(t), cases[i]);
        for (int k = 0; k < t->n_runs; k++) CHECK(run(t, k)->style == 0);
    }
}

static void t_heading(void) {
    CHECK(parse("## Title\nbody"));
    CHECK(doc.n_blocks == 2);
    CHECK(doc.blocks[0].kind == MD_HEADING && doc.blocks[0].level == 2);
    CHECK_STR(T(&doc.blocks[0].text), "Title");
    CHECK(doc.blocks[1].kind == MD_PARA);
    CHECK_STR(T(&doc.blocks[1].text), "body");
}

static void t_lists(void) {
    CHECK(parse("- a\n  - b\n12. c\n3) d"));
    CHECK(doc.n_blocks == 4);
    CHECK(doc.blocks[0].kind == MD_BULLET && doc.blocks[0].level == 0);
    CHECK_STR(T(&doc.blocks[0].text), "a");
    CHECK(doc.blocks[1].kind == MD_BULLET && doc.blocks[1].level == 1);
    CHECK_STR(T(&doc.blocks[1].text), "b");
    CHECK(doc.blocks[2].kind == MD_NUMBER);
    CHECK_STR(doc.blocks[2].marker, "12.");
    CHECK_STR(T(&doc.blocks[2].text), "c");
    CHECK(doc.blocks[3].kind == MD_NUMBER);
    CHECK_STR(doc.blocks[3].marker, "3)");
    CHECK_STR(T(&doc.blocks[3].text), "d");
}

static void t_fence(void) {
    CHECK(parse("x\n```c\nint a;\n  b\n```\ny"));
    CHECK(doc.n_blocks == 3);
    CHECK(doc.blocks[0].kind == MD_PARA);
    CHECK_STR(T(&doc.blocks[0].text), "x");
    CHECK(doc.blocks[1].kind == MD_CODE);
    CHECK_STR(T(&doc.blocks[1].text), "int a;\n  b");
    CHECK(doc.blocks[2].kind == MD_PARA);
    CHECK_STR(T(&doc.blocks[2].text), "y");
}

static void t_unclosed_fence(void) {
    CHECK(parse("```\nline1\nline2…"));
    CHECK(doc.n_blocks == 1 && doc.blocks[0].kind == MD_CODE);
    CHECK_STR(T(&doc.blocks[0].text), "line1\nline2…");
}

static void t_inline_triple_backticks_not_a_fence(void) {
    CHECK(parse("run ```ls -la``` now\nnext"));
    CHECK(doc.n_blocks == 1 && doc.blocks[0].kind == MD_PARA);
    CHECK(strstr(T(&doc.blocks[0].text), "ls -la") && strstr(T(&doc.blocks[0].text), "next"));
    CHECK(parse("```ls -la```\nafter"));
    CHECK(doc.n_blocks == 1 && doc.blocks[0].kind == MD_PARA);
    CHECK(strstr(T(&doc.blocks[0].text), "ls -la") && strstr(T(&doc.blocks[0].text), "after"));
}

static void t_closing_fence_must_be_bare(void) {
    CHECK(parse("```\na\n```js\nb\n```\nc"));
    CHECK(doc.n_blocks == 2 && doc.blocks[0].kind == MD_CODE);
    CHECK_STR(T(&doc.blocks[0].text), "a\n```js\nb");
    CHECK_STR(T(&doc.blocks[1].text), "c");
}

static void t_table(void) {
    CHECK(parse("| A | **B** |\n|---|:-:|\n| 1 | `2` |"));
    CHECK(doc.n_blocks == 1);
    const MdBlock *b = &doc.blocks[0];
    CHECK(b->kind == MD_TABLE && b->n_rows == 2 && b->n_cols == 2);
    CHECK_STR(T(cell(b, 0, 0)), "A");
    CHECK_STR(T(cell(b, 0, 1)), "B");
    CHECK(run(cell(b, 0, 1), 0)->style == MD_S_BOLD);
    CHECK_STR(T(cell(b, 1, 1)), "2");
    CHECK(run(cell(b, 1, 1), 0)->style == MD_S_CODE);
}

static void t_not_a_table(void) {
    CHECK(parse("| a | b |\nplain"));
    CHECK(doc.n_blocks == 1 && doc.blocks[0].kind == MD_PARA);
    CHECK_STR(T(&doc.blocks[0].text), "| a | b |\nplain");
}

static void t_ragged_table(void) {
    CHECK(parse("| a | b | c |\n|---|---|---|\n| 1 |\n| 1 | 2 | 3 | 4 |"));
    const MdBlock *b = &doc.blocks[0];
    CHECK(b->kind == MD_TABLE && b->n_rows == 3 && b->n_cols == 3);
    CHECK_STR(T(cell(b, 1, 0)), "1");
    CHECK_STR(T(cell(b, 1, 1)), "");
    CHECK_STR(T(cell(b, 1, 2)), "");
    CHECK_STR(T(cell(b, 2, 2)), "3 | 4");
}

static void t_capacity_falls_back(void) {
    char *s = malloc(300 * 4 + 1);
    for (int i = 0; i < 300; i++) memcpy(s + 4 * i, "- x\n", 4);
    s[1200] = 0;
    CHECK(!parse(s));
    free(s);
}

static void t_style_at(void) {
    CHECK(parse("a **b**"));
    CHECK(md_style_at(&doc, 2) == MD_S_BOLD); /* text is "a b" */
    CHECK(md_style_at(&doc, 0) == 0);
}

static void t_run_at(void) {
    CHECK(parse("ab `cd` e"));
    const MdRun *r = md_run_at(&doc, 4); /* text "ab cd e": 'd' */
    CHECK(r && r->start == 3 && r->len == 2 && r->style == MD_S_CODE);
    CHECK(md_run_at(&doc, 100) == NULL);
}

static int ten(const char *s, int n, void *ud) {
    (void)s;
    (void)ud;
    return 10 * n;
}

static char *table_src(const char *a, const char *b, const char *c) {
    static char buf[2048];
    if (c) snprintf(buf, sizeof buf, "| %s | %s | %s |\n|---|---|---|", a, b, c);
    else snprintf(buf, sizeof buf, "| %s | %s |\n|---|---|", a, b);
    return buf;
}

static char *xs(char ch, int n) {
    static char bufs[3][256];
    static int k;
    char *b = bufs[k++ % 3];
    memset(b, ch, (size_t)n);
    b[n] = 0;
    return b;
}

static void widths(const char *src, int avail, int *w, bool *over, int *total) {
    CHECK(parse(src));
    CHECK(doc.blocks[0].kind == MD_TABLE);
    *total = md_table_widths(&doc, &doc.blocks[0], avail, 0, ten, NULL, w, over);
}

static void t_widths_natural_fit(void) {
    int w[3], total;
    bool over;
    widths(table_src("aaaa", "bb", NULL), 100, w, &over, &total);
    CHECK(w[0] == 40 && w[1] == 20 && !over && total == 60);
}

static void t_widths_squeeze_fit(void) {
    int w[3], total;
    bool over;
    widths(table_src(xs('a', 15), "b", NULL), 100, w, &over, &total);
    CHECK(w[1] == 10 && w[0] == 90 && !over); /* a wraps to 2 lines */
}

static void t_widths_overflow(void) {
    int w[3], total;
    bool over;
    widths(table_src(xs('x', 50), xs('y', 50), NULL), 100, w, &over, &total);
    CHECK(w[0] == 60 && w[1] == 60 && total == 120 && over);
}

static void t_widths_overflow_short_col(void) {
    int w[3], total;
    bool over;
    widths(table_src(xs('x', 50), "ab", NULL), 100, w, &over, &total);
    CHECK(w[0] == 80 && w[1] == 20 && total == 100 && !over); /* capping would only make it narrower */
}

static void t_widths_overflow_three(void) {
    int w[3], total;
    bool over;
    widths(table_src(xs('x', 50), xs('y', 50), xs('z', 50)), 150, w, &over, &total);
    CHECK(w[0] == 90 && w[1] == 90 && w[2] == 90 && total == 270 && over);
}

int main(void) {
    RUN(t_plain_paragraph);
    RUN(t_bold_and_code);
    RUN(t_code_keeps_stars);
    RUN(t_unpaired_markers_literal);
    RUN(t_heading);
    RUN(t_lists);
    RUN(t_fence);
    RUN(t_unclosed_fence);
    RUN(t_inline_triple_backticks_not_a_fence);
    RUN(t_closing_fence_must_be_bare);
    RUN(t_table);
    RUN(t_not_a_table);
    RUN(t_ragged_table);
    RUN(t_capacity_falls_back);
    RUN(t_style_at);
    RUN(t_run_at);
    RUN(t_widths_natural_fit);
    RUN(t_widths_squeeze_fit);
    RUN(t_widths_overflow);
    RUN(t_widths_overflow_short_col);
    RUN(t_widths_overflow_three);
    return TEST_RESULT();
}
