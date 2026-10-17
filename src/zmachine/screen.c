/*
 * screen.c: see zm/screen.h. Implements the output streams of Spec §7 and
 * the state of the screen model of Spec §8, and writes the header fields
 * of Spec §11.1 that describe the interpreter.
 */

#include "zm/screen.h"

#include <string.h>

#include "zm/fatal.h"
#include "zm/header.h"
#include "zm/text.h"

/* Spec §11.1.3: Infocom's number for their own DECSystem-20, the machine
 * with no screen features to speak of, which is how 'Beyond Zork' is
 * told not to expect the character graphics font (Spec §16). Spec
 * §11.1.3.1: the version is conventionally a capital letter. */
#define INTERPRETER_NUMBER 1
#define INTERPRETER_VERSION 'Q'

/* Spec §8.4: the screen should be at least 60 by 14; this is the size a
 * front end gets unless it says otherwise. */
#define DEFAULT_WIDTH 80
#define DEFAULT_HEIGHT 24

/*
 * Set-up and the header.
 */

void zm_screen_write_header(zm_screen *s)
{
    zm_memory *mem = s->mem;
    const zm_io *io = s->io;
    unsigned flags1 = zm_read_byte(mem, ZM_HEADER_FLAGS1);
    unsigned flags2 = zm_read_word(mem, ZM_HEADER_FLAGS2);

    if (mem->version <= 3) {
        /* Spec §8.2, §11.1.4: bit 4 is set when no status line can be
         * shown; Spec §8.6.1.2: bit 5 when the screen can be split. Bit 6
         * would claim a variable-pitch default font, and the front ends
         * so far print in one pitch. */
        flags1 &= ~(unsigned)(ZM_FLAGS1_V3_NO_STATUS_LINE | ZM_FLAGS1_V3_SCREEN_SPLITTING
                              | ZM_FLAGS1_V3_VARIABLE_PITCH);
        if (io->show_status == NULL) {
            flags1 |= (unsigned)ZM_FLAGS1_V3_NO_STATUS_LINE;
        }
        if (io->split_window != NULL) {
            flags1 |= (unsigned)ZM_FLAGS1_V3_SCREEN_SPLITTING;
        }
    } else {
        /* Spec §11.1.4: each bit says whether the front end can do the
         * thing. Pictures (Spec §8.8.6) and timed input (Spec §15 read)
         * are not offered yet. */
        flags1 &= ~(unsigned)(ZM_FLAGS1_COLOURS | ZM_FLAGS1_PICTURES | ZM_FLAGS1_BOLD | ZM_FLAGS1_ITALIC
                              | ZM_FLAGS1_FIXED_PITCH | ZM_FLAGS1_SOUND | ZM_FLAGS1_TIMED_INPUT);
        if (io->set_colour != NULL) {
            flags1 |= (unsigned)ZM_FLAGS1_COLOURS;             /* Spec §8.3.3 */
        }
        if (io->styles & ZM_STYLE_BOLD) {
            flags1 |= (unsigned)ZM_FLAGS1_BOLD;
        }
        if (io->styles & ZM_STYLE_ITALIC) {
            flags1 |= (unsigned)ZM_FLAGS1_ITALIC;
        }
        if (io->styles & ZM_STYLE_FIXED_PITCH) {
            flags1 |= (unsigned)ZM_FLAGS1_FIXED_PITCH;
        }
        if (io->sound_effect != NULL) {
            flags1 |= (unsigned)ZM_FLAGS1_SOUND;               /* Spec §9.1.1 */
        }
    }
    zm_write_byte(mem, ZM_HEADER_FLAGS1, (uint8_t)flags1);

    /* Spec §11.1.2: the game sets these bits to ask for a feature and the
     * interpreter clears the ones it cannot provide. Undo it can (Spec
     * §6.1.4), so that bit is left as the game set it; the transcript
     * bit is the game's and stream 2's (Spec §7.4) and is left to
     * zm_screen_sync_transcript(). */
    flags2 &= ~(unsigned)(ZM_FLAGS2_PICTURES | ZM_FLAGS2_MOUSE | ZM_FLAGS2_MENUS);
    if (io->sound_effect == NULL) {
        flags2 &= ~(unsigned)ZM_FLAGS2_SOUND;                  /* Spec §9.1.2 */
    }
    zm_write_word(mem, ZM_HEADER_FLAGS2, (uint16_t)flags2);

    if (mem->version >= 4) {
        /* Spec §11.1.3, §8.4: who the interpreter is and how big the
         * screen is, in lines and characters. */
        zm_write_byte(mem, ZM_HEADER_INTERPRETER_NUMBER, INTERPRETER_NUMBER);
        zm_write_byte(mem, ZM_HEADER_INTERPRETER_VERSION, INTERPRETER_VERSION);
        zm_write_byte(mem, ZM_HEADER_SCREEN_HEIGHT, (uint8_t)(s->height > 255 ? 255 : s->height));
        zm_write_byte(mem, ZM_HEADER_SCREEN_WIDTH, (uint8_t)(s->width > 255 ? 255 : s->width));
    }
    if (mem->version >= 5) {
        /* Spec §8.4.2, §8.4.3, §8.1.1: units are characters, so the
         * screen in units is the screen in characters and a character
         * is 1 by 1. Both font bytes are 1, so the Version 6 swap of
         * width and height makes no difference. */
        zm_write_word(mem, ZM_HEADER_SCREEN_WIDTH_UNITS, (uint16_t)s->width);
        zm_write_word(mem, ZM_HEADER_SCREEN_HEIGHT_UNITS, (uint16_t)s->height);
        zm_write_byte(mem, ZM_HEADER_FONT_BYTE_26, 1);
        zm_write_byte(mem, ZM_HEADER_FONT_BYTE_27, 1);

        /* Spec §8.3.2, §8.3.3: the colours text starts in. */
        zm_write_byte(mem, ZM_HEADER_DEFAULT_BACKGROUND, (uint8_t)s->background);
        zm_write_byte(mem, ZM_HEADER_DEFAULT_FOREGROUND, (uint8_t)s->foreground);
    }

    /* Spec §11.1.5: the revision of the Standard this interpreter
     * follows. Inform games read it before using the opcodes the
     * revision added, such as check_unicode. */
    zm_write_byte(mem, ZM_HEADER_STANDARD_MAJOR, 1);
    zm_write_byte(mem, ZM_HEADER_STANDARD_MINOR, 1);
}

void zm_screen_init(zm_screen *s, zm_memory *mem, const zm_io *io)
{
    memset(s, 0, sizeof *s);
    s->mem = mem;
    s->io = io;
    s->width = io->screen_width != 0 ? io->screen_width : DEFAULT_WIDTH;
    s->height = io->screen_height != 0 ? io->screen_height : DEFAULT_HEIGHT;

    /* Spec §7.1.1: the screen is always selected to begin with. Spec
     * §8.6.1, §8.7.2: the lower window, with no upper window. Spec
     * §7.2.1: buffering starts on. */
    s->streams = ZM_STREAM_SCREEN;
    s->window = ZM_WINDOW_LOWER;
    s->upper_row = 1;
    s->upper_column = 1;
    s->buffering = 1;
    s->font = 1;
    s->foreground = io->default_foreground != 0 ? io->default_foreground : ZM_COLOUR_WHITE;
    s->background = io->default_background != 0 ? io->default_background : ZM_COLOUR_BLACK;

    zm_screen_write_header(s);

    /* Spec §15 restart: the transcript bit survives a restart, so a game
     * may begin with it set. */
    zm_screen_sync_transcript(s);
}

/*
 * Streams.
 */

static void write_transcript_flag(zm_screen *s, int on)
{
    unsigned flags2 = zm_read_word(s->mem, ZM_HEADER_FLAGS2);

    if (on) {
        flags2 |= (unsigned)ZM_FLAGS2_TRANSCRIPT;
    } else {
        flags2 &= ~(unsigned)ZM_FLAGS2_TRANSCRIPT;
    }
    zm_write_word(s->mem, ZM_HEADER_FLAGS2, (uint16_t)flags2);
}

/* Start or stop the transcript, and leave both the stream mask and the
 * header bit saying what actually happened. */
static void set_transcript(zm_screen *s, int on)
{
    int running = (s->streams & ZM_STREAM_TRANSCRIPT) != 0;

    if (on && !running) {
        running = s->io->set_transcript != NULL && s->io->set_transcript(s->io->context, 1);
    } else if (!on && running) {
        if (s->io->set_transcript != NULL) {
            s->io->set_transcript(s->io->context, 0);
        }
        running = 0;
    }
    if (running) {
        s->streams |= ZM_STREAM_TRANSCRIPT;
    } else {
        s->streams &= ~(unsigned)ZM_STREAM_TRANSCRIPT;
    }
    write_transcript_flag(s, running);
}

void zm_screen_sync_transcript(zm_screen *s)
{
    int wanted = (zm_read_word(s->mem, ZM_HEADER_FLAGS2) & ZM_FLAGS2_TRANSCRIPT) != 0;

    if (wanted != ((s->streams & ZM_STREAM_TRANSCRIPT) != 0)) {
        set_transcript(s, wanted);
    }
}

void zm_screen_select_stream(zm_screen *s, int16_t stream, uint16_t table)
{
    switch (stream) {
    case 0:
        break;
    case 1:
        s->streams |= ZM_STREAM_SCREEN;
        break;
    case -1:
        s->streams &= ~(unsigned)ZM_STREAM_SCREEN;
        break;
    case 2:
        set_transcript(s, 1);
        break;
    case -2:
        set_transcript(s, 0);
        break;
    case 3:
        /* Spec §7.1.2.1: the table's first word is the count, written
         * when the stream closes; Spec §7.1.2.1.1: selections nest. */
        if (s->table_depth == ZM_STREAM3_DEPTH) {
            zm_fatal("output_stream 3 selected %u times over, more than the %u the Standard allows",
                     ZM_STREAM3_DEPTH + 1u, (unsigned)ZM_STREAM3_DEPTH);
        }
        s->tables[s->table_depth] = table;
        s->table_counts[s->table_depth] = 0;
        s->table_depth++;
        s->streams |= ZM_STREAM_MEMORY;
        break;
    case -3:
        if (s->table_depth > 0) {
            s->table_depth--;
            zm_write_word(s->mem, s->tables[s->table_depth], s->table_counts[s->table_depth]);
            if (s->table_depth == 0) {
                s->streams &= ~(unsigned)ZM_STREAM_MEMORY;
            }
        }
        break;
    case 4:
        s->streams |= ZM_STREAM_COMMANDS;
        break;
    case -4:
        s->streams &= ~(unsigned)ZM_STREAM_COMMANDS;
        break;
    default:
        zm_fatal("output_stream %d: there is no such stream", (int)stream);
    }
}

/*
 * Printing.
 */

/* Whether text printed to the upper window reaches a screen: it needs
 * the stream selected and a front end that has an upper window. */
static int upper_window_shows(const zm_screen *s)
{
    return (s->streams & ZM_STREAM_SCREEN) != 0 && s->io->split_window != NULL;
}

/* Keep the upper window's cursor where the front end's will be: one
 * character to the right, or the start of the next line. Spec §8.7.3.1:
 * the upper window never scrolls, and a cursor at its bottom right
 * stays put. */
static void advance_upper_cursor(zm_screen *s, uint16_t zscii)
{
    if (zscii == ZM_ZSCII_NEWLINE) {
        if (s->upper_row < s->upper_height) {
            s->upper_row++;
        }
        s->upper_column = 1;
    } else if (s->upper_column < s->width) {
        s->upper_column++;
    }
}

void zm_screen_print(zm_screen *s, uint16_t zscii)
{
    /* Spec §3.8.2.1: null prints nothing, on any stream. */
    if (zscii == ZM_ZSCII_NULL) {
        return;
    }
    zm_screen_sync_transcript(s);

    /* Spec §7.1.2.2: while stream 3 is selected nothing reaches the
     * others. Spec §7.1.2.2.1: new-lines are written as 13, which is
     * what they already are here. */
    if (s->streams & ZM_STREAM_MEMORY) {
        unsigned top = s->table_depth - 1u;

        zm_write_byte(s->mem, s->tables[top] + 2u + s->table_counts[top], (uint8_t)zscii);
        s->table_counts[top]++;
        return;
    }

    if (s->window == ZM_WINDOW_UPPER) {
        /* The upper window holds status lines, menus and maps, none of
         * which belong in a transcript of the story. */
        if (upper_window_shows(s)) {
            s->io->print_char(s->io->context, zscii);
            advance_upper_cursor(s, zscii);
        }
        return;
    }

    if (s->streams & ZM_STREAM_SCREEN) {
        s->io->print_char(s->io->context, zscii);
    }
    if ((s->streams & ZM_STREAM_TRANSCRIPT) && s->io->transcript_char != NULL) {
        s->io->transcript_char(s->io->context, zscii);
    }
}

void zm_screen_print_unicode(zm_screen *s, uint16_t unicode)
{
    uint16_t zscii = zm_unicode_to_zscii(s->mem, unicode);

    /* Spec §7.5.3: a character ZSCII can hold is printed as that; in
     * memory, one it cannot becomes a question mark. */
    if (zscii != 0 || (s->streams & ZM_STREAM_MEMORY)) {
        zm_screen_print(s, zscii != 0 ? zscii : '?');
        return;
    }

    zm_screen_sync_transcript(s);
    if (s->window == ZM_WINDOW_UPPER) {
        if (upper_window_shows(s)) {
            if (s->io->print_unicode != NULL) {
                s->io->print_unicode(s->io->context, unicode);
            } else {
                s->io->print_char(s->io->context, '?');
            }
            advance_upper_cursor(s, '?');
        }
        return;
    }
    if (s->streams & ZM_STREAM_SCREEN) {
        if (s->io->print_unicode != NULL) {
            s->io->print_unicode(s->io->context, unicode);
        } else {
            s->io->print_char(s->io->context, '?');
        }
    }
    /* Spec §7.5.2: the transcript may represent the character as it
     * likes; a question mark is honest about what it did not have. */
    if ((s->streams & ZM_STREAM_TRANSCRIPT) && s->io->transcript_char != NULL) {
        s->io->transcript_char(s->io->context, '?');
    }
}

uint16_t zm_screen_check_unicode(const zm_screen *s, uint16_t unicode)
{
    /* Spec §15 check_unicode: bit 0 for printing, bit 1 for typing. A
     * character with a ZSCII code goes both ways; one without can be
     * printed if the front end prints Unicode, and typed never, since
     * read stores ZSCII. */
    if (zm_unicode_to_zscii(s->mem, unicode) != 0) {
        return 3;
    }
    return s->io->print_unicode != NULL ? 1 : 0;
}

void zm_screen_echo_command(zm_screen *s, const uint16_t *zscii, unsigned count)
{
    zm_screen_sync_transcript(s);

    /* Spec §7.1.1.1: in Versions 1 to 5 the command is echoed to the
     * transcript, so that it reads as the session did. The screen echo
     * is the front end's, made as the player types. */
    if (s->mem->version <= 5 && (s->streams & ZM_STREAM_TRANSCRIPT) && s->io->transcript_char != NULL) {
        for (unsigned i = 0; i < count; i++) {
            s->io->transcript_char(s->io->context, zscii[i]);
        }
        s->io->transcript_char(s->io->context, ZM_ZSCII_NEWLINE);
    }

    /* Spec §7.1.2.3: stream 4 takes commands whole, once finished. */
    if ((s->streams & ZM_STREAM_COMMANDS) && s->io->record_input != NULL) {
        s->io->record_input(s->io->context, zscii, count);
    }
}

void zm_screen_echo_key(zm_screen *s, uint16_t zscii)
{
    if ((s->streams & ZM_STREAM_COMMANDS) && s->io->record_input != NULL) {
        s->io->record_input(s->io->context, &zscii, 1);
    }
}

/*
 * Windows.
 */

static void report_cursor(zm_screen *s)
{
    if (s->io->set_cursor != NULL && upper_window_shows(s)) {
        s->io->set_cursor(s->io->context, s->upper_row, s->upper_column);
    }
}

void zm_screen_split(zm_screen *s, unsigned lines)
{
    /* Spec §8.6.1.1, §8.7.2.1: the upper window takes the top lines of
     * the screen, and cannot sensibly take more than there are. */
    s->upper_height = lines > s->height ? s->height : lines;
    if (s->io->split_window != NULL) {
        s->io->split_window(s->io->context, s->upper_height);
    }

    /* Spec §8.6.1.1.2: in Version 3 a split clears the upper window. */
    if (s->mem->version == 3 && s->io->erase_window != NULL) {
        s->io->erase_window(s->io->context, ZM_WINDOW_UPPER);
    }

    /* Spec §8.7.2.1.1: a cursor left outside the new upper window goes
     * back to its top left. */
    if (s->window == ZM_WINDOW_UPPER && s->upper_row > s->upper_height) {
        s->upper_row = 1;
        s->upper_column = 1;
        report_cursor(s);
    }
}

void zm_screen_restart(zm_screen *s)
{
    /* Spec §6.1.3: Flags 2 survives, so a transcript that was running
     * keeps running; every other stream is as at the start (Spec
     * §7.1.1), and any memory stream is abandoned. */
    s->streams &= (unsigned)ZM_STREAM_TRANSCRIPT;
    s->streams |= (unsigned)ZM_STREAM_SCREEN;
    s->table_depth = 0;

    /* Spec §6.1.3: the Rst header fields are reset. */
    zm_screen_write_header(s);

    /* The text comes out as it did at first: plain, in the default
     * colours and font, word-wrapped. Spec §8.7.3.3: the whole screen is
     * cleared, which also collapses the upper window and selects the
     * lower. */
    zm_screen_set_style(s, ZM_STYLE_ROMAN);
    zm_screen_set_colour(s, ZM_COLOUR_DEFAULT, ZM_COLOUR_DEFAULT);
    zm_screen_set_buffering(s, 1);
    s->font = 1;
    zm_screen_erase_window(s, -1);
}

void zm_screen_set_window(zm_screen *s, unsigned window)
{
    s->window = window;
    if (window == ZM_WINDOW_UPPER) {
        /* Spec §8.6.1, §8.7.2: selecting the upper window puts its
         * cursor at the top left. */
        s->upper_row = 1;
        s->upper_column = 1;
    }
    if (s->io->set_window != NULL) {
        s->io->set_window(s->io->context, window);
    }
}

void zm_screen_erase_window(zm_screen *s, int16_t window)
{
    if (window == -1) {
        /* Spec §8.7.3.3: the whole screen, with the upper window
         * collapsed and the lower window selected. */
        s->upper_height = 0;
        s->window = ZM_WINDOW_LOWER;
        s->upper_row = 1;
        s->upper_column = 1;
    } else if (window == ZM_WINDOW_UPPER) {
        /* Spec §8.7.3.2.1: an erased upper window's cursor goes to the
         * top left in every Version. */
        s->upper_row = 1;
        s->upper_column = 1;
    }
    if (s->io->erase_window != NULL) {
        s->io->erase_window(s->io->context, window);
    }
}

void zm_screen_erase_line(zm_screen *s)
{
    if (s->io->erase_line != NULL) {
        s->io->erase_line(s->io->context);
    }
}

void zm_screen_set_cursor(zm_screen *s, unsigned row, unsigned column)
{
    /* Spec §8.7.2.3.1: the opcode has no effect in the lower window. */
    if (s->window != ZM_WINDOW_UPPER) {
        return;
    }

    /* Spec §8.7.2.3.1 makes a position outside the upper window illegal,
     * and Spec §8's Remarks note that games have often been lax about it
     * and recommend enlarging the window to contain the cursor instead
     * of halting; this does that, and keeps a column within the screen. */
    if (row < 1) {
        row = 1;
    }
    if (column < 1) {
        column = 1;
    }
    if (column > s->width) {
        column = s->width;
    }
    if (row > s->upper_height) {
        zm_screen_split(s, row);
        if (row > s->upper_height) {
            row = s->upper_height;
        }
    }
    s->upper_row = row;
    s->upper_column = column;
    report_cursor(s);
}

void zm_screen_get_cursor(const zm_screen *s, unsigned *row, unsigned *column)
{
    /* Spec §8.7.2.3.2: the upper window's cursor, whichever window is
     * selected. */
    *row = s->upper_row;
    *column = s->upper_column;
}

/*
 * Appearance.
 */

void zm_screen_set_style(zm_screen *s, unsigned style)
{
    /* Spec §15 set_text_style, Standard 1.1: Roman clears every style;
     * anything else adds to those already set. */
    s->style = style == ZM_STYLE_ROMAN ? ZM_STYLE_ROMAN : (s->style | style);
    if (s->io->set_text_style != NULL) {
        s->io->set_text_style(s->io->context, s->style);
    }
}

static unsigned resolve_colour(unsigned requested, unsigned current, unsigned default_colour)
{
    /* Spec §8.3.1: 0 keeps the current colour and 1 asks for the
     * default. */
    if (requested == ZM_COLOUR_CURRENT) {
        return current;
    }
    if (requested == ZM_COLOUR_DEFAULT) {
        return default_colour;
    }
    return requested;
}

void zm_screen_set_colour(zm_screen *s, unsigned foreground, unsigned background)
{
    unsigned default_foreground = s->io->default_foreground != 0 ? s->io->default_foreground : ZM_COLOUR_WHITE;
    unsigned default_background = s->io->default_background != 0 ? s->io->default_background : ZM_COLOUR_BLACK;

    s->foreground = resolve_colour(foreground, s->foreground, default_foreground);
    s->background = resolve_colour(background, s->background, default_background);
    if (s->io->set_colour != NULL) {
        s->io->set_colour(s->io->context, s->foreground, s->background);
    }
}

/* Spec §8.3.1: the true colour recommended for each of codes 2 to 12,
 * as 15-bit values with blue in the top five bits and red in the
 * bottom. */
static const uint16_t true_colours[] = {
    0x0000, 0x001d, 0x0340, 0x03bd, 0x59a0, 0x7c1f, 0x77a0, 0x7fff, 0x5ad6, 0x4631, 0x2d6b
};

static unsigned nearest_colour(uint16_t true_colour)
{
    unsigned best = ZM_COLOUR_BLACK;
    int32_t best_distance = -1;

    for (unsigned i = 0; i < sizeof true_colours / sizeof true_colours[0]; i++) {
        int32_t dr = (int32_t)(true_colour & 0x1f) - (int32_t)(true_colours[i] & 0x1f);
        int32_t dg = (int32_t)((true_colour >> 5) & 0x1f) - (int32_t)((true_colours[i] >> 5) & 0x1f);
        int32_t db = (int32_t)((true_colour >> 10) & 0x1f) - (int32_t)((true_colours[i] >> 10) & 0x1f);
        int32_t distance = dr * dr + dg * dg + db * db;

        if (best_distance < 0 || distance < best_distance) {
            best_distance = distance;
            best = ZM_COLOUR_BLACK + i;
        }
    }
    return best;
}

static unsigned colour_from_true(int16_t true_colour)
{
    /* Spec §8.3.7: -1 asks for the default and -2 for the current
     * colour, which set_colour's codes 1 and 0 already mean. */
    if (true_colour == -1) {
        return ZM_COLOUR_DEFAULT;
    }
    if (true_colour < 0) {
        return ZM_COLOUR_CURRENT;
    }
    return nearest_colour((uint16_t)true_colour);
}

void zm_screen_set_true_colour(zm_screen *s, int16_t foreground, int16_t background)
{
    /* Spec §8.3.7.1: the minimal implementation, which matches each
     * colour to the closest of the standard set. */
    zm_screen_set_colour(s, colour_from_true(foreground), colour_from_true(background));
}

unsigned zm_screen_set_font(zm_screen *s, unsigned font)
{
    unsigned previous = s->font;

    /* Spec §15 set_font: 0 asks which font is current and changes
     * nothing. A front end with fonts of its own answers for the rest;
     * without one there is only font 1 (Spec §8.1.3). */
    if (font == 0) {
        return previous;
    }
    if (s->io->set_font != NULL) {
        if (s->io->set_font(s->io->context, font) == 0) {
            return 0;
        }
    } else if (font != 1) {
        return 0;
    }
    s->font = font;
    return previous;
}

void zm_screen_set_buffering(zm_screen *s, int on)
{
    s->buffering = on != 0;
    if (s->io->set_buffering != NULL) {
        s->io->set_buffering(s->io->context, s->buffering);
    }
}

void zm_screen_sound_effect(zm_screen *s, unsigned number, unsigned effect, unsigned volume, unsigned repeats)
{
    if (s->io->sound_effect != NULL) {
        s->io->sound_effect(s->io->context, number, effect, volume, repeats);
    }
}
