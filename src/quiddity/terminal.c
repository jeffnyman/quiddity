/*
 * terminal.c: see terminal.h.
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L /* for fileno() and isatty() */
#endif

#include "terminal.h"
#include "utf8.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zm/blorb.h"
#include "zm/screen.h"
#include "zm/text.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <conio.h>
#include <io.h>
#include <windows.h>
#define terminal_isatty(stream) _isatty(_fileno(stream))
#else
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>
#define terminal_isatty(stream) isatty(fileno(stream))
#endif

/*
 * Output.
 */

/* Write the first count characters of the held-back line without ending
 * it, and keep the rest. */
static void emit_line(terminal *t, unsigned count)
{
    utf8_put_units(stdout, t->line, count);
    memmove(t->line, t->line + count, (t->line_length - count) * sizeof t->line[0]);
    t->line_length -= count;
}

static void flush_line(terminal *t)
{
    emit_line(t, t->line_length);
    fflush(stdout);
}

/* The held-back line is full. Spec §7.2: with buffering on, break it
 * after the last word that fits; otherwise after the last character. */
static void wrap_line(terminal *t)
{
    unsigned cut = t->line_length;  /* characters that go out before the break */
    unsigned skip = 0;              /* characters dropped at the break */

    if (t->buffering) {
        unsigned i = t->line_length;

        while (i > 0 && t->line[i - 1] != ' ') {
            i--;
        }
        if (i > 0) {
            cut = i - 1;    /* the space itself is the break */
            skip = 1;
        }
    }
    emit_line(t, cut);
    putchar('\n');
    if (skip > 0) {
        memmove(t->line, t->line + skip, (t->line_length - skip) * sizeof t->line[0]);
        t->line_length -= skip;
    }
}

/* The upper window is a grid. Spec §8.7.3.1: it never scrolls, and a
 * cursor at its bottom right stays put. */
static void put_upper(terminal *t, uint16_t unicode)
{
    if (t->upper_row > t->upper_height) {
        return;
    }
    if (unicode == '\n') {
        if (t->upper_row < t->upper_height) {
            t->upper_row++;
        }
        t->upper_column = 1;
        return;
    }
    if (t->upper_column <= TERMINAL_WIDTH) {
        t->upper[t->upper_row - 1][t->upper_column - 1] = unicode;
        t->upper_shown = 0;
        if (t->upper_column < TERMINAL_WIDTH) {
            t->upper_column++;
        }
    }
}

static void put_lower(terminal *t, uint16_t unicode)
{
    if (unicode == '\n') {
        flush_line(t);
        putchar('\n');
        return;
    }
    /* The second half of a surrogate pair stays on its first half's
     * line, whatever the margin says. */
    if (t->line_length == TERMINAL_WIDTH && !(unicode >= 0xdc00 && unicode <= 0xdfff)) {
        if (unicode == ' ') {
            /* A space at the margin is the break itself. */
            flush_line(t);
            putchar('\n');
            return;
        }
        wrap_line(t);
    }
    if (t->line_length == TERMINAL_LINE_MAX) {
        flush_line(t);
    }
    t->line[t->line_length++] = unicode;
}

static void put_char(terminal *t, uint16_t unicode)
{
    if (t->window == ZM_WINDOW_UPPER) {
        put_upper(t, unicode);
    } else {
        put_lower(t, unicode);
    }
}

static void terminal_print_char(void *context, uint16_t zscii)
{
    terminal *t = context;
    uint16_t unicode;

    if (zscii == ZM_ZSCII_NEWLINE) {
        put_char(t, '\n');
        return;
    }

    /* Spec §3.8.2.1: null prints nothing. Spec §3.8.5.4.3: a character
     * with no letter-form is shown as a question mark; the same is done
     * here for any code that has no printable meaning at all. */
    if (zscii == ZM_ZSCII_NULL) {
        return;
    }
    unicode = zm_zscii_to_unicode(t->mem, zscii);
    put_char(t, unicode != 0 ? unicode : '?');
}

static void terminal_print_unicode(void *context, uint16_t unicode)
{
    put_char(context, unicode);
}

/* Print the upper window's grid, if it has changed since it was last
 * printed: every row down to the last that has anything on it, with
 * trailing blanks cut. */
static void show_upper(terminal *t)
{
    unsigned rows = 0;

    if (t->upper_shown) {
        return;
    }
    t->upper_shown = 1;
    for (unsigned r = 0; r < t->upper_height; r++) {
        for (unsigned c = 0; c < TERMINAL_WIDTH; c++) {
            if (t->upper[r][c] != ' ') {
                rows = r + 1;
                break;
            }
        }
    }
    for (unsigned r = 0; r < rows; r++) {
        unsigned end = TERMINAL_WIDTH;

        while (end > 0 && t->upper[r][end - 1] == ' ') {
            end--;
        }
        utf8_put_units(stdout, t->upper[r], end);
        putchar('\n');
    }
}

static void clear_upper_rows(terminal *t, unsigned from, unsigned to)
{
    for (unsigned r = from; r < to && r < TERMINAL_HEIGHT; r++) {
        for (unsigned c = 0; c < TERMINAL_WIDTH; c++) {
            t->upper[r][c] = ' ';
        }
    }
}

static void terminal_show_status(void *context, const uint16_t *name, unsigned name_length,
                                 int time_game, int16_t score_or_hours, int16_t turns_or_minutes)
{
    terminal *t = context;
    char right[40];
    unsigned right_length;
    unsigned room;
    unsigned used = 0;

    /* Spec §8.2.3: score and turns, or the time on a 24-hour clock. */
    if (time_game) {
        snprintf(right, sizeof right, "Time: %d:%02d", (int)score_or_hours, (int)turns_or_minutes);
    } else {
        snprintf(right, sizeof right, "Score: %d  Moves: %d", (int)score_or_hours, (int)turns_or_minutes);
    }
    right_length = (unsigned)strlen(right);

    /* The status line goes above whatever the game has begun to print,
     * which is usually its prompt: that stays held back and follows.
     *
     * Spec §8.2.2: the location on the left; Spec §8.2.2.2: cut to fit
     * with an ellipsis if it must be. */
    room = TERMINAL_WIDTH > right_length + 2u ? TERMINAL_WIDTH - right_length - 2u : 0;
    if (name_length > room && room > 3) {
        for (unsigned i = 0; i < room - 3; i++) {
            utf8_put(stdout, zm_zscii_to_unicode(t->mem, name[i]));
        }
        fputs("...", stdout);
        used = room;
    } else {
        for (unsigned i = 0; i < name_length && i < room; i++) {
            utf8_put(stdout, zm_zscii_to_unicode(t->mem, name[i]));
        }
        used = name_length < room ? name_length : room;
    }
    for (unsigned i = used; i + right_length < TERMINAL_WIDTH; i++) {
        putchar(' ');
    }
    fputs(right, stdout);
    putchar('\n');
}

/*
 * The screen model.
 */

static void terminal_split_window(void *context, unsigned lines)
{
    terminal *t = context;

    if (lines > TERMINAL_HEIGHT) {
        lines = TERMINAL_HEIGHT;
    }

    /* Rows the window gives up are printed first if nobody has seen
     * them: 'Trinity' and the Inform library draw boxed quotations this
     * way, in an upper window that is enlarged for them and shrunk again
     * once they have been read. Rows it gains start blank. */
    if (lines < t->upper_height) {
        show_upper(t);
        clear_upper_rows(t, lines, t->upper_height);
    } else {
        clear_upper_rows(t, t->upper_height, lines);
    }
    t->upper_height = lines;
}

static void terminal_set_window(void *context, unsigned window)
{
    terminal *t = context;

    t->window = window;
    if (window == ZM_WINDOW_UPPER) {
        t->upper_row = 1;
        t->upper_column = 1;
    }
}

static void terminal_erase_window(void *context, int window)
{
    terminal *t = context;

    /* Spec §15 erase_window. Whatever is on the teletype stays, so
     * erasing the lower window only finishes its line; erasing the
     * upper window loses its grid, after printing anything not yet
     * seen. */
    if (window == -1 || window == -2 || window == ZM_WINDOW_UPPER) {
        show_upper(t);
        clear_upper_rows(t, 0, TERMINAL_HEIGHT);
        t->upper_row = 1;
        t->upper_column = 1;
    }
    if (window == -1) {
        t->upper_height = 0;
        t->window = ZM_WINDOW_LOWER;
    }
    if (window != ZM_WINDOW_UPPER && t->line_length > 0) {
        flush_line(t);
        putchar('\n');
    }
}

static void terminal_erase_line(void *context)
{
    terminal *t = context;

    /* Spec §8.7.3.4: in the upper window, from the cursor to the right
     * edge. In the lower window the rest of the line is already blank. */
    if (t->window == ZM_WINDOW_UPPER && t->upper_row <= t->upper_height) {
        for (unsigned c = t->upper_column - 1; c < TERMINAL_WIDTH; c++) {
            if (t->upper[t->upper_row - 1][c] != ' ') {
                t->upper[t->upper_row - 1][c] = ' ';
                t->upper_shown = 0;
            }
        }
    }
}

static void terminal_set_cursor(void *context, unsigned row, unsigned column)
{
    terminal *t = context;

    t->upper_row = row > TERMINAL_HEIGHT ? TERMINAL_HEIGHT : row;
    t->upper_column = column > TERMINAL_WIDTH ? TERMINAL_WIDTH : column;
}

static void terminal_set_buffering(void *context, int on)
{
    terminal *t = context;

    t->buffering = on;
}

/*
 * The Version 6 canvas (Spec §8.8).
 */

/* Print the columns from one to another of a row of the canvas, without
 * trailing blanks, and count the row as seen. */
static void emit_row(terminal *t, unsigned row, unsigned from, unsigned to)
{
    unsigned end = to < TERMINAL_WIDTH ? to : TERMINAL_WIDTH;

    while (end > from && t->cells[row][end - 1] == ' ') {
        end--;
    }
    for (unsigned c = 0; c < from && c < end; c++) {
        putchar(' ');
    }
    if (end > from) {
        utf8_put_units(stdout, t->cells[row] + from, end - from);
    }
    putchar('\n');
    t->row_changed[row] = 0;
}

static int row_is_blank(const terminal *t, unsigned row)
{
    for (unsigned c = 0; c < TERMINAL_WIDTH; c++) {
        if (t->cells[row][c] != ' ') {
            return 0;
        }
    }
    return 1;
}

/* Print every row that has changed since it was last printed, in order,
 * with a run of blank rows reduced to one empty line. */
static void show_canvas(terminal *t)
{
    int blank_pending = 0;

    for (unsigned r = 0; r < TERMINAL_HEIGHT; r++) {
        if (!t->row_changed[r]) {
            continue;
        }
        if (row_is_blank(t, r)) {
            blank_pending = 1;
            t->row_changed[r] = 0;
            continue;
        }
        if (blank_pending) {
            putchar('\n');
            blank_pending = 0;
        }
        emit_row(t, r, 0, TERMINAL_WIDTH);
    }
    fflush(stdout);
}

static void set_cell(terminal *t, unsigned row, unsigned column, uint16_t c)
{
    if (row < TERMINAL_HEIGHT && column < TERMINAL_WIDTH && t->cells[row][column] != c) {
        t->cells[row][column] = c;
        t->row_changed[row] = 1;
    }
}

static void terminal_plot_text(void *context, unsigned y, unsigned x, const uint16_t *unicode, unsigned count,
                               const zm_pen *pen)
{
    terminal *t = context;
    int seen = y - 1 < TERMINAL_HEIGHT && !t->row_changed[y - 1];

    (void)pen;
    for (unsigned i = 0; i < count; i++) {
        set_cell(t, y - 1, x - 1 + i, unicode[i]);
    }

    /* The line the player typed is plotted back by the machine, and
     * need not be printed again on their account. */
    if (t->echo_pending) {
        t->echo_pending = 0;
        if (seen && y - 1 < TERMINAL_HEIGHT) {
            t->row_changed[y - 1] = 0;
        }
    }
}

static void terminal_erase_area(void *context, unsigned y, unsigned x, unsigned height, unsigned width,
                                const zm_pen *pen)
{
    terminal *t = context;

    (void)pen;
    for (unsigned r = 0; r < height; r++) {
        for (unsigned c = 0; c < width; c++) {
            set_cell(t, y - 1 + r, x - 1 + c, ' ');
        }
    }
}

static void terminal_scroll_area(void *context, unsigned y, unsigned x, unsigned height, unsigned width, int units,
                                 const zm_pen *pen)
{
    terminal *t = context;
    unsigned top = y - 1;
    unsigned bottom = y - 1 + height;      /* one past the last row */
    unsigned by = units < 0 ? (unsigned)-units : (unsigned)units;

    (void)pen;
    if (bottom > TERMINAL_HEIGHT) {
        bottom = TERMINAL_HEIGHT;
    }
    if (top >= bottom || by == 0) {
        return;
    }
    if (by > bottom - top) {
        by = bottom - top;
    }

    /* What a row holds moves with it, and so does whether it has been
     * printed: a line already seen is not seen again for having moved.
     * A row leaving at the top unseen is printed as it goes, its part
     * inside the window only, which is how running text in a scrolling
     * window reaches the player before the game next stops. */
    if (units > 0) {
        for (unsigned r = top; r < top + by; r++) {
            if (t->row_changed[r] && !row_is_blank(t, r)) {
                emit_row(t, r, x - 1, x - 1 + width);
            }
        }
        for (unsigned r = top; r + by < bottom; r++) {
            for (unsigned c = 0; c < width && x - 1 + c < TERMINAL_WIDTH; c++) {
                t->cells[r][x - 1 + c] = t->cells[r + by][x - 1 + c];
            }
            t->row_changed[r] = t->row_changed[r + by];
        }
        for (unsigned r = bottom - by; r < bottom; r++) {
            t->row_changed[r] = 0;
            terminal_erase_area(t, r + 1, x, 1, width, pen);
        }
    } else {
        for (unsigned r = bottom; r-- > top + by;) {
            for (unsigned c = 0; c < width && x - 1 + c < TERMINAL_WIDTH; c++) {
                t->cells[r][x - 1 + c] = t->cells[r - by][x - 1 + c];
            }
            t->row_changed[r] = t->row_changed[r - by];
        }
        for (unsigned r = top; r < top + by; r++) {
            t->row_changed[r] = 0;
            terminal_erase_area(t, r + 1, x, 1, width, pen);
        }
    }
}

/* A picture is a box: its edges, its number inside the bottom right
 * corner if there is room, and blank within, since it covers what was
 * there. The number sits at the bottom because the top row of a tall
 * picture beside a text window is printed again with every line that
 * scrolls out of that window. */
static void terminal_draw_picture(void *context, const zm_picture *picture, unsigned y, unsigned x,
                                  unsigned height, unsigned width)
{
    terminal *t = context;
    char number[16];
    size_t digits;

    if (height == 0 || width == 0) {
        return;
    }
    terminal_erase_area(t, y, x, height, width, NULL);
    for (unsigned c = 0; c < width; c++) {
        set_cell(t, y - 1, x - 1 + c, '-');
        set_cell(t, y - 2 + height, x - 1 + c, '-');
    }
    for (unsigned r = 0; r < height; r++) {
        set_cell(t, y - 1 + r, x - 1, '|');
        set_cell(t, y - 1 + r, x - 2 + width, '|');
    }
    set_cell(t, y - 1, x - 1, '+');
    set_cell(t, y - 1, x - 2 + width, '+');
    set_cell(t, y - 2 + height, x - 1, '+');
    set_cell(t, y - 2 + height, x - 2 + width, '+');

    digits = (size_t)snprintf(number, sizeof number, "%u", picture->number);
    if (height >= 3 && width >= digits + 2) {
        for (size_t i = 0; i < digits; i++) {
            set_cell(t, y - 3 + height, x - 2 + width - (unsigned)digits + (unsigned)i, (uint16_t)number[i]);
        }
    }
}

/* Spec §8.8.6, Blorb §11.2: on a screen of character cells the Blorb
 * rule, which scales pictures by the room the standard window has, would
 * leave them a dozen cells wide; cells are twice as tall as they are
 * wide besides. So the standard window is taken to fill this screen,
 * and a picture is scaled to its share of that, in each direction
 * separately, and to a cell at least. The Infocom picture files name a
 * 320 by 200 window; a file with no resolution chunk is given that. */
static void terminal_picture_size(void *context, const zm_picture *picture, unsigned *height, unsigned *width)
{
    unsigned standard_width = picture->standard_width != 0 ? picture->standard_width : 320;
    unsigned standard_height = picture->standard_height != 0 ? picture->standard_height : 200;

    (void)context;
    *width = (picture->width * TERMINAL_WIDTH + standard_width / 2) / standard_width;
    *height = (picture->height * TERMINAL_HEIGHT + standard_height / 2) / standard_height;
    if (*width == 0 && picture->width > 0) {
        *width = 1;
    }
    if (*height == 0 && picture->height > 0) {
        *height = 1;
    }
    if (*width > TERMINAL_WIDTH) {
        *width = TERMINAL_WIDTH;
    }
    if (*height > TERMINAL_HEIGHT) {
        *height = TERMINAL_HEIGHT;
    }
}

/*
 * Files.
 */

static int terminal_set_transcript(void *context, int on)
{
    terminal *t = context;

    /* Spec §7.1.1.2: the file is opened once and kept, however often the
     * game turns the stream off and on. */
    if (on && t->transcript == NULL) {
        t->transcript = fopen(t->transcript_path, "a");
        if (t->transcript == NULL) {
            fprintf(stderr, "quiddity: cannot write a transcript to %s\n", t->transcript_path);
            return 0;
        }
    }
    if (!on && t->transcript != NULL) {
        fflush(t->transcript);
    }
    return t->transcript != NULL;
}

static void terminal_transcript_char(void *context, uint16_t zscii)
{
    terminal *t = context;
    uint16_t unicode;

    if (t->transcript == NULL) {
        return;
    }
    if (zscii == ZM_ZSCII_NEWLINE) {
        utf8_put_end(t->transcript, &t->transcript_held);
        putc('\n', t->transcript);
        return;
    }
    unicode = zm_zscii_to_unicode(t->mem, zscii);
    if (unicode != 0) {
        utf8_put_unit(t->transcript, unicode, &t->transcript_held);
    }
}

static void terminal_record_input(void *context, const uint16_t *zscii, unsigned count)
{
    terminal *t = context;
    uint16_t held = 0;

    if (t->record == NULL) {
        t->record = fopen(t->record_path, "a");
        if (t->record == NULL) {
            fprintf(stderr, "quiddity: cannot record commands to %s\n", t->record_path);
            t->record_path = NULL;
            return;
        }
    }

    /* One command or keypress per line, in the style Spec §7's Remarks
     * suggest: a key with no printable form is written as its code in
     * square brackets. */
    for (unsigned i = 0; i < count; i++) {
        uint16_t unicode = zm_zscii_to_unicode(t->mem, zscii[i]);

        if (unicode != 0) {
            utf8_put_unit(t->record, unicode, &held);
        } else {
            utf8_put_end(t->record, &held);
            fprintf(t->record, "[%u]", (unsigned)zscii[i]);
        }
    }
    utf8_put_end(t->record, &held);
    putc('\n', t->record);
    fflush(t->record);
}

/*
 * Input.
 */

/* Translate a character typed by the player into ZSCII (Spec §3.8,
 * §10.7): one code, or none if ZSCII has no code for it, or two for a
 * character beyond the Basic Multilingual Plane whose surrogate halves
 * the story's extra characters table names (utf8.h). */
static unsigned typed_to_zscii(const terminal *t, uint32_t unicode, uint16_t out[2])
{
    uint16_t units[2];
    unsigned count = utf8_units(unicode, units);

    for (unsigned i = 0; i < count; i++) {
        out[i] = zm_unicode_to_zscii(t->mem, units[i]);
        if (out[i] == 0) {
            return 0;
        }
    }
    return count;
}

/* The game is about to wait: everything it has drawn should be visible,
 * the upper window above the unfinished line, or the canvas as it now
 * stands. */
static void before_input(terminal *t)
{
    if (t->canvas) {
        show_canvas(t);
    } else {
        show_upper(t);
    }
    flush_line(t);
}

/* Whether there is something to read on standard input within a time
 * in tenths of a second, 0 meaning however long it takes. Standard input
 * is read unbuffered (see terminal_init), so what the system reports is
 * all there is. */
static int input_ready(unsigned tenths)
{
#ifdef _WIN32
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    DWORD type = GetFileType(in);
    ULONGLONG deadline = GetTickCount64() + (ULONGLONG)tenths * 100u;

    /* A file is always ready. A console reports keys pressed; a pipe
     * reports bytes waiting, or fails once its writer has gone, which
     * is the end of input and so ready too. */
    if (tenths == 0 || (type != FILE_TYPE_CHAR && type != FILE_TYPE_PIPE)) {
        return 1;
    }
    for (;;) {
        DWORD available = 0;

        if (type == FILE_TYPE_CHAR ? _kbhit() != 0
                                   : !PeekNamedPipe(in, NULL, 0, NULL, &available, NULL) || available > 0) {
            return 1;
        }
        if (GetTickCount64() >= deadline) {
            return 0;
        }
        Sleep(10);
    }
#else
    fd_set fds;
    struct timeval tv;

    if (tenths == 0) {
        return 1;
    }
    FD_ZERO(&fds);
    FD_SET(0, &fds);
    tv.tv_sec = tenths / 10;
    tv.tv_usec = (tenths % 10) * 100000L;
    return select(1, &fds, NULL, NULL, &tv) != 0;
#endif
}

/*
 * Keys (Spec §10.6, §3.8.4).
 *
 * A line is typed with the terminal's own line editor, but a single key
 * has to arrive as it is pressed, without a return after it, or
 * "press any key" waits for two. So for a read_char the console is put
 * into a mode that hands keys over one at a time and does not echo
 * them, and put back after. The Escape key arrives as its own code, 27,
 * which is also what the console's line editor would have swallowed;
 * the cursor keys arrive as the sequences ESC [ A to D, which become
 * the cursor codes 129 to 132; backspace, as 8 or 127, becomes the
 * delete code 8. Nothing of this applies when input is a file.
 */

#ifdef _WIN32
static DWORD cooked_mode;
#else
static struct termios cooked_mode;
#endif

static void keys_mode(const terminal *t, int on)
{
    if (!t->interactive) {
        return;
    }
#ifdef _WIN32
    {
        HANDLE in = GetStdHandle(STD_INPUT_HANDLE);

        if (on) {
            if (!GetConsoleMode(in, &cooked_mode)) {
                return;
            }
            SetConsoleMode(in, (cooked_mode & ~(DWORD)(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT))
                                   | ENABLE_VIRTUAL_TERMINAL_INPUT);
        } else {
            SetConsoleMode(in, cooked_mode);
        }
    }
#else
    if (on) {
        struct termios raw;

        if (tcgetattr(0, &cooked_mode) != 0) {
            return;
        }
        raw = cooked_mode;
        raw.c_lflag &= ~(tcflag_t)(ICANON | ECHO);
        raw.c_cc[VMIN] = 1;
        raw.c_cc[VTIME] = 0;
        tcsetattr(0, TCSANOW, &raw);
    } else {
        tcsetattr(0, TCSANOW, &cooked_mode);
    }
#endif
}

/* An escape has been read: if the rest of a cursor key's sequence is
 * right behind it, that key; otherwise the escape itself. */
static uint16_t escape_or_cursor_key(const terminal *t)
{
    int c;

    if (!t->interactive || !input_ready(1)) {
        return ZM_ZSCII_ESCAPE;
    }
    c = getchar();
    if (c != '[' && c != 'O') {
        return ZM_ZSCII_ESCAPE;
    }
    switch (getchar()) {
    case 'A': return 129;   /* up */
    case 'B': return 130;   /* down */
    case 'D': return 131;   /* left */
    case 'C': return 132;   /* right */
    default:  return ZM_ZSCII_ESCAPE;
    }
}

/* Read one line typed by the player, without its line ending. Returns 0
 * when there is no more input. */
static int read_typed_line(terminal *t, char *line, size_t size)
{
    size_t length;

    if (fgets(line, (int)size, stdin) == NULL) {
        return 0;
    }
    length = strlen(line);
    if (length > 0 && line[length - 1] == '\n') {
        line[--length] = '\0';
    } else if (length == size - 1) {
        /* A line longer than the buffer: drop the rest of it. */
        int c;
        while ((c = getchar()) != EOF && c != '\n') {
        }
    }
    if (length > 0 && line[length - 1] == '\r') {
        line[--length] = '\0';
    }

    /* Spec §10.2.2: when the commands come from a file, the player is
     * not typing them, so they are shown as they would have been. */
    if (!t->interactive) {
        fputs(line, stdout);
        putchar('\n');
    }
    return 1;
}

/* A line of the interpreter's own, in the lower window. */
static void say(terminal *t, const char *text)
{
    for (; *text != '\0'; text++) {
        put_lower(t, (uint16_t)(unsigned char)*text);
    }
    put_lower(t, '\n');
}

static int terminal_read_line(void *context, uint16_t *buffer, unsigned *count, unsigned max,
                              uint16_t *terminator, unsigned timeout)
{
    terminal *t = context;
    char line[TERMINAL_LINE_MAX];
    size_t length;
    size_t i = 0;

    before_input(t);
    if (!input_ready(timeout)) {
        return ZM_INPUT_TIMEOUT;
    }
    if (!read_typed_line(t, line, sizeof line)) {
        return ZM_INPUT_END;
    }
    length = strlen(line);

    while (i < length && *count < max) {
        uint16_t zscii[2];
        unsigned n = typed_to_zscii(t, utf8_next((const unsigned char *)line, &i), zscii);

        if (n > 0 && *count + n <= max) {
            for (unsigned k = 0; k < n; k++) {
                buffer[(*count)++] = zscii[k];
            }
        }
    }
    *terminator = ZM_ZSCII_NEWLINE;
    t->echo_pending = t->canvas && *count > 0;
    return 0;
}

static int terminal_read_char(void *context, unsigned timeout)
{
    terminal *t = context;
    unsigned char bytes[4];
    size_t i = 0;
    int c;
    uint16_t zscii[2];
    unsigned n;

    if (t->pending_key != 0) {
        zscii[0] = t->pending_key;
        t->pending_key = 0;
        return zscii[0];
    }
    before_input(t);
    fflush(stdout);
    keys_mode(t, 1);
    if (!input_ready(timeout)) {
        keys_mode(t, 0);
        return ZM_INPUT_TIMEOUT;
    }

    c = getchar();
    if (c == EOF) {
        keys_mode(t, 0);
        return ZM_INPUT_END;
    }
    if (c == '\n' || c == '\r') {
        keys_mode(t, 0);
        return ZM_ZSCII_NEWLINE;
    }
    if (c == 0x1b) {
        zscii[0] = escape_or_cursor_key(t);
        keys_mode(t, 0);
        return zscii[0];
    }
    if (c == 0x08 || c == 0x7f) {
        keys_mode(t, 0);
        return ZM_ZSCII_DELETE;
    }

    /* Gather a UTF-8 sequence, then translate it as a line would be.
     * A key with no ZSCII code becomes a space rather than nothing, so
     * that "press any key" still works. */
    bytes[0] = (unsigned char)c;
    if (c >= 0x80) {
        unsigned extra = (c & 0xe0) == 0xc0 ? 1 : (c & 0xf0) == 0xe0 ? 2 : (c & 0xf8) == 0xf0 ? 3 : 0;
        for (unsigned k = 1; k <= extra; k++) {
            int next = getchar();
            if (next == EOF) {
                break;
            }
            bytes[k] = (unsigned char)next;
        }
    }
    keys_mode(t, 0);
    n = typed_to_zscii(t, utf8_next(bytes, &i), zscii);
    if (n == 2) {
        t->pending_key = zscii[1];
    }
    return n != 0 ? zscii[0] : ZM_ZSCII_SPACE;
}

/*
 * Saved games and auxiliary files (Spec §15 save and restore, §7.6).
 */

/* The default name for a saved game: the story file's own name with its
 * extension replaced by .sav, in the directory the player is in. */
static void default_save_name(const char *story_path, char *name, size_t size)
{
    const char *base = story_path;
    const char *dot;
    size_t length;

    for (const char *p = story_path; *p != '\0'; p++) {
        if (*p == '/' || *p == '\\') {
            base = p + 1;
        }
    }
    dot = strrchr(base, '.');
    length = dot != NULL && dot != base ? (size_t)(dot - base) : strlen(base);
    if (length > size - 5) {
        length = size - 5;
    }
    memcpy(name, base, length);
    strcpy(name + length, ".sav");
}

/* Spec §7.6.1: an auxiliary file's name as the game gives it is made
 * safe to use: letters and digits only, in upper case, cut at the first
 * full stop, and given the extension .AUX, so that a game can neither
 * reach outside the current directory nor overwrite its own files
 * (Spec §7.6.1.2, §7.6.1.4). A name that is a device on Windows (CON,
 * AUX, NUL, and the like) is set apart with an underscore. */
static void auxiliary_name(const char *given, char *name, size_t size)
{
    static const char *const devices[] = { "CON", "PRN", "AUX", "NUL", "COM", "LPT" };
    size_t length = 0;

    for (; given != NULL && *given != '\0' && *given != '.'; given++) {
        if (isalnum((unsigned char)*given) && length < size - 6) {
            name[length++] = (char)toupper((unsigned char)*given);
        }
    }
    name[length] = '\0';
    if (length == 0) {
        strcpy(name, "DATA");
        length = 4;
    }
    for (unsigned i = 0; i < sizeof devices / sizeof devices[0]; i++) {
        if (strncmp(name, devices[i], 3) == 0 && (length == 3 || (length == 4 && isdigit((unsigned char)name[3])))) {
            memmove(name + 1, name, length + 1);
            name[0] = '_';
            length++;
        }
    }
    strcpy(name + length, ".AUX");
}

static int file_exists(const char *path)
{
    FILE *file = fopen(path, "rb");

    if (file == NULL) {
        return 0;
    }
    fclose(file);
    return 1;
}

/* Put a question to the player and take a line in answer; an empty
 * answer means the default. Returns 0 if the input has run out. */
static int ask(terminal *t, const char *question, const char *default_answer, char *answer, size_t size)
{
    before_input(t);
    printf("%s [%s] ", question, default_answer);
    fflush(stdout);
    if (!read_typed_line(t, answer, size)) {
        return 0;
    }
    if (answer[0] == '\0') {
        strncpy(answer, default_answer, size - 1);
        answer[size - 1] = '\0';
    }
    return 1;
}

/* Settle which file a save or restore means. "All questions about
 * filenames are asked by interpreters" (Spec §15 save): a saved game is
 * always asked about, with the last name used as the default, and an
 * existing file is not written over without a yes; an auxiliary file is
 * asked about unless the game said not to (Spec §15 save, prompt). */
static int choose_file(terminal *t, zm_file_kind kind, const char *name, int prompt, int writing, char *path)
{
    char answer[TERMINAL_NAME_MAX];

    if (kind == ZM_FILE_AUXILIARY) {
        auxiliary_name(name, path, TERMINAL_NAME_MAX);
        if (prompt == 0) {
            return 1;
        }
        if (!ask(t, writing ? "Enter a file name to write to." : "Enter a file name to read from.",
                 path, answer, sizeof answer)) {
            return 0;
        }
        strcpy(path, answer);
        return 1;
    }

    if (!ask(t, writing ? "Enter a file name to save to." : "Enter a file name to restore from.",
             t->save_name, path, TERMINAL_NAME_MAX)) {
        return 0;
    }
    if (writing && file_exists(path)) {
        if (!ask(t, "That file exists. Overwrite it? (y/n)", "n", answer, sizeof answer)
            || (answer[0] != 'y' && answer[0] != 'Y')) {
            return 0;
        }
    }
    return 1;
}

static int terminal_write_file(void *context, zm_file_kind kind, const char *name, int prompt,
                               const uint8_t *data, uint32_t size)
{
    terminal *t = context;
    char path[TERMINAL_NAME_MAX];
    FILE *file;
    int ok;

    if (!choose_file(t, kind, name, prompt, 1, path)) {
        return 0;
    }
    file = fopen(path, "wb");
    if (file == NULL) {
        say(t, "That file cannot be written.");
        return 0;
    }
    ok = fwrite(data, 1, size, file) == size;
    ok = fclose(file) == 0 && ok;
    if (!ok) {
        say(t, "That file could not be written in full.");
    } else if (kind == ZM_FILE_SAVED_GAME) {
        strcpy(t->save_name, path);
    }
    return ok;
}

static uint8_t *terminal_read_file(void *context, zm_file_kind kind, const char *name, int prompt, uint32_t *size)
{
    terminal *t = context;
    char path[TERMINAL_NAME_MAX];
    FILE *file;
    long length;
    uint8_t *data;

    if (!choose_file(t, kind, name, prompt, 0, path)) {
        return NULL;
    }
    file = fopen(path, "rb");
    if (file == NULL) {
        say(t, "That file cannot be read.");
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        say(t, "That file cannot be read.");
        return NULL;
    }
    data = malloc(length > 0 ? (size_t)length : 1);
    if (data == NULL || fread(data, 1, (size_t)length, file) != (size_t)length) {
        free(data);
        fclose(file);
        say(t, "That file could not be read in full.");
        return NULL;
    }
    fclose(file);
    if (kind == ZM_FILE_SAVED_GAME) {
        strcpy(t->save_name, path);
    }
    *size = (uint32_t)length;
    return data;
}

/*
 * Set-up.
 */

void terminal_init(terminal *t, const zm_memory *mem, zm_io *io, const char *story_path,
                   const char *transcript_path, const char *record_path)
{
    memset(t, 0, sizeof *t);
    t->mem = mem;
    default_save_name(story_path, t->save_name, sizeof t->save_name);
    t->interactive = terminal_isatty(stdin);
    t->buffering = 1;
    t->window = ZM_WINDOW_LOWER;
    t->upper_row = 1;
    t->upper_column = 1;
    t->upper_shown = 1;
    t->transcript_path = transcript_path;
    t->record_path = record_path;
    clear_upper_rows(t, 0, TERMINAL_HEIGHT);
    t->canvas = mem->version == 6;
    for (unsigned r = 0; r < TERMINAL_HEIGHT; r++) {
        for (unsigned c = 0; c < TERMINAL_WIDTH; c++) {
            t->cells[r][c] = ' ';
        }
    }

    /* Standard input is read as it arrives rather than in buffered
     * chunks, so that input_ready() sees everything there is to see:
     * a line already drawn into a buffer behind the system's back would
     * look like silence. */
    setvbuf(stdin, NULL, _IONBF, 0);

    memset(io, 0, sizeof *io);
    io->context = t;
    io->screen_width = TERMINAL_WIDTH;
    io->screen_height = TERMINAL_HEIGHT;
    io->timed_input = 1;

    /* Everything a terminal prints is in one fixed-pitch font; the other
     * styles and colours are left unclaimed, and text asked for in them
     * is printed plainly. */
    io->styles = ZM_STYLE_FIXED_PITCH;

    io->print_char = terminal_print_char;
    io->print_unicode = terminal_print_unicode;
    io->read_line = terminal_read_line;
    io->read_char = terminal_read_char;
    io->show_status = terminal_show_status;
    io->split_window = terminal_split_window;
    io->set_window = terminal_set_window;
    io->erase_window = terminal_erase_window;
    io->erase_line = terminal_erase_line;
    io->set_cursor = terminal_set_cursor;
    io->set_buffering = terminal_set_buffering;
    if (transcript_path != NULL) {
        io->set_transcript = terminal_set_transcript;
        io->transcript_char = terminal_transcript_char;
    }
    if (record_path != NULL) {
        io->record_input = terminal_record_input;
    }
    io->write_file = terminal_write_file;
    io->read_file = terminal_read_file;
    if (t->canvas) {
        io->plot_text = terminal_plot_text;
        io->erase_area = terminal_erase_area;
        io->scroll_area = terminal_scroll_area;
        io->draw_picture = terminal_draw_picture;
        io->picture_size = terminal_picture_size;
    }

#ifdef _WIN32
    /* The Windows console decodes output in its own code page unless told
     * otherwise; everything written here is UTF-8, and read as UTF-8. */
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
}

void terminal_finish(terminal *t)
{
    if (t->canvas) {
        show_canvas(t);
    } else {
        show_upper(t);
    }
    flush_line(t);
    if (t->transcript != NULL) {
        fclose(t->transcript);
        t->transcript = NULL;
    }
    if (t->record != NULL) {
        fclose(t->record);
        t->record = NULL;
    }
}
