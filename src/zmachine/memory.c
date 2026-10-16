/*
 * memory.c: see zm/memory.h. Implements Spec §1 and §2.1.
 */

#include "zm/memory.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zm/fatal.h"

/* Header offsets this layer needs in order to lay out the memory map. The
 * full header is described by zm/header.h; these three fields are
 * duplicated here because the map itself is defined in terms of them
 * (Spec §1.1) and the memory layer cannot depend on the header layer. */
#define HEADER_VERSION      0x00u
#define HEADER_HIGH_BASE    0x04u
#define HEADER_STATIC_BASE  0x0eu

/* Spec §1.2.3: the routine and string offsets used by Versions 6 and 7,
 * stored in the header divided by 8. */
#define HEADER_ROUTINES_OFFSET  0x28u
#define HEADER_STRINGS_OFFSET   0x2au

uint32_t zm_memory_max_size(uint8_t version)
{
    /* Spec §1.1.4. Each limit is what a 16-bit packed address can reach
     * once scaled by the Version's factor; see the note on §1.2.3. */
    switch (version) {
    case 1: case 2: case 3:
        return 128u * 1024u;
    case 4: case 5:
        return 256u * 1024u;
    case 6: case 7: case 8:
        return 512u * 1024u;
    default:
        return 0;
    }
}

/* Is this the start of a Blorb file? Blorb is an IFF container: the file
 * begins "FORM", then a 4-byte length, then the form type "IFRS". The
 * story inside has not been unwrapped yet; we only recognise the wrapper
 * so the error can say what the file is. */
static int looks_like_blorb(const uint8_t *bytes, uint32_t size)
{
    return size >= 12
        && memcmp(bytes, "FORM", 4) == 0
        && memcmp(bytes + 8, "IFRS", 4) == 0;
}

/* Check that a buffer can be a story file and fill in the memory map.
 * Shared by both load functions so they cannot disagree. */
static zm_status establish(zm_memory *mem, uint8_t *bytes, uint32_t size)
{
    uint8_t version;
    uint32_t static_base;

    if (looks_like_blorb(bytes, size)) {
        return ZM_ERROR_BLORB_UNSUPPORTED;
    }

    /* Spec §1.1: dynamic memory, and so the file, holds at least the
     * header. */
    if (size < ZM_HEADER_SIZE) {
        return ZM_ERROR_TOO_SMALL;
    }

    /* Spec §11.1: the Version number is 1 to 8. */
    version = bytes[HEADER_VERSION];
    if (version < 1 || version > 8) {
        return ZM_ERROR_BAD_VERSION;
    }

    /* Spec §1.1.4. */
    if (size > zm_memory_max_size(version)) {
        return ZM_ERROR_TOO_LARGE;
    }

    /* Spec §1.1: dynamic memory runs from 0 to the byte before the static
     * base, and must contain at least 64 bytes. It cannot extend past the
     * end of the file either, or the game would be reading and writing
     * memory the story never defined. */
    static_base = (uint32_t)((bytes[HEADER_STATIC_BASE] << 8) | bytes[HEADER_STATIC_BASE + 1]);
    if (static_base < ZM_HEADER_SIZE || static_base > size) {
        return ZM_ERROR_BAD_MEMORY_MAP;
    }

    mem->bytes = bytes;
    mem->size = size;
    mem->version = version;
    mem->static_base = static_base;
    mem->high_base = (uint32_t)((bytes[HEADER_HIGH_BASE] << 8) | bytes[HEADER_HIGH_BASE + 1]);

    return ZM_OK;
}

zm_status zm_memory_load_bytes(zm_memory *mem, const uint8_t *bytes, uint32_t size)
{
    uint8_t *copy;
    zm_status status;

    memset(mem, 0, sizeof *mem);

    /* Allocate at least one byte so that a zero-length input still yields
     * a valid pointer and falls through to the size check. */
    copy = malloc(size > 0 ? size : 1);
    if (copy == NULL) {
        return ZM_ERROR_OUT_OF_MEMORY;
    }
    memcpy(copy, bytes, size);

    status = establish(mem, copy, size);
    if (status != ZM_OK) {
        free(copy);
    }
    return status;
}

zm_status zm_memory_load_file(zm_memory *mem, const char *path)
{
    FILE *file;
    long length;
    uint8_t *bytes;
    size_t bytes_read;
    zm_status status;

    memset(mem, 0, sizeof *mem);

    file = fopen(path, "rb");
    if (file == NULL) {
        return ZM_ERROR_IO;
    }

    /* Measure the file by seeking to its end. Story files are small
     * (Spec §1.1.4 caps them at 512K) so a long is always enough. */
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return ZM_ERROR_IO;
    }
    length = ftell(file);
    if (length < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return ZM_ERROR_IO;
    }

    /* Refuse anything beyond the largest limit of any Version before
     * allocating for it; establish() applies the per-Version limit. */
    if ((unsigned long)length > zm_memory_max_size(8)) {
        fclose(file);
        return ZM_ERROR_TOO_LARGE;
    }

    bytes = malloc(length > 0 ? (size_t)length : 1);
    if (bytes == NULL) {
        fclose(file);
        return ZM_ERROR_OUT_OF_MEMORY;
    }

    bytes_read = fread(bytes, 1, (size_t)length, file);
    fclose(file);
    if (bytes_read != (size_t)length) {
        free(bytes);
        return ZM_ERROR_IO;
    }

    status = establish(mem, bytes, (uint32_t)length);
    if (status != ZM_OK) {
        free(bytes);
    }
    return status;
}

void zm_memory_free(zm_memory *mem)
{
    free(mem->bytes);
    memset(mem, 0, sizeof *mem);
}

/* Fault unless [address, address + length) lies inside memory. Written to
 * avoid overflow: address + length could wrap for a corrupt address. */
static void require_readable(const zm_memory *mem, uint32_t address, uint32_t length)
{
    if (address >= mem->size || length > mem->size - address) {
        zm_fatal("read of %u byte(s) at $%05x is outside memory (size $%05x)",
                 (unsigned)length, (unsigned)address, (unsigned)mem->size);
    }
}

/* Fault unless [address, address + length) lies inside dynamic memory
 * (Spec §1.1.1, §1.1.2). */
static void require_writable(const zm_memory *mem, uint32_t address, uint32_t length)
{
    if (address >= mem->static_base || length > mem->static_base - address) {
        zm_fatal("write of %u byte(s) at $%05x is outside dynamic memory (static base $%05x)",
                 (unsigned)length, (unsigned)address, (unsigned)mem->static_base);
    }
}

uint8_t zm_read_byte(const zm_memory *mem, uint32_t address)
{
    require_readable(mem, address, 1);
    return mem->bytes[address];
}

uint16_t zm_read_word(const zm_memory *mem, uint32_t address)
{
    /* Spec §2.1: a word is stored high byte first. */
    require_readable(mem, address, 2);
    return (uint16_t)((mem->bytes[address] << 8) | mem->bytes[address + 1]);
}

void zm_write_byte(zm_memory *mem, uint32_t address, uint8_t value)
{
    require_writable(mem, address, 1);
    mem->bytes[address] = value;
}

void zm_write_word(zm_memory *mem, uint32_t address, uint16_t value)
{
    require_writable(mem, address, 2);
    mem->bytes[address] = (uint8_t)(value >> 8);
    mem->bytes[address + 1] = (uint8_t)(value & 0xff);
}

/* Spec §1.2.3. The offset words at $28 and $2a are stored divided by 8,
 * so they are multiplied back up here. */
static uint32_t unpack(const zm_memory *mem, uint16_t packed, uint32_t offset_field)
{
    switch (mem->version) {
    case 1: case 2: case 3:
        return 2u * packed;
    case 4: case 5:
        return 4u * packed;
    case 6: case 7:
        return 4u * packed + 8u * zm_read_word(mem, offset_field);
    case 8:
        return 8u * packed;
    default:
        zm_fatal("cannot unpack an address for Version %u", (unsigned)mem->version);
    }
}

uint32_t zm_unpack_routine(const zm_memory *mem, uint16_t packed)
{
    return unpack(mem, packed, HEADER_ROUTINES_OFFSET);
}

uint32_t zm_unpack_string(const zm_memory *mem, uint16_t packed)
{
    return unpack(mem, packed, HEADER_STRINGS_OFFSET);
}
