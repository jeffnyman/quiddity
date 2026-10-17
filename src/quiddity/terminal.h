/*
 * terminal.h: the Z-machine's I/O, realised on a plain terminal.
 *
 * This supplies the zm_io functions (see zm/io.h) for a front end that
 * writes to standard output and reads from standard input. It is the
 * simplest presentation layer that can play a game, and it treats the
 * terminal as a teletype: text goes out one line after another and
 * nothing already printed can be changed.
 *
 * The screen model (Spec §8) asks for more than a teletype has, so two
 * things are arranged here:
 *
 * The lower window's output is held back one line at a time. A game
 * prints its prompt and then asks for the status line, which on a real
 * screen sits above the prompt; keeping the unfinished line until
 * something follows it lets the status line go first here too. The held
 * line is also where words are wrapped (Spec §7.2).
 *
 * The upper window (Spec §8.6.1, §8.7.2) is kept as a grid of characters
 * that the game writes into at will. It cannot be drawn where it belongs,
 * so it is printed, as a block, whenever the game stops for input and the
 * grid has changed since it was last printed, and before any part of it
 * is erased unseen. For the usual status line that means one line above
 * each prompt, as in Version 3; for a menu or a map it means the whole
 * thing, once per keypress.
 *
 * Saved games (Spec §15 save and restore) go to files the player names
 * at a prompt, with the story's own name offered as the default; the
 * machine supplies the bytes (zm/quetzal.h) and this file only puts them
 * somewhere.
 *
 * Timed input (Spec §15 read) is kept by waiting for something to arrive
 * on standard input before reading it. The terminal's own line editor
 * takes over once the first key of a command is pressed, so the clock
 * runs while the player is thinking and stops while they are typing;
 * what the interrupt routine prints appears above the line being typed,
 * which the editor keeps intact.
 */

#ifndef QUIDDITY_TERMINAL_H
#define QUIDDITY_TERMINAL_H

#include <stdio.h>

#include "zm/io.h"
#include "zm/memory.h"

/* The screen the terminal claims to be (Spec §8.4). */
#define TERMINAL_WIDTH 80
#define TERMINAL_HEIGHT 24

#define TERMINAL_LINE_MAX 1024

/* The longest file name the prompts accept. */
#define TERMINAL_NAME_MAX 260

typedef struct terminal {
    const zm_memory *mem;

    /* The unfinished lower-window line, as Unicode, not yet written. */
    uint16_t line[TERMINAL_LINE_MAX];
    unsigned line_length;

    /* Spec §7.2: whether to break lines between words. */
    int buffering;

    /* Whether standard input is a person at a keyboard. When it is a
     * file, the commands read from it are echoed so the transcript
     * reads as a session would. */
    int interactive;

    /* The upper window as a grid, its height in lines, and its cursor
     * with (1, 1) at the top left. */
    uint16_t upper[TERMINAL_HEIGHT][TERMINAL_WIDTH];
    unsigned upper_height;
    unsigned upper_row;
    unsigned upper_column;

    /* Which window text goes to: 0 for the lower, 1 for the upper. */
    unsigned window;

    /* Whether the grid has been printed since it last changed. */
    int upper_shown;

    /* Output stream 2 and output stream 4 (Spec §7.1.1, §7.1.2.3), when
     * the player asked for them on the command line. */
    const char *transcript_path;
    FILE *transcript;
    const char *record_path;
    FILE *record;

    /* The file the last saved game went to or came from, offered as the
     * default at the next prompt. */
    char save_name[TERMINAL_NAME_MAX];
} terminal;

/* Prepare the terminal and fill in the I/O struct to point at it. The
 * terminal needs the memory to translate ZSCII (Spec §3.8) and the story
 * file's path to name saved games after. The other paths, which may be
 * NULL, are where a transcript and a record of commands go if the game
 * asks for them. */
void terminal_init(terminal *t, const zm_memory *mem, zm_io *io, const char *story_path,
                   const char *transcript_path, const char *record_path);

/* Write out anything still held back and close any files. Call when the
 * game is over. */
void terminal_finish(terminal *t);

#endif /* QUIDDITY_TERMINAL_H */
