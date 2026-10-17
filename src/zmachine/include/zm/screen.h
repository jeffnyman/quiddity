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
 * Version 6 (Spec §8.8) moves the line between deciding and drawing. Its
 * eight windows have positions, sizes, margins, and cursors in units,
 * all of which the game can read back, and a new-line may call a routine
 * of the game's. So for Version 6 this module lays the text out itself:
 * it keeps each window, wraps words between its margins, scrolls it when
 * it is full, and sends the front end nothing but text to plot at a
 * position, areas to erase or scroll, and pictures to draw. Pictures
 * come from a resource file (zm/blorb.h) the front end supplies.
 */

#ifndef ZM_SCREEN_H
#define ZM_SCREEN_H

#include <stdint.h>

#include "zm/blorb.h"
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

/* Spec §8.6.1, §8.7.2: the two windows of Versions 3 to 5; in Version 6
 * they are the first two of eight (Spec §8.8.3), and -3 means whichever
 * is current. */
enum {
    ZM_WINDOW_LOWER = 0,
    ZM_WINDOW_UPPER = 1
};
#define ZM_WINDOWS 8
#define ZM_WINDOW_CURRENT (-3)

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
 * everywhere; transparent (15) and "under the cursor" (-1) are Version
 * 6 requests that this module cannot grant, and leaves the colour as it
 * was. */
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
    ZM_COLOUR_DARK_GREY   = 12,
    ZM_COLOUR_TRANSPARENT = 15
};

/* Spec §15 sound_effect: what to do with a sound. */
enum {
    ZM_SOUND_PREPARE = 1,
    ZM_SOUND_START   = 2,
    ZM_SOUND_STOP    = 3,
    ZM_SOUND_FINISH  = 4
};

/* Spec §8.8.3.1: the attributes of a Version 6 window, as bits. */
enum {
    ZM_WINDOW_WRAPPING   = 1,
    ZM_WINDOW_SCROLLING  = 2,
    ZM_WINDOW_TRANSCRIPT = 4,
    ZM_WINDOW_BUFFERING  = 8
};

/* Spec §8.8.3.2: the properties of a Version 6 window, by number. */
enum {
    ZM_WINDOW_PROP_Y = 0,
    ZM_WINDOW_PROP_X,
    ZM_WINDOW_PROP_HEIGHT,
    ZM_WINDOW_PROP_WIDTH,
    ZM_WINDOW_PROP_CURSOR_Y,
    ZM_WINDOW_PROP_CURSOR_X,
    ZM_WINDOW_PROP_LEFT_MARGIN,
    ZM_WINDOW_PROP_RIGHT_MARGIN,
    ZM_WINDOW_PROP_NEWLINE_ROUTINE,
    ZM_WINDOW_PROP_COUNTDOWN,
    ZM_WINDOW_PROP_STYLE,
    ZM_WINDOW_PROP_COLOUR,
    ZM_WINDOW_PROP_FONT,
    ZM_WINDOW_PROP_FONT_SIZE,
    ZM_WINDOW_PROP_ATTRIBUTES,
    ZM_WINDOW_PROP_LINE_COUNT,
    ZM_WINDOW_PROP_TRUE_FOREGROUND,
    ZM_WINDOW_PROP_TRUE_BACKGROUND,
    ZM_WINDOW_PROPERTIES
};

/* Spec §8.8.3.2.6: the line count that means "never print [MORE]". */
#define ZM_LINE_COUNT_NO_MORE (-999)

/* The longest word buffered printing (Spec §8.8.3.1.2) gathers before
 * laying it out regardless. */
#define ZM_WORD_MAX 256

/* One of the eight Version 6 windows (Spec §8.8.3). Everything is in
 * units, positions and sizes relative to the screen and the cursor
 * relative to the window, all counted from 1. */
typedef struct zm_window {
    uint16_t y;
    uint16_t x;
    uint16_t height;
    uint16_t width;
    uint16_t cursor_y;
    uint16_t cursor_x;
    uint16_t left_margin;
    uint16_t right_margin;
    uint16_t newline_routine;   /* packed address, Spec §8.8.3.2.2 */
    uint16_t countdown;
    uint16_t style;
    uint16_t foreground;
    uint16_t background;
    uint16_t font;
    uint16_t attributes;
    int16_t line_count;
} zm_window;

/* A routine of the game's that the screen must call: the new-line
 * interrupt of Spec §8.8.3.2.2. The machine supplies one. */
typedef void (*zm_screen_interrupt)(void *context, uint16_t routine);

typedef struct zm_screen {
    zm_memory *mem;
    const zm_io *io;

    /* Spec §7.1: the selected output streams, as ZM_STREAM_* bits. */
    unsigned streams;

    /* Spec §7.1.2.1: the tables stream 3 is writing to, the one selected
     * most recently last, and how many characters each has taken. In
     * Version 6 a table may be formatted into lines of a given width in
     * units (Spec §15 output_stream), or ZM_TABLE_PLAIN; and the width
     * of the text on its current line and in all is kept for the header
     * (Spec §11.1, the word at $30). */
    uint32_t tables[ZM_STREAM3_DEPTH];
    uint16_t table_counts[ZM_STREAM3_DEPTH];
    uint16_t table_widths[ZM_STREAM3_DEPTH];
    uint16_t table_line_width[ZM_STREAM3_DEPTH];
    uint16_t table_total_width[ZM_STREAM3_DEPTH];
    unsigned table_depth;

    /* The screen's size in characters and lines, as the front end
     * described it (Spec §8.4), and the size of a character in units
     * (Spec §8.4.2), which is 1 by 1 before Version 6. */
    unsigned width;
    unsigned height;
    unsigned font_width;
    unsigned font_height;

    /* Spec §8.6.1, §8.7.2: which window is selected and how tall the
     * upper one is. In Version 6 the window is 0 to 7. */
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

    /*
     * Version 6 (Spec §8.8).
     */

    zm_window windows[ZM_WINDOWS];

    /* Spec §8.8.3.1.2: the word being gathered by buffered printing, as
     * Unicode, not yet laid out. Also gathers words for a formatted
     * stream 3. */
    uint16_t word[ZM_WORD_MAX];
    unsigned word_length;

    /* Spec §15 set_cursor: whether the cursor is shown. */
    int cursor_shown;

    /* Spec §15 mouse_window: the window the mouse is confined to, or -1
     * for the whole screen. */
    int mouse_window;

    /* Spec §15 buffer_screen: what the game last asked for. This module
     * always acts as if it were 0, as §8.8.7 permits. */
    int screen_buffering;

    /* Where the pictures are (Spec §8.8.6), or NULL. */
    const zm_blorb *resources;

    /* Spec §8.8.3.2.2: who runs a new-line interrupt routine. */
    zm_screen_interrupt interrupt;
    void *interrupt_context;
    int in_interrupt;

    /* Spec §8.8.3.2.2.1: this is 'Zork Zero' release 393, whose new-line
     * interrupts are called after the cursor has moved. */
    int zork_zero_393;
} zm_screen;

/* A stream 3 table that is not formatted into lines. */
#define ZM_TABLE_PLAIN 0xffffu

/* Prepare the screen for a loaded story: the front end's description,
 * the initial state (Spec §8.5.2, §8.6.3, §8.7.3.3, §8.8.3.3), and the
 * header fields the interpreter owns (Spec §11.1). */
void zm_screen_init(zm_screen *s, zm_memory *mem, const zm_io *io);

/* Write the interpreter's header fields (Spec §11.1 columns "Int" and
 * "Rst"), as at start-up and again after a restart or restore. */
void zm_screen_write_header(zm_screen *s);

/* Give the screen the story's resource file, which may be NULL. The
 * header's picture bits say whether pictures can now be shown. */
void zm_screen_use_resources(zm_screen *s, const zm_blorb *resources);

/* Supply the function that runs a new-line interrupt routine. */
void zm_screen_set_interrupt(zm_screen *s, zm_screen_interrupt interrupt, void *context);

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

/* Lay out any text still gathered for buffered printing (Spec
 * §8.8.3.2.7). Called before the game waits for input and before any
 * position is read back; harmless at other times and before Version 6. */
void zm_screen_flush(zm_screen *s);

/* The player's command, as read, for the streams that record input: the
 * transcript (Spec §7.1.1.1) and the command file (Spec §7.1.2.3). */
void zm_screen_echo_command(zm_screen *s, const uint16_t *zscii, unsigned count);

/* A keypress read by read_char, for the command file (Spec §7.1.2.3). */
void zm_screen_echo_key(zm_screen *s, uint16_t zscii);

/* In Version 6 the interpreter shows what the player typed in the
 * window the read happened in, and ends the line if the input was. In
 * earlier Versions the front end has shown it already and this does
 * nothing. */
void zm_screen_show_input(zm_screen *s, const uint16_t *zscii, unsigned count, int newline);

/*
 * Streams (Spec §7).
 */

/* Spec §15 output_stream: select (positive) or deselect (negative) a
 * stream; 0 does nothing. Stream 3 needs the table to write to, and in
 * Version 6 may be given a width, in units if negative or as a window
 * number if not, to format the text into lines (Spec §15 print_form).
 * Faults on a stream number the Standard does not define, and on the
 * seventeenth nested selection of stream 3 (Spec §7.1.2.1.1). */
void zm_screen_select_stream(zm_screen *s, int16_t stream, uint16_t table, int16_t width, int has_width);

/* Spec §7.3, §7.4: bit 0 of Flags 2 and stream 2 are the same thing,
 * whichever of them the game changes. Call before anything that depends
 * on the stream being right. */
void zm_screen_sync_transcript(zm_screen *s);

/*
 * Windows (Spec §8.6, §8.7, §8.8).
 */

/* Spec §15 split_window: the upper window's height in lines, or in
 * Version 6 in units (Spec §8.8.4.1). */
void zm_screen_split(zm_screen *s, unsigned size);

/* Spec §15 restart: the screen begins again as at the start of play,
 * cleared, with only a running transcript carried over. */
void zm_screen_restart(zm_screen *s);

/* Spec §15 set_window: 0 or 1, or 0 to 7 in Version 6. */
void zm_screen_set_window(zm_screen *s, unsigned window);

/* Spec §15 erase_window: a window, -1 (unsplit and clear) or -2
 * (clear). */
void zm_screen_erase_window(zm_screen *s, int16_t window);

/* Spec §15 erase_line: 1 erases from the cursor to the end of the line;
 * in Version 6 any other value erases that many units less one. */
void zm_screen_erase_line(zm_screen *s, uint16_t value);

/* Spec §15 set_cursor: move a window's cursor. Before Version 6 only the
 * upper window has one to move, and a row below the split enlarges it
 * to reach (Spec §8 Remarks). In Version 6 a negative y turns the
 * cursor off (-1) or on (-2), and 0 for y or x keeps that coordinate. */
void zm_screen_set_cursor(zm_screen *s, unsigned window, int16_t y, int16_t x);

/* Spec §15 get_cursor: the upper window's cursor, or in Version 6 the
 * current window's, after any buffered text has been laid out. */
void zm_screen_get_cursor(zm_screen *s, unsigned *y, unsigned *x);

/*
 * Appearance (Spec §8.1, §8.3, §8.7.1).
 */

/* Spec §15 set_text_style: 0 for Roman, or styles to add; in Version 6
 * for the current window. */
void zm_screen_set_style(zm_screen *s, unsigned style);

/* Spec §15 set_colour, with codes 0 to 12 (Spec §8.3.1), for a window
 * in Version 6. */
void zm_screen_set_colour(zm_screen *s, unsigned window, int16_t foreground, int16_t background);

/* Spec §15 set_true_colour, with 15-bit colours or the requests of
 * Spec §8.3.7; each is matched to the nearest of the standard colours
 * (Spec §8.3.7.1). */
void zm_screen_set_true_colour(zm_screen *s, unsigned window, int16_t foreground, int16_t background);

/* Spec §15 set_font: returns the previous font, or 0 if the requested
 * one is unavailable. */
unsigned zm_screen_set_font(zm_screen *s, unsigned window, unsigned font);

/* Spec §15 buffer_mode. */
void zm_screen_set_buffering(zm_screen *s, int on);

/* Spec §15 sound_effect, §9: bleeps and sounds go to the front end if it
 * has sound; otherwise nothing happens. */
void zm_screen_sound_effect(zm_screen *s, unsigned number, unsigned effect, unsigned volume, unsigned repeats);

/*
 * Version 6 only (Spec §8.8). Windows are 0 to 7; the caller resolves
 * -3 to the current window first.
 */

/* Spec §15 move_window, window_size, window_style. */
void zm_screen_move_window(zm_screen *s, unsigned window, uint16_t y, uint16_t x);
void zm_screen_size_window(zm_screen *s, unsigned window, uint16_t height, uint16_t width);
void zm_screen_style_window(zm_screen *s, unsigned window, uint16_t flags, unsigned operation);

/* Spec §15 get_wind_prop and put_wind_prop, §8.8.3.2. Faults on a
 * property that does not exist, and on writing one the game may not. */
uint16_t zm_screen_window_property(zm_screen *s, unsigned window, unsigned property);
void zm_screen_put_window_property(zm_screen *s, unsigned window, unsigned property, uint16_t value);

/* Spec §15 scroll_window: up by so many units, or down if negative. */
void zm_screen_scroll_window(zm_screen *s, unsigned window, int16_t units);

/* Spec §15 set_margins. */
void zm_screen_set_margins(zm_screen *s, unsigned window, uint16_t left, uint16_t right);

/* Spec §15 picture_data: for a picture number, its height and width in
 * units and 1, or 0 if there is no such picture; for 0, how many
 * pictures there are and the resource file's release number, and
 * whether there are any. */
int zm_screen_picture_data(zm_screen *s, unsigned number, uint16_t *height, uint16_t *width);

/* Spec §15 draw_picture and erase_picture: at (y, x) in the current
 * window, or at the cursor for a coordinate of 0. A picture that does
 * not exist is a fault. */
void zm_screen_draw_picture(zm_screen *s, unsigned number, uint16_t y, uint16_t x);
void zm_screen_erase_picture(zm_screen *s, unsigned number, uint16_t y, uint16_t x);

/* Spec §15 mouse_window: a window, or -1 for the whole screen. */
void zm_screen_mouse_window(zm_screen *s, int window);

/* Spec §15 buffer_screen: returns the previous state. */
uint16_t zm_screen_buffer_screen(zm_screen *s, int16_t mode);

#endif /* ZM_SCREEN_H */
