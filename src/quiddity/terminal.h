/*
 * terminal.h: the Z-machine's I/O, realised on a plain terminal.
 *
 * This supplies the zm_io functions (see zm/io.h) for a front end that
 * writes to standard output and reads from standard input. It is the
 * simplest presentation layer that can play a game; the screen model
 * (Spec §8) will make it do more.
 *
 * Output is held back one line at a time. A game prints its prompt and
 * then asks for the status line, which on a real screen sits above the
 * prompt; keeping the unfinished line until something follows it lets
 * the status line go first here too.
 */

#ifndef QUIDDITY_TERMINAL_H
#define QUIDDITY_TERMINAL_H

#include "zm/io.h"
#include "zm/memory.h"

#define TERMINAL_LINE_MAX 1024

typedef struct terminal {
    const zm_memory *mem;

    /* The unfinished output line, as Unicode, not yet written. */
    uint16_t line[TERMINAL_LINE_MAX];
    unsigned line_length;

    /* Whether standard input is a person at a keyboard. When it is a
     * file, the commands read from it are echoed so the transcript
     * reads as a session would. */
    int interactive;

    /* Columns available, for laying out the status line. */
    unsigned width;
} terminal;

/* Prepare the terminal and fill in the I/O struct to point at it. The
 * terminal needs the memory to translate ZSCII (Spec §3.8). */
void terminal_init(terminal *t, const zm_memory *mem, zm_io *io);

/* Write out anything still held back. Call when the game is over. */
void terminal_finish(terminal *t);

#endif /* QUIDDITY_TERMINAL_H */
