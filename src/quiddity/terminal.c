/*
 * terminal.c: see terminal.h.
 */

#include "terminal.h"

#include <stdio.h>

#include "zm/text.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

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

static void terminal_print_char(void *context, uint16_t zscii)
{
    terminal *t = context;
    uint16_t unicode;

    if (zscii == ZM_ZSCII_NEWLINE) {
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
    put_utf8(unicode != 0 ? unicode : '?');
}

void terminal_init(terminal *t, const zm_memory *mem, zm_io *io)
{
    t->mem = mem;

    io->context = t;
    io->print_char = terminal_print_char;

#ifdef _WIN32
    /* The Windows console decodes output in its own code page unless told
     * otherwise; everything written here is UTF-8. */
    SetConsoleOutputCP(CP_UTF8);
#endif
}
