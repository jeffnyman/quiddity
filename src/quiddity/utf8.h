/*
 * utf8.h: UTF-8 at the edges of the front ends.
 *
 * The Z-machine's characters are ZSCII inside and Unicode at the seam
 * (Spec §3.8), and never leave the Basic Multilingual Plane (Spec
 * §3.8.5.1), so a character is at most three bytes of UTF-8. Whatever
 * the front end writes to, a terminal or a buffer, and whatever it
 * reads from, is UTF-8 here, and these are the two translations.
 *
 * A character beyond the Plane, an emoji say, has no ZSCII code and no
 * 16-bit Unicode value, so a story that wants one prints it as the two
 * halves of its UTF-16 surrogate pair, one print_unicode each, and
 * names the halves in its extra characters table so that one typed can
 * be read back the same way (smileys.z5 does both). The halves are not
 * characters, and written one at a time they are not UTF-8 either, so
 * the functions below put a pair back together on the way out and take
 * a character apart on the way in.
 */

#ifndef QUIDDITY_UTF8_H
#define QUIDDITY_UTF8_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* Encode one character into out, which must have room for three bytes,
 * and return how many were used. */
unsigned utf8_encode(uint16_t c, char *out);

/* Encode a character beyond the Basic Multilingual Plane, given as the
 * surrogate pair a story must print it as (Spec §3.8.5.1 stops at
 * 16 bits), into out, which must have room for four bytes, and return
 * how many were used. */
unsigned utf8_encode_pair(uint16_t high, uint16_t low, char *out);

/* Write one character to a stream. */
void utf8_put(FILE *out, uint16_t c);

/* Write UTF-16 code units to a stream as the characters they spell: a
 * high surrogate followed by a low one is the character beyond the
 * Plane they stand for, and a surrogate on its own, which is not a
 * character, is written as a question mark. */
void utf8_put_units(FILE *out, const uint16_t *units, size_t count);

/* The same one unit at a time, for text that arrives that way: *held
 * carries a high surrogate from one call to the next, and starts at 0.
 * utf8_put_end writes out whatever is still held, as a question mark,
 * where the text ends. */
void utf8_put_unit(FILE *out, uint16_t unit, uint16_t *held);
void utf8_put_end(FILE *out, uint16_t *held);

/* The UTF-16 code units of a code point, one or two, in out; returns
 * how many. */
unsigned utf8_units(uint32_t c, uint16_t out[2]);

/* Decode one sequence from a byte string, advancing the index past it.
 * Malformed input yields U+FFFD, the replacement character, which ZSCII
 * cannot hold, so that a caller translating to ZSCII drops it. */
uint32_t utf8_next(const unsigned char *s, size_t *i);

#endif /* QUIDDITY_UTF8_H */
