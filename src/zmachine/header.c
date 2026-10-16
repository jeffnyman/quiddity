/*
 * header.c: see zm/header.h. Implements Spec §11.
 */

#include "zm/header.h"

#include <string.h>

uint32_t zm_header_length_scale(uint8_t version)
{
    /* Spec §11.1.6. */
    if (version <= 3) {
        return 2;
    }
    if (version <= 5) {
        return 4;
    }
    return 8;
}

/* Copy a run of ASCII out of the header into a NUL-terminated buffer,
 * replacing anything unprintable so the result is always safe to print. */
static void read_ascii(const zm_memory *mem, uint32_t address, char *out, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++) {
        uint8_t c = zm_read_byte(mem, address + i);
        out[i] = (c >= 0x20 && c < 0x7f) ? (char)c : '.';
    }
    out[count] = '\0';
}

void zm_header_read(const zm_memory *mem, zm_header *h)
{
    memset(h, 0, sizeof *h);

    /* Fields present in every Version (Spec §11.1). */
    h->version     = zm_read_byte(mem, ZM_HEADER_VERSION);
    h->flags1      = zm_read_byte(mem, ZM_HEADER_FLAGS1);
    h->release     = zm_read_word(mem, ZM_HEADER_RELEASE);
    h->high_base   = zm_read_word(mem, ZM_HEADER_HIGH_BASE);
    h->initial_pc  = zm_read_word(mem, ZM_HEADER_INITIAL_PC);
    h->dictionary  = zm_read_word(mem, ZM_HEADER_DICTIONARY);
    h->objects     = zm_read_word(mem, ZM_HEADER_OBJECTS);
    h->globals     = zm_read_word(mem, ZM_HEADER_GLOBALS);
    h->static_base = zm_read_word(mem, ZM_HEADER_STATIC_BASE);
    h->flags2      = zm_read_word(mem, ZM_HEADER_FLAGS2);

    /* The serial code and abbreviations table are marked V2 in the table,
     * but Version 1 files carry the same bytes and nothing else lives
     * there, so they are read unconditionally. */
    read_ascii(mem, ZM_HEADER_SERIAL, h->serial, 6);
    h->abbreviations = zm_read_word(mem, ZM_HEADER_ABBREVIATIONS);

    /* Spec §11.1.6: the stored length is divided by a Version-dependent
     * constant. Early Version 3 files store 0, which stays 0. */
    h->file_length = zm_read_word(mem, ZM_HEADER_FILE_LENGTH) * zm_header_length_scale(h->version);
    h->checksum    = zm_read_word(mem, ZM_HEADER_CHECKSUM);

    h->standard_major = zm_read_byte(mem, ZM_HEADER_STANDARD_MAJOR);
    h->standard_minor = zm_read_byte(mem, ZM_HEADER_STANDARD_MINOR);
    read_ascii(mem, ZM_HEADER_INFORM_VERSION, h->inform_version, 4);

    if (h->version < 4) {
        return;
    }

    /* Version 4 and later. */
    h->interpreter_number  = zm_read_byte(mem, ZM_HEADER_INTERPRETER_NUMBER);
    h->interpreter_version = zm_read_byte(mem, ZM_HEADER_INTERPRETER_VERSION);
    h->screen_height       = zm_read_byte(mem, ZM_HEADER_SCREEN_HEIGHT);
    h->screen_width        = zm_read_byte(mem, ZM_HEADER_SCREEN_WIDTH);

    if (h->version < 5) {
        return;
    }

    /* Version 5 and later. */
    h->screen_width_units  = zm_read_word(mem, ZM_HEADER_SCREEN_WIDTH_UNITS);
    h->screen_height_units = zm_read_word(mem, ZM_HEADER_SCREEN_HEIGHT_UNITS);
    h->default_background  = zm_read_byte(mem, ZM_HEADER_DEFAULT_BACKGROUND);
    h->default_foreground  = zm_read_byte(mem, ZM_HEADER_DEFAULT_FOREGROUND);
    h->terminating_chars   = zm_read_word(mem, ZM_HEADER_TERMINATING_CHARS);
    h->alphabet_table      = zm_read_word(mem, ZM_HEADER_ALPHABET_TABLE);
    h->extension_table     = zm_read_word(mem, ZM_HEADER_EXTENSION_TABLE);

    /* Spec §11.1: bytes $26 and $27 swap meaning between Version 5 and
     * Version 6. In Version 5 (and 7 and 8, which follow it per §1.2.4)
     * $26 is the font width and $27 the height; in Version 6 it is the
     * other way round. */
    if (h->version == 6) {
        h->font_height_units = zm_read_byte(mem, ZM_HEADER_FONT_BYTE_26);
        h->font_width_units  = zm_read_byte(mem, ZM_HEADER_FONT_BYTE_27);
    } else {
        h->font_width_units  = zm_read_byte(mem, ZM_HEADER_FONT_BYTE_26);
        h->font_height_units = zm_read_byte(mem, ZM_HEADER_FONT_BYTE_27);
    }

    if (h->version == 6 || h->version == 7) {
        h->routines_offset = zm_read_word(mem, ZM_HEADER_ROUTINES_OFFSET);
        h->strings_offset  = zm_read_word(mem, ZM_HEADER_STRINGS_OFFSET);
    }

    if (h->version == 6) {
        h->stream3_width = zm_read_word(mem, ZM_HEADER_STREAM3_WIDTH);
    }
}

uint16_t zm_header_compute_checksum(const zm_memory *mem)
{
    uint32_t length;
    uint32_t sum = 0;

    /* Spec §15 (verify): the sum stops at the header's file length, not
     * the end of the file, because some story files are padded out to a
     * page boundary and the padding is not always zero. A header with no
     * length, as in early Version 3 files, means summing the whole file. */
    length = zm_read_word(mem, ZM_HEADER_FILE_LENGTH) * zm_header_length_scale(mem->version);
    if (length == 0 || length > mem->size) {
        length = mem->size;
    }

    /* The sum is of the file, so the dynamic part comes from the copy
     * kept at load time; the game may have written to memory since. */
    for (uint32_t address = ZM_HEADER_SIZE; address < length; address++) {
        sum += address < mem->static_base ? mem->original[address] : mem->bytes[address];
    }

    return (uint16_t)(sum & 0xffff);
}

uint16_t zm_header_extension_word(const zm_memory *mem, uint16_t index)
{
    uint32_t table;
    uint16_t count;

    /* Spec §11.1.7: the table lives only in Version 5 and later; before
     * that the bytes at $36 belong to no field and must not be read as
     * an address. */
    if (mem->version < 5) {
        return 0;
    }

    table = zm_read_word(mem, ZM_HEADER_EXTENSION_TABLE);
    if (table == 0) {
        return 0;
    }

    /* Spec §11.1.7: word 0 holds the number of words that follow. Word 0
     * itself is always readable; anything past the count is 0 by
     * §11.1.7.1. */
    count = zm_read_word(mem, table);
    if (index > count) {
        return 0;
    }

    return zm_read_word(mem, table + 2u * index);
}
