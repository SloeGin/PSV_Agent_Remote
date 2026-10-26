#include "markdown.h"

#include <stdio.h>
#include <string.h>

#include "util.h"

typedef struct {
    MdDoc *doc;
    bool full; /* a capacity was exceeded */
} Ctx;

/* ---------- text and runs ---------- */

static void begin_text(Ctx *c, MdText *t) {
    t->start = c->doc->text_len;
    t->len = 0;
    t->run_first = c->doc->n_runs;
    t->n_runs = 0;
}

static void end_text(Ctx *c, MdText *t) {
    t->len = c->doc->text_len - t->start;
    t->n_runs = c->doc->n_runs - t->run_first;
}

/* Append bytes in one style, merging with the text's last run when it matches. */
static void put(Ctx *c, const MdText *t, const char *s, int n, unsigned style) {
    MdDoc *d = c->doc;
    for (int i = 0; i < n; i++) {
        if (s[i] == '\r') continue;
        if (d->text_len >= MD_MAX_TEXT) {
            c->full = true;
            return;
        }
        MdRun *last = d->n_runs > t->run_first ? &d->runs[d->n_runs - 1] : NULL;
        if (!last || last->style != style) {
            if (d->n_runs >= MD_MAX_RUNS) {
                c->full = true;
                return;
            }
            last = &d->runs[d->n_runs++];
            *last = (MdRun){d->text_len, 0, (unsigned char)style};
        }
        d->text[d->text_len++] = s[i];
        last->len++;
    }
}

static int find(const char *s, int from, int to, const char *pat) {
    int n = (int)strlen(pat);
    for (int i = from; i + n <= to; i++)
        if (!memcmp(s + i, pat, (size_t)n)) return i;
    return -1;
}

static bool all_spaces(const char *s, int a, int b) {
    for (int i = a; i < b; i++)
        if (s[i] != ' ' && s[i] != '\t' && s[i] != '\n' && s[i] != '\r') return false;
    return true;
}

/* Bold and inline code in s[a, b); unpaired markers stay literal. */
static void inline_md(Ctx *c, const MdText *t, const char *s, int a, int b, unsigned style) {
    int i = a;
    while (i < b && !c->full) {
        if (s[i] == '`') {
            int close = find(s, i + 1, b, "`");
            if (close > i + 1) {
                put(c, t, s + i + 1, close - i - 1, style | MD_S_CODE);
                i = close + 1;
                continue;
            }
        } else if (!(style & MD_S_BOLD) && i + 1 < b && s[i] == '*' && s[i + 1] == '*') {
            int close = find(s, i + 2, b, "**");
            if (close > i + 2 && !all_spaces(s, i + 2, close)) {
                inline_md(c, t, s, i + 2, close, style | MD_S_BOLD);
                i = close + 2;
                continue;
            }
            put(c, t, s + i, 2, style); /* both stars, so the pair is not re-read */
            i += 2;
            continue;
        }
        put(c, t, s + i, 1, style);
        i++;
    }
}

static void plain(Ctx *c, const MdText *t, const char *s, int a, int b) { put(c, t, s + a, b - a, 0); }

/* ---------- blocks ---------- */

static MdBlock *new_block(Ctx *c, MdKind kind) {
    MdDoc *d = c->doc;
    if (d->n_blocks >= MD_MAX_BLOCKS) {
        c->full = true;
        return NULL;
    }
    MdBlock *b = &d->blocks[d->n_blocks++];
    memset(b, 0, sizeof *b);
    b->kind = kind;
    return b;
}

static void text_block(Ctx *c, MdKind kind, int level, const char *s, int a, int b, bool md) {
    MdBlock *blk = new_block(c, kind);
    if (!blk) return;
    blk->level = level;
    begin_text(c, &blk->text);
    if (md) inline_md(c, &blk->text, s, a, b, 0);
    else plain(c, &blk->text, s, a, b);
    end_text(c, &blk->text);
}

static int skip_spaces(const char *s, int i, int end) {
    while (i < end && s[i] == ' ') i++;
    return i;
}

static void trim(const char *s, int *a, int *b) {
    while (*a < *b && (s[*a] == ' ' || s[*a] == '\t' || s[*a] == '\r')) (*a)++;
    while (*b > *a && (s[*b - 1] == ' ' || s[*b - 1] == '\t' || s[*b - 1] == '\r')) (*b)--;
}

/* ``` plus an info string; a backtick in it means inline code such as ```ls``` instead. */
static bool is_open_fence(const char *s, int a, int b) {
    int i = skip_spaces(s, a, b);
    if (i - a > 3 || i + 3 > b || memcmp(s + i, "```", 3)) return false;
    for (i += 3; i < b && s[i] == '`'; i++) {}
    for (; i < b; i++)
        if (s[i] == '`') return false;
    return true;
}

/* Only backticks and spaces, so no text after the fence is lost. */
static bool is_close_fence(const char *s, int a, int b) {
    int i = skip_spaces(s, a, b);
    if (i - a > 3 || i + 3 > b || memcmp(s + i, "```", 3)) return false;
    for (; i < b; i++)
        if (s[i] != '`' && s[i] != ' ' && s[i] != '\t' && s[i] != '\r') return false;
    return true;
}

/* Cell boundaries of a table row: the text between pipes, outer pipes dropped. */
static int split_row(const char *s, int a, int b, int *ca, int *cb, int max) {
    trim(s, &a, &b);
    if (a < b && s[a] == '|') a++;
    if (b > a && s[b - 1] == '|') b--;
    int n = 0, start = a;
    for (int i = a; i <= b && n < max; i++) {
        if (i == b || s[i] == '|') {
            ca[n] = start;
            cb[n] = i;
            n++;
            start = i + 1;
        }
    }
    return n;
}

static bool is_row(const char *s, int a, int b) {
    trim(s, &a, &b);
    return a < b && s[a] == '|';
}

static bool is_separator(const char *s, int a, int b) {
    if (!is_row(s, a, b)) return false;
    bool dash = false;
    for (int i = a; i < b; i++) {
        if (s[i] == '-') dash = true;
        else if (s[i] != '|' && s[i] != ':' && s[i] != ' ' && s[i] != '\t' && s[i] != '\r') return false;
    }
    return dash;
}

static void add_cell(Ctx *c, const char *s, int a, int b) {
    MdDoc *d = c->doc;
    if (d->n_cells >= MD_MAX_CELLS) {
        c->full = true;
        return;
    }
    MdText *t = &d->cells[d->n_cells++];
    trim(s, &a, &b);
    begin_text(c, t);
    inline_md(c, t, s, a, b, 0);
    end_text(c, t);
}

typedef struct {
    int a, b; /* line without its '\n' */
} Line;

#define MD_MAX_LINES 4096

bool md_parse(const char *src, int len, MdDoc *doc) {
    static Line lines[MD_MAX_LINES];
    doc->text_len = doc->n_blocks = doc->n_runs = doc->n_cells = 0;
    Ctx c = {doc, false};

    int n = 0;
    for (int i = 0, start = 0; i <= len; i++) {
        if (i == len || src[i] == '\n') {
            if (n >= MD_MAX_LINES) return false;
            lines[n++] = (Line){start, i};
            start = i + 1;
        }
    }

    int para_a = -1, para_b = -1;
#define FLUSH()                                                        \
    do {                                                               \
        if (para_a >= 0) text_block(&c, MD_PARA, 0, src, para_a, para_b, true); \
        para_a = -1;                                                   \
    } while (0)

    for (int li = 0; li < n && !c.full; li++) {
        int a = lines[li].a, b = lines[li].b;
        if (is_open_fence(src, a, b)) {
            FLUSH();
            int first = li + 1, last = first;
            while (last < n && !is_close_fence(src, lines[last].a, lines[last].b)) last++;
            int ca = first < n ? lines[first].a : len;
            int cb = last > first ? lines[last - 1].b : ca;
            text_block(&c, MD_CODE, 0, src, ca, cb, false);
            li = last; /* the closing fence, or past the end */
            continue;
        }
        if (all_spaces(src, a, b)) {
            FLUSH();
            continue;
        }
        int i = skip_spaces(src, a, b), indent = i - a;

        int hashes = 0;
        while (i + hashes < b && src[i + hashes] == '#') hashes++;
        if (indent <= 3 && hashes >= 1 && hashes <= 6 && i + hashes < b && src[i + hashes] == ' ') {
            FLUSH();
            int ta = i + hashes, tb = b;
            trim(src, &ta, &tb);
            text_block(&c, MD_HEADING, hashes, src, ta, tb, true);
            continue;
        }

        if (i + 1 < b && (src[i] == '-' || src[i] == '*' || src[i] == '+') && src[i + 1] == ' ') {
            FLUSH();
            int ta = i + 2, tb = b;
            trim(src, &ta, &tb);
            text_block(&c, MD_BULLET, indent >= 2 ? 1 : 0, src, ta, tb, true);
            continue;
        }

        int digits = 0;
        while (i + digits < b && src[i + digits] >= '0' && src[i + digits] <= '9') digits++;
        if (digits >= 1 && digits <= 9 && i + digits + 1 < b && (src[i + digits] == '.' || src[i + digits] == ')') &&
            src[i + digits + 1] == ' ') {
            FLUSH();
            int ta = i + digits + 2, tb = b;
            trim(src, &ta, &tb);
            text_block(&c, MD_NUMBER, indent >= 2 ? 1 : 0, src, ta, tb, true);
            if (doc->n_blocks && !c.full)
                snprintf(doc->blocks[doc->n_blocks - 1].marker, sizeof doc->blocks[0].marker, "%.*s", digits + 1, src + i);
            continue;
        }

        if (is_row(src, a, b) && li + 1 < n && is_separator(src, lines[li + 1].a, lines[li + 1].b)) {
            int ca[MD_MAX_COLS + 1], cb[MD_MAX_COLS + 1];
            int cols = split_row(src, a, b, ca, cb, MD_MAX_COLS + 1);
            if (cols >= 1 && cols <= MD_MAX_COLS) {
                FLUSH();
                MdBlock *blk = new_block(&c, MD_TABLE);
                if (!blk) break;
                blk->cell_first = doc->n_cells;
                blk->n_cols = cols;
                for (int k = 0; k < cols; k++) add_cell(&c, src, ca[k], cb[k]);
                blk->n_rows = 1;
                int ri = li + 2;
                for (; ri < n && is_row(src, lines[ri].a, lines[ri].b) && !c.full; ri++) {
                    int got = split_row(src, lines[ri].a, lines[ri].b, ca, cb, MD_MAX_COLS + 1);
                    if (got > cols) { /* extra cells go into the last one, pipes and all */
                        int ra = lines[ri].a, rb = lines[ri].b;
                        trim(src, &ra, &rb);
                        if (rb > ra && src[rb - 1] == '|') rb--;
                        cb[cols - 1] = rb;
                    }
                    for (int k = 0; k < cols; k++) {
                        if (k < got) add_cell(&c, src, ca[k], cb[k]);
                        else add_cell(&c, src, 0, 0);
                    }
                    blk->n_rows++;
                }
                li = ri - 1;
                continue;
            }
        }

        if (para_a < 0) para_a = a;
        para_b = b;
    }
    FLUSH();
#undef FLUSH
    return !c.full;
}

const MdRun *md_run_at(const MdDoc *doc, int off) {
    int lo = 0, hi = doc->n_runs - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        const MdRun *r = &doc->runs[mid];
        if (off < r->start) hi = mid - 1;
        else if (off >= r->start + r->len) lo = mid + 1;
        else return r;
    }
    return NULL;
}

unsigned md_style_at(const MdDoc *doc, int off) {
    const MdRun *r = md_run_at(doc, off);
    return r ? r->style : 0;
}

/* ---------- tables ---------- */

static int text_width(const MdDoc *doc, const MdText *t, MeasureFn measure, void *ud) {
    int w = 0;
    for (int i = 0; i < t->len;) {
        int n = (int)utf8_seq_len(doc->text + t->start + i, (size_t)(t->len - i));
        if (n == 0) n = 1;
        w += measure(doc->text + t->start + i, n, ud);
        i += n;
    }
    return w;
}

static bool cells_fit(const MdDoc *doc, const MdBlock *b, const int *w, int pad, MeasureFn measure, void *ud) {
    LineSpan tmp[MD_TABLE_MAX_LINES + 1];
    for (int r = 0; r < b->n_rows; r++)
        for (int c = 0; c < b->n_cols; c++) {
            const MdText *t = &doc->cells[b->cell_first + r * b->n_cols + c];
            int inner = w[c] - pad > 1 ? w[c] - pad : 1;
            if (layout_wrap(doc->text + t->start, t->len, inner, measure, ud, tmp, MD_TABLE_MAX_LINES + 1) >
                MD_TABLE_MAX_LINES)
                return false;
        }
    return true;
}

int md_table_widths(const MdDoc *doc, const MdBlock *b, int avail_w, int pad, MeasureFn measure, void *ud,
                    int *col_w, bool *overflow) {
    int natural[MD_MAX_COLS], sum = 0;
    for (int c = 0; c < b->n_cols; c++) {
        natural[c] = pad;
        for (int r = 0; r < b->n_rows; r++) {
            int w = text_width(doc, &doc->cells[b->cell_first + r * b->n_cols + c], measure, ud) + pad;
            if (w > natural[c]) natural[c] = w;
        }
        sum += natural[c];
    }
    int total = 0;
    if (sum <= avail_w) {
        for (int c = 0; c < b->n_cols; c++) total += col_w[c] = natural[c];
        *overflow = false;
        return total;
    }

    /* Fair share: narrow columns keep their width, the rest split what is left. */
    bool fixed[MD_MAX_COLS] = {false};
    int remaining = avail_w, left = b->n_cols;
    for (bool changed = true; changed && left > 0;) {
        changed = false;
        int share = remaining / left;
        for (int c = 0; c < b->n_cols; c++)
            if (!fixed[c] && natural[c] <= share) {
                col_w[c] = natural[c];
                remaining -= natural[c];
                left--;
                fixed[c] = changed = true;
            }
    }
    for (int c = 0, k = 0; c < b->n_cols; c++)
        if (!fixed[c]) col_w[c] = remaining / left + (++k == left ? remaining % left : 0);

    if (!cells_fit(doc, b, col_w, pad, measure, ud)) {
        int cap = avail_w * MD_COL_CAP_PCT / 100;
        for (int c = 0; c < b->n_cols; c++) {
            int capped = natural[c] < cap ? natural[c] : cap;
            if (capped > col_w[c]) col_w[c] = capped;
        }
    }
    for (int c = 0; c < b->n_cols; c++) total += col_w[c];
    *overflow = total > avail_w;
    return total;
}
