/*
 * zm/screen.h: output streams (Spec §7) and the screen model (Spec §8).
 *
 * The Standard's screen is two things. One is state the game can observe
 * and change: which output streams are selected, the table stream 3 is
 * filling, which window is current, how tall the upper window is, where
 * its cursor sits, the text style, the colours, whether printing is
 * buffered. That is decided by the game's opcodes and kept here, exactly.
 * The other is the picture on the screen, which belongs to the front end
 * (zm/io.h) and may look however the front end likes.
 *
 * So this module decides and the front end draws. Every change of state
 * is reported to the front end through zm_io, and a front end that
 * cannot do a thing leaves that function NULL; the module answers for it
 * in the header (Spec §11.1), where the Standard provides a bit, and
 * otherwise carries on as if the thing had been done. That is what lets
 * a plain terminal, a window, and a test all run the same games.
 *
 * Version 6, with its eight windows and pixel coordinates (Spec §8.8), is
 * not modelled yet; the screen opcodes that take Version 6 forms say so.
 */

#ifndef ZM_SCREEN_H
#define ZM_SCREEN_H

#include <stdint.h>

#include "zm/io.h"
#include "zm/memory.h"

/* Spec §7.1.2.1.1: stream 3 can be selected while already selected, up
 * to this many times over. */
#define ZM_STREAM3_DEPTH 16

/* Spec §7.1.1, §7.1.2: the four output streams, as bits of a mask. */
enum {
    ZM_STREAM_SCREEN     = 1 << 1,
    ZM_STREAM_TRANSCRIPT = 1 << 2,
    ZM_STREAM_MEMORY     = 1 << 3,
    ZM_STREAM_COMMANDS   = 1 << 4
};

/* Spec §8.6.1, §8.7.2: the two windows. */
enum {
    ZM_WINDOW_LOWER = 0,
    ZM_WINDOW_UPPER = 1
};

/* Spec §15 set_text_style: the styles, which add together. */
enum {
    ZM_STYLE_ROMAN       = 0,
    ZM_STYLE_REVERSE     = 1,
    ZM_STYLE_BOLD        = 2,
    ZM_STYLE_ITALIC      = 4,
    ZM_STYLE_FIXED_PITCH = 8
};

/* Spec §8.3.1: the colour codes. 0 and 1 are requests, not colours. The
 * greys are listed as Version 6 colours, but they are colours like any
 * other and 'Praxix' asks for them in Version 5, so they are accepted
 * everywhere; transparent (15) and "under the cursor" (-1) are not. */
enum {
    ZM_COLOUR_CURRENT     = 0,
    ZM_COLOUR_DEFAULT     = 1,
    ZM_COLOUR_BLACK       = 2,
    ZM_COLOUR_RED         = 3,
    ZM_COLOUR_GREEN       = 4,
    ZM_COLOUR_YELLOW      = 5,
    ZM_COLOUR_BLUE        = 6,
    ZM_COLOUR_MAGENTA     = 7,
    ZM_COLOUR_CYAN        = 8,
    ZM_COLOUR_WHITE       = 9,
    ZM_COLOUR_LIGHT_GREY  = 10,
    ZM_COLOUR_MEDIUM_GREY = 11,
    ZM_COLOUR_DARK_GREY   = 12
};

/* Spec §15 sound_effect: what to do with a sound. */
enum {
    ZM_SOUND_PREPARE = 1,
    ZM_SOUND_START   = 2,
    ZM_SOUND_STOP    = 3,
    ZM_SOUND_FINISH  = 4
};

typedef struct zm_screen {
    zm_memory *mem;
    const zm_io *io;

    /* Spec §7.1: the selected output streams, as ZM_STREAM_* bits. */
    unsigned streams;

    /* Spec §7.1.2.1: the tables stream 3 is writing to, the one selected
     * most recently last, and how many characters each has taken. */
    uint32_t tables[ZM_STREAM3_DEPTH];
    uint16_t table_counts[ZM_STREAM3_DEPTH];
    unsigned table_depth;

    /* The screen's size in characters and lines, as the front end
     * described it (Spec §8.4). */
    unsigned width;
    unsigned height;

    /* Spec §8.6.1, §8.7.2: which window is selected and how tall the
     * upper one is. */
    unsigned window;
    unsigned upper_height;

    /* The upper window's cursor, with (1, 1) at the top left (Spec
     * §8.7.2.3). The lower window's cursor is the front end's affair. */
    unsigned upper_row;
    unsigned upper_column;

    /* Spec §8.7.1: the current text style, as ZM_STYLE_* bits. */
    unsigned style;

    /* Spec §8.3: the current colours, as codes 2 to 9. */
    unsigned foreground;
    unsigned background;

    /* Spec §7.2: whether lower-window printing is buffered for
     * word-wrapping. */
    int buffering;

    /* Spec §8.1.2: the current font; 1 unless the front end has more. */
    unsigned font;
} zm_screen;

/* Prepare the screen for a loaded story: the front end's description,
 * the initial state (Spec §8.5.2, §8.6.3, §8.7.3.3), and the header
 * fields the interpreter owns (Spec §11.1). */
void zm_screen_init(zm_screen *s, zm_memory *mem, const zm_io *io);

/* Write the interpreter's header fields (Spec §11.1 columns "Int" and
 * "Rst"), as at start-up and again after a restart or restore. */
void zm_screen_write_header(zm_screen *s);

/*
 * Printing.
 */

/* Print a ZSCII character to the selected streams (Spec §7.1). */
void zm_screen_print(zm_screen *s, uint16_t zscii);

/* Print a Unicode character (Spec §15 print_unicode, §7.5). */
void zm_screen_print_unicode(zm_screen *s, uint16_t unicode);

/* Spec §15 check_unicode: bit 0 if the character can be printed, bit 1
 * if it can be typed. */
uint16_t zm_screen_check_unicode(const zm_screen *s, uint16_t unicode);

/* The player's command, as read, for the streams that record input: the
 * transcript (Spec §7.1.1.1) and the command file (Spec §7.1.2.3). */
void zm_screen_echo_command(zm_screen *s, const uint16_t *zscii, unsigned count);

/* A keypress read by read_char, for the command file (Spec §7.1.2.3). */
void zm_screen_echo_key(zm_screen *s, uint16_t zscii);

/*
 * Streams (Spec §7).
 */

/* Spec §15 output_stream: select (positive) or deselect (negative) a
 * stream; 0 does nothing. Stream 3 needs the table to write to. Faults
 * on a stream number the Standard does not define, and on the
 * seventeenth nested selection of stream 3 (Spec §7.1.2.1.1). */
void zm_screen_select_stream(zm_screen *s, int16_t stream, uint16_t table);

/* Spec §7.3, §7.4: bit 0 of Flags 2 and stream 2 are the same thing,
 * whichever of them the game changes. Call before anything that depends
 * on the stream being right. */
void zm_screen_sync_transcript(zm_screen *s);

/*
 * Windows (Spec §8.6, §8.7).
 */

/* Spec §15 split_window. */
void zm_screen_split(zm_screen *s, unsigned lines);

/* Spec §15 set_window: 0 or 1. */
void zm_screen_set_window(zm_screen *s, unsigned window);

/* Spec §15 erase_window: 0, 1, -1 (unsplit and clear) or -2 (clear). */
void zm_screen_erase_window(zm_screen *s, int16_t window);

/* Spec §15 erase_line: from the cursor to the end of the line. */
void zm_screen_erase_line(zm_screen *s);

/* Spec §15 set_cursor: move the upper window's cursor; no effect in the
 * lower window. A row below the split enlarges the upper window to
 * reach it (Spec §8 Remarks). */
void zm_screen_set_cursor(zm_screen *s, unsigned row, unsigned column);

/* Spec §15 get_cursor: the upper window's cursor. */
void zm_screen_get_cursor(const zm_screen *s, unsigned *row, unsigned *column);

/*
 * Appearance (Spec §8.1, §8.3, §8.7.1).
 */

/* Spec §15 set_text_style: 0 for Roman, or styles to add. */
void zm_screen_set_style(zm_screen *s, unsigned style);

/* Spec §15 set_colour, with codes 0 to 12 (Spec §8.3.1). */
void zm_screen_set_colour(zm_screen *s, unsigned foreground, unsigned background);

/* Spec §15 set_true_colour, with 15-bit colours or the requests -1
 * (default) and -2 (current) of Spec §8.3.7; each is matched to the
 * nearest of the standard colours (Spec §8.3.7.1). */
void zm_screen_set_true_colour(zm_screen *s, int16_t foreground, int16_t background);

/* Spec §15 set_font: returns the previous font, or 0 if the requested
 * one is unavailable. */
unsigned zm_screen_set_font(zm_screen *s, unsigned font);

/* Spec §15 buffer_mode. */
void zm_screen_set_buffering(zm_screen *s, int on);

/* Spec §15 sound_effect, §9: bleeps and sounds go to the front end if it
 * has sound; otherwise nothing happens. */
void zm_screen_sound_effect(zm_screen *s, unsigned number, unsigned effect, unsigned volume, unsigned repeats);

#endif /* ZM_SCREEN_H */
