/*
 * test_text.c: decoding Z-strings into ZSCII.
 *
 * Covers Spec §3 through zm/text.h. Most tests build a tiny story in
 * memory and write Z-characters into it by hand, so each one shows the
 * exact five-bit sequence a rule is about. The Z-character numbers below
 * come from the alphabet table in Spec §3.5.3: in A0, 'a' is 6 and 'z' is
 * 31; A1 is the same for capitals; A2 runs ' ', '^', '0' to '9', then
 * ".,!?_#'\"/\\-:()". The last test reads the simple-test story files to
 * show the decoder agrees with what Inform wrote.
 */

#include <setjmp.h>
#include <stdio.h>
#include <string.h>

#include "zm/fatal.h"
#include "zm/header.h"
#include "zm/memory.h"
#include "zm/text.h"

#include "check.h"

/* Z-characters with a fixed job, named so sequences read as the Standard
 * describes them (Spec §3.2.2, §3.2.3, §3.3, §3.4). */
enum {
    Z_SPACE   = 0,
    Z_ABBREV1 = 1,  /* Versions 2+: abbreviation bank 1; Version 1: new-line */
    Z_ABBREV2 = 2,  /* Versions 3+: abbreviation bank 2; Versions 1-2: shift up */
    Z_ABBREV3 = 3,  /* Versions 3+: abbreviation bank 3; Versions 1-2: shift down */
    Z_SHIFT_A1 = 4, /* Versions 3+: next char in A1; Versions 1-2: shift lock up */
    Z_SHIFT_A2 = 5, /* Versions 3+: next char in A2; Versions 1-2: shift lock down */
    Z_ESCAPE  = 6,  /* in A2 only */
    Z_NEWLINE = 7   /* in A2 only, Versions 2+ */
};

/* The Z-character for a lower-case letter in A0, or its capital in A1. */
#define Z(letter) ((uint8_t)(((letter) | 0x20) - 'a' + 6))

/* A sink that keeps everything it is given. The text form shows ASCII as
 * itself, new-line as '\n', and anything else as '?', so that most checks
 * can compare against a plain string; the codes are kept too for the
 * tests about non-ASCII values. */
typedef struct {
    char text[256];
    uint16_t codes[256];
    int count;
} output;

static void collect(void *context, uint16_t zscii)
{
    output *out = context;

    if (out->count < 255) {
        out->codes[out->count] = zscii;
        if (zscii >= 32 && zscii <= 126) {
            out->text[out->count] = (char)zscii;
        } else if (zscii == ZM_ZSCII_NEWLINE) {
            out->text[out->count] = '\n';
        } else {
            out->text[out->count] = '?';
        }
        out->count++;
        out->text[out->count] = '\0';
    }
}

static void output_clear(output *out)
{
    memset(out, 0, sizeof *out);
}

/* A handler that records the fault and jumps back to the test. */
static jmp_buf fault_jump;
static char fault_message[256];

static void trap_fault(const char *message)
{
    strncpy(fault_message, message, sizeof fault_message - 1);
    fault_message[sizeof fault_message - 1] = '\0';
    longjmp(fault_jump, 1);
}

/*
 * Building stories.
 *
 * Every hand-built story is 512 bytes: a header, then room for strings
 * and tables. The abbreviations table is placed at $40 and holds 96
 * entries, so it runs to $100; strings are written from $100 up.
 */

#define STORY_SIZE 512u
#define ABBREVIATIONS_TABLE 0x40u
#define STRINGS_START 0x100u

static void put_word(uint8_t *bytes, uint32_t offset, uint16_t value)
{
    bytes[offset] = (uint8_t)(value >> 8);
    bytes[offset + 1] = (uint8_t)(value & 0xff);
}

static void build_story(uint8_t *bytes, uint8_t version)
{
    memset(bytes, 0, STORY_SIZE);
    bytes[ZM_HEADER_VERSION] = version;
    put_word(bytes, ZM_HEADER_STATIC_BASE, STORY_SIZE);
    put_word(bytes, ZM_HEADER_ABBREVIATIONS, ABBREVIATIONS_TABLE);
}

/* Write Z-characters as a string at `at`, three to a word, padded with
 * 5s (Spec §3.6) and with the end bit on the last word (Spec §3.2).
 * Returns the address after the string. */
static uint32_t put_zchars(uint8_t *bytes, uint32_t at, const uint8_t *z, size_t count)
{
    size_t i = 0;

    do {
        uint16_t word = 0;

        for (int k = 0; k < 3; k++, i++) {
            uint16_t c = (uint16_t)(i < count ? z[i] : 5);
            word = (uint16_t)(word | (c << (10 - 5 * k)));
        }
        if (i >= count) {
            word = (uint16_t)(word | 0x8000u);
        }
        put_word(bytes, at, word);
        at += 2;
    } while (i < count);

    return at;
}

/* Make abbreviation `number` print the Z-characters given, storing the
 * string at `at` and its word address in the table (Spec §3.3). Returns
 * the address after the string. */
static uint32_t put_abbreviation(uint8_t *bytes, unsigned number, uint32_t at, const uint8_t *z, size_t count)
{
    put_word(bytes, ABBREVIATIONS_TABLE + 2u * number, (uint16_t)(at / 2));
    return put_zchars(bytes, at, z, count);
}

/* Decode the string at `at` into `out`, returning the address after it. */
static uint32_t decode(const zm_memory *mem, uint32_t at, output *out)
{
    output_clear(out);
    return zm_text_decode(mem, at, collect, out);
}

static void test_three_zchars_per_word_until_end_bit(void)
{
    uint8_t bytes[STORY_SIZE];
    zm_memory mem;
    output out;
    uint32_t next;

    build_story(bytes, 3);

    /* Spec §3.2, worked example: "abc" packs to $18e8, and $98e8 is the
     * same word marked as the last. Two words are laid out so the first
     * is seen to continue and the second to stop; the third would decode
     * as "ghi" if the end bit were not honoured. */
    put_word(bytes, STRINGS_START + 0, 0x18e8);
    put_word(bytes, STRINGS_START + 2, 0xa54b);  /* "def", end bit set */
    put_word(bytes, STRINGS_START + 4, 0xb1ae);  /* "ghi", end bit set */

    CHECK_EQ_INT(zm_memory_load_bytes(&mem, bytes, sizeof bytes), ZM_OK);

    next = decode(&mem, STRINGS_START, &out);
    CHECK_EQ_STR(out.text, "abcdef");
    CHECK_EQ_INT(next, STRINGS_START + 4);

    /* A single-word string. */
    next = decode(&mem, STRINGS_START + 4, &out);
    CHECK_EQ_STR(out.text, "ghi");
    CHECK_EQ_INT(next, STRINGS_START + 6);

    zm_memory_free(&mem);
}

static void test_padding_prints_nothing(void)
{
    uint8_t bytes[STORY_SIZE];
    zm_memory mem;
    output out;

    /* Spec §3.6: a string is padded to a whole word with 5s (or, as the
     * Standard notes, 4s would do as well); Spec §3.2.4 says any run of
     * shifts prints nothing. Both pads are tried, and so is a run of
     * shifts in the middle of a string, where the last shift is the one
     * that decides the alphabet of the character after it. */
    static const uint8_t with_fives[] = { Z('a'), 5, 5 };
    static const uint8_t with_fours[] = { Z('a'), 4, 4 };
    static const uint8_t shifts_inside[] = { Z('a'), 4, 5, 4, Z('b') };

    build_story(bytes, 3);
    put_zchars(bytes, STRINGS_START, with_fives, 3);
    put_zchars(bytes, STRINGS_START + 2, with_fours, 3);
    put_zchars(bytes, STRINGS_START + 4, shifts_inside, 5);
    CHECK_EQ_INT(zm_memory_load_bytes(&mem, bytes, sizeof bytes), ZM_OK);

    decode(&mem, STRINGS_START, &out);
    CHECK_EQ_STR(out.text, "a");
    decode(&mem, STRINGS_START + 2, &out);
    CHECK_EQ_STR(out.text, "a");
    decode(&mem, STRINGS_START + 4, &out);
    CHECK_EQ_STR(out.text, "aB");

    zm_memory_free(&mem);
}

static void test_version_3_alphabets_and_shifts(void)
{
    uint8_t bytes[STORY_SIZE];
    zm_memory mem;
    output out;

    /* "Hello, World!" with a new-line, as Version 3 encodes it: shifts
     * last for one character (Spec §3.2.3) and the punctuation comes
     * from A2 (Spec §3.5.3). */
    static const uint8_t text[] = {
        Z_SHIFT_A1, Z('H'), Z('e'), Z('l'), Z('l'), Z('o'),
        Z_SHIFT_A2, 19,                     /* ',' */
        Z_SPACE,
        Z_SHIFT_A1, Z('W'), Z('o'), Z('r'), Z('l'), Z('d'),
        Z_SHIFT_A2, 20,                     /* '!' */
        Z_SHIFT_A2, Z_NEWLINE
    };

    /* Every A2 character, in table order: ' ' and '^' are skipped
     * because 6 and 7 are the escape and new-line. */
    static const uint8_t a2[] = {
        Z_SHIFT_A2,  8, Z_SHIFT_A2,  9, Z_SHIFT_A2, 10, Z_SHIFT_A2, 11,
        Z_SHIFT_A2, 12, Z_SHIFT_A2, 13, Z_SHIFT_A2, 14, Z_SHIFT_A2, 15,
        Z_SHIFT_A2, 16, Z_SHIFT_A2, 17, Z_SHIFT_A2, 18, Z_SHIFT_A2, 19,
        Z_SHIFT_A2, 20, Z_SHIFT_A2, 21, Z_SHIFT_A2, 22, Z_SHIFT_A2, 23,
        Z_SHIFT_A2, 24, Z_SHIFT_A2, 25, Z_SHIFT_A2, 26, Z_SHIFT_A2, 27,
        Z_SHIFT_A2, 28, Z_SHIFT_A2, 29, Z_SHIFT_A2, 30, Z_SHIFT_A2, 31
    };

    /* A shift followed by a shift: only the last one counts, and a
     * shift at the very end prints nothing (Spec §3.2.4). */
    static const uint8_t doubled[] = { Z_SHIFT_A1, Z_SHIFT_A2, 8, Z('a'), Z_SHIFT_A1 };

    build_story(bytes, 3);
    put_zchars(bytes, STRINGS_START, text, sizeof text);
    put_zchars(bytes, STRINGS_START + 0x40, a2, sizeof a2);
    put_zchars(bytes, STRINGS_START + 0x80, doubled, sizeof doubled);
    CHECK_EQ_INT(zm_memory_load_bytes(&mem, bytes, sizeof bytes), ZM_OK);

    decode(&mem, STRINGS_START, &out);
    CHECK_EQ_STR(out.text, "Hello, World!\n");

    decode(&mem, STRINGS_START + 0x40, &out);
    CHECK_EQ_STR(out.text, "0123456789.,!?_#'\"/\\-:()");

    decode(&mem, STRINGS_START + 0x80, &out);
    CHECK_EQ_STR(out.text, "0a");

    zm_memory_free(&mem);
}

static void test_zscii_escape(void)
{
    uint8_t bytes[STORY_SIZE];
    zm_memory mem;
    output out;

    /* Spec §3.4, worked example: '@' is ZSCII 64, which no alphabet
     * holds, so it is shift to A2, escape, then 64 >> 5 and 64 & 31. */
    static const uint8_t at_sign[] = { Z_SHIFT_A2, Z_ESCAPE, 2, 0 };

    /* The escape carries ten bits, enough for an extra character (Spec
     * §3.8.5): 155 is 4 << 5 | 27. After it the alphabet is back to A0,
     * so the trailing 6 is 'a' and not another escape. */
    static const uint8_t extra[] = { Z_SHIFT_A2, Z_ESCAPE, 4, 27, Z_ESCAPE };

    /* 6 outside A2 is just 'a' (Spec §3.4 editor's note). */
    static const uint8_t not_escape[] = { Z_ESCAPE, Z_SHIFT_A1, Z_ESCAPE };

    build_story(bytes, 3);
    put_zchars(bytes, STRINGS_START, at_sign, sizeof at_sign);
    put_zchars(bytes, STRINGS_START + 0x10, extra, sizeof extra);
    put_zchars(bytes, STRINGS_START + 0x20, not_escape, sizeof not_escape);
    CHECK_EQ_INT(zm_memory_load_bytes(&mem, bytes, sizeof bytes), ZM_OK);

    decode(&mem, STRINGS_START, &out);
    CHECK_EQ_STR(out.text, "@");
    CHECK_EQ_INT(out.codes[0], 64);

    decode(&mem, STRINGS_START + 0x10, &out);
    CHECK_EQ_INT(out.count, 2);
    CHECK_EQ_INT(out.codes[0], 155);
    CHECK_EQ_INT(out.codes[1], 'a');

    decode(&mem, STRINGS_START + 0x20, &out);
    CHECK_EQ_STR(out.text, "aA");

    zm_memory_free(&mem);
}

static void test_incomplete_construction_is_ignored(void)
{
    uint8_t bytes[STORY_SIZE];
    zm_memory mem;
    output out;
    uint32_t next;

    /* Spec §3.6.1: a string may end part way through an escape or an
     * abbreviation, as happens to dictionary words cut to length. The
     * partial construction prints nothing, and the decoder still stops
     * at the right place.
     *
     * Each sequence fills its words exactly. Padding would not do: the
     * pad 5s are Z-characters like any other, so an escape that is one
     * Z-character short of a full word is completed by a 5, not left
     * incomplete. The first sequence is six Z-characters, the width of
     * a Version 3 dictionary word. */
    static const uint8_t half_escape[] = { Z('a'), 5, 5, Z_SHIFT_A2, Z_ESCAPE, 2 };  /* no low bits */
    static const uint8_t bare_escape[] = { Z('a'), Z_SHIFT_A2, Z_ESCAPE };           /* nothing at all */
    static const uint8_t half_abbrev[] = { Z('a'), Z('b'), Z_ABBREV1 };              /* no entry number */

    build_story(bytes, 3);
    put_zchars(bytes, STRINGS_START, half_escape, 6);        /* two words */
    put_zchars(bytes, STRINGS_START + 0x10, bare_escape, 3); /* one word */
    put_zchars(bytes, STRINGS_START + 0x20, half_abbrev, 3); /* one word */
    CHECK_EQ_INT(zm_memory_load_bytes(&mem, bytes, sizeof bytes), ZM_OK);

    next = decode(&mem, STRINGS_START, &out);
    CHECK_EQ_STR(out.text, "a");
    CHECK_EQ_INT(next, STRINGS_START + 4);

    next = decode(&mem, STRINGS_START + 0x10, &out);
    CHECK_EQ_STR(out.text, "a");
    CHECK_EQ_INT(next, STRINGS_START + 0x12);

    next = decode(&mem, STRINGS_START + 0x20, &out);
    CHECK_EQ_STR(out.text, "ab");
    CHECK_EQ_INT(next, STRINGS_START + 0x22);

    zm_memory_free(&mem);
}

static void test_version_1_alphabet_and_shift_locks(void)
{
    uint8_t bytes[STORY_SIZE];
    zm_memory mem;
    output out;

    /* Spec §3.2.2: in Versions 1 and 2, 4 and 5 are shift locks and 2 and
     * 3 are single shifts, all relative to the current alphabet. From A0,
     * 4 locks into A1; from A1, 2 shifts to A2 for one character; 5 from
     * A1 locks back to A0. Spec §3.5.4: Version 1's A2 has '0' at 7, so
     * the character after the shift is '0'. */
    static const uint8_t text[] = {
        4, Z('A'), Z('B'),       /* lock A1: "AB" */
        2, 7,                    /* shift A1 -> A2 for one: "0" */
        Z('C'),                  /* back in A1: "C" */
        5, Z('a'),               /* lock A1 -> A0: "a" */
        3, Z('D'),               /* shift A0 -> A2 for one: A2's 9 is '2' */
        Z('b')
    };

    /* Spec §3.5.2: Z-character 1 is new-line in Version 1, and there is
     * no new-line in A2 (its 7 is '0'). Spec §3.5.4: '<' is at 27. */
    static const uint8_t v1_only[] = { Z('a'), 1, Z('b'), 3, 27, 3, 7 };

    build_story(bytes, 1);
    put_zchars(bytes, STRINGS_START, text, sizeof text);
    put_zchars(bytes, STRINGS_START + 0x20, v1_only, sizeof v1_only);
    CHECK_EQ_INT(zm_memory_load_bytes(&mem, bytes, sizeof bytes), ZM_OK);

    decode(&mem, STRINGS_START, &out);
    CHECK_EQ_STR(out.text, "AB0Ca2b");

    decode(&mem, STRINGS_START + 0x20, &out);
    CHECK_EQ_STR(out.text, "a\nb<0");

    zm_memory_free(&mem);
}

static void test_version_2_has_one_abbreviation_bank(void)
{
    uint8_t bytes[STORY_SIZE];
    zm_memory mem;
    output out;

    static const uint8_t zork[] = { Z('z'), Z('o'), Z('r'), Z('k') };

    /* Spec §3.3: in Version 2, Z-character 1 introduces one of 32
     * abbreviations, while 2 and 3 are still the shifts of Spec §3.2.2.
     * Spec §3.5.3: Version 2 already has new-line at A2's 7. */
    static const uint8_t text[] = {
        Z_ABBREV1, 0,            /* abbreviation 0: "zork" */
        Z_SPACE,
        4, Z('I'),               /* lock A1 */
        3, 7,                    /* shift A1 -> A0 for one: 'b' */
        2, 7,                    /* shift A1 -> A2 for one: new-line */
        Z('C')
    };

    build_story(bytes, 2);
    put_abbreviation(bytes, 0, STRINGS_START, zork, sizeof zork);
    put_zchars(bytes, STRINGS_START + 0x20, text, sizeof text);
    CHECK_EQ_INT(zm_memory_load_bytes(&mem, bytes, sizeof bytes), ZM_OK);

    decode(&mem, STRINGS_START + 0x20, &out);
    CHECK_EQ_STR(out.text, "zork Ib\nC");

    zm_memory_free(&mem);
}

static void test_version_3_abbreviations(void)
{
    uint8_t bytes[STORY_SIZE];
    zm_memory mem;
    output out;
    uint32_t at = STRINGS_START;

    static const uint8_t the[]   = { Z_SHIFT_A1, Z('T'), Z('h'), Z('e'), Z_SPACE };
    static const uint8_t white[] = { Z('w'), Z('h'), Z('i'), Z('t'), Z('e'), Z_SPACE };
    static const uint8_t house[] = { Z('h'), Z('o'), Z('u'), Z('s'), Z('e') };

    /* Spec §3.3: entry 32(z-1)+x, so 1/0 is entry 0, 2/5 is entry 37,
     * and 3/31 is entry 95, the last. The abbreviation strings are
     * decoded by the same rules as any other, including a shift inside
     * one (Spec §3.3.1), and the outer string carries on afterwards. */
    static const uint8_t text[] = {
        Z_ABBREV1, 0, Z_ABBREV2, 5, Z_ABBREV3, 31,
        Z_SHIFT_A2, 18           /* '.' */
    };

    build_story(bytes, 3);
    at = put_abbreviation(bytes, 0,  at, the,   sizeof the);
    at = put_abbreviation(bytes, 37, at, white, sizeof white);
    at = put_abbreviation(bytes, 95, at, house, sizeof house);
    put_zchars(bytes, at, text, sizeof text);
    CHECK_EQ_INT(zm_memory_load_bytes(&mem, bytes, sizeof bytes), ZM_OK);

    decode(&mem, at, &out);
    CHECK_EQ_STR(out.text, "The white house.");

    /* The same entries, fetched by number. */
    output_clear(&out);
    zm_text_decode_abbreviation(&mem, 37, collect, &out);
    CHECK_EQ_STR(out.text, "white ");

    output_clear(&out);
    zm_text_decode_abbreviation(&mem, 95, collect, &out);
    CHECK_EQ_STR(out.text, "house");

    zm_memory_free(&mem);
}

static void test_abbreviation_inside_abbreviation_faults(void)
{
    uint8_t bytes[STORY_SIZE];
    zm_memory mem;
    output out;

    static const uint8_t inner[] = { Z('x') };
    static const uint8_t outer[] = { Z('a'), Z_ABBREV1, 0, Z('b') };   /* refers to entry 0 */
    static const uint8_t text[]  = { Z_ABBREV1, 1 };                    /* refers to entry 1 */

    build_story(bytes, 3);
    put_abbreviation(bytes, 0, STRINGS_START, inner, sizeof inner);
    put_abbreviation(bytes, 1, STRINGS_START + 0x10, outer, sizeof outer);
    put_zchars(bytes, STRINGS_START + 0x20, text, sizeof text);
    CHECK_EQ_INT(zm_memory_load_bytes(&mem, bytes, sizeof bytes), ZM_OK);

    /* Spec §3.3.1: an abbreviation must not use abbreviations. The
     * decoder gets as far as the 'a' before it finds out. */
    zm_set_fatal_handler(trap_fault);
    if (setjmp(fault_jump) == 0) {
        decode(&mem, STRINGS_START + 0x20, &out);
        CHECK(0 && "abbreviation inside abbreviation was not reported");
    } else {
        CHECK(strstr(fault_message, "abbreviation") != NULL);
        CHECK_EQ_STR(out.text, "a");
    }

    /* And there are only 96 of them. */
    if (setjmp(fault_jump) == 0) {
        zm_text_decode_abbreviation(&mem, 96, collect, &out);
        CHECK(0 && "abbreviation 96 was accepted");
    } else {
        CHECK(strstr(fault_message, "96") != NULL);
    }
    zm_set_fatal_handler(NULL);

    zm_memory_free(&mem);
}

static void test_version_5_custom_alphabet_table(void)
{
    uint8_t bytes[STORY_SIZE];
    zm_memory mem;
    output out;
    const uint32_t table = 0x1a0;

    /* Spec §3.5.5.1: three blocks of 26 ZSCII values for Z-characters 6
     * to 31. This table swaps the cases of A0 and A1 and fills A2 with
     * '#', so anything that still comes out as the default alphabet was
     * not read from the table. */
    static const uint8_t text[] = {
        Z('a'),                  /* table A0: 'A' */
        Z_SHIFT_A1, Z('b'),      /* table A1: 'b' */
        Z_SHIFT_A2, 8,           /* table A2: '#' */
        Z_SHIFT_A2, Z_NEWLINE,   /* still new-line, Spec §3.5.5.1 */
        Z_SHIFT_A2, Z_ESCAPE, 2, 0,  /* still the escape: '@' */
        Z_SHIFT_A2, 31           /* table A2: '#' */
    };

    build_story(bytes, 5);
    memcpy(bytes + table,      "ABCDEFGHIJKLMNOPQRSTUVWXYZ", 26);
    memcpy(bytes + table + 26, "abcdefghijklmnopqrstuvwxyz", 26);
    memset(bytes + table + 52, '#', 26);
    put_word(bytes, ZM_HEADER_ALPHABET_TABLE, (uint16_t)table);
    put_zchars(bytes, STRINGS_START, text, sizeof text);
    CHECK_EQ_INT(zm_memory_load_bytes(&mem, bytes, sizeof bytes), ZM_OK);

    decode(&mem, STRINGS_START, &out);
    CHECK_EQ_STR(out.text, "Ab#\n@#");

    /* Spec §3.5.5: a zero word means the default table. */
    put_word(bytes, ZM_HEADER_ALPHABET_TABLE, 0);
    zm_memory_free(&mem);
    CHECK_EQ_INT(zm_memory_load_bytes(&mem, bytes, sizeof bytes), ZM_OK);
    decode(&mem, STRINGS_START, &out);
    CHECK_EQ_STR(out.text, "aB0\n@)");
    zm_memory_free(&mem);

    /* And before Version 5 the word at $34 is not an alphabet table at
     * all, whatever it holds. */
    bytes[ZM_HEADER_VERSION] = 4;
    put_word(bytes, ZM_HEADER_ALPHABET_TABLE, (uint16_t)table);
    CHECK_EQ_INT(zm_memory_load_bytes(&mem, bytes, sizeof bytes), ZM_OK);
    decode(&mem, STRINGS_START, &out);
    CHECK_EQ_STR(out.text, "aB0\n@)");
    zm_memory_free(&mem);
}

static void test_zscii_to_unicode_default_table(void)
{
    uint8_t bytes[STORY_SIZE];
    zm_memory mem;

    build_story(bytes, 3);
    CHECK_EQ_INT(zm_memory_load_bytes(&mem, bytes, sizeof bytes), ZM_OK);

    /* Spec §3.8.3: ASCII is itself. */
    CHECK_EQ_INT(zm_zscii_to_unicode(&mem, ' '), ' ');
    CHECK_EQ_INT(zm_zscii_to_unicode(&mem, 'A'), 'A');
    CHECK_EQ_INT(zm_zscii_to_unicode(&mem, '~'), '~');

    /* Spec §3.8.5.3, Table 1: the first, a middle one, and the last of
     * the default extra characters. */
    CHECK_EQ_INT(zm_zscii_to_unicode(&mem, 155), 0x00e4);  /* a-diaeresis */
    CHECK_EQ_INT(zm_zscii_to_unicode(&mem, 161), 0x00df);  /* sz-ligature */
    CHECK_EQ_INT(zm_zscii_to_unicode(&mem, 180), 0x00dd);  /* Y-acute */
    CHECK_EQ_INT(zm_zscii_to_unicode(&mem, 220), 0x0153);  /* oe-ligature */
    CHECK_EQ_INT(zm_zscii_to_unicode(&mem, 223), 0x00bf);  /* inverted ? */

    /* Beyond the default table, and everything that is not printable:
     * control codes (Spec §3.8.2), the undefined 127 and 128 (§3.8.3.1),
     * input-only codes (§3.8.4, §3.8.6), 255 (§3.8.7), and the unused
     * range above (§3.8.1). */
    CHECK_EQ_INT(zm_zscii_to_unicode(&mem, 224), 0);
    CHECK_EQ_INT(zm_zscii_to_unicode(&mem, 251), 0);
    CHECK_EQ_INT(zm_zscii_to_unicode(&mem, 0), 0);
    CHECK_EQ_INT(zm_zscii_to_unicode(&mem, ZM_ZSCII_NEWLINE), 0);
    CHECK_EQ_INT(zm_zscii_to_unicode(&mem, 127), 0);
    CHECK_EQ_INT(zm_zscii_to_unicode(&mem, 128), 0);
    CHECK_EQ_INT(zm_zscii_to_unicode(&mem, 133), 0);
    CHECK_EQ_INT(zm_zscii_to_unicode(&mem, 254), 0);
    CHECK_EQ_INT(zm_zscii_to_unicode(&mem, 255), 0);
    CHECK_EQ_INT(zm_zscii_to_unicode(&mem, 1023), 0);

    zm_memory_free(&mem);
}

static void test_zscii_to_unicode_story_table(void)
{
    uint8_t bytes[STORY_SIZE];
    zm_memory mem;
    const uint32_t extension = 0x100;
    const uint32_t unicode_table = 0x120;

    /* Spec §3.8.5.2: Version 5 and later may put a translation table's
     * address in word 3 of the header extension. The table is a count
     * byte and then one word per character from 155 up (§3.8.5.2.1). */
    build_story(bytes, 5);
    put_word(bytes, ZM_HEADER_EXTENSION_TABLE, (uint16_t)extension);
    put_word(bytes, extension + 0, 3);                        /* three words follow */
    put_word(bytes, extension + 2 * ZM_HEADER_EXT_UNICODE_TABLE, (uint16_t)unicode_table);
    bytes[unicode_table] = 2;
    put_word(bytes, unicode_table + 1, 0x0416);               /* Cyrillic Zhe */
    put_word(bytes, unicode_table + 3, 0x263a);               /* smiling face */
    CHECK_EQ_INT(zm_memory_load_bytes(&mem, bytes, sizeof bytes), ZM_OK);

    CHECK_EQ_INT(zm_zscii_to_unicode(&mem, 155), 0x0416);
    CHECK_EQ_INT(zm_zscii_to_unicode(&mem, 156), 0x263a);
    CHECK_EQ_INT(zm_zscii_to_unicode(&mem, 157), 0);          /* past the count, §3.8.5.2.2 */
    CHECK_EQ_INT(zm_zscii_to_unicode(&mem, 'A'), 'A');       /* ASCII is unaffected */
    zm_memory_free(&mem);

    /* A table with no entries leaves the whole block undefined. */
    bytes[unicode_table] = 0;
    CHECK_EQ_INT(zm_memory_load_bytes(&mem, bytes, sizeof bytes), ZM_OK);
    CHECK_EQ_INT(zm_zscii_to_unicode(&mem, 155), 0);
    zm_memory_free(&mem);

    /* Spec §3.8.5.2: before Version 5 the default table is always used,
     * so the same bytes in a Version 4 story mean nothing. */
    bytes[ZM_HEADER_VERSION] = 4;
    bytes[unicode_table] = 2;
    CHECK_EQ_INT(zm_memory_load_bytes(&mem, bytes, sizeof bytes), ZM_OK);
    CHECK_EQ_INT(zm_zscii_to_unicode(&mem, 155), 0x00e4);
    zm_memory_free(&mem);
}

static void test_simple_test_files_say_hello(void)
{
    static const char *const files[] = {
        "/simple-test-r0-s260727.z1",
        "/simple-test-r0-s260727.z2",
        "/simple-test-r1-s260727.z3",
        "/simple-test-r1-s260727.z4",
        "/simple-test-r1-s260727.z5",
        "/simple-test-r1-s260727.z6",
        "/simple-test-r1-s260727.z7",
        "/simple-test-r1-s260727.z8",
    };

    for (size_t i = 0; i < sizeof files / sizeof files[0]; i++) {
        char path[512];
        zm_memory mem;
        zm_header h;
        uint32_t pc, main_routine, string;
        output out;

        strcpy(path, QUIDDITY_TEST_DATA_DIR);
        strcat(path, files[i]);

        printf("    %s\n", files[i] + 1);
        CHECK_EQ_INT(zm_memory_load_file(&mem, path), ZM_OK);
        zm_header_read(&mem, &h);

        /* Inform compiles the same start-up for every Version: a call to
         * Main, which is a routine with no local variables whose first
         * instruction prints the string by its packed address. Walking
         * that code here involves a few facts about instruction encoding
         * (Spec §4, which is the next layer up), so it is spelled out:
         *
         *   $e0 $3f  hh ll   call with one large-constant operand (Spec
         *                    §4.3.3, §4.4.3): the packed address of Main
         *   ...
         *   $00              Main: zero locals (Spec §5.2)
         *   $8d  hh ll       print_paddr with a large constant (Spec
         *                    §4.3.1): the packed address of the string
         *
         * Before Version 6 the header gives the first instruction's
         * address directly; in Version 6 it gives a routine to call, and
         * the call to Main is its first instruction (Spec §5.5). */
        if (h.version == 6) {
            pc = zm_unpack_routine(&mem, h.initial_pc) + 1;
        } else {
            pc = h.initial_pc;
        }
        CHECK_EQ_INT(zm_read_byte(&mem, pc), 0xe0);
        CHECK_EQ_INT(zm_read_byte(&mem, pc + 1), 0x3f);
        main_routine = zm_unpack_routine(&mem, zm_read_word(&mem, pc + 2));

        CHECK_EQ_INT(zm_read_byte(&mem, main_routine), 0);
        CHECK_EQ_INT(zm_read_byte(&mem, main_routine + 1), 0x8d);
        string = zm_unpack_string(&mem, zm_read_word(&mem, main_routine + 2));

        decode(&mem, string, &out);
        CHECK_EQ_STR(out.text, "hello from all z machine versions");

        zm_memory_free(&mem);
    }
}

int main(void)
{
    RUN_TEST(test_three_zchars_per_word_until_end_bit);
    RUN_TEST(test_padding_prints_nothing);
    RUN_TEST(test_version_3_alphabets_and_shifts);
    RUN_TEST(test_zscii_escape);
    RUN_TEST(test_incomplete_construction_is_ignored);
    RUN_TEST(test_version_1_alphabet_and_shift_locks);
    RUN_TEST(test_version_2_has_one_abbreviation_bank);
    RUN_TEST(test_version_3_abbreviations);
    RUN_TEST(test_abbreviation_inside_abbreviation_faults);
    RUN_TEST(test_version_5_custom_alphabet_table);
    RUN_TEST(test_zscii_to_unicode_default_table);
    RUN_TEST(test_zscii_to_unicode_story_table);
    RUN_TEST(test_simple_test_files_say_hello);
    return CHECK_SUMMARY();
}
