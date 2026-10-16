/*
 * terminal.c: see terminal.h.
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L /* for fileno() and isatty() */
#endif

#include "terminal.h"

#include <stdio.h>
#include <string.h>

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

/* Write a Unicode character to stdout as UTF-8. The Z-machine never
 * needs more than the Basic Multilingual Plane (Spec §3.8.5.1), so three
 * bytes is the most it takes. */
static void put_utf8(uint16_t c)
{
    if (c < 0x80) {
        putchar((int)c);
    } else if (c < 0x800) {
        putchar(0xc0 | (c >> 6));
        putchar(0x80 | (c & 0x3f));
    } else {
        putchar(0xe0 | (c >> 12));
        putchar(0x80 | ((c >> 6) & 0x3f));
        putchar(0x80 | (c & 0x3f));
    }
}

/* Write the held-back line without ending it. */
static void flush_line(terminal *t)
{
    for (unsigned i = 0; i < t->line_length; i++) {
        put_utf8(t->line[i]);
    }
    t->line_length = 0;
    fflush(stdout);
}

static void terminal_print_char(void *context, uint16_t zscii)
{
    terminal *t = context;
    uint16_t unicode;

    if (zscii == ZM_ZSCII_NEWLINE) {
        flush_line(t);
        putchar('\n');
        return;
    }

    /* Spec §3.8.2.1: null prints nothing. Spec §3.8.5.4.3: a character
     * with no letter-form is shown as a question mark; the same is done
     * here for any code that has no printable meaning at all. */
    if (zscii == ZM_ZSCII_NULL) {
        return;
    }
    unicode = zm_zscii_to_unicode(t->mem, zscii);
    if (t->line_length == TERMINAL_LINE_MAX) {
        flush_line(t);
    }
    t->line[t->line_length++] = unicode != 0 ? unicode : '?';
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
    room = t->width > right_length + 2u ? t->width - right_length - 2u : 0;
    if (name_length > room && room > 3) {
        for (unsigned i = 0; i < room - 3; i++) {
            put_utf8(zm_zscii_to_unicode(t->mem, name[i]));
        }
        fputs("...", stdout);
        used = room;
    } else {
        for (unsigned i = 0; i < name_length && i < room; i++) {
            put_utf8(zm_zscii_to_unicode(t->mem, name[i]));
        }
        used = name_length < room ? name_length : room;
    }
    for (unsigned i = used; i + right_length < t->width; i++) {
        putchar(' ');
    }
    fputs(right, stdout);
    putchar('\n');
}

/*
 * Input.
 */

/* Translate a Unicode character typed by the player into ZSCII, or 0 if
 * ZSCII has no code for it (Spec §3.8, §10.7). */
static uint16_t unicode_to_zscii(const terminal *t, uint32_t unicode)
{
    if (unicode >= 32 && unicode <= 126) {
        return (uint16_t)unicode;
    }
    if (unicode > 0xffff) {
        return 0;
    }
    /* Spec §3.8.5: the extra characters, through the story's table. */
    for (uint16_t zscii = ZM_ZSCII_EXTRA_FIRST; zscii <= ZM_ZSCII_EXTRA_LAST; zscii++) {
        if (zm_zscii_to_unicode(t->mem, zscii) == unicode) {
            return zscii;
        }
    }
    return 0;
}

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

static int terminal_read_line(void *context, uint16_t *buffer, unsigned max, uint16_t *terminator)
{
    terminal *t = context;
    char line[TERMINAL_LINE_MAX];
    size_t length;
    size_t i = 0;
    unsigned count = 0;

    /* The prompt, if any, has been waiting; the player should see it. */
    flush_line(t);

    if (fgets(line, sizeof line, stdin) == NULL) {
        return -1;
    }
    length = strlen(line);
    if (length > 0 && line[length - 1] == '\n') {
        line[--length] = '\0';
    } else if (length == sizeof line - 1) {
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

    flush_line(t);

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
 * Set-up.
 */

void terminal_init(terminal *t, const zm_memory *mem, zm_io *io)
{
    memset(t, 0, sizeof *t);
    t->mem = mem;
    t->interactive = terminal_isatty(stdin);
    t->width = 80;

    io->context = t;
    io->print_char = terminal_print_char;
    io->read_line = terminal_read_line;
    io->read_char = terminal_read_char;
    io->show_status = terminal_show_status;

#ifdef _WIN32
    /* The Windows console decodes output in its own code page unless told
     * otherwise; everything written here is UTF-8, and read as UTF-8. */
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
}

void terminal_finish(terminal *t)
{
    flush_line(t);
}
