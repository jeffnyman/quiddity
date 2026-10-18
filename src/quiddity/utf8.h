/*
 * utf8.h: UTF-8 at the edges of the front ends.
 *
 * The Z-machine's characters are ZSCII inside and Unicode at the seam
 * (Spec §3.8), and never leave the Basic Multilingual Plane (Spec
 * §3.8.5.1), so a character is at most three bytes of UTF-8. Whatever
 * the front end writes to, a terminal or a buffer, and whatever it
 * reads from, is UTF-8 here, and these are the two translations.
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

/* Decode one sequence from a byte string, advancing the index past it.
 * Malformed input yields U+FFFD, the replacement character, which ZSCII
 * cannot hold, so that a caller translating to ZSCII drops it. */
uint32_t utf8_next(const unsigned char *s, size_t *i);

#endif /* QUIDDITY_UTF8_H */
