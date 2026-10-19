/*
 * test_utf8.c: the UTF-8 translations at the edges of the front ends.
 *
 * Covers src/quiddity/utf8.h: encoding and decoding, and the surrogate
 * pairs a story prints a character beyond the Basic Multilingual Plane
 * as, which are put together on the way out and taken apart on the way
 * in.
 */

#include <stdio.h>
#include <string.h>

#include "utf8.h"

#include "check.h"

static size_t read_file(const char *path, char *text, size_t size)
{
    FILE *file = fopen(path, "rb");
    size_t read = 0;

    if (file != NULL) {
        read = fread(text, 1, size - 1, file);
        fclose(file);
    }
    text[read] = '\0';
    return read;
}

static void test_characters_are_encoded_and_decoded(void)
{
    char bytes[4];
    size_t i = 0;
    static const unsigned char text[] = "a\xc3\xa9\xe2\x86\x91\xf0\x9f\x98\x83\xff";

    CHECK_EQ_INT(utf8_encode('a', bytes), 1);
    CHECK_EQ_INT(utf8_encode(0xe9, bytes), 2);
    CHECK(memcmp(bytes, "\xc3\xa9", 2) == 0);
    CHECK_EQ_INT(utf8_encode(0x2191, bytes), 3);
    CHECK(memcmp(bytes, "\xe2\x86\x91", 3) == 0);
    CHECK_EQ_INT(utf8_encode_pair(0xd83d, 0xde03, bytes), 4);
    CHECK(memcmp(bytes, "\xf0\x9f\x98\x83", 4) == 0);

    CHECK_EQ_INT(utf8_next(text, &i), 'a');
    CHECK_EQ_INT(utf8_next(text, &i), 0xe9);
    CHECK_EQ_INT(utf8_next(text, &i), 0x2191);
    CHECK_EQ_INT(utf8_next(text, &i), 0x1f603);
    CHECK_EQ_INT(utf8_next(text, &i), 0xfffd);     /* malformed */
    CHECK_EQ_INT(i, 11);
}

static void test_surrogate_pairs_are_put_together_and_taken_apart(void)
{
    static const char path[] = "utf8-by-test.txt";
    static const uint16_t units[] = { 'a', 0xd83d, 0xde03, 'b', 0xd83d, 'c', 0xde03, 0xd83d };
    uint16_t out[2];
    uint16_t held = 0;
    char text[64];
    FILE *file;

    /* A pair becomes the one character; a half on its own, which is
     * not a character, a question mark. */
    file = fopen(path, "wb");
    CHECK(file != NULL);
    if (file != NULL) {
        utf8_put_units(file, units, sizeof units / sizeof units[0]);
        fclose(file);
    }
    read_file(path, text, sizeof text);
    CHECK_EQ_STR(text, "a\xf0\x9f\x98\x83" "b?c??");

    /* The same one unit at a time, across calls. */
    file = fopen(path, "wb");
    CHECK(file != NULL);
    if (file != NULL) {
        utf8_put_unit(file, 0xd83d, &held);
        CHECK_EQ_INT(held, 0xd83d);
        utf8_put_unit(file, 0xde03, &held);
        CHECK_EQ_INT(held, 0);
        utf8_put_unit(file, 0xd83d, &held);
        utf8_put_end(file, &held);
        CHECK_EQ_INT(held, 0);
        fclose(file);
    }
    read_file(path, text, sizeof text);
    CHECK_EQ_STR(text, "\xf0\x9f\x98\x83?");
    remove(path);

    /* A character typed comes apart into the halves a story names. */
    CHECK_EQ_INT(utf8_units('a', out), 1);
    CHECK_EQ_INT(out[0], 'a');
    CHECK_EQ_INT(utf8_units(0xffff, out), 1);
    CHECK_EQ_INT(utf8_units(0x1f603, out), 2);
    CHECK_EQ_INT(out[0], 0xd83d);
    CHECK_EQ_INT(out[1], 0xde03);
}

int main(void)
{
    RUN_TEST(test_characters_are_encoded_and_decoded);
    RUN_TEST(test_surrogate_pairs_are_put_together_and_taken_apart);
    return CHECK_SUMMARY();
}
