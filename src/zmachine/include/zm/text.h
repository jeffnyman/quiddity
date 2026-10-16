/*
 * zm/text.h: decoding text out of memory.
 *
 * Implements Spec §3. Text in a story file is stored as Z-characters,
 * five-bit values packed three to a word, and reading it back is a small
 * state machine: the current alphabet, shifts that change it, escapes for
 * characters no alphabet holds, and abbreviations that splice in another
 * string. What comes out is ZSCII (Spec §3.8), the Z-machine's own
 * character set, which agrees with ASCII from 32 to 126 and leaves a block
 * of "extra characters" whose meaning is given by a Unicode table.
 *
 * This file does no output. The decoder hands each ZSCII character to a
 * sink supplied by the caller, so the same function serves the print
 * opcodes, the dictionary, and the tests, none of which agree on what to
 * do with a character once they have it.
 */

#ifndef ZM_TEXT_H
#define ZM_TEXT_H

#include <stdint.h>

#include "zm/memory.h"

/* ZSCII codes with a fixed meaning (Spec §3.8.2, §3.8.3). ZSCII is a
 * ten-bit code, so a uint16_t carries it. */
enum {
    ZM_ZSCII_NULL    = 0,
    ZM_ZSCII_DELETE  = 8,    /* input only */
    ZM_ZSCII_TAB     = 9,    /* output, Version 6 only */
    ZM_ZSCII_SPACE_SENTENCE = 11, /* output, Version 6 only */
    ZM_ZSCII_NEWLINE = 13,
    ZM_ZSCII_ESCAPE  = 27,   /* input only */
    ZM_ZSCII_SPACE   = 32,

    /* Spec §3.8.5: the block whose meaning the story file may define. */
    ZM_ZSCII_EXTRA_FIRST = 155,
    ZM_ZSCII_EXTRA_LAST  = 251
};

/* Receives one ZSCII character. The context pointer is whatever the
 * caller passed to zm_text_decode(). */
typedef void (*zm_text_sink)(void *context, uint16_t zscii);

/* Decode the Z-string at a byte address, passing each ZSCII character to
 * the sink in order. Returns the byte address just past the string's last
 * word, which is where execution resumes after an opcode with the string
 * embedded in it (Spec §4.8, print and print_ret).
 *
 * The rules applied depend on the Version in memory: the alphabet table,
 * whether Z-characters 2 and 3 are shifts or abbreviations, and whether
 * shift locks exist all changed between Versions 1 and 3 (Spec §3.2, §3.3,
 * §3.5). A string that uses an abbreviation from inside an abbreviation is
 * a fault (Spec §3.3.1). */
uint32_t zm_text_decode(const zm_memory *mem, uint32_t address, zm_text_sink sink, void *context);

/* Decode an abbreviation by number, 0 to 95 (Spec §3.3). This is what
 * zm_text_decode() does when it meets one, exposed so a tool can list a
 * story's abbreviations. */
void zm_text_decode_abbreviation(const zm_memory *mem, unsigned number, zm_text_sink sink, void *context);

/* The Unicode character a ZSCII code prints as, or 0 if the code has no
 * printable form (Spec §3.8). Codes 32 to 126 are themselves; the extra
 * characters 155 to 251 go through the story's Unicode translation table,
 * or the default one in Spec §3.8.5.3 if the story has none. Everything
 * else, including newline, is a control code or input-only, and the
 * caller must handle those before asking here. */
uint16_t zm_zscii_to_unicode(const zm_memory *mem, uint16_t zscii);

#endif /* ZM_TEXT_H */
