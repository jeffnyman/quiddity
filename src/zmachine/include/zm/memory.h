/*
 * zm/memory.h: the Z-machine's memory and the three kinds of address.
 *
 * Implements Spec §1 (the memory map) and the byte-order rule of §2.1. A
 * story file is loaded whole into a byte array; everything the machine
 * does afterwards is reads and writes against that array, so this is the
 * lowest layer of the interpreter and the one every other part depends on.
 *
 * Memory is divided into three regions (Spec §1.1):
 *
 *     $00000 ........... dynamic memory, readable and writable
 *     static_base ...... static memory, read-only to the game
 *     high_base ........ high memory, holds routines and strings
 *     size ............. end of the story file
 *
 * The first 64 bytes of dynamic memory are the header (Spec §1.1.1.1),
 * which zm/header.h knows how to read.
 */

#ifndef ZM_MEMORY_H
#define ZM_MEMORY_H

#include <stdint.h>

#include "zm/status.h"

/* Spec §1.1: dynamic memory must contain at least the 64-byte header. */
#define ZM_HEADER_SIZE 64u

typedef struct zm_memory {
    uint8_t *bytes;

    /* Number of bytes loaded: the length of the story file on disk. */
    uint32_t size;

    /* Byte address where static memory begins, from the header word at
     * $0e (Spec §1.1). Everything below it is dynamic memory. */
    uint32_t static_base;

    /* Byte address of the high memory mark, from the header word at $04
     * (Spec §1.1). */
    uint32_t high_base;

    /* The Version number from header byte $00, kept here because address
     * unpacking (Spec §1.2.3) depends on it. */
    uint8_t version;
} zm_memory;

/* Load a story file. On success the memory is ready to use and must later
 * be released with zm_memory_free(). On failure nothing is allocated and
 * the status says why; for ZM_ERROR_IO, errno holds the system's reason. */
zm_status zm_memory_load_file(zm_memory *mem, const char *path);

/* Load a story from a byte array, copying it. Applies the same checks as
 * zm_memory_load_file(); this is the route tests use to build stories in
 * memory. */
zm_status zm_memory_load_bytes(zm_memory *mem, const uint8_t *bytes, uint32_t size);

/* Release memory obtained from either load function. Safe to call on a
 * zm_memory that failed to load, or more than once. */
void zm_memory_free(zm_memory *mem);

/* The largest story file a Version permits, in bytes (Spec §1.1.4). */
uint32_t zm_memory_max_size(uint8_t version);

/* Reads. A word is two bytes, high byte first (Spec §2.1). An address
 * outside memory is a fault and goes to zm_fatal(). */
uint8_t zm_read_byte(const zm_memory *mem, uint32_t address);
uint16_t zm_read_word(const zm_memory *mem, uint32_t address);

/* Writes. Only dynamic memory may be written (Spec §1.1.1, §1.1.2); an
 * address at or above static_base is a fault. */
void zm_write_byte(zm_memory *mem, uint32_t address, uint8_t value);
void zm_write_word(zm_memory *mem, uint32_t address, uint16_t value);

/* Convert a packed address to a byte address (Spec §1.2.3). Routines and
 * strings unpack differently in Versions 6 and 7, which is why there are
 * two functions. */
uint32_t zm_unpack_routine(const zm_memory *mem, uint16_t packed);
uint32_t zm_unpack_string(const zm_memory *mem, uint16_t packed);

#endif /* ZM_MEMORY_H */
