#pragma once

/* The small Markdown subset Claude writes, parsed for the turn view: bold,
 * inline code, fenced code blocks, headings, lists and tables. Markup that is
 * not recognised stays in the text exactly as written. */

#include <stdbool.h>

#include "textlayout.h"

#define MD_MAX_TEXT 16384 /* the Mac sends at most 4000 characters, up to 4 bytes each */
#define MD_MAX_BLOCKS 256
#define MD_MAX_RUNS 1024
#define MD_MAX_CELLS 512
#define MD_MAX_COLS 12

enum { MD_S_BOLD = 1, MD_S_CODE = 2 }; /* run style bits */

typedef enum { MD_PARA, MD_HEADING, MD_BULLET, MD_NUMBER, MD_CODE, MD_TABLE } MdKind;

typedef struct {
    int start, len; /* into MdDoc.text */
    unsigned char style;
} MdRun;

typedef struct {
    int start, len, run_first, n_runs; /* the runs tile [start, start + len) */
} MdText;

typedef struct {
    MdKind kind;
    int level;       /* heading 1-6; list indent 0 or 1 */
    char marker[12]; /* MD_NUMBER: the number as written, e.g. "12." or "3)" */
    MdText text;     /* every kind but MD_TABLE */
    int cell_first, n_rows, n_cols; /* MD_TABLE: cells row-major, row 0 is the header */
} MdBlock;

typedef struct {
    char text[MD_MAX_TEXT]; /* the source with markup removed */
    int text_len;
    MdBlock blocks[MD_MAX_BLOCKS];
    int n_blocks;
    MdRun runs[MD_MAX_RUNS];
    int n_runs;
    MdText cells[MD_MAX_CELLS];
    int n_cells;
} MdDoc;

/* False if a capacity is exceeded: the caller then shows the text plain. */
bool md_parse(const char *src, int len, MdDoc *doc);

/* The run holding byte offset `off` of doc->text, or NULL. */
const MdRun *md_run_at(const MdDoc *doc, int off);
/* Style bits at byte offset `off` of doc->text (0 outside every run). */
unsigned md_style_at(const MdDoc *doc, int off);

#define MD_TABLE_MAX_LINES 2 /* stop squeezing a table once a cell needs more lines */
#define MD_COL_CAP_PCT 60    /* an overflowing table's columns stay within this share of the width */

/* Column widths for a table block. measure(s, n, ud) gets pointers into
 * doc->text and must apply the style there itself; pad is added to every
 * column. Fills col_w[n_cols], returns the total width and sets *overflow
 * when it exceeds avail_w. */
int md_table_widths(const MdDoc *doc, const MdBlock *b, int avail_w, int pad, MeasureFn measure, void *ud,
                    int *col_w, bool *overflow);
