/*
 * test_header.c: decoding the header.
 *
 * Covers Spec §11 through zm/header.h. The first tests use hand-built
 * headers so that every field has a known value; the last ones read the
 * simple-test story files, one per Version, which Inform compiled on
 * 27 July 2026 and which therefore carry a real serial code, length, and
 * checksum.
 */

#include <string.h>

#include "zm/header.h"
#include "zm/memory.h"

#include "check.h"

static void put_word(uint8_t *bytes, uint32_t offset, uint16_t value)
{
    bytes[offset] = (uint8_t)(value >> 8);
    bytes[offset + 1] = (uint8_t)(value & 0xff);
}

static void test_length_scale_follows_version(void)
{
    /* Spec §11.1.6. */
    CHECK_EQ_INT(zm_header_length_scale(1), 2);
    CHECK_EQ_INT(zm_header_length_scale(3), 2);
    CHECK_EQ_INT(zm_header_length_scale(4), 4);
    CHECK_EQ_INT(zm_header_length_scale(5), 4);
    CHECK_EQ_INT(zm_header_length_scale(6), 8);
    CHECK_EQ_INT(zm_header_length_scale(8), 8);
}

static void test_reads_version_3_fields(void)
{
    uint8_t bytes[256];
    zm_memory mem;
    zm_header h;

    memset(bytes, 0, sizeof bytes);
    bytes[ZM_HEADER_VERSION] = 3;
    bytes[ZM_HEADER_FLAGS1] = ZM_FLAGS1_V3_STATUS_TIME | ZM_FLAGS1_V3_TANDY;
    put_word(bytes, ZM_HEADER_RELEASE, 88);
    put_word(bytes, ZM_HEADER_HIGH_BASE, 0x00c0);
    put_word(bytes, ZM_HEADER_INITIAL_PC, 0x00c1);
    put_word(bytes, ZM_HEADER_DICTIONARY, 0x0090);
    put_word(bytes, ZM_HEADER_OBJECTS, 0x0050);
    put_word(bytes, ZM_HEADER_GLOBALS, 0x0060);
    put_word(bytes, ZM_HEADER_STATIC_BASE, 0x0080);
    put_word(bytes, ZM_HEADER_FLAGS2, ZM_FLAGS2_TRANSCRIPT);
    memcpy(bytes + ZM_HEADER_SERIAL, "840726", 6);
    put_word(bytes, ZM_HEADER_ABBREVIATIONS, 0x0042);
    put_word(bytes, ZM_HEADER_FILE_LENGTH, 128);   /* x2 = 256 bytes */
    put_word(bytes, ZM_HEADER_CHECKSUM, 0x1234);
    bytes[ZM_HEADER_STANDARD_MAJOR] = 1;
    bytes[ZM_HEADER_STANDARD_MINOR] = 1;
    memcpy(bytes + ZM_HEADER_INFORM_VERSION, "6.42", 4);

    /* Version 4+ fields are deliberately non-zero so the test can show
     * they are ignored in Version 3. */
    bytes[ZM_HEADER_INTERPRETER_NUMBER] = 6;
    bytes[ZM_HEADER_SCREEN_HEIGHT] = 24;

    CHECK_EQ_INT(zm_memory_load_bytes(&mem, bytes, sizeof bytes), ZM_OK);
    zm_header_read(&mem, &h);

    CHECK_EQ_INT(h.version, 3);
    CHECK_EQ_INT(h.flags1, ZM_FLAGS1_V3_STATUS_TIME | ZM_FLAGS1_V3_TANDY);
    CHECK_EQ_INT(h.release, 88);
    CHECK_EQ_INT(h.high_base, 0x00c0);
    CHECK_EQ_INT(h.initial_pc, 0x00c1);
    CHECK_EQ_INT(h.dictionary, 0x0090);
    CHECK_EQ_INT(h.objects, 0x0050);
    CHECK_EQ_INT(h.globals, 0x0060);
    CHECK_EQ_INT(h.static_base, 0x0080);
    CHECK_EQ_INT(h.flags2, ZM_FLAGS2_TRANSCRIPT);
    CHECK_EQ_STR(h.serial, "840726");
    CHECK_EQ_INT(h.abbreviations, 0x0042);
    CHECK_EQ_INT(h.file_length, 256);
    CHECK_EQ_INT(h.checksum, 0x1234);
    CHECK_EQ_INT(h.standard_major, 1);
    CHECK_EQ_INT(h.standard_minor, 1);
    CHECK_EQ_STR(h.inform_version, "6.42");

    CHECK_EQ_INT(h.interpreter_number, 0);
    CHECK_EQ_INT(h.screen_height, 0);

    zm_memory_free(&mem);
}

static void test_reads_version_5_fields(void)
{
    uint8_t bytes[256];
    zm_memory mem;
    zm_header h;

    memset(bytes, 0, sizeof bytes);
    bytes[ZM_HEADER_VERSION] = 5;
    put_word(bytes, ZM_HEADER_STATIC_BASE, 0x0080);
    put_word(bytes, ZM_HEADER_FILE_LENGTH, 64);   /* x4 = 256 bytes */
    bytes[ZM_HEADER_INTERPRETER_NUMBER] = 6;
    bytes[ZM_HEADER_INTERPRETER_VERSION] = 'A';
    bytes[ZM_HEADER_SCREEN_HEIGHT] = 24;
    bytes[ZM_HEADER_SCREEN_WIDTH] = 80;
    put_word(bytes, ZM_HEADER_SCREEN_WIDTH_UNITS, 640);
    put_word(bytes, ZM_HEADER_SCREEN_HEIGHT_UNITS, 400);
    bytes[ZM_HEADER_FONT_BYTE_26] = 8;    /* Version 5: width */
    bytes[ZM_HEADER_FONT_BYTE_27] = 16;   /* Version 5: height */
    bytes[ZM_HEADER_DEFAULT_BACKGROUND] = 2;
    bytes[ZM_HEADER_DEFAULT_FOREGROUND] = 9;
    put_word(bytes, ZM_HEADER_TERMINATING_CHARS, 0x0070);
    put_word(bytes, ZM_HEADER_ALPHABET_TABLE, 0x0072);
    put_word(bytes, ZM_HEADER_EXTENSION_TABLE, 0x0074);

    /* Version 6-only fields, which Version 5 must not pick up. */
    put_word(bytes, ZM_HEADER_ROUTINES_OFFSET, 0x0010);
    put_word(bytes, ZM_HEADER_STREAM3_WIDTH, 0x0100);

    CHECK_EQ_INT(zm_memory_load_bytes(&mem, bytes, sizeof bytes), ZM_OK);
    zm_header_read(&mem, &h);

    CHECK_EQ_INT(h.file_length, 256);
    CHECK_EQ_INT(h.interpreter_number, 6);
    CHECK_EQ_INT(h.interpreter_version, 'A');
    CHECK_EQ_INT(h.screen_height, 24);
    CHECK_EQ_INT(h.screen_width, 80);
    CHECK_EQ_INT(h.screen_width_units, 640);
    CHECK_EQ_INT(h.screen_height_units, 400);
    CHECK_EQ_INT(h.font_width_units, 8);
    CHECK_EQ_INT(h.font_height_units, 16);
    CHECK_EQ_INT(h.default_background, 2);
    CHECK_EQ_INT(h.default_foreground, 9);
    CHECK_EQ_INT(h.terminating_chars, 0x0070);
    CHECK_EQ_INT(h.alphabet_table, 0x0072);
    CHECK_EQ_INT(h.extension_table, 0x0074);

    CHECK_EQ_INT(h.routines_offset, 0);
    CHECK_EQ_INT(h.stream3_width, 0);

    zm_memory_free(&mem);
}

static void test_version_6_swaps_font_bytes_and_reads_offsets(void)
{
    uint8_t bytes[256];
    zm_memory mem;
    zm_header h;

    memset(bytes, 0, sizeof bytes);
    bytes[ZM_HEADER_VERSION] = 6;
    put_word(bytes, ZM_HEADER_STATIC_BASE, 0x0080);
    put_word(bytes, ZM_HEADER_FILE_LENGTH, 32);   /* x8 = 256 bytes */
    bytes[ZM_HEADER_FONT_BYTE_26] = 16;   /* Version 6: height */
    bytes[ZM_HEADER_FONT_BYTE_27] = 8;    /* Version 6: width */
    put_word(bytes, ZM_HEADER_ROUTINES_OFFSET, 0x0010);
    put_word(bytes, ZM_HEADER_STRINGS_OFFSET, 0x0020);
    put_word(bytes, ZM_HEADER_STREAM3_WIDTH, 0x0100);

    CHECK_EQ_INT(zm_memory_load_bytes(&mem, bytes, sizeof bytes), ZM_OK);
    zm_header_read(&mem, &h);

    /* Spec §11.1: $26 and $27 swap between Versions 5 and 6. */
    CHECK_EQ_INT(h.file_length, 256);
    CHECK_EQ_INT(h.font_width_units, 8);
    CHECK_EQ_INT(h.font_height_units, 16);
    CHECK_EQ_INT(h.routines_offset, 0x0010);
    CHECK_EQ_INT(h.strings_offset, 0x0020);
    CHECK_EQ_INT(h.stream3_width, 0x0100);

    zm_memory_free(&mem);
}

static void test_checksum_sums_from_0x40_to_file_length(void)
{
    uint8_t bytes[256];
    zm_memory mem;

    memset(bytes, 0, sizeof bytes);
    bytes[ZM_HEADER_VERSION] = 3;
    put_word(bytes, ZM_HEADER_STATIC_BASE, 0x0040);

    /* Fill everything after the header with 1s, then mark the header
     * itself with a value that must not be counted. */
    memset(bytes + ZM_HEADER_SIZE, 1, sizeof bytes - ZM_HEADER_SIZE);
    bytes[0x30] = 0xff;

    /* With no length recorded, the sum runs to the end of the file:
     * 192 bytes of 1. */
    CHECK_EQ_INT(zm_memory_load_bytes(&mem, bytes, sizeof bytes), ZM_OK);
    CHECK_EQ_INT(zm_header_compute_checksum(&mem), 192);
    zm_memory_free(&mem);

    /* With a length of 128 bytes (stored as 64, Spec §11.1.6), only the
     * 64 bytes from $40 to $7f count; the rest is treated as padding
     * (Spec §15, verify). */
    put_word(bytes, ZM_HEADER_FILE_LENGTH, 64);
    CHECK_EQ_INT(zm_memory_load_bytes(&mem, bytes, sizeof bytes), ZM_OK);
    CHECK_EQ_INT(zm_header_compute_checksum(&mem), 64);
    zm_memory_free(&mem);

    /* The sum is modulo $10000. 192 bytes of $ff is $bf40, which does not
     * overflow, so use two passes' worth of evidence: $ff x 192 = 48960. */
    memset(bytes + ZM_HEADER_SIZE, 0xff, sizeof bytes - ZM_HEADER_SIZE);
    put_word(bytes, ZM_HEADER_FILE_LENGTH, 0);
    CHECK_EQ_INT(zm_memory_load_bytes(&mem, bytes, sizeof bytes), ZM_OK);
    CHECK_EQ_INT(zm_header_compute_checksum(&mem), 48960);
    zm_memory_free(&mem);
}

static void test_extension_table(void)
{
    uint8_t bytes[256];
    zm_memory mem;

    memset(bytes, 0, sizeof bytes);
    bytes[ZM_HEADER_VERSION] = 5;
    put_word(bytes, ZM_HEADER_STATIC_BASE, 0x0080);

    /* No table at all: every word reads as 0 (Spec §11.1.7.1). */
    CHECK_EQ_INT(zm_memory_load_bytes(&mem, bytes, sizeof bytes), ZM_OK);
    CHECK_EQ_INT(zm_header_extension_word(&mem, ZM_HEADER_EXT_COUNT), 0);
    CHECK_EQ_INT(zm_header_extension_word(&mem, ZM_HEADER_EXT_UNICODE_TABLE), 0);
    zm_memory_free(&mem);

    /* A table of 3 words at $a0: count, mouse x, mouse y, Unicode table.
     * Word 4 (Flags 3) is past the count and must read as 0 even though
     * the bytes there are non-zero. */
    put_word(bytes, ZM_HEADER_EXTENSION_TABLE, 0x00a0);
    put_word(bytes, 0xa0, 3);
    put_word(bytes, 0xa2, 10);
    put_word(bytes, 0xa4, 20);
    put_word(bytes, 0xa6, 0x00c0);
    put_word(bytes, 0xa8, 0xffff);

    CHECK_EQ_INT(zm_memory_load_bytes(&mem, bytes, sizeof bytes), ZM_OK);
    CHECK_EQ_INT(zm_header_extension_word(&mem, ZM_HEADER_EXT_COUNT), 3);
    CHECK_EQ_INT(zm_header_extension_word(&mem, ZM_HEADER_EXT_MOUSE_X), 10);
    CHECK_EQ_INT(zm_header_extension_word(&mem, ZM_HEADER_EXT_MOUSE_Y), 20);
    CHECK_EQ_INT(zm_header_extension_word(&mem, ZM_HEADER_EXT_UNICODE_TABLE), 0x00c0);
    CHECK_EQ_INT(zm_header_extension_word(&mem, ZM_HEADER_EXT_FLAGS3), 0);
    zm_memory_free(&mem);

    /* Before Version 5 there is no table, whatever $36 holds. */
    bytes[ZM_HEADER_VERSION] = 3;
    CHECK_EQ_INT(zm_memory_load_bytes(&mem, bytes, sizeof bytes), ZM_OK);
    CHECK_EQ_INT(zm_header_extension_word(&mem, ZM_HEADER_EXT_COUNT), 0);
    zm_memory_free(&mem);
}

/* The simple-test stories: one per Version, all compiled from the same
 * source on the same day. */
static void test_simple_test_stories_decode(void)
{
    static const struct {
        const char *file;
        uint8_t version;
        uint16_t release;
    } stories[] = {
        { "/simple-test-r0-s260727.z1", 1, 0 },
        { "/simple-test-r0-s260727.z2", 2, 0 },
        { "/simple-test-r1-s260727.z3", 3, 1 },
        { "/simple-test-r1-s260727.z4", 4, 1 },
        { "/simple-test-r1-s260727.z5", 5, 1 },
        { "/simple-test-r1-s260727.z6", 6, 1 },
        { "/simple-test-r1-s260727.z7", 7, 1 },
        { "/simple-test-r1-s260727.z8", 8, 1 },
    };

    for (size_t i = 0; i < sizeof stories / sizeof stories[0]; i++) {
        char path[512];
        zm_memory mem;
        zm_header h;

        strcpy(path, QUIDDITY_TEST_DATA_DIR);
        strcat(path, stories[i].file);

        printf("    %s\n", stories[i].file + 1);
        CHECK_EQ_INT(zm_memory_load_file(&mem, path), ZM_OK);
        zm_header_read(&mem, &h);

        CHECK_EQ_INT(h.version, stories[i].version);
        CHECK_EQ_INT(h.release, stories[i].release);
        CHECK_EQ_STR(h.serial, "260727");

        /* Inform records the length and checksum, so the length fits the
         * file and the checksum verifies (Spec §15, verify). */
        CHECK(h.file_length > ZM_HEADER_SIZE);
        CHECK(h.file_length <= mem.size);
        CHECK_EQ_INT(zm_header_compute_checksum(&mem), h.checksum);

        /* Every table the header points at lies inside the file. */
        CHECK(h.dictionary < mem.size);
        CHECK(h.objects < mem.size);
        CHECK(h.globals < mem.size);
        CHECK(h.abbreviations < mem.size);
        CHECK(h.high_base <= mem.size);

        /* Spec §11.1: the start address is a byte address before Version
         * 6 and a packed routine address in Version 6 (and, following
         * §1.2.4, Versions 7 and 8 behave like 5). */
        if (h.version == 6) {
            CHECK(zm_unpack_routine(&mem, h.initial_pc) < mem.size);
        } else {
            CHECK(h.initial_pc < mem.size);
            CHECK(h.initial_pc >= h.high_base);
        }

        zm_memory_free(&mem);
    }
}

int main(void)
{
    RUN_TEST(test_length_scale_follows_version);
    RUN_TEST(test_reads_version_3_fields);
    RUN_TEST(test_reads_version_5_fields);
    RUN_TEST(test_version_6_swaps_font_bytes_and_reads_offsets);
    RUN_TEST(test_checksum_sums_from_0x40_to_file_length);
    RUN_TEST(test_extension_table);
    RUN_TEST(test_simple_test_stories_decode);
    return CHECK_SUMMARY();
}
