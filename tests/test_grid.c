/*
 * test_grid.c: the SDL front end's screen of character cells.
 *
 * Covers src/sdl/grid.h: where the cursor starts, how the lower window
 * wraps and scrolls and when it pauses for the player, the upper window
 * and the status line over it, and the screen model's operations as
 * Spec §8.5 to §8.7 describe them. The grid has no SDL in it, so it is
 * checked here without a window.
 */

#include <string.h>

#include "grid.h"
#include "zm/screen.h"

#include "check.h"

/* A row of the grid as text, without its trailing blanks. Only the ASCII
 * characters these tests print are expected. */
static const char *row_text(grid *g, unsigned row)
{
    static char text[256];
    unsigned length = 0;

    for (unsigned c = 0; c < g->columns; c++) {
        uint32_t ch = grid_at(g, row, c)->ch;

        text[length++] = ch < 128 ? (char)ch : '#';
    }
    while (length > 0 && text[length - 1] == ' ') {
        length--;
    }
    text[length] = '\0';
    return text;
}

static void put_text(grid *g, const char *text)
{
    for (; *text != '\0'; text++) {
        grid_put(g, (unsigned char)*text);
    }
}

static void test_the_cursor_starts_where_the_version_says(void)
{
    grid g;

    /* Spec §8.6.3: bottom left, below a status line. */
    CHECK(grid_init(&g, 3, 20, 5));
    CHECK_EQ_INT(g.top, 1);
    CHECK_EQ_INT(g.lower_row, 4);
    CHECK_EQ_INT(g.lower_column, 0);
    grid_free(&g);

    /* Spec §8.7.3.3: bottom left in Version 4, top left from 5. */
    CHECK(grid_init(&g, 4, 20, 5));
    CHECK_EQ_INT(g.top, 0);
    CHECK_EQ_INT(g.lower_row, 4);
    grid_free(&g);
    CHECK(grid_init(&g, 5, 20, 5));
    CHECK_EQ_INT(g.lower_row, 0);
    grid_free(&g);

    /* A screen no game could be told about. */
    CHECK(!grid_init(&g, 5, 256, 5));
    CHECK(!grid_init(&g, 5, 20, 1));
}

static void test_words_wrap_whole_and_lose_the_space(void)
{
    grid g;

    CHECK(grid_init(&g, 5, 10, 5));
    put_text(&g, "hello there world");
    CHECK_EQ_STR(row_text(&g, 0), "hello");
    CHECK_EQ_STR(row_text(&g, 1), "there");
    CHECK_EQ_STR(row_text(&g, 2), "world");
    CHECK_EQ_INT(g.lower_row, 2);
    CHECK_EQ_INT(g.lower_column, 5);
    grid_free(&g);

    /* A row filled exactly and then ended is not followed by an empty
     * one; and a space that falls at the edge is the break itself. */
    CHECK(grid_init(&g, 5, 10, 5));
    put_text(&g, "abcdefghij\nk");
    CHECK_EQ_STR(row_text(&g, 0), "abcdefghij");
    CHECK_EQ_STR(row_text(&g, 1), "k");
    grid_free(&g);
    CHECK(grid_init(&g, 5, 10, 5));
    put_text(&g, "abcdefghij klm");
    CHECK_EQ_STR(row_text(&g, 0), "abcdefghij");
    CHECK_EQ_STR(row_text(&g, 1), "klm");
    grid_free(&g);

    /* A word as wide as the row can only be broken. */
    CHECK(grid_init(&g, 5, 10, 5));
    put_text(&g, "abcdefghijklm");
    CHECK_EQ_STR(row_text(&g, 0), "abcdefghij");
    CHECK_EQ_STR(row_text(&g, 1), "klm");
    grid_free(&g);

    /* Spec §7.2: without buffering, the break comes anywhere. */
    CHECK(grid_init(&g, 5, 10, 5));
    g.buffering = 0;
    put_text(&g, "hello there");
    CHECK_EQ_STR(row_text(&g, 0), "hello ther");
    CHECK_EQ_STR(row_text(&g, 1), "e");
    grid_free(&g);
}

/* The more function's record: how often it was called, and the screen
 * as it stood the first two times. */
typedef struct pause_log {
    grid *g;
    unsigned calls;
    char rows[2][4][16];
    unsigned column;
} pause_log;

static void log_more(void *context)
{
    pause_log *log = context;

    if (log->calls < 2) {
        for (unsigned r = 0; r < 4; r++) {
            strcpy(log->rows[log->calls][r], row_text(log->g, r));
        }
    }
    log->column = log->g->lower_column;
    log->calls++;
}

static void test_a_screenful_unseen_pauses_on_an_empty_row(void)
{
    grid g;
    pause_log log;

    memset(&log, 0, sizeof log);
    CHECK(grid_init(&g, 5, 10, 4));
    log.g = &g;
    g.more = log_more;
    g.more_context = &log;

    /* Spec §8.4.1: from the top of a cleared screen, three lines fill
     * the window but for its bottom row, where [MORE] goes. */
    put_text(&g, "1\n2\n");
    CHECK_EQ_INT(log.calls, 0);
    put_text(&g, "3\n");
    CHECK_EQ_INT(log.calls, 1);
    CHECK_EQ_STR(log.rows[0][0], "1");
    CHECK_EQ_STR(log.rows[0][2], "3");
    CHECK_EQ_STR(log.rows[0][3], "");
    CHECK_EQ_INT(log.column, 0);

    /* And then three more, scrolling, before the next. */
    put_text(&g, "4\n5\n");
    CHECK_EQ_INT(log.calls, 1);
    put_text(&g, "6\n");
    CHECK_EQ_INT(log.calls, 2);
    CHECK_EQ_STR(log.rows[1][0], "4");
    CHECK_EQ_STR(log.rows[1][2], "6");
    CHECK_EQ_STR(log.rows[1][3], "");
    CHECK_EQ_INT(g.scrolls, 3);

    /* What the player has seen is not counted again. */
    grid_seen(&g);
    put_text(&g, "7\n8\n");
    CHECK_EQ_INT(log.calls, 2);
    grid_free(&g);

    /* Empty rows are no loss: a game that clears the screen with a run
     * of new-lines, as many Inform games do at the start, is not stopped
     * for them, but the text after them is still waited for. */
    memset(&log, 0, sizeof log);
    CHECK(grid_init(&g, 5, 10, 4));
    log.g = &g;
    g.more = log_more;
    g.more_context = &log;
    for (unsigned i = 0; i < 30; i++) {
        grid_put(&g, '\n');
    }
    CHECK_EQ_INT(log.calls, 0);
    put_text(&g, "a\nb\nc\n");
    CHECK_EQ_INT(log.calls, 1);
    CHECK_EQ_STR(log.rows[0][0], "a");
    grid_free(&g);
}

static void test_the_upper_window_overlays_and_stays_put(void)
{
    grid g;

    CHECK(grid_init(&g, 4, 10, 6));
    put_text(&g, "below");
    grid_split(&g, 2);
    grid_select(&g, ZM_WINDOW_UPPER);
    grid_set_cursor(&g, 2, 3);
    put_text(&g, "ab");
    CHECK_EQ_STR(row_text(&g, 1), "  ab");

    /* Spec §8.7.3.1: the cursor stays at the right edge, and the last
     * character there is overwritten. */
    grid_set_cursor(&g, 1, 1);
    put_text(&g, "0123456789XY");
    CHECK_EQ_STR(row_text(&g, 0), "012345678Y");

    /* A new-line on its bottom row stays there; below it, text goes
     * nowhere. */
    put_text(&g, "\n\n\nab");
    CHECK_EQ_STR(row_text(&g, 1), "abab");
    grid_set_cursor(&g, 3, 1);
    put_text(&g, "lost");
    CHECK_EQ_STR(row_text(&g, 2), "");

    /* The lower window kept its own cursor. */
    grid_select(&g, ZM_WINDOW_LOWER);
    CHECK_EQ_INT(g.lower_row, 5);
    CHECK_EQ_STR(row_text(&g, 5), "below");

    /* Spec §8.7.3.4: erase_line clears from the cursor rightward. */
    grid_select(&g, ZM_WINDOW_UPPER);
    grid_set_cursor(&g, 1, 4);
    grid_erase_line(&g);
    CHECK_EQ_STR(row_text(&g, 0), "012");
    grid_free(&g);
}

static void test_a_split_moves_a_swallowed_cursor_below(void)
{
    grid g;

    /* Spec §8.7.2.2. */
    CHECK(grid_init(&g, 5, 10, 6));
    put_text(&g, "top");
    grid_split(&g, 3);
    CHECK_EQ_INT(g.lower_row, 3);
    CHECK_EQ_INT(g.lower_column, 0);

    /* The upper window can be no taller than the screen. */
    grid_split(&g, 9);
    CHECK_EQ_INT(g.upper_height, 6);
    grid_free(&g);
}

static void test_erasing_puts_the_cursors_where_the_version_says(void)
{
    grid g;

    /* Spec §8.7.3.3: everything goes, the upper window collapses, the
     * lower is selected, and its cursor goes to the bottom left in
     * Version 4 and the top left from 5. */
    CHECK(grid_init(&g, 4, 10, 5));
    put_text(&g, "text");
    grid_split(&g, 1);
    grid_select(&g, ZM_WINDOW_UPPER);
    put_text(&g, "status");
    grid_erase(&g, -1);
    CHECK_EQ_INT(g.upper_height, 0);
    CHECK_EQ_INT(g.window, ZM_WINDOW_LOWER);
    CHECK_EQ_INT(g.lower_row, 4);
    CHECK_EQ_STR(row_text(&g, 0), "");
    CHECK_EQ_STR(row_text(&g, 4), "");
    grid_free(&g);

    CHECK(grid_init(&g, 5, 10, 5));
    grid_split(&g, 2);
    put_text(&g, "lower");
    grid_select(&g, ZM_WINDOW_UPPER);
    put_text(&g, "upper");
    grid_select(&g, ZM_WINDOW_LOWER);

    /* Spec §8.7.3.2.1: one window only, its cursor to the top left. */
    grid_erase(&g, ZM_WINDOW_LOWER);
    CHECK_EQ_STR(row_text(&g, 0), "upper");
    CHECK_EQ_STR(row_text(&g, 2), "");
    CHECK_EQ_INT(g.lower_row, 2);
    grid_erase(&g, -1);
    CHECK_EQ_INT(g.lower_row, 0);
    CHECK_EQ_STR(row_text(&g, 0), "");
    grid_free(&g);
}

static void test_the_status_line_is_reversed_and_cut_to_fit(void)
{
    static const uint32_t name[] = { 'W', 'e', 's', 't', ' ', 'o', 'f', ' ', 'H', 'o', 'u', 's', 'e' };
    grid g;

    CHECK(grid_init(&g, 3, 40, 5));
    grid_status(&g, name, 13, "Score: 0  Moves: 1");
    CHECK_EQ_STR(row_text(&g, 0), " West of House       Score: 0  Moves: 1");
    CHECK_EQ_INT(grid_at(&g, 0, 0)->style, ZM_STYLE_REVERSE);
    CHECK_EQ_INT(grid_at(&g, 0, 39)->style, ZM_STYLE_REVERSE);

    /* Spec §8.6.1.1: the upper window starts below the status line. */
    grid_split(&g, 1);
    grid_select(&g, ZM_WINDOW_UPPER);
    put_text(&g, "upper");
    CHECK_EQ_STR(row_text(&g, 1), "upper");
    grid_free(&g);

    /* Spec §8.2.2.2: a name too long is cut with an ellipsis. */
    CHECK(grid_init(&g, 3, 24, 5));
    grid_status(&g, name, 13, "Time: 9:05");
    CHECK_EQ_STR(row_text(&g, 0), " West of...  Time: 9:05");
    grid_free(&g);

    /* There is none from Version 4. */
    CHECK(grid_init(&g, 4, 24, 5));
    grid_status(&g, name, 13, "Time: 9:05");
    CHECK_EQ_STR(row_text(&g, 0), "");
    grid_free(&g);
}

static void test_surrogate_halves_make_one_character(void)
{
    grid g;

    CHECK(grid_init(&g, 5, 10, 5));
    grid_put_unit(&g, 'a');
    grid_put_unit(&g, 0xd83d);
    grid_put_unit(&g, 0xde03);
    grid_put_unit(&g, 0xde03);  /* a second half alone is dropped */
    grid_put_unit(&g, 'b');
    CHECK_EQ_INT(grid_at(&g, 0, 1)->ch, 0x1f603);
    CHECK_EQ_INT(grid_at(&g, 0, 2)->ch, 'b');
    CHECK_EQ_INT(g.printed, 3);
    grid_free(&g);
}

static void test_cells_are_cleared_across_rows(void)
{
    grid g;

    CHECK(grid_init(&g, 5, 10, 5));
    put_text(&g, "abcdefghijklmnop");
    grid_clear(&g, 0, 8, 4);
    CHECK_EQ_STR(row_text(&g, 0), "abcdefgh");
    CHECK_EQ_STR(row_text(&g, 1), "  mnop");
    grid_free(&g);
}

int main(void)
{
    RUN_TEST(test_the_cursor_starts_where_the_version_says);
    RUN_TEST(test_words_wrap_whole_and_lose_the_space);
    RUN_TEST(test_a_screenful_unseen_pauses_on_an_empty_row);
    RUN_TEST(test_the_upper_window_overlays_and_stays_put);
    RUN_TEST(test_a_split_moves_a_swallowed_cursor_below);
    RUN_TEST(test_erasing_puts_the_cursors_where_the_version_says);
    RUN_TEST(test_the_status_line_is_reversed_and_cut_to_fit);
    RUN_TEST(test_surrogate_halves_make_one_character);
    RUN_TEST(test_cells_are_cleared_across_rows);
    return CHECK_SUMMARY();
}
