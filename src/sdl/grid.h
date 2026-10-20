/*
 * grid.h: the screen of Versions 1 to 5, as a grid of character cells.
 *
 * The SDL front end draws in one fixed-pitch font, so the screen it shows
 * a game is a grid of cells, each holding a character and the style it
 * was printed in, and this file keeps that grid. It knows nothing of SDL:
 * the front end draws the cells, and the unit tests read them.
 *
 * The screen is laid out as the Standard has it (Spec §8.5 to §8.7). In
 * Versions 1 to 3 the top row is the status line. Below it, or from the
 * top in later Versions, comes the upper window, as many rows as the game
 * has split off, and below that the lower window, which scrolls. The upper
 * window overlays whatever the lower window printed on those rows before
 * the split (Spec §8.7.2.1); both are views onto the one grid.
 *
 * The lower window wraps text at the right edge, between words when
 * buffering is on and anywhere when it is off (Spec §7.2). When it has
 * scrolled a screenful that the player has not had a chance to read, it
 * asks the front end to pause (Spec §8.4.1), through the more function.
 */

#ifndef QUIDDITY_GRID_H
#define QUIDDITY_GRID_H

#include <stdint.h>

/* One cell: a Unicode character, and the ZM_STYLE_* bits it was printed
 * in. An empty cell holds a space in no style. */
typedef struct grid_cell {
    uint32_t ch;
    unsigned char style;
} grid_cell;

typedef struct grid {
    unsigned version;
    unsigned columns;
    unsigned rows;
    grid_cell *cells;           /* rows * columns, a row at a time */

    /* 1 when the top row is the status line (Versions 1 to 3). */
    unsigned top;

    /* The upper window's height in rows, and its cursor, with (1, 1) its
     * top left, as the game counts. */
    unsigned upper_height;
    unsigned upper_row;
    unsigned upper_column;

    /* The lower window's cursor, as a row and column of the grid, both
     * counted from 0. */
    unsigned lower_row;
    unsigned lower_column;

    /* Which window text goes to (ZM_WINDOW_*), the style it goes in, and
     * whether the lower window breaks lines between words. */
    unsigned window;
    unsigned style;
    int buffering;

    /* For each row, whether it has text on it that the player has not
     * seen, which grid_seen() forgets; and how many times the lower window
     * has scrolled in all, which lets text be found again after the rows
     * under it have moved. */
    unsigned char *unseen;
    unsigned long scrolls;

    /* How many characters have gone into the lower window in all, so a
     * front end can tell whether anything was printed while it was not
     * looking. */
    unsigned long printed;

    /* The first half of a surrogate pair, awaiting its second. */
    uint16_t high_surrogate;

    /* Called when the next scroll of the lower window would take away
     * text the player has not seen, with the cursor at the start of its
     * empty bottom row: the front end shows the player what there is and
     * waits, and it all counts as seen. May be NULL, for a screen nobody
     * waits at. */
    void (*more)(void *context);
    void *more_context;
} grid;

/* Make a blank grid of the given size for a story of the given Version,
 * with the cursor where Spec §8.5.2, §8.6.3, and §8.7.3.3 put it at the
 * start of a game. Returns 0 if there is no memory for it. */
int grid_init(grid *g, unsigned version, unsigned columns, unsigned rows);
void grid_free(grid *g);

/* The cell at a row and column of the grid, both counted from 0. */
grid_cell *grid_at(grid *g, unsigned row, unsigned column);

/* The first row of the lower window, and how many rows it has. */
unsigned grid_lower_top(const grid *g);
unsigned grid_lower_height(const grid *g);

/* The player has seen the screen as it is. */
void grid_seen(grid *g);

/* Print a character to the selected window: a Unicode code point, or a
 * new-line as '\n'. grid_put_unit takes UTF-16, joining the halves of a
 * surrogate pair into one character. */
void grid_put(grid *g, uint32_t ch);
void grid_put_unit(grid *g, uint16_t unit);

/* The screen model operations, as zm/io.h describes them. */
void grid_split(grid *g, unsigned lines);
void grid_select(grid *g, unsigned window);
void grid_erase(grid *g, int window);
void grid_erase_line(grid *g);
void grid_set_cursor(grid *g, unsigned row, unsigned column);

/* Fill the status line (Spec §8.2): the location's name on the left, cut
 * with an ellipsis if it must be, and the rest on the right, the whole
 * row in reverse video. Does nothing without a status line. */
void grid_status(grid *g, const uint32_t *name, unsigned name_length, const char *right);

/* Scroll the lower window up a row, emptying its bottom row, without
 * pausing; for the front end, to make room below the cursor. */
void grid_scroll(grid *g);

/* Empty count cells of the lower window from a row and column onward,
 * running on to the next row at the edge. */
void grid_clear(grid *g, unsigned row, unsigned column, unsigned count);

#endif /* QUIDDITY_GRID_H */
