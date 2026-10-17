/*
 * test_memory.c: the memory map, byte order, bounds, and packed addresses.
 *
 * Covers Spec §1 and §2.1 through zm/memory.h. Stories are built in memory
 * rather than read from disk, so each test controls exactly what the
 * header says. Faults are caught with a handler that longjmps back to the
 * test instead of exiting.
 */

#include <setjmp.h>
#include <string.h>

#include "zm/fatal.h"
#include "zm/memory.h"

#include "check.h"

/* A handler that records the fault and jumps back to the test. */
static jmp_buf fault_jump;
static char fault_message[256];

static void trap_fault(const char *message)
{
    strncpy(fault_message, message, sizeof fault_message - 1);
    fault_message[sizeof fault_message - 1] = '\0';
    longjmp(fault_jump, 1);
}

/* Build the smallest valid story: a 64-byte header followed by `extra`
 * bytes of zeros, with the static base placed at `static_base`. Header
 * offsets follow Spec §11.1. */
static uint32_t build_story(uint8_t *out, uint8_t version, uint16_t static_base, uint32_t extra)
{
    uint32_t size = ZM_HEADER_SIZE + extra;

    memset(out, 0, size);
    out[0x00] = version;
    out[0x0e] = (uint8_t)(static_base >> 8);
    out[0x0f] = (uint8_t)(static_base & 0xff);
    return size;
}

static void test_load_rejects_short_file(void)
{
    uint8_t bytes[ZM_HEADER_SIZE];
    zm_memory mem;

    memset(bytes, 0, sizeof bytes);
    bytes[0] = 3;

    /* Spec §1.1: a story is at least the 64-byte header. */
    CHECK_EQ_INT(zm_memory_load_bytes(&mem, bytes, 63), ZM_ERROR_TOO_SMALL);
    CHECK_EQ_INT(zm_memory_load_bytes(&mem, bytes, 0), ZM_ERROR_TOO_SMALL);
}

static void test_load_rejects_bad_version(void)
{
    uint8_t bytes[128];
    zm_memory mem;
    uint32_t size;

    /* Spec §11.1: Versions run from 1 to 8. */
    size = build_story(bytes, 0, 64, 0);
    CHECK_EQ_INT(zm_memory_load_bytes(&mem, bytes, size), ZM_ERROR_BAD_VERSION);

    size = build_story(bytes, 9, 64, 0);
    CHECK_EQ_INT(zm_memory_load_bytes(&mem, bytes, size), ZM_ERROR_BAD_VERSION);
}

static void test_load_rejects_bad_static_base(void)
{
    uint8_t bytes[128];
    zm_memory mem;
    uint32_t size;

    /* Spec §1.1: dynamic memory holds at least 64 bytes... */
    size = build_story(bytes, 3, 63, 0);
    CHECK_EQ_INT(zm_memory_load_bytes(&mem, bytes, size), ZM_ERROR_BAD_MEMORY_MAP);

    /* ...and cannot run past the end of the file. */
    size = build_story(bytes, 3, 65, 0);
    CHECK_EQ_INT(zm_memory_load_bytes(&mem, bytes, size), ZM_ERROR_BAD_MEMORY_MAP);

    /* Exactly the whole file being dynamic is allowed. */
    size = build_story(bytes, 3, 64, 0);
    CHECK_EQ_INT(zm_memory_load_bytes(&mem, bytes, size), ZM_OK);
    zm_memory_free(&mem);
}

static void test_load_rejects_blorb_without_a_story(void)
{
    uint8_t bytes[128];
    zm_memory mem;

    /* A Blorb with no resource index at all is damaged (Blorb §1); one
     * whose index holds no Z-code has nothing to run (Blorb §5). The
     * stories inside well-formed Blorbs are loaded in test_blorb.c. */
    memset(bytes, 0, sizeof bytes);
    memcpy(bytes, "FORM", 4);
    memcpy(bytes + 8, "IFRS", 4);
    CHECK_EQ_INT(zm_memory_load_bytes(&mem, bytes, sizeof bytes), ZM_ERROR_BAD_BLORB);

    bytes[7] = 12;
    memcpy(bytes + 12, "RIdx", 4);
    bytes[19] = 4;
    CHECK_EQ_INT(zm_memory_load_bytes(&mem, bytes, 24), ZM_ERROR_BLORB_NO_STORY);
}

static void test_max_size_follows_version(void)
{
    /* Spec §1.1.4. */
    CHECK_EQ_INT(zm_memory_max_size(1), 128 * 1024);
    CHECK_EQ_INT(zm_memory_max_size(3), 128 * 1024);
    CHECK_EQ_INT(zm_memory_max_size(4), 256 * 1024);
    CHECK_EQ_INT(zm_memory_max_size(5), 256 * 1024);
    CHECK_EQ_INT(zm_memory_max_size(6), 512 * 1024);
    CHECK_EQ_INT(zm_memory_max_size(8), 512 * 1024);
    CHECK_EQ_INT(zm_memory_max_size(9), 0);
}

static void test_load_rejects_oversized_story(void)
{
    /* One byte over the Version 3 limit. The buffer is static because it
     * is too large for the stack on some platforms. */
    static uint8_t bytes[128 * 1024 + 1];
    zm_memory mem;

    build_story(bytes, 3, 64, 0);
    CHECK_EQ_INT(zm_memory_load_bytes(&mem, bytes, sizeof bytes), ZM_ERROR_TOO_LARGE);

    /* The same bytes labelled Version 4 are within that Version's limit. */
    bytes[0] = 4;
    CHECK_EQ_INT(zm_memory_load_bytes(&mem, bytes, sizeof bytes), ZM_OK);
    zm_memory_free(&mem);
}

static void test_load_records_memory_map(void)
{
    uint8_t bytes[256];
    zm_memory mem;
    uint32_t size;

    size = build_story(bytes, 5, 0x80, 192);
    bytes[0x04] = 0x00; /* high memory mark $00c0 (Spec §1.1) */
    bytes[0x05] = 0xc0;

    CHECK_EQ_INT(zm_memory_load_bytes(&mem, bytes, size), ZM_OK);
    CHECK_EQ_INT(mem.size, 256);
    CHECK_EQ_INT(mem.version, 5);
    CHECK_EQ_INT(mem.static_base, 0x80);
    CHECK_EQ_INT(mem.high_base, 0xc0);
    zm_memory_free(&mem);

    /* Freed memory is empty and safe to free again. */
    CHECK(mem.bytes == NULL);
    CHECK_EQ_INT(mem.size, 0);
    zm_memory_free(&mem);
}

static void test_words_are_big_endian(void)
{
    uint8_t bytes[128];
    zm_memory mem;
    uint32_t size;

    size = build_story(bytes, 3, 0x40, 64);
    bytes[0x40] = 0x12;
    bytes[0x41] = 0x34;

    CHECK_EQ_INT(zm_memory_load_bytes(&mem, bytes, size), ZM_OK);

    /* Spec §2.1: high byte first. */
    CHECK_EQ_INT(zm_read_byte(&mem, 0x40), 0x12);
    CHECK_EQ_INT(zm_read_byte(&mem, 0x41), 0x34);
    CHECK_EQ_INT(zm_read_word(&mem, 0x40), 0x1234);

    /* Writes go through the same order. */
    zm_write_word(&mem, 0x10, 0xabcd);
    CHECK_EQ_INT(zm_read_byte(&mem, 0x10), 0xab);
    CHECK_EQ_INT(zm_read_byte(&mem, 0x11), 0xcd);
    CHECK_EQ_INT(zm_read_word(&mem, 0x10), 0xabcd);

    zm_write_byte(&mem, 0x11, 0x01);
    CHECK_EQ_INT(zm_read_word(&mem, 0x10), 0xab01);

    zm_memory_free(&mem);
}

static void test_read_outside_memory_faults(void)
{
    uint8_t bytes[128];
    zm_memory mem;
    uint32_t size;
    int faulted;

    size = build_story(bytes, 3, 0x40, 64);
    CHECK_EQ_INT(zm_memory_load_bytes(&mem, bytes, size), ZM_OK);
    zm_set_fatal_handler(trap_fault);

    /* The last byte is readable; one past it is not. */
    faulted = 0;
    if (setjmp(fault_jump) == 0) {
        (void)zm_read_byte(&mem, 127);
    } else {
        faulted = 1;
    }
    CHECK_EQ_INT(faulted, 0);

    faulted = 0;
    if (setjmp(fault_jump) == 0) {
        (void)zm_read_byte(&mem, 128);
    } else {
        faulted = 1;
    }
    CHECK_EQ_INT(faulted, 1);

    /* A word needs two bytes, so the last byte address is out of range
     * for a word read. */
    faulted = 0;
    if (setjmp(fault_jump) == 0) {
        (void)zm_read_word(&mem, 127);
    } else {
        faulted = 1;
    }
    CHECK_EQ_INT(faulted, 1);

    /* An address so large that address + 2 would wrap around. */
    faulted = 0;
    if (setjmp(fault_jump) == 0) {
        (void)zm_read_word(&mem, 0xffffffffu);
    } else {
        faulted = 1;
    }
    CHECK_EQ_INT(faulted, 1);

    zm_set_fatal_handler(NULL);
    zm_memory_free(&mem);
}

static void test_write_to_static_memory_faults(void)
{
    uint8_t bytes[128];
    zm_memory mem;
    uint32_t size;
    int faulted;

    /* Dynamic memory is $00 to $3f; static begins at $40 (Spec §1.1). */
    size = build_story(bytes, 3, 0x40, 64);
    CHECK_EQ_INT(zm_memory_load_bytes(&mem, bytes, size), ZM_OK);
    zm_set_fatal_handler(trap_fault);

    /* Spec §1.1.1: dynamic memory may be written. */
    faulted = 0;
    if (setjmp(fault_jump) == 0) {
        zm_write_byte(&mem, 0x3f, 1);
    } else {
        faulted = 1;
    }
    CHECK_EQ_INT(faulted, 0);
    CHECK_EQ_INT(zm_read_byte(&mem, 0x3f), 1);

    /* Spec §1.1.2: static memory may not. */
    faulted = 0;
    if (setjmp(fault_jump) == 0) {
        zm_write_byte(&mem, 0x40, 1);
    } else {
        faulted = 1;
    }
    CHECK_EQ_INT(faulted, 1);
    CHECK_EQ_INT(zm_read_byte(&mem, 0x40), 0);

    /* A word straddling the boundary is refused as a whole. */
    faulted = 0;
    if (setjmp(fault_jump) == 0) {
        zm_write_word(&mem, 0x3f, 0x0101);
    } else {
        faulted = 1;
    }
    CHECK_EQ_INT(faulted, 1);
    CHECK_EQ_INT(zm_read_byte(&mem, 0x40), 0);

    zm_set_fatal_handler(NULL);
    zm_memory_free(&mem);
}

static void test_packed_addresses_follow_version(void)
{
    uint8_t bytes[128];
    zm_memory mem;
    uint32_t size;

    /* Spec §1.2.3, with the worked example from the editor's note:
     * Version 5, packed $1a2b, byte address 4 x $1a2b = $68ac. */
    static const struct {
        uint8_t version;
        uint16_t packed;
        uint32_t routine;
        uint32_t string;
    } cases[] = {
        { 1, 0x1a2b, 2u * 0x1a2b, 2u * 0x1a2b },
        { 3, 0x1a2b, 2u * 0x1a2b, 2u * 0x1a2b },
        { 4, 0x1a2b, 4u * 0x1a2b, 4u * 0x1a2b },
        { 5, 0x1a2b, 0x68ac,      0x68ac },
        { 8, 0x1a2b, 8u * 0x1a2b, 8u * 0x1a2b },
        /* Versions 6 and 7 add 8 x the header offsets: routines offset
         * $0010 and strings offset $0020 are set below. */
        { 6, 0x1a2b, 4u * 0x1a2b + 8u * 0x10, 4u * 0x1a2b + 8u * 0x20 },
        { 7, 0x1a2b, 4u * 0x1a2b + 8u * 0x10, 4u * 0x1a2b + 8u * 0x20 },
        /* The largest packed address reaches the Version's size limit
         * (Spec §1.1.4), which is the point of the scaling. */
        { 3, 0xffff, 0x1fffe, 0x1fffe },
        { 8, 0xffff, 0x7fff8, 0x7fff8 },
    };

    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        size = build_story(bytes, cases[i].version, 0x40, 64);
        bytes[0x28] = 0x00; bytes[0x29] = 0x10; /* routines offset */
        bytes[0x2a] = 0x00; bytes[0x2b] = 0x20; /* strings offset */

        CHECK_EQ_INT(zm_memory_load_bytes(&mem, bytes, size), ZM_OK);
        CHECK_EQ_INT(zm_unpack_routine(&mem, cases[i].packed), cases[i].routine);
        CHECK_EQ_INT(zm_unpack_string(&mem, cases[i].packed), cases[i].string);
        zm_memory_free(&mem);
    }
}

static void test_load_file_reports_missing_file(void)
{
    zm_memory mem;

    CHECK_EQ_INT(zm_memory_load_file(&mem, QUIDDITY_TEST_DATA_DIR "/no-such-story.z3"), ZM_ERROR_IO);
    CHECK(mem.bytes == NULL);
}

static void test_load_file_reads_real_story(void)
{
    zm_memory mem;

    CHECK_EQ_INT(zm_memory_load_file(&mem, QUIDDITY_TEST_DATA_DIR "/simple-test-r1-s260727.z3"), ZM_OK);
    CHECK_EQ_INT(mem.version, 3);
    CHECK_EQ_INT(mem.size, 1536);
    CHECK(mem.static_base >= ZM_HEADER_SIZE);
    CHECK(mem.static_base <= mem.size);
    zm_memory_free(&mem);
}

int main(void)
{
    RUN_TEST(test_load_rejects_short_file);
    RUN_TEST(test_load_rejects_bad_version);
    RUN_TEST(test_load_rejects_bad_static_base);
    RUN_TEST(test_load_rejects_blorb_without_a_story);
    RUN_TEST(test_max_size_follows_version);
    RUN_TEST(test_load_rejects_oversized_story);
    RUN_TEST(test_load_records_memory_map);
    RUN_TEST(test_words_are_big_endian);
    RUN_TEST(test_read_outside_memory_faults);
    RUN_TEST(test_write_to_static_memory_faults);
    RUN_TEST(test_packed_addresses_follow_version);
    RUN_TEST(test_load_file_reports_missing_file);
    RUN_TEST(test_load_file_reads_real_story);
    return CHECK_SUMMARY();
}
