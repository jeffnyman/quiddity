/*
 * test_dictionary.c: encoding words and looking them up.
 *
 * Covers the dictionary encoding of Spec §3.7 through zm/text.h and the
 * dictionary and lexer of Spec §13 through zm/dictionary.h. The
 * dictionaries are built by hand with their words already encoded, so
 * that a lookup test does not depend on the encoder it is meant to be
 * checking; the Z-character arithmetic for each word is in a comment.
 */

#include <setjmp.h>
#include <stdio.h>
#include <string.h>

#include "zm/dictionary.h"
#include "zm/fatal.h"
#include "zm/header.h"
#include "zm/memory.h"
#include "zm/text.h"

#include "check.h"

#define STORY_SIZE 1024u
#define TEXT 0x100u
#define PARSE 0x140u
#define DICT 0x200u
#define STATIC_BASE 0x400u

typedef struct {
    uint8_t bytes[STORY_SIZE];
    uint8_t version;
} story;

static void put_word(uint8_t *bytes, uint32_t offset, uint16_t value)
{
    bytes[offset] = (uint8_t)(value >> 8);
    bytes[offset + 1] = (uint8_t)(value & 0xff);
}

static void story_init(story *s, uint8_t version)
{
    memset(s, 0, sizeof *s);
    s->version = version;
    s->bytes[ZM_HEADER_VERSION] = version;
    put_word(s->bytes, ZM_HEADER_HIGH_BASE, STATIC_BASE);
    put_word(s->bytes, ZM_HEADER_STATIC_BASE, STATIC_BASE);
    put_word(s->bytes, ZM_HEADER_DICTIONARY, DICT);
}

static void load(const story *s, zm_memory *mem)
{
    CHECK_EQ_INT(zm_memory_load_bytes(mem, s->bytes, STORY_SIZE), ZM_OK);
}

/* The dictionary used throughout: separators '.' ',' '"' (Spec §13.2),
 * three data bytes per entry as Inform does, and the words "go", "look",
 * and "open" in order (Spec §13.5).
 *
 * Z-characters (Spec §3.5.3): g 12, o 20, l 17, k 16, p 21, e 10, n 19,
 * pad 5. Three to a word, top bit on the last (Spec §3.2):
 *
 *   go    12 20  5 | 5 5 5            $3285 $94a5
 *   look  17 20 20 | 16 5 5           $4694 $c0a5
 *   open  20 21 10 | 19 5 5           $52aa $cca5
 *
 * and in Version 4 and later, nine Z-characters (Spec §13.4):
 *
 *   go    12 20  5 | 5 5 5 | 5 5 5    $3285 $14a5 $94a5
 *   look  17 20 20 | 16 5 5 | 5 5 5   $4694 $40a5 $94a5
 *   open  20 21 10 | 19 5 5 | 5 5 5   $52aa $4ca5 $94a5
 */
static const uint8_t words_v3[3][4] = {
    { 0x32, 0x85, 0x94, 0xa5 },
    { 0x46, 0x94, 0xc0, 0xa5 },
    { 0x52, 0xaa, 0xcc, 0xa5 },
};
static const uint8_t words_v5[3][6] = {
    { 0x32, 0x85, 0x14, 0xa5, 0x94, 0xa5 },
    { 0x46, 0x94, 0x40, 0xa5, 0x94, 0xa5 },
    { 0x52, 0xaa, 0x4c, 0xa5, 0x94, 0xa5 },
};

/* Build the dictionary at DICT. A negative count writes the entries in
 * reverse order and declares them unsorted (Spec §15 tokenise). */
static void build_dictionary(story *s, int unsorted)
{
    unsigned encoded = s->version <= 3 ? 4u : 6u;
    unsigned entry_length = encoded + 3u;
    uint32_t at = DICT;

    s->bytes[at++] = 3;
    s->bytes[at++] = '.';
    s->bytes[at++] = ',';
    s->bytes[at++] = '"';
    s->bytes[at++] = (uint8_t)entry_length;
    put_word(s->bytes, at, unsorted ? (uint16_t)-3 : 3);
    at += 2;

    for (unsigned i = 0; i < 3; i++) {
        unsigned w = unsorted ? 2 - i : i;

        memcpy(s->bytes + at, s->version <= 3 ? words_v3[w] : words_v5[w], encoded);
        at += entry_length;
    }
}

/* Entry addresses: header of 1 + 3 + 1 + 2 = 7 bytes, then entries. */
static uint32_t entry_address(uint8_t version, unsigned index)
{
    return DICT + 7u + index * (version <= 3 ? 7u : 9u);
}

/* Put text into the text buffer the way read leaves it (Spec §15 read):
 * after a zero terminator before Version 5, after a length byte from
 * Version 5 on. */
static void set_text(story *s, const char *text)
{
    size_t length = strlen(text);

    s->bytes[TEXT] = 100;
    if (s->version <= 4) {
        memcpy(s->bytes + TEXT + 1, text, length);
        s->bytes[TEXT + 1 + length] = 0;
    } else {
        s->bytes[TEXT + 1] = (uint8_t)length;
        memcpy(s->bytes + TEXT + 2, text, length);
    }
}

/* Encode an ASCII string. */
static unsigned encode(const zm_memory *mem, const char *text, uint8_t *out)
{
    uint16_t zscii[64];
    unsigned length = (unsigned)strlen(text);

    for (unsigned i = 0; i < length; i++) {
        zscii[i] = (uint8_t)text[i];
    }
    return zm_text_encode(mem, zscii, length, out);
}

/* Check an encoding against expected words, printing both on failure. */
static void check_encoding(const zm_memory *mem, const char *text, const uint16_t *expected, unsigned words)
{
    uint8_t out[ZM_TEXT_ENCODED_MAX];
    unsigned length = encode(mem, text, out);
    int same = length == 2 * words;

    for (unsigned i = 0; same && i < words; i++) {
        same = out[2 * i] == (expected[i] >> 8) && out[2 * i + 1] == (expected[i] & 0xff);
    }
    if (!same) {
        fprintf(stderr, "    \"%s\" encoded as", text);
        for (unsigned i = 0; i < length / 2; i++) {
            fprintf(stderr, " $%02x%02x", out[2 * i], out[2 * i + 1]);
        }
        fprintf(stderr, ", expected");
        for (unsigned i = 0; i < words; i++) {
            fprintf(stderr, " $%04x", expected[i]);
        }
        fprintf(stderr, "\n");
    }
    CHECK(same);
}

#define ENCODES(mem, text, ...) \
    check_encoding((mem), (text), (const uint16_t[]){ __VA_ARGS__ }, \
                   sizeof((const uint16_t[]){ __VA_ARGS__ }) / sizeof(uint16_t))

/* Fault trapping, as in test_object.c. */
static jmp_buf fault_jump;
static char fault_message[256];

static void trap_fault(const char *message)
{
    strncpy(fault_message, message, sizeof fault_message - 1);
    fault_message[sizeof fault_message - 1] = '\0';
    longjmp(fault_jump, 1);
}

#define EXPECT_FAULT(needle, statement)                                       \
    do {                                                                      \
        zm_set_fatal_handler(trap_fault);                                     \
        if (setjmp(fault_jump) == 0) {                                        \
            statement;                                                        \
            CHECK(0 && "expected a fault: " needle);                          \
        } else {                                                              \
            if (strstr(fault_message, needle) == NULL) {                      \
                fprintf(stderr, "    fault was: %s\n", fault_message);        \
            }                                                                 \
            CHECK(strstr(fault_message, needle) != NULL);                     \
        }                                                                     \
        zm_set_fatal_handler(NULL);                                           \
    } while (0)

/*
 * Encoding (Spec §3.7).
 */

static void test_encoding_pads_to_six_or_nine_zchars(void)
{
    story s;
    zm_memory mem;

    /* The example in Spec §3.7, as its editor's note corrects it: "i" is
     * Z-character 14 followed by pads. */
    story_init(&s, 3);
    load(&s, &mem);
    CHECK_EQ_INT(zm_text_encoded_length(&mem), 4);
    ENCODES(&mem, "i", 0x38a5, 0x94a5);
    ENCODES(&mem, "go", 0x3285, 0x94a5);
    ENCODES(&mem, "look", 0x4694, 0xc0a5);
    ENCODES(&mem, "", 0x14a5, 0x94a5);
    zm_memory_free(&mem);

    story_init(&s, 5);
    load(&s, &mem);
    CHECK_EQ_INT(zm_text_encoded_length(&mem), 6);
    ENCODES(&mem, "i", 0x38a5, 0x14a5, 0x94a5);
    ENCODES(&mem, "open", 0x52aa, 0x4ca5, 0x94a5);
    zm_memory_free(&mem);
}

static void test_encoding_uses_single_shifts_from_version_3(void)
{
    story s;
    zm_memory mem;

    story_init(&s, 3);
    load(&s, &mem);

    /* Spec §3.2.3: 4 shifts to A1 and 5 to A2 for one character. 'A' is
     * A1's 6; '.' is A2's 18 (position 12 of " ^0123456789.,!?_#'\"/\\-:()"
     * plus 6); '1' is A2's 9. Spaces are Z-character 0 (Spec §3.5.1). */
    ENCODES(&mem, "Ab", (4 << 10) | (6 << 5) | 7, 0x94a5);
    ENCODES(&mem, "a.b", (6 << 10) | (5 << 5) | 18, 0x8000 | (7 << 10) | (5 << 5) | 5);
    ENCODES(&mem, "a 1", (6 << 10) | (0 << 5) | 5, 0x8000 | (9 << 10) | (5 << 5) | 5);
    ENCODES(&mem, "AB", (4 << 10) | (6 << 5) | 4, 0x8000 | (7 << 10) | (5 << 5) | 5);

    /* Spec §3.4: a character in no alphabet is the escape 6 in A2 then
     * its ZSCII code in two five-bit halves. ZSCII 200 is 6, 8. */
    {
        uint16_t zscii[1] = { 200 };
        uint8_t out[ZM_TEXT_ENCODED_MAX];

        zm_text_encode(&mem, zscii, 1, out);
        CHECK_EQ_INT(out[0], (5 << 2) | (6 >> 3));
        CHECK_EQ_INT(out[1], ((6 & 7) << 5) | 6);
        CHECK_EQ_INT(out[2], 0x80 | (8 << 2) | (5 >> 3));
        CHECK_EQ_INT(out[3], ((5 & 7) << 5) | 5);
    }

    /* Spec §3.7: cut off at six Z-characters, leaving a construction
     * incomplete rather than dropping it: the shift before '!' survives,
     * the '!' does not. */
    ENCODES(&mem, "abcdefgh", (6 << 10) | (7 << 5) | 8, 0x8000 | (9 << 10) | (10 << 5) | 11);
    ENCODES(&mem, "abcde!", (6 << 10) | (7 << 5) | 8, 0x8000 | (9 << 10) | (10 << 5) | 5);

    zm_memory_free(&mem);
}

static void test_encoding_uses_shift_locks_in_versions_1_and_2(void)
{
    story s;
    zm_memory mem;

    /* Spec §3.2.2 and §3.7.1: in Versions 1 and 2, 2 and 3 shift up and
     * down for one character, 4 and 5 lock; a dictionary word locks
     * when the next character comes from the same alphabet. From A0,
     * A1 is up and A2 is down. */
    story_init(&s, 2);
    load(&s, &mem);
    ENCODES(&mem, "Ab", (2 << 10) | (6 << 5) | 7, 0x94a5);
    ENCODES(&mem, "AB", (4 << 10) | (6 << 5) | 7, 0x94a5);
    /* After the lock on A1, A0 is down: 'c' alone takes 3. */
    ENCODES(&mem, "ABc", (4 << 10) | (6 << 5) | 7, 0x8000 | (3 << 10) | (8 << 5) | 5);
    /* Digits are in A2, down from A0; '1' is A2's 9 in Version 2. */
    ENCODES(&mem, "a1", (6 << 10) | (3 << 5) | 9, 0x94a5);
    ENCODES(&mem, "12", (5 << 10) | (9 << 5) | 10, 0x94a5);
    /* A space between does not count as the same alphabet. */
    ENCODES(&mem, "A B", (2 << 10) | (6 << 5) | 0, 0x8000 | (2 << 10) | (7 << 5) | 5);
    zm_memory_free(&mem);

    /* Spec §3.5.4: Version 1's A2 has no new-line, so '1' is 8 there. */
    story_init(&s, 1);
    load(&s, &mem);
    ENCODES(&mem, "a1", (6 << 10) | (3 << 5) | 8, 0x94a5);
    zm_memory_free(&mem);
}

static void test_encoding_follows_a_custom_alphabet(void)
{
    story s;
    zm_memory mem;

    /* Spec §3.5.5: a Version 5 story may supply its own 78-byte table.
     * This one swaps 'a' and 'z' in A0, so "a" encodes as 31. */
    story_init(&s, 5);
    put_word(s.bytes, ZM_HEADER_ALPHABET_TABLE, 0x300);
    memcpy(s.bytes + 0x300, "zbcdefghijklmnopqrstuvwxya", 26);
    memcpy(s.bytes + 0x300 + 26, "ABCDEFGHIJKLMNOPQRSTUVWXYZ", 26);
    memcpy(s.bytes + 0x300 + 52, " ^0123456789.,!?_#'\"/\\-:()", 26);
    load(&s, &mem);
    ENCODES(&mem, "a", (31 << 10) | (5 << 5) | 5, 0x14a5, 0x94a5);
    ENCODES(&mem, "z", (6 << 10) | (5 << 5) | 5, 0x14a5, 0x94a5);
    zm_memory_free(&mem);
}

/*
 * The dictionary (Spec §13.1 to §13.5).
 */

static void test_dictionary_header_and_lookup(void)
{
    story s;
    zm_memory mem;
    zm_dictionary dict;

    story_init(&s, 3);
    build_dictionary(&s, 0);
    load(&s, &mem);

    zm_dictionary_read_main(&mem, &dict);
    CHECK_EQ_INT(dict.address, DICT);
    CHECK_EQ_INT(dict.separator_count, 3);
    CHECK_EQ_INT(dict.entry_length, 7);
    CHECK_EQ_INT(dict.entry_count, 3);
    CHECK_EQ_INT(dict.sorted, 1);
    CHECK_EQ_INT(dict.entries, DICT + 7);
    CHECK_EQ_INT(zm_dictionary_is_separator(&mem, &dict, ','), 1);
    CHECK_EQ_INT(zm_dictionary_is_separator(&mem, &dict, '"'), 1);
    CHECK_EQ_INT(zm_dictionary_is_separator(&mem, &dict, ' '), 0);
    CHECK_EQ_INT(zm_dictionary_is_separator(&mem, &dict, 'a'), 0);

    CHECK_EQ_INT(zm_dictionary_lookup(&mem, &dict, words_v3[0]), entry_address(3, 0));
    CHECK_EQ_INT(zm_dictionary_lookup(&mem, &dict, words_v3[1]), entry_address(3, 1));
    CHECK_EQ_INT(zm_dictionary_lookup(&mem, &dict, words_v3[2]), entry_address(3, 2));
    {
        /* "gp": one more than "go", between two entries. */
        static const uint8_t gp[4] = { 0x32, 0xa5, 0x94, 0xa5 };
        /* "aa": before every entry. */
        static const uint8_t aa[4] = { 0x18, 0xc5, 0x94, 0xa5 };
        /* "zz": after every entry. */
        static const uint8_t zz[4] = { 0x7f, 0xe5, 0x94, 0xa5 };

        CHECK_EQ_INT(zm_dictionary_lookup(&mem, &dict, gp), 0);
        CHECK_EQ_INT(zm_dictionary_lookup(&mem, &dict, aa), 0);
        CHECK_EQ_INT(zm_dictionary_lookup(&mem, &dict, zz), 0);
    }
    zm_memory_free(&mem);

    /* Spec §15 tokenise: a negative count means unsorted, and a linear
     * search finds the words wherever they are. */
    story_init(&s, 5);
    build_dictionary(&s, 1);
    load(&s, &mem);
    zm_dictionary_read_main(&mem, &dict);
    CHECK_EQ_INT(dict.entry_count, 3);
    CHECK_EQ_INT(dict.sorted, 0);
    CHECK_EQ_INT(dict.entry_length, 9);
    CHECK_EQ_INT(zm_dictionary_lookup(&mem, &dict, words_v5[0]), entry_address(5, 2));
    CHECK_EQ_INT(zm_dictionary_lookup(&mem, &dict, words_v5[2]), entry_address(5, 0));
    zm_memory_free(&mem);

    /* Spec §13.2: entries must hold a word. */
    story_init(&s, 5);
    build_dictionary(&s, 0);
    s.bytes[DICT + 4] = 5;
    load(&s, &mem);
    EXPECT_FAULT("too short", zm_dictionary_read_main(&mem, &dict));
    zm_memory_free(&mem);
}

/*
 * Lexing (Spec §13.6).
 */

static void check_block(const zm_memory *mem, unsigned index, uint32_t address, unsigned length, unsigned position)
{
    uint32_t block = PARSE + 2u + 4u * index;

    CHECK_EQ_INT(zm_read_word(mem, block), address);
    CHECK_EQ_INT(zm_read_byte(mem, block + 2), length);
    CHECK_EQ_INT(zm_read_byte(mem, block + 3), position);
}

static void test_lexer_divides_at_spaces_and_separators(void)
{
    story s;
    zm_memory mem;
    zm_dictionary dict;

    /* The example of Spec §13.6.1: "fred,go  fishing" is four words. The
     * positions count from the start of the text buffer, where the text
     * begins at byte 1 (Spec §15 read). */
    story_init(&s, 3);
    build_dictionary(&s, 0);
    set_text(&s, "fred,go  fishing");
    s.bytes[PARSE] = 10;
    load(&s, &mem);

    zm_dictionary_read_main(&mem, &dict);
    zm_lexer_tokenise(&mem, TEXT, PARSE, &dict, 0);
    CHECK_EQ_INT(zm_read_byte(&mem, PARSE + 1), 4);
    check_block(&mem, 0, 0, 4, 1);                         /* fred */
    check_block(&mem, 1, 0, 1, 5);                         /* , */
    check_block(&mem, 2, entry_address(3, 0), 2, 6);       /* go */
    check_block(&mem, 3, 0, 7, 10);                        /* fishing */
    zm_memory_free(&mem);

    /* Leading and trailing spaces, a separator run, and upper case,
     * which read would have folded but tokenise sees as typed and so
     * fails to match. */
    story_init(&s, 3);
    build_dictionary(&s, 0);
    set_text(&s, "  look,.\"Open ");
    s.bytes[PARSE] = 10;
    load(&s, &mem);
    zm_dictionary_read_main(&mem, &dict);
    zm_lexer_tokenise(&mem, TEXT, PARSE, &dict, 0);
    CHECK_EQ_INT(zm_read_byte(&mem, PARSE + 1), 5);
    check_block(&mem, 0, entry_address(3, 1), 4, 3);       /* look */
    check_block(&mem, 1, 0, 1, 7);                         /* , */
    check_block(&mem, 2, 0, 1, 8);                         /* . */
    check_block(&mem, 3, 0, 1, 9);                         /* " */
    check_block(&mem, 4, 0, 4, 10);                        /* Open */
    zm_memory_free(&mem);
}

static void test_lexer_respects_the_parse_buffer_size(void)
{
    story s;
    zm_memory mem;
    zm_dictionary dict;

    /* Spec §15 read: no more blocks than byte 0 allows, and byte 1 says
     * how many were written. */
    story_init(&s, 3);
    build_dictionary(&s, 0);
    set_text(&s, "open go look");
    s.bytes[PARSE] = 2;
    memset(s.bytes + PARSE + 2, 0xee, 12);
    load(&s, &mem);
    zm_dictionary_read_main(&mem, &dict);
    zm_lexer_tokenise(&mem, TEXT, PARSE, &dict, 0);
    CHECK_EQ_INT(zm_read_byte(&mem, PARSE + 1), 2);
    check_block(&mem, 0, entry_address(3, 2), 4, 1);
    check_block(&mem, 1, entry_address(3, 0), 2, 6);
    CHECK_EQ_INT(zm_read_word(&mem, PARSE + 10), 0xeeee);  /* the third block is untouched */
    zm_memory_free(&mem);
}

static void test_lexer_can_leave_unrecognised_words_alone(void)
{
    story s;
    zm_memory mem;
    zm_dictionary dict;

    /* Spec §15 tokenise: with the flag set, a word not in the dictionary
     * leaves its block as it was, so successive calls with different
     * dictionaries fill in more of the table. */
    story_init(&s, 5);
    build_dictionary(&s, 0);
    set_text(&s, "go fred look");
    s.bytes[PARSE] = 10;
    memset(s.bytes + PARSE + 2, 0xee, 12);
    load(&s, &mem);
    zm_dictionary_read_main(&mem, &dict);
    zm_lexer_tokenise(&mem, TEXT, PARSE, &dict, 1);
    CHECK_EQ_INT(zm_read_byte(&mem, PARSE + 1), 3);
    check_block(&mem, 0, entry_address(5, 0), 2, 2);       /* text starts at byte 2 from Version 5 */
    check_block(&mem, 1, 0xeeee, 0xee, 0xee);
    check_block(&mem, 2, entry_address(5, 1), 4, 10);

    /* Without the flag the unknown word is written as 0. */
    zm_lexer_tokenise(&mem, TEXT, PARSE, &dict, 0);
    check_block(&mem, 1, 0, 4, 5);
    zm_memory_free(&mem);
}

int main(void)
{
    RUN_TEST(test_encoding_pads_to_six_or_nine_zchars);
    RUN_TEST(test_encoding_uses_single_shifts_from_version_3);
    RUN_TEST(test_encoding_uses_shift_locks_in_versions_1_and_2);
    RUN_TEST(test_encoding_follows_a_custom_alphabet);
    RUN_TEST(test_dictionary_header_and_lookup);
    RUN_TEST(test_lexer_divides_at_spaces_and_separators);
    RUN_TEST(test_lexer_respects_the_parse_buffer_size);
    RUN_TEST(test_lexer_can_leave_unrecognised_words_alone);
    return CHECK_SUMMARY();
}
