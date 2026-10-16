/*
 * zm/header.h: the story file header.
 *
 * Implements Spec §11. The header is the first 64 bytes of dynamic memory
 * (Spec §1.1.1.1). Some of its fields describe the story (where its tables
 * are, what Version it needs); others are written by the interpreter to
 * describe itself (what the screen can do). This file reads the whole
 * table into a struct so the rest of the interpreter can use names rather
 * than offsets. Writing the interpreter's own fields comes later, with the
 * screen model.
 *
 * Every offset below is taken from the table in Spec §11.1.
 */

#ifndef ZM_HEADER_H
#define ZM_HEADER_H

#include <stdint.h>

#include "zm/memory.h"

/* Byte offsets of header fields (Spec §11.1). */
enum {
    ZM_HEADER_VERSION            = 0x00, /* 1 byte,  V1 */
    ZM_HEADER_FLAGS1             = 0x01, /* 1 byte,  V1 */
    ZM_HEADER_RELEASE            = 0x02, /* word,    V1 */
    ZM_HEADER_HIGH_BASE          = 0x04, /* word,    V1 */
    ZM_HEADER_INITIAL_PC         = 0x06, /* word,    V1 (V6: packed main routine) */
    ZM_HEADER_DICTIONARY         = 0x08, /* word,    V1 */
    ZM_HEADER_OBJECTS            = 0x0a, /* word,    V1 */
    ZM_HEADER_GLOBALS            = 0x0c, /* word,    V1 */
    ZM_HEADER_STATIC_BASE        = 0x0e, /* word,    V1 */
    ZM_HEADER_FLAGS2             = 0x10, /* word,    V1 */
    ZM_HEADER_SERIAL             = 0x12, /* 6 bytes, V2 */
    ZM_HEADER_ABBREVIATIONS      = 0x18, /* word,    V2 */
    ZM_HEADER_FILE_LENGTH        = 0x1a, /* word,    V3+ (scaled, see §11.1.6) */
    ZM_HEADER_CHECKSUM           = 0x1c, /* word,    V3+ */
    ZM_HEADER_INTERPRETER_NUMBER = 0x1e, /* 1 byte,  V4 */
    ZM_HEADER_INTERPRETER_VERSION= 0x1f, /* 1 byte,  V4 */
    ZM_HEADER_SCREEN_HEIGHT      = 0x20, /* 1 byte,  V4, lines */
    ZM_HEADER_SCREEN_WIDTH       = 0x21, /* 1 byte,  V4, characters */
    ZM_HEADER_SCREEN_WIDTH_UNITS = 0x22, /* word,    V5 */
    ZM_HEADER_SCREEN_HEIGHT_UNITS= 0x24, /* word,    V5 */
    ZM_HEADER_FONT_BYTE_26       = 0x26, /* 1 byte,  V5: font width; V6: font height */
    ZM_HEADER_FONT_BYTE_27       = 0x27, /* 1 byte,  V5: font height; V6: font width */
    ZM_HEADER_ROUTINES_OFFSET    = 0x28, /* word,    V6, divided by 8 */
    ZM_HEADER_STRINGS_OFFSET     = 0x2a, /* word,    V6, divided by 8 */
    ZM_HEADER_DEFAULT_BACKGROUND = 0x2c, /* 1 byte,  V5 */
    ZM_HEADER_DEFAULT_FOREGROUND = 0x2d, /* 1 byte,  V5 */
    ZM_HEADER_TERMINATING_CHARS  = 0x2e, /* word,    V5 */
    ZM_HEADER_STREAM3_WIDTH      = 0x30, /* word,    V6 */
    ZM_HEADER_STANDARD_MAJOR     = 0x32, /* 1 byte,  V1 (see §11.1.5) */
    ZM_HEADER_STANDARD_MINOR     = 0x33, /* 1 byte,  V1 */
    ZM_HEADER_ALPHABET_TABLE     = 0x34, /* word,    V5 */
    ZM_HEADER_EXTENSION_TABLE    = 0x36, /* word,    V5 */
    ZM_HEADER_INFORM_VERSION     = 0x3c  /* 4 bytes, by convention (Inform 6) */
};

/* Flags 1 bits, Versions 1 to 3 (Spec §11.1.4). */
enum {
    ZM_FLAGS1_V3_STATUS_TIME      = 1 << 1, /* 0 = score/turns, 1 = hours:mins */
    ZM_FLAGS1_V3_SPLIT_FILE       = 1 << 2, /* story split across two discs */
    ZM_FLAGS1_V3_TANDY            = 1 << 3, /* the "Tandy" bit; see §11 Remarks */
    ZM_FLAGS1_V3_NO_STATUS_LINE   = 1 << 4,
    ZM_FLAGS1_V3_SCREEN_SPLITTING = 1 << 5,
    ZM_FLAGS1_V3_VARIABLE_PITCH   = 1 << 6
};

/* Flags 1 bits, Version 4 and later (Spec §11.1.4). */
enum {
    ZM_FLAGS1_COLOURS     = 1 << 0, /* V5 */
    ZM_FLAGS1_PICTURES    = 1 << 1, /* V6 */
    ZM_FLAGS1_BOLD        = 1 << 2, /* V4 */
    ZM_FLAGS1_ITALIC      = 1 << 3, /* V4 */
    ZM_FLAGS1_FIXED_PITCH = 1 << 4, /* V4 */
    ZM_FLAGS1_SOUND       = 1 << 5, /* V6 */
    ZM_FLAGS1_TIMED_INPUT = 1 << 7  /* V4 */
};

/* Flags 2 bits (Spec §11.1.2). Bit 0 is bit 0 of the second byte of the
 * word, so these are simply bit positions within the 16-bit value. */
enum {
    ZM_FLAGS2_TRANSCRIPT   = 1 << 0, /* V1 */
    ZM_FLAGS2_FORCE_FIXED  = 1 << 1, /* V3 */
    ZM_FLAGS2_REDRAW       = 1 << 2, /* V6 */
    ZM_FLAGS2_PICTURES     = 1 << 3, /* V5 */
    ZM_FLAGS2_UNDO         = 1 << 4, /* V5 */
    ZM_FLAGS2_MOUSE        = 1 << 5, /* V5 */
    ZM_FLAGS2_COLOURS      = 1 << 6, /* V5 */
    ZM_FLAGS2_SOUND        = 1 << 7, /* V5 */
    ZM_FLAGS2_MENUS        = 1 << 8  /* V6 */
};

/* Words of the header extension table (Spec §11.1.7.3). */
enum {
    ZM_HEADER_EXT_COUNT         = 0,
    ZM_HEADER_EXT_MOUSE_X       = 1,
    ZM_HEADER_EXT_MOUSE_Y       = 2,
    ZM_HEADER_EXT_UNICODE_TABLE = 3,
    ZM_HEADER_EXT_FLAGS3        = 4,
    ZM_HEADER_EXT_TRUE_FOREGROUND = 5,
    ZM_HEADER_EXT_TRUE_BACKGROUND = 6
};

typedef struct zm_header {
    uint8_t version;
    uint8_t flags1;
    uint16_t release;
    uint16_t high_base;

    /* Versions 1 to 5: byte address of the first instruction to execute.
     * Version 6: packed address of the "main" routine to call (Spec §11.1,
     * §5.5). */
    uint16_t initial_pc;

    uint16_t dictionary;
    uint16_t objects;
    uint16_t globals;
    uint16_t static_base;
    uint16_t flags2;

    /* Six ASCII characters plus a terminator. Infocom and Inform use the
     * compilation date as YYMMDD. */
    char serial[7];

    uint16_t abbreviations;

    /* Length of the story in bytes, already multiplied by the Version's
     * constant (Spec §11.1.6). Zero in early Version 3 files that have
     * no length. */
    uint32_t file_length;

    /* Spec §11.1: sum of all bytes from $40 to the end of the file, modulo
     * $10000 (the rule is given under verify, Spec §15). Zero in early
     * Version 3 files. */
    uint16_t checksum;

    /* Version 4 and later. */
    uint8_t interpreter_number;
    uint8_t interpreter_version;
    uint8_t screen_height;        /* lines; 255 means "infinite" */
    uint8_t screen_width;         /* characters */

    /* Version 5 and later. */
    uint16_t screen_width_units;
    uint16_t screen_height_units;
    uint8_t font_width_units;     /* the width of a '0' */
    uint8_t font_height_units;
    uint8_t default_background;
    uint8_t default_foreground;
    uint16_t terminating_chars;   /* byte address of the table */
    uint16_t alphabet_table;      /* byte address, or 0 for the default */
    uint16_t extension_table;     /* byte address, or 0 if absent */

    /* Versions 6 and 7 (Spec §1.2.3), stored divided by 8. */
    uint16_t routines_offset;
    uint16_t strings_offset;

    /* Version 6. */
    uint16_t stream3_width;

    /* Spec §11.1.5: the revision of the Standard the interpreter obeys. */
    uint8_t standard_major;
    uint8_t standard_minor;

    /* Four ASCII characters plus a terminator; "6.11" and the like for
     * Inform 6 stories, otherwise whatever happened to be there. */
    char inform_version[5];
} zm_header;

/* Decode the header out of memory. */
void zm_header_read(const zm_memory *mem, zm_header *header);

/* The constant by which the file length at $1a is divided (Spec §11.1.6). */
uint32_t zm_header_length_scale(uint8_t version);

/* Compute the checksum the header should hold: every byte from $40 up to
 * the header's file length, modulo $10000 (Spec §15, verify). If the header
 * gives no length, the sum runs to the end of the file. */
uint16_t zm_header_compute_checksum(const zm_memory *mem);

/* Read a word from the header extension table (Spec §11.1.7). Returns 0 if
 * the table is absent or shorter than the index (Spec §11.1.7.1). */
uint16_t zm_header_extension_word(const zm_memory *mem, uint16_t index);

#endif /* ZM_HEADER_H */
