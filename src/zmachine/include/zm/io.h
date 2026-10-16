/*
 * zm/io.h: the seam between the machine and the outside world.
 *
 * The Z-machine core never touches a screen, a keyboard, or a file of its
 * own. Everything it needs from the outside is a function in this struct,
 * supplied by the front end when the machine is created. A terminal
 * front end fills it with calls that read and write stdin and stdout; a
 * windowed one draws into a window; a test records what would have been
 * shown and scripts what would have been typed.
 *
 * This is the part of the interpreter the Standard calls presentation,
 * and the part Quiddity is willing to vary. It grows as the screen model
 * (Spec §8) and the output streams (Spec §7) are taken on. Every function
 * takes the context pointer first so a front end can keep its state
 * wherever it likes. A function left NULL means the front end cannot do
 * that thing, and the machine says so in the header where the Standard
 * provides a bit for it (Spec §11.1.4).
 */

#ifndef ZM_IO_H
#define ZM_IO_H

#include <stdint.h>

typedef struct zm_io {
    void *context;

    /* Print one ZSCII character (Spec §3.8) to the screen. New-line is
     * ZSCII 13. The front end turns the code into something visible with
     * zm_zscii_to_unicode() or its own tables. */
    void (*print_char)(void *context, uint16_t zscii);

    /* Read a line of input for the read opcode (Spec §15 read, §10.5).
     * Store up to max ZSCII characters in buffer, without the
     * terminator, and return how many; put the terminating character in
     * *terminator, which is ZSCII 13 for a new-line. Only characters
     * defined for input and output may be stored (Spec §10.7.2). Return
     * -1 when there is no more input to be had, which ends the game. */
    int (*read_line)(void *context, uint16_t *buffer, unsigned max, uint16_t *terminator);

    /* Read one keypress for read_char (Spec §15 read_char, §10.6) as a
     * ZSCII input code, with new-line as 13. Return -1 at the end of
     * input. */
    int (*read_char)(void *context);

    /* Show the status line (Spec §8.2): the location's short name, as
     * ZSCII, and either the score and turn count or, for a time game,
     * the hours and minutes. Called before each read in Versions 1 to 3
     * and by show_status (Spec §8.2.4). A front end that leaves this
     * NULL has the "status line not available" bit set for it. */
    void (*show_status)(void *context, const uint16_t *name, unsigned name_length,
                        int time_game, int16_t score_or_hours, int16_t turns_or_minutes);
} zm_io;

#endif /* ZM_IO_H */
