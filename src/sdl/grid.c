/*
 * grid.c: see grid.h.
 */

#include "grid.h"

#include <stdlib.h>
#include <string.h>

#include "zm/screen.h"

/* Spec §11.1: the header has a byte for the screen's width, so no screen
 * a game is told about is wider than this. */
#define GRID_MAX_COLUMNS 255

static const grid_cell blank = { ' ', 0 };

int grid_init(grid *g, unsigned version, unsigned columns, unsigned rows)
{
    memset(g, 0, sizeof *g);
    if (columns < 1 || columns > GRID_MAX_COLUMNS || rows < 2) {
        return 0;
    }
    g->cells = malloc((size_t)columns * rows * sizeof g->cells[0]);
    g->unseen = calloc(rows, 1);
    if (g->cells == NULL || g->unseen == NULL) {
        grid_free(g);
        return 0;
    }
    g->version = version;
    g->columns = columns;
    g->rows = rows;
    g->top = version <= 3 ? 1u : 0u;
    g->upper_row = 1;
    g->upper_column = 1;
    g->window = ZM_WINDOW_LOWER;
    g->buffering = 1;
    for (unsigned i = 0; i < columns * rows; i++) {
        g->cells[i] = blank;
    }

    /* Spec §8.5.2, §8.6.3, §8.7.3.3: a game starts with the cursor at
     * the bottom left, so that text scrolls up as it goes, except from
     * Version 5, which starts at the top left. */
    g->lower_row = version >= 5 ? g->top : rows - 1u;
    return 1;
}

void grid_free(grid *g)
{
    free(g->cells);
    free(g->unseen);
    g->cells = NULL;
    g->unseen = NULL;
}

void grid_seen(grid *g)
{
    memset(g->unseen, 0, g->rows);
}

grid_cell *grid_at(grid *g, unsigned row, unsigned column)
{
    return &g->cells[(size_t)row * g->columns + column];
}

unsigned grid_lower_top(const grid *g)
{
    return g->top + g->upper_height;
}

unsigned grid_lower_height(const grid *g)
{
    unsigned top = grid_lower_top(g);

    return top < g->rows ? g->rows - top : 0;
}

static void clear_rows(grid *g, unsigned from, unsigned to)
{
    for (unsigned r = from; r < to && r < g->rows; r++) {
        for (unsigned c = 0; c < g->columns; c++) {
            *grid_at(g, r, c) = blank;
        }
        g->unseen[r] = 0;
    }
}

/* Spec §8.7.3.1: the new bottom row is blank, not in the current style,
 * and the upper window above stays where it is. */
void grid_scroll(grid *g)
{
    unsigned top = grid_lower_top(g);

    if (top + 1u >= g->rows) {
        clear_rows(g, top, g->rows);
    } else {
        memmove(grid_at(g, top, 0), grid_at(g, top + 1u, 0),
                (size_t)(g->rows - top - 1u) * g->columns * sizeof g->cells[0]);
        memmove(g->unseen + top, g->unseen + top + 1u, g->rows - top - 1u);
        clear_rows(g, g->rows - 1u, g->rows);
    }
    g->scrolls++;
}

/* Move the lower window's cursor to the start of the next row, scrolling
 * if it is on the bottom one. Spec §8.4.1: when the cursor reaches the
 * bottom row and the next scroll would take away text the player has not
 * seen, the front end pauses for them, with that row empty for [MORE].
 * Rows with nothing on them are no loss, so a game that clears the
 * screen with a run of new-lines is not stopped for it. */
static void lower_newline(grid *g)
{
    unsigned top = grid_lower_top(g);

    g->lower_column = 0;
    if (g->lower_row + 1u < g->rows) {
        g->lower_row++;
    } else {
        grid_scroll(g);
    }
    if (g->more != NULL && top + 1u < g->rows && g->lower_row == g->rows - 1u && g->unseen[top]) {
        g->more(g->more_context);
        grid_seen(g);
    }
}

/* The lower window's row is full and buffering is on. Spec §7.2: the
 * word being printed goes to the next row whole, if there is a space on
 * this row to break at; the space itself is the break and is lost. */
static void wrap_word(grid *g)
{
    grid_cell word[GRID_MAX_COLUMNS];
    unsigned start = g->columns;
    unsigned length;

    while (start > 0 && grid_at(g, g->lower_row, start - 1u)->ch != ' ') {
        start--;
    }
    if (start == 0) {
        /* One word as wide as the row: it can only be broken. */
        lower_newline(g);
        return;
    }
    length = g->columns - start;
    memcpy(word, grid_at(g, g->lower_row, start), length * sizeof word[0]);
    for (unsigned c = start - 1u; c < g->columns; c++) {
        *grid_at(g, g->lower_row, c) = blank;
    }
    lower_newline(g);
    memcpy(grid_at(g, g->lower_row, 0), word, length * sizeof word[0]);
    g->unseen[g->lower_row] = 1;
    g->lower_column = length;
}

static void put_lower(grid *g, uint32_t ch)
{
    grid_cell *cell;

    if (ch == '\n') {
        lower_newline(g);
        return;
    }

    /* The cursor waits beyond the last column rather than wrapping at
     * once, so that a row filled exactly is not followed by an empty
     * one when a new-line comes next. A space at the edge is the break
     * itself. */
    if (g->lower_column >= g->columns) {
        if (ch == ' ') {
            lower_newline(g);
            return;
        }
        if (g->buffering) {
            wrap_word(g);
        } else {
            lower_newline(g);
        }
    }
    cell = grid_at(g, g->lower_row, g->lower_column);
    cell->ch = ch;
    cell->style = (unsigned char)g->style;
    if (ch != ' ') {
        g->unseen[g->lower_row] = 1;
    }
    g->lower_column++;
    g->printed++;
}

/* Spec §8.6.1.1.1, §8.7.2.5, §8.7.3.1: the upper window overlays what
 * is under it, never wraps a word or scrolls, and its cursor stays put
 * at the right edge. Text below its bottom row goes nowhere. */
static void put_upper(grid *g, uint32_t ch)
{
    unsigned row = g->top + g->upper_row - 1u;
    grid_cell *cell;

    if (g->upper_row > g->upper_height || row >= g->rows) {
        return;
    }
    if (ch == '\n') {
        if (g->upper_row < g->upper_height) {
            g->upper_row++;
        }
        g->upper_column = 1;
        return;
    }
    cell = grid_at(g, row, g->upper_column - 1u);
    cell->ch = ch;
    cell->style = (unsigned char)g->style;
    if (g->upper_column < g->columns) {
        g->upper_column++;
    }
}

void grid_put(grid *g, uint32_t ch)
{
    if (g->window == ZM_WINDOW_UPPER) {
        put_upper(g, ch);
    } else {
        put_lower(g, ch);
    }
}

void grid_put_unit(grid *g, uint16_t unit)
{
    if (unit >= 0xd800 && unit <= 0xdbff) {
        g->high_surrogate = unit;
        return;
    }
    if (unit >= 0xdc00 && unit <= 0xdfff) {
        if (g->high_surrogate != 0) {
            grid_put(g, 0x10000u + (((uint32_t)g->high_surrogate - 0xd800u) << 10) + (unit - 0xdc00u));
        }
        g->high_surrogate = 0;
        return;
    }
    g->high_surrogate = 0;
    grid_put(g, unit);
}

void grid_split(grid *g, unsigned lines)
{
    unsigned top;

    if (lines > g->rows - g->top) {
        lines = g->rows - g->top;
    }
    g->upper_height = lines;

    /* Spec §8.7.2.1.1: the upper window's cursor goes to its top left if
     * the window no longer reaches it. */
    if (g->upper_row > lines) {
        g->upper_row = 1;
        g->upper_column = 1;
    }

    /* Spec §8.7.2.2: a lower window cursor the upper window would
     * swallow moves down to the row below it. */
    top = grid_lower_top(g);
    if (g->lower_row < top) {
        g->lower_row = top < g->rows ? top : g->rows - 1u;
        g->lower_column = 0;
    }
}

void grid_select(grid *g, unsigned window)
{
    g->window = window;
    if (window == ZM_WINDOW_UPPER) {
        g->upper_row = 1;
        g->upper_column = 1;
    }
}

/* Spec §8.7.3.2.1, §8.7.3.3: the lower window's cursor goes to its top
 * left from Version 5, and to its bottom left before. */
static void home_lower(grid *g)
{
    g->lower_row = g->version >= 5 ? grid_lower_top(g) : g->rows - 1u;
    if (g->lower_row >= g->rows) {
        g->lower_row = g->rows - 1u;
    }
    g->lower_column = 0;
}

void grid_erase(grid *g, int window)
{
    switch (window) {
    case ZM_WINDOW_LOWER:
        clear_rows(g, grid_lower_top(g), g->rows);
        home_lower(g);
        break;
    case ZM_WINDOW_UPPER:
        clear_rows(g, g->top, grid_lower_top(g));
        g->upper_row = 1;
        g->upper_column = 1;
        break;
    case -1:
        /* Spec §8.7.3.3: the whole screen, with the upper window
         * collapsed and the lower selected. */
        clear_rows(g, g->top, g->rows);
        g->upper_height = 0;
        g->upper_row = 1;
        g->upper_column = 1;
        g->window = ZM_WINDOW_LOWER;
        home_lower(g);
        break;
    case -2:
        clear_rows(g, g->top, g->rows);
        break;
    default:
        break;
    }
}

/* Spec §8.7.3.4: from the cursor to the right edge. The lower window is
 * cleared the same way, as Frotz does. */
void grid_erase_line(grid *g)
{
    unsigned row;
    unsigned column;

    if (g->window == ZM_WINDOW_UPPER) {
        if (g->upper_row > g->upper_height) {
            return;
        }
        row = g->top + g->upper_row - 1u;
        column = g->upper_column - 1u;
    } else {
        row = g->lower_row;
        column = g->lower_column;
    }
    for (; row < g->rows && column < g->columns; column++) {
        *grid_at(g, row, column) = blank;
    }
}

void grid_set_cursor(grid *g, unsigned row, unsigned column)
{
    unsigned rows = g->rows - g->top;

    g->upper_row = row < 1 ? 1 : row > rows ? rows : row;
    g->upper_column = column < 1 ? 1 : column > g->columns ? g->columns : column;
}

void grid_status(grid *g, const uint32_t *name, unsigned name_length, const char *right)
{
    unsigned right_length = (unsigned)strlen(right);
    unsigned room;
    unsigned column = 1;

    if (g->top == 0) {
        return;
    }
    for (unsigned c = 0; c < g->columns; c++) {
        grid_cell *cell = grid_at(g, 0, c);

        cell->ch = ' ';
        cell->style = ZM_STYLE_REVERSE;
    }

    /* A space at either end, and two at least between the name and the
     * rest; Spec §8.2.2.2: a name too long for what is left is cut, and
     * ends in an ellipsis. */
    room = g->columns > right_length + 4u ? g->columns - right_length - 4u : 0;
    if (name_length > room && room > 3) {
        for (unsigned i = 0; i < room - 3u; i++) {
            grid_at(g, 0, column++)->ch = name[i];
        }
        for (unsigned i = 0; i < 3; i++) {
            grid_at(g, 0, column++)->ch = '.';
        }
    } else {
        for (unsigned i = 0; i < name_length && i < room; i++) {
            grid_at(g, 0, column++)->ch = name[i];
        }
    }
    if (right_length + 1u < g->columns) {
        column = g->columns - 1u - right_length;
        for (unsigned i = 0; i < right_length; i++) {
            grid_at(g, 0, column + i)->ch = (unsigned char)right[i];
        }
    }
}

void grid_clear(grid *g, unsigned row, unsigned column, unsigned count)
{
    while (count > 0 && row < g->rows) {
        if (column >= g->columns) {
            row++;
            column = 0;
            continue;
        }
        *grid_at(g, row, column) = blank;
        column++;
        count--;
    }
}
