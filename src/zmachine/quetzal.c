/*
 * quetzal.c: see zm/quetzal.h. Section numbers below are Quetzal's own
 * unless marked Spec.
 */

#include "zm/quetzal.h"

#include <stdlib.h>
#include <string.h>

#include "zm/header.h"

/* §5.4: the fixed part of an IFhd chunk. */
#define IFHD_LENGTH 13u

/* §4.3: the fixed part of a stack frame, before its locals and stack
 * words. */
#define FRAME_HEADER 8u

/*
 * Writing.
 */

/* A byte buffer that grows as the file is built. Once an allocation
 * fails the buffer stops accepting bytes and remembers that it did, so
 * the writer can finish and report one failure at the end. */
typedef struct buffer {
    uint8_t *bytes;
    uint32_t size;
    uint32_t capacity;
    int failed;
} buffer;

static void put_bytes(buffer *b, const void *data, uint32_t count)
{
    if (b->failed) {
        return;
    }
    if (count > b->capacity - b->size) {
        uint32_t capacity = b->capacity ? b->capacity * 2 : 4096;
        uint8_t *bytes;

        while (capacity - b->size < count) {
            capacity *= 2;
        }
        bytes = realloc(b->bytes, capacity);
        if (bytes == NULL) {
            b->failed = 1;
            return;
        }
        b->bytes = bytes;
        b->capacity = capacity;
    }
    memcpy(b->bytes + b->size, data, count);
    b->size += count;
}

static void put_byte(buffer *b, uint8_t value)
{
    put_bytes(b, &value, 1);
}

/* §1.4: every number is big-endian, as in the Z-machine itself. */
static void put_word(buffer *b, uint16_t value)
{
    put_byte(b, (uint8_t)(value >> 8));
    put_byte(b, (uint8_t)(value & 0xff));
}

static void put_long(buffer *b, uint32_t value)
{
    put_word(b, (uint16_t)(value >> 16));
    put_word(b, (uint16_t)(value & 0xffff));
}

static void put_long_at(buffer *b, uint32_t offset, uint32_t value)
{
    if (b->failed) {
        return;
    }
    b->bytes[offset] = (uint8_t)(value >> 24);
    b->bytes[offset + 1] = (uint8_t)(value >> 16);
    b->bytes[offset + 2] = (uint8_t)(value >> 8);
    b->bytes[offset + 3] = (uint8_t)(value & 0xff);
}

/* §8.3: a chunk is a four-character ID, a length, and the data. The
 * length is not known until the data has been written, so begin_chunk
 * leaves a hole and returns where it is for end_chunk to fill. */
static uint32_t begin_chunk(buffer *b, const char *id)
{
    put_bytes(b, id, 4);
    put_long(b, 0);
    return b->size - 4;
}

static void end_chunk(buffer *b, uint32_t length_at)
{
    uint32_t length = b->size - (length_at + 4);

    put_long_at(b, length_at, length);

    /* §8.4.1: an odd-length chunk is followed by a pad byte that is not
     * counted in its length. */
    if (length & 1) {
        put_byte(b, 0);
    }
}

/* §3.2: dynamic memory is exclusive-ored with the original from the
 * story file and the result run-length coded: a non-zero byte stands for
 * itself, a zero byte and a count n for n + 1 zeros. Since most of
 * dynamic memory never changes, most of it codes as a few runs. §3.4: a
 * run at the very end can be left off, and is. */
static void write_memory(buffer *b, const zm_memory *mem)
{
    uint32_t end = mem->static_base;
    uint32_t i = 0;

    while (end > 0 && mem->bytes[end - 1] == mem->original[end - 1]) {
        end--;
    }
    while (i < end) {
        uint8_t difference = mem->bytes[i] ^ mem->original[i];

        if (difference != 0) {
            put_byte(b, difference);
            i++;
        } else {
            uint32_t run = 0;

            while (i + run < end && run < 256 && mem->bytes[i + run] == mem->original[i + run]) {
                run++;
            }
            put_byte(b, 0);
            put_byte(b, (uint8_t)(run - 1));
            i += run;
        }
    }
}

/* §4.3: each frame is the return PC in three bytes, a flags byte with
 * the local count and a bit for "result discarded", the result variable,
 * a byte with one bit per argument supplied, the number of stack words
 * the frame owns, the locals, and those stack words. §4.2, §4.7: oldest
 * frame first, oldest stack word first. */
static void write_frames(buffer *b, const zm_machine *m)
{
    for (uint32_t i = 0; i < m->depth; i++) {
        const zm_frame *f = &m->frames[i];
        uint32_t stack_end = i + 1 < m->depth ? m->frames[i + 1].stack_base : m->sp;
        uint32_t words = stack_end - f->stack_base;
        unsigned arguments = f->argument_count > 7 ? 7 : f->argument_count;
        int discards = f->result_variable < 0;
        int variable = discards ? 0 : f->result_variable;

        /* §4.11: outside Version 6 the first frame is a dummy standing
         * for the code that runs before any routine is called, with
         * every field zero but the stack count. That is what frame 0 of
         * this machine is, except that its "no result" marker must not
         * be written as the discard flag. */
        if (i == 0 && m->mem->version != 6) {
            discards = 0;
        }

        put_byte(b, (uint8_t)(f->return_pc >> 16));
        put_word(b, (uint16_t)(f->return_pc & 0xffff));
        put_byte(b, (uint8_t)((discards ? 0x10 : 0) | f->local_count));
        put_byte(b, (uint8_t)variable);
        put_byte(b, (uint8_t)((1u << arguments) - 1));
        put_word(b, (uint16_t)words);
        for (unsigned k = 0; k < f->local_count; k++) {
            put_word(b, f->locals[k]);
        }
        for (uint32_t k = 0; k < words; k++) {
            put_word(b, m->stack[f->stack_base + k]);
        }
    }
}

zm_status zm_quetzal_write(const zm_machine *m, uint32_t pc, uint8_t **data, uint32_t *size)
{
    const zm_memory *mem = m->mem;
    buffer b = { 0 };
    uint32_t chunk;

    /* §8.5: one FORM chunk holds everything, its sub-ID IFZS (§2.1). */
    put_bytes(&b, "FORM", 4);
    put_long(&b, 0);
    put_bytes(&b, "IFZS", 4);

    /* §5.4: the story's release, serial, and checksum as the header gives
     * them, then the PC. The checksum is written as the header holds it,
     * zero included: §5.5 would have one computed for an early story
     * that has none, but Frotz compares against the header word, and a
     * file it refuses would defeat the purpose of a common format. */
    chunk = begin_chunk(&b, "IFhd");
    put_bytes(&b, mem->original + ZM_HEADER_RELEASE, 2);
    put_bytes(&b, mem->original + ZM_HEADER_SERIAL, 6);
    put_bytes(&b, mem->original + ZM_HEADER_CHECKSUM, 2);
    put_byte(&b, (uint8_t)(pc >> 16));
    put_word(&b, (uint16_t)(pc & 0xffff));
    end_chunk(&b, chunk);

    /* §3.7, §4.10: the memory and the stacks. */
    chunk = begin_chunk(&b, "CMem");
    write_memory(&b, mem);
    end_chunk(&b, chunk);

    chunk = begin_chunk(&b, "Stks");
    write_frames(&b, m);
    end_chunk(&b, chunk);

    /* The FORM's length counts everything after its own header. */
    put_long_at(&b, 4, b.size - 8);

    if (b.failed) {
        free(b.bytes);
        return ZM_ERROR_OUT_OF_MEMORY;
    }
    *data = b.bytes;
    *size = b.size;
    return ZM_OK;
}

/*
 * Reading.
 */

static uint32_t get_word(const uint8_t *p)
{
    return (uint32_t)((p[0] << 8) | p[1]);
}

static uint32_t get_long(const uint8_t *p)
{
    return (get_word(p) << 16) | get_word(p + 2);
}

static int has_id(const uint8_t *p, const char *id)
{
    return memcmp(p, id, 4) == 0;
}

/* §5.3, §5.4: the file belongs to this story if its release, serial, and
 * checksum match the header's. A story too early to carry a checksum has
 * zero there; a file from an interpreter that followed §5.5 and computed
 * one is accepted for such a story too. */
static zm_status read_header(const zm_memory *mem, const uint8_t *p, uint32_t length, uint32_t *pc)
{
    uint32_t checksum;

    if (length < IFHD_LENGTH) {
        return ZM_ERROR_BAD_SAVE;
    }
    if (memcmp(p, mem->original + ZM_HEADER_RELEASE, 2) != 0
        || memcmp(p + 2, mem->original + ZM_HEADER_SERIAL, 6) != 0) {
        return ZM_ERROR_WRONG_STORY;
    }
    checksum = get_word(mem->original + ZM_HEADER_CHECKSUM);
    if (get_word(p + 8) != checksum
        && !(checksum == 0 && get_word(p + 8) == zm_header_compute_checksum(mem))) {
        return ZM_ERROR_WRONG_STORY;
    }
    *pc = ((uint32_t)p[10] << 16) | get_word(p + 11);
    if (*pc >= mem->size) {
        return ZM_ERROR_BAD_SAVE;
    }
    return ZM_OK;
}

/* §3.2 to §3.5: undo the run-length coding and the exclusive-or. Data
 * that runs past the end of dynamic memory, or ends in the middle of a
 * run, is taken as damage. */
static zm_status read_compressed_memory(const zm_memory *mem, const uint8_t *p, uint32_t length, uint8_t *out)
{
    uint32_t i = 0;
    uint32_t at = 0;

    memcpy(out, mem->original, mem->static_base);
    while (at < length) {
        uint8_t byte = p[at++];

        if (byte != 0) {
            if (i >= mem->static_base) {
                return ZM_ERROR_BAD_SAVE;
            }
            out[i] ^= byte;
            i++;
        } else {
            if (at >= length) {
                return ZM_ERROR_BAD_SAVE;
            }
            i += (uint32_t)p[at++] + 1;
            if (i > mem->static_base) {
                return ZM_ERROR_BAD_SAVE;
            }
        }
    }
    return ZM_OK;
}

/* §3.6, §3.8: a plain dump, which must be exactly dynamic memory. */
static zm_status read_plain_memory(const zm_memory *mem, const uint8_t *p, uint32_t length, uint8_t *out)
{
    if (length != mem->static_base) {
        return ZM_ERROR_BAD_SAVE;
    }
    memcpy(out, p, length);
    return ZM_OK;
}

/* §4.3 to §4.11: rebuild the frames and the stack. The frame pointers
 * this machine keeps are not in the file (§4.7, §4.9); they come from
 * adding up the stack words each frame owns. */
static zm_status read_frames(const zm_memory *mem, const uint8_t *p, uint32_t length, zm_snapshot *snapshot)
{
    uint32_t at = 0;

    snapshot->depth = 0;
    snapshot->sp = 0;
    while (at < length) {
        zm_frame *f;
        unsigned flags, arguments, words;

        if (length - at < FRAME_HEADER || snapshot->depth >= ZM_MAX_FRAMES) {
            return ZM_ERROR_BAD_SAVE;
        }
        f = &snapshot->frames[snapshot->depth];
        memset(f, 0, sizeof *f);
        f->return_pc = ((uint32_t)p[at] << 16) | get_word(p + at + 1);
        flags = p[at + 3];
        f->result_variable = (flags & 0x10) ? -1 : p[at + 4];
        arguments = p[at + 5];
        words = get_word(p + at + 6);
        at += FRAME_HEADER;

        /* §4.3.2: the low four bits are the local count, so it cannot
         * exceed 15 (Spec §5.2); the top three bits are always zero. */
        if (flags & 0xe0) {
            return ZM_ERROR_BAD_SAVE;
        }
        f->local_count = (uint8_t)(flags & 0x0f);

        /* §4.3.4, §4.7: one bit per argument, a to g. */
        while (arguments & 1) {
            f->argument_count++;
            arguments >>= 1;
        }

        if (length - at < 2u * (f->local_count + words)) {
            return ZM_ERROR_BAD_SAVE;
        }
        for (unsigned k = 0; k < f->local_count; k++) {
            f->locals[k] = (uint16_t)get_word(p + at);
            at += 2;
        }

        /* §4.8: a stack larger than this machine's is an error it must
         * trap rather than overrun. */
        if (words > ZM_STACK_WORDS - snapshot->sp) {
            return ZM_ERROR_BAD_SAVE;
        }
        f->stack_base = snapshot->sp;
        for (unsigned k = 0; k < words; k++) {
            snapshot->stack[snapshot->sp++] = (uint16_t)get_word(p + at);
            at += 2;
        }

        /* §4.11: the dummy frame that starts every non-Version 6 file
         * is this machine's frame 0, whose result variable is always
         * "none" (Spec §5.5). */
        if (snapshot->depth == 0 && mem->version != 6) {
            f->result_variable = -1;
        }
        snapshot->depth++;
    }
    if (snapshot->depth == 0) {
        return ZM_ERROR_BAD_SAVE;
    }
    return ZM_OK;
}

zm_status zm_quetzal_read(zm_machine *m, const uint8_t *data, uint32_t size)
{
    const zm_memory *mem = m->mem;
    zm_snapshot snapshot = { 0 };
    zm_status status = ZM_OK;
    uint32_t form_length;
    uint32_t total;
    uint32_t at;
    int have_header = 0, have_memory = 0, have_stacks = 0;

    /* §8.5, §2.1: a single FORM of type IFZS. */
    if (size < 12 || !has_id(data, "FORM") || !has_id(data + 8, "IFZS")) {
        return ZM_ERROR_NOT_A_SAVE;
    }
    form_length = get_long(data + 4);
    if (form_length < 4 || form_length - 4 > size - 12) {
        return ZM_ERROR_BAD_SAVE;
    }
    total = 8 + form_length;

    /* Everything is unpacked into a snapshot first, so that a file that
     * turns out to be damaged half-way through leaves the game alone. */
    snapshot.memory = malloc(mem->static_base);
    snapshot.stack = malloc(ZM_STACK_WORDS * sizeof *snapshot.stack);
    snapshot.frames = malloc(ZM_MAX_FRAMES * sizeof *snapshot.frames);
    if (snapshot.memory == NULL || snapshot.stack == NULL || snapshot.frames == NULL) {
        zm_snapshot_free(&snapshot);
        return ZM_ERROR_OUT_OF_MEMORY;
    }

    /* §8.6: the chunks follow the sub-ID, each padded to an even length
     * (§8.4.1). Chunks this interpreter does not know, such as AUTH and
     * ANNO (§7.2) or another interpreter's IntD (§7.8), are skipped. */
    at = 12;
    while (status == ZM_OK && at < total && total - at >= 8) {
        const uint8_t *id = data + at;
        const uint8_t *body = data + at + 8;
        uint32_t length = get_long(data + at + 4);

        if (length > total - (at + 8)) {
            status = ZM_ERROR_BAD_SAVE;
            break;
        }

        if (has_id(id, "IFhd")) {
            if (have_header) {
                status = ZM_ERROR_BAD_SAVE;
            } else {
                status = read_header(mem, body, length, &snapshot.pc);
                have_header = 1;
            }
        } else if (has_id(id, "CMem") || has_id(id, "UMem")) {
            /* §5.4: IFhd comes before the memory and stacks, so that a
             * file for the wrong story is refused before any decoding. */
            if (!have_header || have_memory) {
                status = ZM_ERROR_BAD_SAVE;
            } else if (id[0] == 'C') {
                status = read_compressed_memory(mem, body, length, snapshot.memory);
            } else {
                status = read_plain_memory(mem, body, length, snapshot.memory);
            }
            have_memory = 1;
        } else if (has_id(id, "Stks")) {
            if (!have_header || have_stacks) {
                status = ZM_ERROR_BAD_SAVE;
            } else {
                status = read_frames(mem, body, length, &snapshot);
            }
            have_stacks = 1;
        }

        at += 8 + length + (length & 1);
    }

    /* §7.18: IFhd, one memory chunk, and Stks are all required. */
    if (status == ZM_OK && !(have_header && have_memory && have_stacks)) {
        status = ZM_ERROR_BAD_SAVE;
    }

    if (status == ZM_OK) {
        zm_machine_apply_snapshot(m, &snapshot);
    }
    zm_snapshot_free(&snapshot);
    return status;
}
