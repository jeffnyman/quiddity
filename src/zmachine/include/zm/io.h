/*
 * zm/io.h: the seam between the machine and the outside world.
 *
 * The Z-machine core never touches a screen, a keyboard, or a file of its
 * own. Everything it needs from the outside is a function in this struct,
 * supplied by the front end when the machine is created. A terminal
 * front end fills it with calls that write to stdout; a windowed one
 * draws into a window; a test records what would have been shown.
 *
 * This is the part of the interpreter the Standard calls presentation,
 * and the part Quiddity is willing to vary. It starts small, with one
 * output call, and grows as the screen model (Spec §8) and input (Spec
 * §10) are taken on. Every function takes the context pointer first so
 * a front end can keep its state wherever it likes.
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
} zm_io;

#endif /* ZM_IO_H */
