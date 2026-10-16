/*
 * test_object.c: the object table.
 *
 * Covers Spec §12 through zm/object.h. Each test lays out an object table
 * by hand in a small story, in the Version 1 to 3 shape (Spec §12.3.1,
 * §12.4.1) or the Version 4 and later shape (Spec §12.3.2, §12.4.2), and
 * checks both what the functions report and the bytes they change.
 *
 * The table sits at $40, just after the header, with static memory from
 * $400 so all of it is writable.
 */

#include <setjmp.h>
#include <stdio.h>
#include <string.h>

#include "zm/fatal.h"
#include "zm/header.h"
#include "zm/memory.h"
#include "zm/object.h"

#include "check.h"

#define STORY_SIZE 1024u
#define OBJECTS 0x40u
#define STATIC_BASE 0x400u

/* Property tables for the objects of the tests, well clear of the tree. */
#define PROPS1 0x200u
#define PROPS2 0x240u
#define PROPS3 0x300u
#define PROPS4 0x340u

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
    put_word(s->bytes, ZM_HEADER_OBJECTS, OBJECTS);
}

static unsigned defaults_count(const story *s)
{
    return s->version <= 3 ? 31u : 63u;
}

static unsigned entry_size(const story *s)
{
    return s->version <= 3 ? 9u : 14u;
}

/* Where object n's entry goes (Spec §12.3). */
static uint32_t entry(const story *s, uint16_t n)
{
    return OBJECTS + 2u * defaults_count(s) + (uint32_t)(n - 1u) * entry_size(s);
}

/* Fill in an object's links and property table address, leaving its
 * attributes clear. */
static void set_object(story *s, uint16_t n, uint16_t parent, uint16_t sibling, uint16_t child, uint16_t props)
{
    uint32_t e = entry(s, n);

    if (s->version <= 3) {
        s->bytes[e + 4] = (uint8_t)parent;
        s->bytes[e + 5] = (uint8_t)sibling;
        s->bytes[e + 6] = (uint8_t)child;
        put_word(s->bytes, e + 7, props);
    } else {
        put_word(s->bytes, e + 6, parent);
        put_word(s->bytes, e + 8, sibling);
        put_word(s->bytes, e + 10, child);
        put_word(s->bytes, e + 12, props);
    }
}

static void write_bytes(story *s, uint32_t address, const uint8_t *bytes, size_t count)
{
    memcpy(s->bytes + address, bytes, count);
}

#define WRITE(s, address, ...) \
    write_bytes((s), (address), (const uint8_t[]){ __VA_ARGS__ }, sizeof((const uint8_t[]){ __VA_ARGS__ }))

/* A tree of four objects: 1 holds 2, 3, and 4 in that order.
 *
 *     1
 *     +-- 2 -- 3 -- 4
 */
static void build_tree(story *s)
{
    set_object(s, 1, 0, 0, 2, PROPS1);
    set_object(s, 2, 1, 3, 0, PROPS2);
    set_object(s, 3, 1, 4, 0, PROPS3);
    set_object(s, 4, 1, 0, 0, PROPS4);
}

static void load(const story *s, zm_memory *mem)
{
    CHECK_EQ_INT(zm_memory_load_bytes(mem, s->bytes, STORY_SIZE), ZM_OK);
}

/* Fault trapping, as in test_machine.c. */
static jmp_buf fault_jump;
static char fault_message[256];

static void trap_fault(const char *message)
{
    strncpy(fault_message, message, sizeof fault_message - 1);
    fault_message[sizeof fault_message - 1] = '\0';
    longjmp(fault_jump, 1);
}

/* Run a statement that is expected to fault, and check the message. */
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
 * Tests.
 */

static void test_entries_follow_the_defaults_table(void)
{
    story s;
    zm_memory mem;

    /* Spec §12.2, §12.3.1: 31 default words, then 9-byte entries. */
    story_init(&s, 3);
    load(&s, &mem);
    CHECK_EQ_INT(zm_object_max(&mem), 255);
    CHECK_EQ_INT(zm_object_address(&mem, 1), OBJECTS + 62);
    CHECK_EQ_INT(zm_object_address(&mem, 2), OBJECTS + 62 + 9);
    CHECK_EQ_INT(zm_object_address(&mem, 10), OBJECTS + 62 + 81);
    zm_memory_free(&mem);

    /* Spec §12.2, §12.3.2: 63 default words, then 14-byte entries. */
    story_init(&s, 5);
    load(&s, &mem);
    CHECK_EQ_INT(zm_object_max(&mem), 65535);
    CHECK_EQ_INT(zm_object_address(&mem, 1), OBJECTS + 126);
    CHECK_EQ_INT(zm_object_address(&mem, 2), OBJECTS + 126 + 14);
    zm_memory_free(&mem);
}

static void test_attributes_are_stored_top_bit_first(void)
{
    story s;
    zm_memory mem;
    uint32_t e;

    /* Spec §12.3.1: attribute 0 is bit 7 of the first byte, 31 is bit 0
     * of the fourth. */
    story_init(&s, 3);
    build_tree(&s);
    load(&s, &mem);
    e = zm_object_address(&mem, 2);

    CHECK_EQ_INT(zm_object_test_attribute(&mem, 2, 0), 0);
    zm_object_set_attribute(&mem, 2, 0);
    CHECK_EQ_INT(zm_object_test_attribute(&mem, 2, 0), 1);
    CHECK_EQ_INT(zm_read_byte(&mem, e), 0x80);

    zm_object_set_attribute(&mem, 2, 31);
    CHECK_EQ_INT(zm_read_byte(&mem, e + 3), 0x01);
    zm_object_set_attribute(&mem, 2, 9);
    CHECK_EQ_INT(zm_read_byte(&mem, e + 1), 0x40);
    CHECK_EQ_INT(zm_object_test_attribute(&mem, 2, 9), 1);
    CHECK_EQ_INT(zm_object_test_attribute(&mem, 2, 8), 0);
    CHECK_EQ_INT(zm_object_test_attribute(&mem, 2, 10), 0);

    /* Clearing leaves the other bits of the byte alone. */
    zm_object_set_attribute(&mem, 2, 1);
    zm_object_clear_attribute(&mem, 2, 0);
    CHECK_EQ_INT(zm_read_byte(&mem, e), 0x40);
    CHECK_EQ_INT(zm_object_test_attribute(&mem, 2, 0), 0);
    CHECK_EQ_INT(zm_object_test_attribute(&mem, 2, 1), 1);

    /* Other objects are untouched. */
    CHECK_EQ_INT(zm_object_test_attribute(&mem, 1, 0), 0);
    CHECK_EQ_INT(zm_object_test_attribute(&mem, 3, 31), 0);

    /* Spec §12.3.1: there are 32 of them. */
    EXPECT_FAULT("attribute 32", zm_object_set_attribute(&mem, 2, 32));
    zm_memory_free(&mem);

    /* Spec §12.3.2: 48 in Version 4 and later. */
    story_init(&s, 5);
    build_tree(&s);
    load(&s, &mem);
    e = zm_object_address(&mem, 3);
    zm_object_set_attribute(&mem, 3, 47);
    CHECK_EQ_INT(zm_read_byte(&mem, e + 5), 0x01);
    CHECK_EQ_INT(zm_object_test_attribute(&mem, 3, 47), 1);
    zm_object_set_attribute(&mem, 3, 32);
    CHECK_EQ_INT(zm_read_byte(&mem, e + 4), 0x80);
    EXPECT_FAULT("attribute 48", zm_object_test_attribute(&mem, 3, 48));
    zm_memory_free(&mem);
}

static void check_links(uint8_t version)
{
    story s;
    zm_memory mem;
    uint32_t e;

    story_init(&s, version);
    build_tree(&s);
    load(&s, &mem);

    CHECK_EQ_INT(zm_object_parent(&mem, 1), 0);
    CHECK_EQ_INT(zm_object_sibling(&mem, 1), 0);
    CHECK_EQ_INT(zm_object_child(&mem, 1), 2);
    CHECK_EQ_INT(zm_object_parent(&mem, 2), 1);
    CHECK_EQ_INT(zm_object_sibling(&mem, 2), 3);
    CHECK_EQ_INT(zm_object_child(&mem, 2), 0);
    CHECK_EQ_INT(zm_object_parent(&mem, 4), 1);
    CHECK_EQ_INT(zm_object_sibling(&mem, 4), 0);

    CHECK_EQ_INT(zm_object_properties(&mem, 1), PROPS1);
    CHECK_EQ_INT(zm_object_properties(&mem, 4), PROPS4);

    /* Writes land in the right bytes of the entry. */
    e = zm_object_address(&mem, 4);
    zm_object_set_parent(&mem, 4, 3);
    zm_object_set_sibling(&mem, 4, 2);
    zm_object_set_child(&mem, 4, 1);
    if (version <= 3) {
        CHECK_EQ_INT(zm_read_byte(&mem, e + 4), 3);
        CHECK_EQ_INT(zm_read_byte(&mem, e + 5), 2);
        CHECK_EQ_INT(zm_read_byte(&mem, e + 6), 1);
    } else {
        CHECK_EQ_INT(zm_read_word(&mem, e + 6), 3);
        CHECK_EQ_INT(zm_read_word(&mem, e + 8), 2);
        CHECK_EQ_INT(zm_read_word(&mem, e + 10), 1);
    }
    CHECK_EQ_INT(zm_object_parent(&mem, 4), 3);
    CHECK_EQ_INT(zm_object_sibling(&mem, 4), 2);
    CHECK_EQ_INT(zm_object_child(&mem, 4), 1);

    zm_memory_free(&mem);
}

static void test_tree_links_by_version(void)
{
    check_links(3);
    check_links(5);
}

static void check_remove(uint8_t version)
{
    story s;
    zm_memory mem;

    /* Removing the first child: the parent's child link moves on. */
    story_init(&s, version);
    build_tree(&s);
    load(&s, &mem);
    zm_object_remove(&mem, 2);
    CHECK_EQ_INT(zm_object_child(&mem, 1), 3);
    CHECK_EQ_INT(zm_object_parent(&mem, 2), 0);
    CHECK_EQ_INT(zm_object_sibling(&mem, 2), 0);
    CHECK_EQ_INT(zm_object_sibling(&mem, 3), 4);
    zm_memory_free(&mem);

    /* Removing a middle child: the previous sibling skips over it. */
    story_init(&s, version);
    build_tree(&s);
    load(&s, &mem);
    zm_object_remove(&mem, 3);
    CHECK_EQ_INT(zm_object_child(&mem, 1), 2);
    CHECK_EQ_INT(zm_object_sibling(&mem, 2), 4);
    CHECK_EQ_INT(zm_object_parent(&mem, 3), 0);
    CHECK_EQ_INT(zm_object_sibling(&mem, 3), 0);
    zm_memory_free(&mem);

    /* Removing the last child. */
    story_init(&s, version);
    build_tree(&s);
    load(&s, &mem);
    zm_object_remove(&mem, 4);
    CHECK_EQ_INT(zm_object_sibling(&mem, 3), 0);
    CHECK_EQ_INT(zm_object_parent(&mem, 4), 0);
    zm_memory_free(&mem);

    /* Spec §15 remove_obj: an object's own children stay with it, and
     * removing a parentless object changes nothing. */
    story_init(&s, version);
    build_tree(&s);
    load(&s, &mem);
    zm_object_remove(&mem, 1);
    CHECK_EQ_INT(zm_object_child(&mem, 1), 2);
    CHECK_EQ_INT(zm_object_parent(&mem, 2), 1);
    zm_memory_free(&mem);
}

static void test_remove_unlinks_from_the_sibling_chain(void)
{
    check_remove(3);
    check_remove(5);
}

static void check_insert(uint8_t version)
{
    story s;
    zm_memory mem;

    /* Spec §15 insert_obj: 4 moves into 2, becoming its first child, and
     * leaves 1's list. */
    story_init(&s, version);
    build_tree(&s);
    load(&s, &mem);
    zm_object_insert(&mem, 4, 2);
    CHECK_EQ_INT(zm_object_parent(&mem, 4), 2);
    CHECK_EQ_INT(zm_object_child(&mem, 2), 4);
    CHECK_EQ_INT(zm_object_sibling(&mem, 4), 0);
    CHECK_EQ_INT(zm_object_sibling(&mem, 3), 0);

    /* Then 3 moves into 2 too, ahead of 4. */
    zm_object_insert(&mem, 3, 2);
    CHECK_EQ_INT(zm_object_child(&mem, 2), 3);
    CHECK_EQ_INT(zm_object_sibling(&mem, 3), 4);
    CHECK_EQ_INT(zm_object_sibling(&mem, 2), 0);
    CHECK_EQ_INT(zm_object_child(&mem, 1), 2);

    /* Moving the last child of 1 to the front of 1's list. */
    zm_object_insert(&mem, 2, 1);
    CHECK_EQ_INT(zm_object_child(&mem, 1), 2);
    CHECK_EQ_INT(zm_object_sibling(&mem, 2), 0);
    CHECK_EQ_INT(zm_object_parent(&mem, 2), 1);
    zm_memory_free(&mem);

    /* A parentless object can be inserted (Spec §15 insert_obj). */
    story_init(&s, version);
    build_tree(&s);
    set_object(&s, 4, 0, 0, 0, PROPS4);
    set_object(&s, 3, 1, 0, 0, PROPS3);
    load(&s, &mem);
    zm_object_insert(&mem, 4, 1);
    CHECK_EQ_INT(zm_object_child(&mem, 1), 4);
    CHECK_EQ_INT(zm_object_sibling(&mem, 4), 2);
    CHECK_EQ_INT(zm_object_parent(&mem, 4), 1);
    zm_memory_free(&mem);
}

static void test_insert_makes_the_object_the_first_child(void)
{
    check_insert(3);
    check_insert(5);
}

static void test_properties_in_versions_1_to_3(void)
{
    story s;
    zm_memory mem;
    zm_property p;
    uint32_t address;

    story_init(&s, 3);
    build_tree(&s);

    /* Defaults 1 and 31 (Spec §12.2). */
    put_word(s.bytes, OBJECTS, 0x1111);
    put_word(s.bytes, OBJECTS + 60, 0x3131);

    /* Object 2's table (Spec §12.4, §12.4.1): a one-word name "a", then
     * property 5 of 2 bytes, property 3 of 1 byte, property 2 of 4 bytes,
     * and the terminator. Size bytes are 32 * (length - 1) + number. */
    WRITE(&s, PROPS2,
          0x01, 0x98, 0xa5,              /* name: 1 word, "a" */
          0x25, 0x12, 0x34,              /* property 5, 2 bytes */
          0x03, 0x56,                    /* property 3, 1 byte */
          0x62, 0x01, 0x02, 0x03, 0x04,  /* property 2, 4 bytes */
          0x00);

    /* Object 3 has no name and no properties. */
    WRITE(&s, PROPS3, 0x00, 0x00);

    load(&s, &mem);

    CHECK_EQ_INT(zm_object_name_length(&mem, 2), 1);
    CHECK_EQ_INT(zm_object_name(&mem, 2), PROPS2 + 1);
    CHECK_EQ_INT(zm_object_first_property(&mem, 2), PROPS2 + 3);
    CHECK_EQ_INT(zm_object_name_length(&mem, 3), 0);
    CHECK_EQ_INT(zm_object_first_property(&mem, 3), PROPS3 + 1);

    /* Walking the list. */
    address = zm_object_first_property(&mem, 2);
    address = zm_property_decode(&mem, address, &p);
    CHECK_EQ_INT(p.number, 5);
    CHECK_EQ_INT(p.length, 2);
    CHECK_EQ_INT(p.address, PROPS2 + 4);
    CHECK_EQ_INT(address, PROPS2 + 6);
    address = zm_property_decode(&mem, address, &p);
    CHECK_EQ_INT(p.number, 3);
    CHECK_EQ_INT(p.length, 1);
    CHECK_EQ_INT(p.address, PROPS2 + 7);
    address = zm_property_decode(&mem, address, &p);
    CHECK_EQ_INT(p.number, 2);
    CHECK_EQ_INT(p.length, 4);
    CHECK_EQ_INT(p.address, PROPS2 + 9);
    CHECK_EQ_INT(address, PROPS2 + 13);
    zm_property_decode(&mem, address, &p);
    CHECK_EQ_INT(p.number, 0);

    /* Finding by number, present and absent. */
    CHECK_EQ_INT(zm_object_find_property(&mem, 2, 5, &p), 1);
    CHECK_EQ_INT(zm_read_word(&mem, p.address), 0x1234);
    CHECK_EQ_INT(zm_object_find_property(&mem, 2, 3, &p), 1);
    CHECK_EQ_INT(zm_read_byte(&mem, p.address), 0x56);
    CHECK_EQ_INT(zm_object_find_property(&mem, 2, 2, &p), 1);
    CHECK_EQ_INT(p.length, 4);
    CHECK_EQ_INT(zm_object_find_property(&mem, 2, 4, &p), 0);
    CHECK_EQ_INT(zm_object_find_property(&mem, 2, 6, &p), 0);
    CHECK_EQ_INT(zm_object_find_property(&mem, 2, 1, &p), 0);
    CHECK_EQ_INT(zm_object_find_property(&mem, 3, 1, &p), 0);

    /* Lengths from data addresses (Spec §15 get_prop_len). */
    CHECK_EQ_INT(zm_property_length_at(&mem, PROPS2 + 4), 2);
    CHECK_EQ_INT(zm_property_length_at(&mem, PROPS2 + 7), 1);
    CHECK_EQ_INT(zm_property_length_at(&mem, PROPS2 + 9), 4);
    CHECK_EQ_INT(zm_property_length_at(&mem, 0), 0);

    /* Defaults. */
    CHECK_EQ_INT(zm_property_default(&mem, 1), 0x1111);
    CHECK_EQ_INT(zm_property_default(&mem, 31), 0x3131);
    CHECK_EQ_INT(zm_property_default(&mem, 2), 0);
    EXPECT_FAULT("property 32", zm_property_default(&mem, 32));
    EXPECT_FAULT("property 0", zm_property_default(&mem, 0));

    zm_memory_free(&mem);
}

static void test_properties_in_versions_4_and_later(void)
{
    story s;
    zm_memory mem;
    zm_property p;
    uint32_t address;

    story_init(&s, 5);
    build_tree(&s);
    put_word(s.bytes, OBJECTS + 124, 0x6363);

    /* Object 2's table (Spec §12.4.2): a two-word name, then property 63
     * with a two-byte header and 5 bytes of data, property 40 with 2
     * bytes (bit 6 set), property 7 with 1 byte, property 1 with a
     * two-byte header whose length field is 0, meaning 64 (Spec
     * §12.4.2.1.1), and the terminator. Bit 6 of the second header byte
     * is set, as in Infocom files, and must be ignored. */
    WRITE(&s, PROPS2,
          0x02, 0x11, 0xaa, 0xc6, 0x34,  /* name: 2 words */
          0xbf, 0xc5, 1, 2, 3, 4, 5,     /* property 63, 5 bytes */
          0x68, 0xab, 0xcd,              /* property 40, 2 bytes */
          0x07, 0xef,                    /* property 7, 1 byte */
          0x81, 0x80);                   /* property 1, 64 bytes (zeros) */
    s.bytes[PROPS2 + 19 + 64] = 0x00;     /* terminator */

    load(&s, &mem);

    CHECK_EQ_INT(zm_object_name_length(&mem, 2), 2);
    CHECK_EQ_INT(zm_object_first_property(&mem, 2), PROPS2 + 5);

    address = zm_property_decode(&mem, PROPS2 + 5, &p);
    CHECK_EQ_INT(p.number, 63);
    CHECK_EQ_INT(p.length, 5);
    CHECK_EQ_INT(p.address, PROPS2 + 7);
    CHECK_EQ_INT(address, PROPS2 + 12);
    address = zm_property_decode(&mem, address, &p);
    CHECK_EQ_INT(p.number, 40);
    CHECK_EQ_INT(p.length, 2);
    CHECK_EQ_INT(p.address, PROPS2 + 13);
    address = zm_property_decode(&mem, address, &p);
    CHECK_EQ_INT(p.number, 7);
    CHECK_EQ_INT(p.length, 1);
    CHECK_EQ_INT(p.address, PROPS2 + 16);
    address = zm_property_decode(&mem, address, &p);
    CHECK_EQ_INT(p.number, 1);
    CHECK_EQ_INT(p.length, 64);
    CHECK_EQ_INT(p.address, PROPS2 + 19);
    CHECK_EQ_INT(address, PROPS2 + 19 + 64);
    zm_property_decode(&mem, address, &p);
    CHECK_EQ_INT(p.number, 0);

    CHECK_EQ_INT(zm_object_find_property(&mem, 2, 40, &p), 1);
    CHECK_EQ_INT(zm_read_word(&mem, p.address), 0xabcd);
    CHECK_EQ_INT(zm_object_find_property(&mem, 2, 41, &p), 0);
    CHECK_EQ_INT(zm_object_find_property(&mem, 2, 1, &p), 1);
    CHECK_EQ_INT(zm_object_find_property(&mem, 2, 2, &p), 0);

    CHECK_EQ_INT(zm_property_length_at(&mem, PROPS2 + 7), 5);
    CHECK_EQ_INT(zm_property_length_at(&mem, PROPS2 + 13), 2);
    CHECK_EQ_INT(zm_property_length_at(&mem, PROPS2 + 16), 1);
    CHECK_EQ_INT(zm_property_length_at(&mem, PROPS2 + 19), 64);

    CHECK_EQ_INT(zm_property_default(&mem, 63), 0x6363);
    EXPECT_FAULT("property 64", zm_property_default(&mem, 64));

    zm_memory_free(&mem);
}

static void test_object_numbers_out_of_range_fault(void)
{
    story s;
    zm_memory mem;

    /* Spec §12.3: object 0 is nothing, and Versions 1 to 3 stop at 255. */
    story_init(&s, 3);
    build_tree(&s);
    load(&s, &mem);
    EXPECT_FAULT("object 0", zm_object_parent(&mem, 0));
    EXPECT_FAULT("object 256", zm_object_child(&mem, 256));
    EXPECT_FAULT("object 0", zm_object_insert(&mem, 2, 0));

    /* A Version 3 link cannot hold a number above 255. */
    EXPECT_FAULT("linked to object 300", zm_object_set_parent(&mem, 2, 300));
    zm_memory_free(&mem);
}

int main(void)
{
    RUN_TEST(test_entries_follow_the_defaults_table);
    RUN_TEST(test_attributes_are_stored_top_bit_first);
    RUN_TEST(test_tree_links_by_version);
    RUN_TEST(test_remove_unlinks_from_the_sibling_chain);
    RUN_TEST(test_insert_makes_the_object_the_first_child);
    RUN_TEST(test_properties_in_versions_1_to_3);
    RUN_TEST(test_properties_in_versions_4_and_later);
    RUN_TEST(test_object_numbers_out_of_range_fault);
    return CHECK_SUMMARY();
}
