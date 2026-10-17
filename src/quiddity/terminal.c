/*
 * terminal.c: see terminal.h.
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L /* for fileno() and isatty() */
#endif

#include "terminal.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zm/screen.h"
#include "zm/text.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <io.h>
#include <windows.h>
#define terminal_isatty(stream) _isatty(_fileno(stream))
#else
#include <unistd.h>
#define terminal_isatty(stream) isatty(fileno(stream))
#endif

/*
 * Output.
 */

/* Write a Unicode character to a stream as UTF-8. The Z-machine never
 * needs more than the Basic Multilingual Plane (Spec §3.8.5.1), so three
 * bytes is the most it takes. */
static void put_utf8(FILE *out, uint16_t c)
{
    if (c < 0x80) {
        putc((int)c, out);
    } else if (c < 0x800) {
        putc(0xc0 | (c >> 6), out);
        putc(0x80 | (c & 0x3f), out);
    } else {
        putc(0xe0 | (c >> 12), out);
        putc(0x80 | ((c >> 6) & 0x3f), out);
        putc(0x80 | (c & 0x3f), out);
    }
}

/* Write the first count characters of the held-back line without ending
 * it, and keep the rest. */
static void emit_line(terminal *t, unsigned count)
{
    for (unsigned i = 0; i < count; i++) {
        put_utf8(stdout, t->line[i]);
    }
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
    if (t->line_length == TERMINAL_WIDTH) {
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
        for (unsigned c = 0; c < end; c++) {
            put_utf8(stdout, t->upper[r][c]);
        }
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
            put_utf8(stdout, zm_zscii_to_unicode(t->mem, name[i]));
        }
        fputs("...", stdout);
        used = room;
    } else {
        for (unsigned i = 0; i < name_length && i < room; i++) {
            put_utf8(stdout, zm_zscii_to_unicode(t->mem, name[i]));
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
        putc('\n', t->transcript);
        return;
    }
    unicode = zm_zscii_to_unicode(t->mem, zscii);
    if (unicode != 0) {
        put_utf8(t->transcript, unicode);
    }
}

static void terminal_record_input(void *context, const uint16_t *zscii, unsigned count)
{
    terminal *t = context;

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
            put_utf8(t->record, unicode);
        } else {
            fprintf(t->record, "[%u]", (unsigned)zscii[i]);
        }
    }
    putc('\n', t->record);
    fflush(t->record);
}

/*
 * Input.
 */

/* Decode one UTF-8 sequence from a byte string, advancing the index.
 * Malformed input yields the replacement character, which ZSCII cannot
 * hold, so it is dropped. */
static uint32_t next_utf8(const unsigned char *s, size_t *i)
{
    unsigned char lead = s[*i];
    uint32_t c;
    unsigned extra;

    (*i)++;
    if (lead < 0x80) {
        return lead;
    }
    if ((lead & 0xe0) == 0xc0) {
        c = lead & 0x1fu;
        extra = 1;
    } else if ((lead & 0xf0) == 0xe0) {
        c = lead & 0x0fu;
        extra = 2;
    } else if ((lead & 0xf8) == 0xf0) {
        c = lead & 0x07u;
        extra = 3;
    } else {
        return 0xfffd;
    }
    for (unsigned k = 0; k < extra; k++) {
        if ((s[*i] & 0xc0) != 0x80) {
            return 0xfffd;
        }
        c = (c << 6) | (s[*i] & 0x3fu);
        (*i)++;
    }
    return c;
}

/* Translate a Unicode character typed by the player into ZSCII, or 0 if
 * ZSCII has no code for it (Spec §3.8, §10.7). */
static uint16_t unicode_to_zscii(const terminal *t, uint32_t unicode)
{
    return unicode > 0xffff ? 0 : zm_unicode_to_zscii(t->mem, (uint16_t)unicode);
}

/* The game is about to wait: everything it has drawn should be visible,
 * the upper window above the unfinished line. */
static void before_input(terminal *t)
{
    show_upper(t);
    flush_line(t);
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

static int terminal_read_line(void *context, uint16_t *buffer, unsigned max, uint16_t *terminator)
{
    terminal *t = context;
    char line[TERMINAL_LINE_MAX];
    size_t length;
    size_t i = 0;
    unsigned count = 0;

    before_input(t);
    if (!read_typed_line(t, line, sizeof line)) {
        return -1;
    }
    length = strlen(line);

    while (i < length && count < max) {
        uint16_t zscii = unicode_to_zscii(t, next_utf8((const unsigned char *)line, &i));

        if (zscii != 0) {
            buffer[count++] = zscii;
        }
    }
    *terminator = ZM_ZSCII_NEWLINE;
    return (int)count;
}

static int terminal_read_char(void *context)
{
    terminal *t = context;
    unsigned char bytes[4];
    size_t i = 0;
    int c;
    uint16_t zscii;

    before_input(t);

    c = getchar();
    if (c == EOF) {
        return -1;
    }
    if (c == '\n' || c == '\r') {
        return ZM_ZSCII_NEWLINE;
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
    zscii = unicode_to_zscii(t, next_utf8(bytes, &i));
    return zscii != 0 ? zscii : ZM_ZSCII_SPACE;
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

    memset(io, 0, sizeof *io);
    io->context = t;
    io->screen_width = TERMINAL_WIDTH;
    io->screen_height = TERMINAL_HEIGHT;

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

#ifdef _WIN32
    /* The Windows console decodes output in its own code page unless told
     * otherwise; everything written here is UTF-8, and read as UTF-8. */
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
}

void terminal_finish(terminal *t)
{
    show_upper(t);
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
