/*
 * test_quetzal.c: saved games in the Quetzal format.
 *
 * Covers zm/quetzal.h against the Quetzal standard (specs/zmachine/
 * quetzal.html): the layout of a file written here, byte by byte; that
 * what is written reads back as the same state of play; that the plain
 * UMem form and unknown chunks are read; and that the ways a file can be
 * wrong are each refused without touching the machine. Section numbers
 * are Quetzal's unless marked Spec.
 */

#include <stdlib.h>
#include <string.h>

#include "zm/header.h"
#include "zm/machine.h"
#include "zm/memory.h"
#include "zm/quetzal.h"

#include "check.h"

#define STORY_SIZE 2048u
#define GLOBALS 0x40u
#define CODE 0x400u
#define MAIN 0x4c0u

/* No screen and no files: the machine needs neither to be saved. */
static zm_io io;

static void put_word(uint8_t *bytes, uint32_t offset, uint16_t value)
{
    bytes[offset] = (uint8_t)(value >> 8);
    bytes[offset + 1] = (uint8_t)(value & 0xff);
}

static uint32_t get_long(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

/* A story with 1024 bytes of dynamic memory, a release, a serial, and a
 * checksum, whose only instruction is quit. In Version 6 the main
 * routine has two locals. */
static void make_story(uint8_t *bytes, uint8_t version, uint16_t checksum)
{
    memset(bytes, 0, STORY_SIZE);
    bytes[ZM_HEADER_VERSION] = version;
    put_word(bytes, ZM_HEADER_RELEASE, 42);
    put_word(bytes, ZM_HEADER_HIGH_BASE, CODE);
    put_word(bytes, ZM_HEADER_INITIAL_PC, version == 6 ? MAIN / 4 : CODE);
    put_word(bytes, ZM_HEADER_GLOBALS, GLOBALS);
    put_word(bytes, ZM_HEADER_STATIC_BASE, CODE);
    memcpy(bytes + ZM_HEADER_SERIAL, "260101", 6);
    put_word(bytes, ZM_HEADER_CHECKSUM, checksum);
    bytes[CODE] = 0xba;
    bytes[MAIN] = 0x02;
    bytes[MAIN + 1] = 0xba;
}

static void start(zm_memory *mem, zm_machine *m, uint8_t version, uint16_t checksum)
{
    uint8_t bytes[STORY_SIZE];

    make_story(bytes, version, checksum);
    CHECK_EQ_INT(zm_memory_load_bytes(mem, bytes, STORY_SIZE), ZM_OK);
    CHECK_EQ_INT(zm_machine_init(m, mem, &io), ZM_OK);
}

static void stop(zm_memory *mem, zm_machine *m)
{
    zm_machine_free(m);
    zm_memory_free(mem);
}

/* Give a machine a state worth saving: changed memory at both ends and
 * in the middle, words on the stack belonging to two frames, and two
 * routines in progress, one of which discards its result. */
static void play(zm_machine *m)
{
    zm_write_word(m->mem, GLOBALS, 0x1234);
    zm_write_byte(m->mem, 0x100, 0xaa);
    zm_write_byte(m->mem, 0x3ff, 0x01);

    m->stack[0] = 0x0101;
    m->stack[1] = 0x0202;
    m->stack[2] = 0x0303;
    m->stack[3] = 0x0404;
    m->stack[4] = 0x0505;
    m->sp = 5;

    memset(&m->frames[1], 0, sizeof m->frames[1]);
    m->frames[1].return_pc = 0x405;
    m->frames[1].stack_base = 2;
    m->frames[1].result_variable = 0x10;
    m->frames[1].argument_count = 2;
    m->frames[1].local_count = 3;
    m->frames[1].locals[0] = 11;
    m->frames[1].locals[1] = 22;
    m->frames[1].locals[2] = 33;

    memset(&m->frames[2], 0, sizeof m->frames[2]);
    m->frames[2].return_pc = 0x123456;
    m->frames[2].stack_base = 5;
    m->frames[2].result_variable = -1;
    m->depth = 3;

    m->pc = 0x404;
}

/* Undo the coding of §3.2 as a reader would, to check the writer by a
 * different route than the library's own reader. */
static void decode_memory(const uint8_t *coded, uint32_t length, const uint8_t *original, uint8_t *out, uint32_t size)
{
    uint32_t i = 0;

    memcpy(out, original, size);
    for (uint32_t at = 0; at < length;) {
        uint8_t byte = coded[at++];

        if (byte != 0) {
            out[i++] ^= byte;
        } else {
            i += (uint32_t)coded[at++] + 1;
        }
    }
}

/* Building files by hand. */
typedef struct {
    uint8_t bytes[4096];
    uint32_t size;
} iff;

static void iff_start(iff *f, const char *form_type)
{
    memcpy(f->bytes, "FORM\0\0\0\0", 8);
    memcpy(f->bytes + 8, form_type, 4);
    f->size = 12;
}

static void iff_chunk(iff *f, const char *id, const void *data, uint32_t length)
{
    memcpy(f->bytes + f->size, id, 4);
    f->bytes[f->size + 4] = (uint8_t)(length >> 24);
    f->bytes[f->size + 5] = (uint8_t)(length >> 16);
    f->bytes[f->size + 6] = (uint8_t)(length >> 8);
    f->bytes[f->size + 7] = (uint8_t)(length & 0xff);
    memcpy(f->bytes + f->size + 8, data, length);
    f->size += 8 + length;
    if (length & 1) {
        f->bytes[f->size++] = 0;
    }
}

static void iff_finish(iff *f)
{
    uint32_t length = f->size - 8;

    f->bytes[4] = (uint8_t)(length >> 24);
    f->bytes[5] = (uint8_t)(length >> 16);
    f->bytes[6] = (uint8_t)(length >> 8);
    f->bytes[7] = (uint8_t)(length & 0xff);
}

/* Find a chunk in a file written by the library. Returns its body, with
 * the length in *length, or NULL. */
static const uint8_t *find_chunk(const uint8_t *data, uint32_t size, const char *id, uint32_t *length)
{
    uint32_t at = 12;

    while (at + 8 <= size) {
        uint32_t chunk_length = get_long(data + at + 4);

        if (memcmp(data + at, id, 4) == 0) {
            *length = chunk_length;
            return data + at + 8;
        }
        at += 8 + chunk_length + (chunk_length & 1);
    }
    return NULL;
}

/*
 * Tests.
 */

static void test_file_layout(void)
{
    zm_memory mem;
    zm_machine m;
    uint8_t *data;
    uint32_t size, length;
    const uint8_t *chunk;
    uint8_t decoded[CODE];

    /* §4.3, §4.11: the three frames as the standard lays them out, the
     * first being the dummy frame for the top level. */
    static const uint8_t frames[] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x01, 0x01, 0x02, 0x02,
        0x00, 0x04, 0x05, 0x03, 0x10, 0x03, 0x00, 0x03,
        0x00, 0x0b, 0x00, 0x16, 0x00, 0x21, 0x03, 0x03, 0x04, 0x04, 0x05, 0x05,
        0x12, 0x34, 0x56, 0x10, 0x00, 0x00, 0x00, 0x00
    };

    /* §3.2: the end of dynamic memory as it codes: the global at $40,
     * 190 unchanged bytes, the byte at $100, 766 unchanged bytes in
     * three runs, and the byte at $3ff. */
    static const uint8_t memory_tail[] = {
        0x12, 0x34, 0x00, 0xbd, 0xaa, 0x00, 0xff, 0x00, 0xff, 0x00, 0xfd, 0x01
    };

    start(&mem, &m, 5, 0xbeef);
    play(&m);
    CHECK_EQ_INT(zm_quetzal_write(&m, 0x404, &data, &size), ZM_OK);

    /* §8.5, §2.1: one FORM of type IFZS whose length covers the rest. */
    CHECK(memcmp(data, "FORM", 4) == 0);
    CHECK_EQ_INT(get_long(data + 4), size - 8);
    CHECK(memcmp(data + 8, "IFZS", 4) == 0);

    /* §5.4: IFhd comes first, 13 bytes long and so padded (§8.4.1):
     * release, serial, checksum, PC. */
    CHECK(memcmp(data + 12, "IFhd", 4) == 0);
    CHECK_EQ_INT(get_long(data + 16), 13);
    CHECK(memcmp(data + 20, "\x00\x2a" "260101" "\xbe\xef" "\x00\x04\x04" "\x00", 14) == 0);
    CHECK(memcmp(data + 34, "CMem", 4) == 0);

    /* §3.2, §3.4: the memory chunk decodes to dynamic memory as it is,
     * and ends with the last change, the rest being left off. */
    chunk = find_chunk(data, size, "CMem", &length);
    CHECK(chunk != NULL);
    if (chunk != NULL) {
        decode_memory(chunk, length, mem.original, decoded, CODE);
        CHECK(memcmp(decoded, mem.bytes, CODE) == 0);
        CHECK(length >= sizeof memory_tail);
        CHECK(memcmp(chunk + length - sizeof memory_tail, memory_tail, sizeof memory_tail) == 0);
    }

    /* §4.10: the stacks, oldest frame first. */
    chunk = find_chunk(data, size, "Stks", &length);
    CHECK(chunk != NULL);
    CHECK_EQ_INT(length, sizeof frames);
    if (chunk != NULL && length == sizeof frames) {
        CHECK(memcmp(chunk, frames, sizeof frames) == 0);
    }

    free(data);
    stop(&mem, &m);
}

static void test_written_games_read_back(void)
{
    zm_memory mem, mem2;
    zm_machine m, m2;
    uint8_t *data;
    uint32_t size;

    start(&mem, &m, 5, 0xbeef);
    play(&m);

    /* Something for the Rst rule of Spec §6.1.2.2 to undo: a header
     * byte the interpreter owns, as another interpreter would have
     * written it; and a Flags 2 the file carries but must not impose
     * (Spec §6.1.2). */
    zm_write_byte(&mem, ZM_HEADER_INTERPRETER_NUMBER, 99);
    zm_write_word(&mem, ZM_HEADER_FLAGS2, ZM_FLAGS2_UNDO);
    CHECK_EQ_INT(zm_quetzal_write(&m, 0x404, &data, &size), ZM_OK);

    start(&mem2, &m2, 5, 0xbeef);
    zm_write_word(&mem2, ZM_HEADER_FLAGS2, ZM_FLAGS2_FORCE_FIXED);
    CHECK_EQ_INT(zm_quetzal_read(&m2, data, size), ZM_OK);

    /* The state of play is back: memory, stack, frames, and PC. */
    CHECK(memcmp(mem2.bytes + GLOBALS, mem.bytes + GLOBALS, CODE - GLOBALS) == 0);
    CHECK_EQ_INT(zm_read_word(&mem2, GLOBALS), 0x1234);
    CHECK_EQ_INT(m2.sp, 5);
    CHECK(memcmp(m2.stack, m.stack, 5 * sizeof m.stack[0]) == 0);
    CHECK_EQ_INT(m2.depth, 3);
    CHECK_EQ_INT(m2.frames[0].result_variable, -1);
    CHECK_EQ_INT(m2.frames[0].stack_base, 0);
    CHECK_EQ_INT(m2.frames[1].return_pc, 0x405);
    CHECK_EQ_INT(m2.frames[1].stack_base, 2);
    CHECK_EQ_INT(m2.frames[1].result_variable, 0x10);
    CHECK_EQ_INT(m2.frames[1].argument_count, 2);
    CHECK_EQ_INT(m2.frames[1].local_count, 3);
    CHECK_EQ_INT(m2.frames[1].locals[2], 33);
    CHECK_EQ_INT(m2.frames[2].return_pc, 0x123456);
    CHECK_EQ_INT(m2.frames[2].stack_base, 5);
    CHECK_EQ_INT(m2.frames[2].result_variable, -1);
    CHECK_EQ_INT(m2.frames[2].local_count, 0);
    CHECK_EQ_INT(m2.pc, 0x404);

    /* Spec §6.1.2: Flags 2 is the player's, not the file's. Spec
     * §6.1.2.2: the interpreter writes its own name again. */
    CHECK_EQ_INT(zm_read_word(&mem2, ZM_HEADER_FLAGS2), ZM_FLAGS2_FORCE_FIXED);
    CHECK_EQ_INT(zm_read_byte(&mem2, ZM_HEADER_INTERPRETER_NUMBER), 1);

    free(data);
    stop(&mem2, &m2);
    stop(&mem, &m);
}

static void test_plain_memory_and_unknown_chunks(void)
{
    zm_memory mem;
    zm_machine m;
    iff f;
    uint8_t memory[CODE];

    /* §4.3: a single frame with two stack words and one local, after
     * an IFhd and before a UMem dump of the whole of dynamic memory
     * (§3.8). Chunks the reader does not know (§7.2) are skipped, odd
     * lengths and all. */
    static const uint8_t header[] = { 0x00, 0x2a, '2', '6', '0', '1', '0', '1', 0xbe, 0xef, 0x00, 0x04, 0x01 };
    static const uint8_t frames[] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x05, 0x00, 0x01, 0x01, 0x01, 0x00, 0x02, 0x00, 0x07, 0x00, 0x08, 0x00, 0x09
    };

    start(&mem, &m, 5, 0xbeef);
    memcpy(memory, mem.bytes, CODE);
    memory[0x200] = 0x5a;

    iff_start(&f, "IFZS");
    iff_chunk(&f, "ANNO", "Hello", 5);
    iff_chunk(&f, "IFhd", header, sizeof header);
    iff_chunk(&f, "AUTH", "Jeff", 4);
    iff_chunk(&f, "UMem", memory, CODE);
    iff_chunk(&f, "Stks", frames, sizeof frames);
    iff_finish(&f);

    CHECK_EQ_INT(zm_quetzal_read(&m, f.bytes, f.size), ZM_OK);
    CHECK_EQ_INT(zm_read_byte(&mem, 0x200), 0x5a);
    CHECK_EQ_INT(m.pc, 0x401);
    CHECK_EQ_INT(m.depth, 2);
    CHECK_EQ_INT(m.sp, 2);
    CHECK_EQ_INT(m.stack[0], 8);
    CHECK_EQ_INT(m.stack[1], 9);
    CHECK_EQ_INT(m.frames[1].return_pc, 0x500);
    CHECK_EQ_INT(m.frames[1].local_count, 1);
    CHECK_EQ_INT(m.frames[1].locals[0], 7);
    CHECK_EQ_INT(m.frames[1].argument_count, 1);
    CHECK_EQ_INT(m.frames[1].result_variable, 1);
    CHECK_EQ_INT(m.frames[1].stack_base, 0);

    stop(&mem, &m);
}

/* Read a hand-built file into a fresh machine, expecting it refused with
 * a particular status and the machine left as it was. */
static void expect_refused(const iff *f, zm_status expected)
{
    zm_memory mem;
    zm_machine m;

    start(&mem, &m, 5, 0xbeef);
    zm_write_byte(&mem, 0x200, 0x11);
    CHECK_EQ_INT(zm_quetzal_read(&m, f->bytes, f->size), expected);
    CHECK_EQ_INT(m.pc, CODE);
    CHECK_EQ_INT(m.depth, 1);
    CHECK_EQ_INT(m.sp, 0);
    CHECK_EQ_INT(zm_read_byte(&mem, 0x200), 0x11);
    stop(&mem, &m);
}

static void test_refusals(void)
{
    iff f;
    uint8_t header[] = { 0x00, 0x2a, '2', '6', '0', '1', '0', '1', 0xbe, 0xef, 0x00, 0x04, 0x01 };
    static const uint8_t dummy_frame[] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    static const uint8_t no_change[] = { 0x00, 0x00 };
    uint8_t bad[CODE + 1];

    /* §2.1, §8.5: not an IFF file, or not an IFZS form. */
    iff_start(&f, "IFZS");
    iff_finish(&f);
    memcpy(f.bytes, "RIFF", 4);
    expect_refused(&f, ZM_ERROR_NOT_A_SAVE);
    iff_start(&f, "IFRS");
    iff_finish(&f);
    expect_refused(&f, ZM_ERROR_NOT_A_SAVE);

    /* §7.18: all three chunks are required. */
    iff_start(&f, "IFZS");
    iff_chunk(&f, "IFhd", header, sizeof header);
    iff_chunk(&f, "CMem", no_change, sizeof no_change);
    iff_finish(&f);
    expect_refused(&f, ZM_ERROR_BAD_SAVE);

    /* §5.4: IFhd must come before the others. */
    iff_start(&f, "IFZS");
    iff_chunk(&f, "CMem", no_change, sizeof no_change);
    iff_chunk(&f, "IFhd", header, sizeof header);
    iff_chunk(&f, "Stks", dummy_frame, sizeof dummy_frame);
    iff_finish(&f);
    expect_refused(&f, ZM_ERROR_BAD_SAVE);

    /* §5.3: a different release, serial, or checksum is another story. */
    header[1] = 0x2b;
    iff_start(&f, "IFZS");
    iff_chunk(&f, "IFhd", header, sizeof header);
    iff_chunk(&f, "CMem", no_change, sizeof no_change);
    iff_chunk(&f, "Stks", dummy_frame, sizeof dummy_frame);
    iff_finish(&f);
    expect_refused(&f, ZM_ERROR_WRONG_STORY);
    header[1] = 0x2a;
    header[9] = 0xf0;
    iff_start(&f, "IFZS");
    iff_chunk(&f, "IFhd", header, sizeof header);
    iff_chunk(&f, "CMem", no_change, sizeof no_change);
    iff_chunk(&f, "Stks", dummy_frame, sizeof dummy_frame);
    iff_finish(&f);
    expect_refused(&f, ZM_ERROR_WRONG_STORY);
    header[9] = 0xef;

    /* A chunk whose length runs past the end of the file. */
    iff_start(&f, "IFZS");
    iff_chunk(&f, "IFhd", header, sizeof header);
    iff_chunk(&f, "CMem", no_change, sizeof no_change);
    iff_chunk(&f, "Stks", dummy_frame, sizeof dummy_frame);
    iff_finish(&f);
    f.bytes[f.size - 9] = 0x7f;
    expect_refused(&f, ZM_ERROR_BAD_SAVE);

    /* §3.5: memory that ends in the middle of a run, or decodes to more
     * than there is. */
    iff_start(&f, "IFZS");
    iff_chunk(&f, "IFhd", header, sizeof header);
    iff_chunk(&f, "CMem", "\x00", 1);
    iff_chunk(&f, "Stks", dummy_frame, sizeof dummy_frame);
    iff_finish(&f);
    expect_refused(&f, ZM_ERROR_BAD_SAVE);
    iff_start(&f, "IFZS");
    iff_chunk(&f, "IFhd", header, sizeof header);
    iff_chunk(&f, "CMem", "\x00\xff\x00\xff\x00\xff\x00\xff\x00\xff", 10);
    iff_chunk(&f, "Stks", dummy_frame, sizeof dummy_frame);
    iff_finish(&f);
    expect_refused(&f, ZM_ERROR_BAD_SAVE);

    /* §3.6: a plain dump of the wrong length. */
    memset(bad, 0, sizeof bad);
    iff_start(&f, "IFZS");
    iff_chunk(&f, "IFhd", header, sizeof header);
    iff_chunk(&f, "UMem", bad, sizeof bad);
    iff_chunk(&f, "Stks", dummy_frame, sizeof dummy_frame);
    iff_finish(&f);
    expect_refused(&f, ZM_ERROR_BAD_SAVE);

    /* §4.8: more stack than this machine has; and a frame cut short. */
    {
        static const uint8_t too_much[] = { 0, 0, 0, 0, 0, 0, 0xff, 0xff };
        static const uint8_t cut_short[] = { 0, 0, 0, 0x02, 0, 0, 0, 0, 0, 1 };

        iff_start(&f, "IFZS");
        iff_chunk(&f, "IFhd", header, sizeof header);
        iff_chunk(&f, "CMem", no_change, sizeof no_change);
        iff_chunk(&f, "Stks", too_much, sizeof too_much);
        iff_finish(&f);
        expect_refused(&f, ZM_ERROR_BAD_SAVE);
        iff_start(&f, "IFZS");
        iff_chunk(&f, "IFhd", header, sizeof header);
        iff_chunk(&f, "CMem", no_change, sizeof no_change);
        iff_chunk(&f, "Stks", cut_short, sizeof cut_short);
        iff_finish(&f);
        expect_refused(&f, ZM_ERROR_BAD_SAVE);
    }

    /* A PC outside the story. */
    header[10] = 0x7f;
    iff_start(&f, "IFZS");
    iff_chunk(&f, "IFhd", header, sizeof header);
    iff_chunk(&f, "CMem", no_change, sizeof no_change);
    iff_chunk(&f, "Stks", dummy_frame, sizeof dummy_frame);
    iff_finish(&f);
    expect_refused(&f, ZM_ERROR_BAD_SAVE);
    header[10] = 0x00;
}

static void test_stories_without_a_checksum(void)
{
    zm_memory mem;
    zm_machine m;
    uint8_t *data;
    uint32_t size, length;
    const uint8_t *chunk;
    uint16_t computed;

    /* An early story has no checksum in its header. §5.5 would have
     * one computed; the file written here carries the header's zero, as
     * Frotz expects, and either is accepted on reading. */
    start(&mem, &m, 3, 0);
    computed = zm_header_compute_checksum(&mem);
    CHECK(computed != 0);
    CHECK_EQ_INT(zm_quetzal_write(&m, 0x401, &data, &size), ZM_OK);
    chunk = find_chunk(data, size, "IFhd", &length);
    CHECK(chunk != NULL);
    if (chunk != NULL) {
        uint8_t *checksum = data + (chunk - data) + 8;

        CHECK_EQ_INT(checksum[0], 0);
        CHECK_EQ_INT(checksum[1], 0);
        CHECK_EQ_INT(zm_quetzal_read(&m, data, size), ZM_OK);

        checksum[0] = (uint8_t)(computed >> 8);
        checksum[1] = (uint8_t)(computed & 0xff);
        CHECK_EQ_INT(zm_quetzal_read(&m, data, size), ZM_OK);

        checksum[0] = (uint8_t)(computed >> 8);
        checksum[1] = (uint8_t)((computed & 0xff) ^ 1);
        CHECK_EQ_INT(zm_quetzal_read(&m, data, size), ZM_ERROR_WRONG_STORY);
    }
    free(data);
    stop(&mem, &m);
}

static void test_version_6_main_routine_is_a_real_frame(void)
{
    zm_memory mem, mem2;
    zm_machine m, m2;
    uint8_t *data;
    uint32_t size, length;
    const uint8_t *chunk;

    /* §4.11 has the dummy frame everywhere but Version 6, where the
     * first frame is the main routine (Spec §5.4): its locals are
     * written, and its flags say its result is discarded. */
    start(&mem, &m, 6, 0xbeef);
    CHECK_EQ_INT(m.depth, 1);
    CHECK_EQ_INT(m.frames[0].local_count, 2);
    m.frames[0].locals[1] = 0x77;
    CHECK_EQ_INT(zm_quetzal_write(&m, m.pc, &data, &size), ZM_OK);
    chunk = find_chunk(data, size, "Stks", &length);
    CHECK(chunk != NULL);
    CHECK_EQ_INT(length, 12);
    if (chunk != NULL && length == 12) {
        CHECK(memcmp(chunk, "\x00\x00\x00\x12\x00\x00\x00\x00\x00\x00\x00\x77", 12) == 0);
    }

    start(&mem2, &m2, 6, 0xbeef);
    CHECK_EQ_INT(zm_quetzal_read(&m2, data, size), ZM_OK);
    CHECK_EQ_INT(m2.depth, 1);
    CHECK_EQ_INT(m2.frames[0].result_variable, -1);
    CHECK_EQ_INT(m2.frames[0].local_count, 2);
    CHECK_EQ_INT(m2.frames[0].locals[1], 0x77);
    CHECK_EQ_INT(m2.pc, MAIN + 1);

    free(data);
    stop(&mem2, &m2);
    stop(&mem, &m);
}

int main(void)
{
    RUN_TEST(test_file_layout);
    RUN_TEST(test_written_games_read_back);
    RUN_TEST(test_plain_memory_and_unknown_chunks);
    RUN_TEST(test_refusals);
    RUN_TEST(test_stories_without_a_checksum);
    RUN_TEST(test_version_6_main_routine_is_a_real_frame);
    return CHECK_SUMMARY();
}
