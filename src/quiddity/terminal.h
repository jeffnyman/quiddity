/*
 * terminal.h: the Z-machine's I/O, realised on a plain terminal.
 *
 * This supplies the zm_io functions (see zm/io.h) for a front end that
 * writes to standard output. It is the simplest presentation layer that
 * can show a game running; the screen model (Spec §8) will make it do
 * more.
 */

#ifndef QUIDDITY_TERMINAL_H
#define QUIDDITY_TERMINAL_H

#include "zm/io.h"
#include "zm/memory.h"

typedef struct terminal {
    const zm_memory *mem;
} terminal;

/* Prepare the terminal for output and fill in the I/O struct to point at
 * it. The terminal needs the memory to translate ZSCII (Spec §3.8). */
void terminal_init(terminal *t, const zm_memory *mem, zm_io *io);

#endif /* QUIDDITY_TERMINAL_H */
