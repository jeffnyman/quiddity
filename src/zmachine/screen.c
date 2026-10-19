/*
 * screen.c: see zm/screen.h. Implements the output streams of Spec §7 and
 * the state of the screen model of Spec §8, including the eight windows
 * of Version 6 (Spec §8.8), and writes the header fields of Spec §11.1
 * that describe the interpreter.
 *
 * The file is in four parts: the header and set-up; the streams and
 * printing, where Version 6 branches off into its own layout of text
 * inside windows; the window operations of every Version; and the
 * operations that exist only in Version 6.
 */

#include "zm/screen.h"

#include <string.h>

#include "zm/fatal.h"
#include "zm/header.h"
#include "zm/text.h"

/* Spec §11.1.3: Infocom's number for their own DECSystem-20, the machine
 * with no screen features to speak of, which is how 'Beyond Zork' is
 * told not to expect the character graphics font (Spec §16). Version 6
 * is different: its Infocom story files were tailored to the machine
 * they shipped for, and the ones that survive with their pictures are
 * the MS-DOS releases, which expect the IBM PC's number (the "Infocom
 * fact sheet", and Spec §11.1.3). Spec §11.1.3.1: the version is
 * conventionally a capital letter. */
#define INTERPRETER_NUMBER 1
#define INTERPRETER_NUMBER_V6 6
#define INTERPRETER_VERSION 'Q'

/* Spec §8.4: the screen should be at least 60 by 14; this is the size a
 * front end gets unless it says otherwise. */
#define DEFAULT_WIDTH 80
#define DEFAULT_HEIGHT 24

static int is_v6(const zm_screen *s)
{
    return s->mem->version == 6;
}

static zm_window *window_at(zm_screen *s, unsigned number)
{
    return &s->windows[number];
}

static zm_window *current_window(zm_screen *s)
{
    return &s->windows[s->window];
}

static unsigned default_foreground(const zm_screen *s)
{
    return s->io->default_foreground != 0 ? s->io->default_foreground : ZM_COLOUR_WHITE;
}

static unsigned default_background(const zm_screen *s)
{
    return s->io->default_background != 0 ? s->io->default_background : ZM_COLOUR_BLACK;
}

/* Whether pictures can be shown: there must be a resource file with
 * pictures in it and a front end that draws them (Spec §8.8.6, §11.1.2
 * Remarks). */
static int pictures_available(const zm_screen *s)
{
    return is_v6(s) && s->resources != NULL && s->io->draw_picture != NULL
        && zm_blorb_picture_count(s->resources) > 0;
}

static int colours_available(const zm_screen *s)
{
    return s->io->colours || s->io->set_colour != NULL;
}

/* Spec §8.3.1: the true colour recommended for each of codes 2 to 12,
 * as 15-bit values with blue in the top five bits and red in the
 * bottom. */
static const uint16_t true_colours[] = {
    0x0000, 0x001d, 0x0340, 0x03bd, 0x59a0, 0x7c1f, 0x77a0, 0x7fff, 0x5ad6, 0x4631, 0x2d6b
};

/* Spec §8.8.3.2.8: the true colour a window is really using. */
static uint16_t true_colour_of(unsigned colour)
{
    if (colour >= ZM_COLOUR_BLACK && colour <= ZM_COLOUR_DARK_GREY) {
        return true_colours[colour - ZM_COLOUR_BLACK];
    }
    return 0;
}

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
                              | ZM_FLAGS1_V3_VARIABLE_PITCH | ZM_FLAGS1_V3_TANDY);
        if (io->show_status == NULL) {
            flags1 |= (unsigned)ZM_FLAGS1_V3_NO_STATUS_LINE;
        }
        if (io->split_window != NULL) {
            flags1 |= (unsigned)ZM_FLAGS1_V3_SCREEN_SPLITTING;
        }
        /* Spec §11 Remarks: bit 3 is the Tandy bit, which is nothing the
         * game can ask for; the front end says whether it is set. */
        if (io->tandy) {
            flags1 |= (unsigned)ZM_FLAGS1_V3_TANDY;
        }
    } else {
        /* Spec §11.1.4: each bit says whether the front end can do the
         * thing. */
        flags1 &= ~(unsigned)(ZM_FLAGS1_COLOURS | ZM_FLAGS1_PICTURES | ZM_FLAGS1_BOLD | ZM_FLAGS1_ITALIC
                              | ZM_FLAGS1_FIXED_PITCH | ZM_FLAGS1_SOUND | ZM_FLAGS1_TIMED_INPUT);
        if (io->timed_input) {
            flags1 |= (unsigned)ZM_FLAGS1_TIMED_INPUT;         /* Spec §10.5.3 */
        }
        if (colours_available(s)) {
            flags1 |= (unsigned)ZM_FLAGS1_COLOURS;             /* Spec §8.3.3 */
        }
        if (pictures_available(s)) {
            flags1 |= (unsigned)ZM_FLAGS1_PICTURES;            /* Spec §8.8.6, §11 Remarks */
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
     * zm_screen_sync_transcript(). No front end has a mouse or menus
     * yet. */
    flags2 &= ~(unsigned)(ZM_FLAGS2_MOUSE | ZM_FLAGS2_MENUS);

    /* Bit 3 asks for pictures in Version 6 (Spec §8.8.6) and for the
     * character graphics font in Version 5 (Spec §8.1.5.1): Beyond Zork
     * reads it back to decide whether its arrows and its map can be
     * drawn in that font, and on an IBM PC without it prints the
     * machine's own arrow codes instead. */
    if (is_v6(s) ? !pictures_available(s) : !(io->fonts & (1u << 3))) {
        flags2 &= ~(unsigned)ZM_FLAGS2_PICTURES;
    }
    if (io->sound_effect == NULL) {
        flags2 &= ~(unsigned)ZM_FLAGS2_SOUND;                  /* Spec §9.1.2 */
    }
    zm_write_word(mem, ZM_HEADER_FLAGS2, (uint16_t)flags2);

    if (mem->version >= 4) {
        /* Spec §11.1.3, §8.4: who the interpreter is and how big the
         * screen is, in lines and characters. */
        zm_write_byte(mem, ZM_HEADER_INTERPRETER_NUMBER,
                      (uint8_t)(io->interpreter_number != 0 ? io->interpreter_number
                                : is_v6(s) ? INTERPRETER_NUMBER_V6 : INTERPRETER_NUMBER));
        zm_write_byte(mem, ZM_HEADER_INTERPRETER_VERSION, INTERPRETER_VERSION);
        zm_write_byte(mem, ZM_HEADER_SCREEN_HEIGHT, (uint8_t)(s->height > 255 ? 255 : s->height));
        zm_write_byte(mem, ZM_HEADER_SCREEN_WIDTH, (uint8_t)(s->width > 255 ? 255 : s->width));
    }
    if (mem->version >= 5) {
        /* Spec §8.4.2, §8.4.3, §8.1.1: the screen in units and the size
         * of a character. Before Version 6 units are characters, so a
         * character is 1 by 1; in Version 6 the front end says, and the
         * two font bytes change places. */
        zm_write_word(mem, ZM_HEADER_SCREEN_WIDTH_UNITS, (uint16_t)(s->width * s->font_width));
        zm_write_word(mem, ZM_HEADER_SCREEN_HEIGHT_UNITS, (uint16_t)(s->height * s->font_height));
        if (is_v6(s)) {
            zm_write_byte(mem, ZM_HEADER_FONT_BYTE_26, (uint8_t)s->font_height);
            zm_write_byte(mem, ZM_HEADER_FONT_BYTE_27, (uint8_t)s->font_width);
        } else {
            zm_write_byte(mem, ZM_HEADER_FONT_BYTE_26, 1);
            zm_write_byte(mem, ZM_HEADER_FONT_BYTE_27, 1);
        }

        /* Spec §8.3.2, §8.3.3: the colours text starts in. */
        zm_write_byte(mem, ZM_HEADER_DEFAULT_BACKGROUND, (uint8_t)default_background(s));
        zm_write_byte(mem, ZM_HEADER_DEFAULT_FOREGROUND, (uint8_t)default_foreground(s));
    }

    /* Spec §11.1.5: the revision of the Standard this interpreter
     * follows. Inform games read it before using the opcodes the
     * revision added, such as check_unicode. */
    zm_write_byte(mem, ZM_HEADER_STANDARD_MAJOR, 1);
    zm_write_byte(mem, ZM_HEADER_STANDARD_MINOR, 1);

    /* Spec §11.1.7.3, §11.1.7.4: in the header extension table, if the
     * story has one long enough, the transparency request in Flags 3 is
     * cleared, since no front end draws it (Spec §8.3.6), and the true
     * colours of the defaults are written (Spec §8.3.7). */
    if (mem->version >= 5) {
        uint32_t table = zm_read_word(mem, ZM_HEADER_EXTENSION_TABLE);
        uint16_t words = table != 0 ? zm_read_word(mem, table) : 0;

        if (words >= ZM_HEADER_EXT_FLAGS3) {
            uint16_t flags3 = zm_read_word(mem, table + 2u * ZM_HEADER_EXT_FLAGS3);

            zm_write_word(mem, table + 2u * ZM_HEADER_EXT_FLAGS3, (uint16_t)(flags3 & ~1u));
        }
        if (words >= ZM_HEADER_EXT_TRUE_BACKGROUND) {
            zm_write_word(mem, table + 2u * ZM_HEADER_EXT_TRUE_FOREGROUND, true_colour_of(default_foreground(s)));
            zm_write_word(mem, table + 2u * ZM_HEADER_EXT_TRUE_BACKGROUND, true_colour_of(default_background(s)));
        }
    }
}

/* Spec §8.8.3.3: the eight windows as they begin. */
static void init_windows(zm_screen *s)
{
    for (unsigned i = 0; i < ZM_WINDOWS; i++) {
        zm_window *w = window_at(s, i);

        memset(w, 0, sizeof *w);
        w->y = 1;
        w->x = 1;
        w->cursor_y = 1;
        w->cursor_x = 1;
        w->foreground = (uint16_t)default_foreground(s);
        w->background = (uint16_t)default_background(s);
        w->font = 1;
        w->attributes = ZM_WINDOW_BUFFERING;
    }
    window_at(s, 0)->width = (uint16_t)(s->width * s->font_width);
    window_at(s, 0)->height = (uint16_t)(s->height * s->font_height);
    window_at(s, 0)->attributes = ZM_WINDOW_WRAPPING | ZM_WINDOW_SCROLLING | ZM_WINDOW_TRANSCRIPT | ZM_WINDOW_BUFFERING;
    window_at(s, 1)->width = (uint16_t)(s->width * s->font_width);
    s->window = 0;
    s->word_length = 0;
    s->cursor_shown = 1;
    s->mouse_window = 1;
}

void zm_screen_init(zm_screen *s, zm_memory *mem, const zm_io *io)
{
    memset(s, 0, sizeof *s);
    s->mem = mem;
    s->io = io;
    s->width = io->screen_width != 0 ? io->screen_width : DEFAULT_WIDTH;
    s->height = io->screen_height != 0 ? io->screen_height : DEFAULT_HEIGHT;
    s->font_width = 1;
    s->font_height = 1;
    if (mem->version == 6) {
        s->font_width = io->font_width != 0 ? io->font_width : 1;
        s->font_height = io->font_height != 0 ? io->font_height : 1;

        /* Spec §8.8.3.2.2.1 names the one story file that needs its
         * new-line interrupts in the other order. */
        s->zork_zero_393 = zm_read_word(mem, ZM_HEADER_RELEASE) == 393
                        && memcmp(mem->bytes + ZM_HEADER_SERIAL, "890714", 6) == 0;
    }

    /* Spec §7.1.1: the screen is always selected to begin with. Spec
     * §8.6.1, §8.7.2: the lower window, with no upper window. Spec
     * §7.2.1: buffering starts on. */
    s->streams = ZM_STREAM_SCREEN;
    s->window = ZM_WINDOW_LOWER;
    s->upper_row = 1;
    s->upper_column = 1;
    s->buffering = 1;
    s->font = 1;
    s->foreground = default_foreground(s);
    s->background = default_background(s);
    if (is_v6(s)) {
        init_windows(s);
    }

    zm_screen_write_header(s);

    /* Spec §15 restart: the transcript bit survives a restart, so a game
     * may begin with it set. */
    zm_screen_sync_transcript(s);
}

void zm_screen_use_resources(zm_screen *s, const zm_blorb *resources)
{
    s->resources = resources;
    zm_screen_write_header(s);
}

void zm_screen_set_interrupt(zm_screen *s, zm_screen_interrupt interrupt, void *context)
{
    s->interrupt = interrupt;
    s->interrupt_context = context;
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

/* Spec §15 output_stream: in Version 6 a formatted table is laid out as
 * if in a window, so its width is that window's between its margins. */
static uint16_t window_text_width(const zm_screen *s, unsigned number)
{
    const zm_window *w = &s->windows[number];

    return (uint16_t)(w->width > w->left_margin + w->right_margin ? w->width - w->left_margin - w->right_margin : 0);
}

/* Spec §7.1.2.1: finish the table's current line. A plain table takes a
 * new-line as a character; a formatted one (Spec §15 print_form) closes
 * the line's record, a count word and its characters, and opens the
 * next record with a count of 0, which is also how the table ends. */
static void memory_newline(zm_screen *s)
{
    unsigned top = s->table_depth - 1u;

    s->table_total_width[top] = (uint16_t)(s->table_total_width[top] + s->table_line_width[top]);
    s->table_line_width[top] = 0;
    if (s->table_widths[top] == ZM_TABLE_PLAIN) {
        zm_write_byte(s->mem, s->tables[top] + 2u + s->table_counts[top], ZM_ZSCII_NEWLINE);
        s->table_counts[top]++;
        return;
    }
    zm_write_word(s->mem, s->tables[top], s->table_counts[top]);
    s->tables[top] = s->tables[top] + 2u + s->table_counts[top];
    s->table_counts[top] = 0;
    zm_write_word(s->mem, s->tables[top], 0);
}

/* Spec §3.8.2.4, §3.8.2.5: in Version 6 ZSCII 9 is a tab, which indents
 * a paragraph by three spaces at the start of a line and is one space
 * anywhere else, and ZSCII 11 a sentence space, two spaces wide; so
 * Infocom's interpreters printed them. Each is kept as its code through
 * the word it leads (gather) and becomes its spaces when the word is
 * laid out (plot_word), so that the whole of it is dropped when the
 * word is moved to a new line, as on those interpreters and in Frotz. */
static unsigned code_units(const zm_screen *s, uint16_t code, int at_line_start)
{
    unsigned spaces = code == ZM_ZSCII_SPACE_SENTENCE ? 2u : code == ZM_ZSCII_TAB ? (at_line_start ? 3u : 1u) : 1u;

    return spaces * s->font_width;
}

static unsigned text_units(const zm_screen *s, const uint16_t *text, unsigned count, int at_line_start)
{
    unsigned units = 0;

    for (unsigned i = 0; i < count; i++) {
        units += code_units(s, text[i], at_line_start && i == 0);
    }
    return units;
}

static int is_spacing_code(uint16_t code)
{
    return code == ZM_ZSCII_TAB || code == ZM_ZSCII_SPACE_SENTENCE;
}

/* A space, tab, or sentence space: where a word ends, and what is lost
 * from the front of a word moved to a new line. */
static int is_spacing(uint16_t code)
{
    return code == ' ' || code == ZM_ZSCII_TAB || code == ZM_ZSCII_SPACE_SENTENCE;
}

static void memory_char(zm_screen *s, uint16_t zscii)
{
    unsigned top = s->table_depth - 1u;

    zm_write_byte(s->mem, s->tables[top] + 2u + s->table_counts[top], (uint8_t)zscii);
    s->table_counts[top]++;
    s->table_line_width[top] = (uint16_t)(s->table_line_width[top] + code_units(s, zscii, s->table_line_width[top] == 0));
}

static void flush_word(zm_screen *s);

void zm_screen_select_stream(zm_screen *s, int16_t stream, uint16_t table, int16_t width, int has_width)
{
    flush_word(s);
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
        s->table_widths[s->table_depth] = ZM_TABLE_PLAIN;
        s->table_line_width[s->table_depth] = 0;
        s->table_total_width[s->table_depth] = 0;
        if (is_v6(s) && has_width) {
            /* Spec §15 output_stream: a window number, or minus a width
             * in units. */
            if (width >= 0) {
                if (width >= ZM_WINDOWS) {
                    zm_fatal("output_stream 3 formatted for window %d, which does not exist", (int)width);
                }
                s->table_widths[s->table_depth] = window_text_width(s, (unsigned)width);
            } else {
                s->table_widths[s->table_depth] = (uint16_t)-width;
            }
            zm_write_word(s->mem, table, 0);
        }
        s->table_depth++;
        s->streams |= ZM_STREAM_MEMORY;
        break;
    case -3:
        if (s->table_depth > 0) {
            unsigned top = s->table_depth - 1u;

            if (s->table_widths[top] != ZM_TABLE_PLAIN) {
                memory_newline(s);
            } else {
                zm_write_word(s->mem, s->tables[top], s->table_counts[top]);
            }
            if (is_v6(s)) {
                /* Spec §7.1.2.1: the width of what was printed, in units,
                 * goes in the header: of the formatted text in all, or of
                 * the last line of a plain table, which is what a game
                 * measuring a string wants. */
                zm_write_word(s->mem, ZM_HEADER_STREAM3_WIDTH,
                              s->table_widths[top] != ZM_TABLE_PLAIN ? s->table_total_width[top]
                                                                      : s->table_line_width[top]);
            }
            s->table_depth--;
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
 * Printing before Version 6.
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

/*
 * Printing in Version 6 (Spec §8.8).
 *
 * Text is laid out here, in units, inside the current window. Each
 * character is font_width units wide: the front ends so far have one
 * fixed-pitch font, and the Standard's Remarks on §8 allow as much. The
 * cursor is kept inside the margins; a word that will not fit is moved
 * to the next line if the window wraps and clipped at the margin if it
 * does not (Spec §8.8.3.1.1), and a window that is full scrolls if its
 * attribute says so and otherwise starts again at the top, which is what
 * Infocom's interpreters did.
 */

static zm_pen pen_of(const zm_window *w)
{
    zm_pen pen;

    pen.style = w->style;
    pen.foreground = w->foreground;
    pen.background = w->background;
    pen.font = w->font;
    return pen;
}

/* Units from the cursor to the right margin, which may be none. */
static int units_left(const zm_window *w)
{
    return (int)w->width - (int)w->right_margin - (int)w->cursor_x + 1;
}

static void reset_cursor(zm_window *w)
{
    w->cursor_y = 1;
    w->cursor_x = (uint16_t)(w->left_margin + 1u);
}

/* Spec §8.8.3.2.2: a new-line counts the window's countdown down, and at
 * zero the game's routine is called before printing goes on. A routine
 * that prints new-lines of its own does not start another. */
static void countdown(zm_screen *s, zm_window *w)
{
    if (w->countdown == 0) {
        return;
    }
    w->countdown--;
    if (w->countdown == 0 && s->interrupt != NULL && !s->in_interrupt) {
        s->in_interrupt = 1;
        s->interrupt(s->interrupt_context, w->newline_routine);
        s->in_interrupt = 0;
    }
}

static void window_newline(zm_screen *s, zm_window *w)
{
    unsigned fh = s->font_height;

    if (s->io->plot_text == NULL && s->io->print_char != NULL) {
        s->io->print_char(s->io->context, ZM_ZSCII_NEWLINE);
        s->line_open = 0;
    }

    /* Spec §8.8.3.2.2.1: 'Zork Zero' release 393 wants the interrupt
     * after the cursor has moved; everyone else before. */
    if (!s->zork_zero_393) {
        countdown(s, w);
    }

    w->cursor_x = (uint16_t)(w->left_margin + 1u);
    if ((unsigned)w->cursor_y + 2u * fh - 1u > w->height) {
        if (w->attributes & ZM_WINDOW_SCROLLING) {
            if (s->io->scroll_area != NULL && w->height > 0 && w->width > 0) {
                zm_pen pen = pen_of(w);

                s->io->scroll_area(s->io->context, w->y, w->x, w->height, w->width, (int)fh, &pen);
            }
        } else {
            w->cursor_y = 1;
        }
    } else {
        w->cursor_y = (uint16_t)(w->cursor_y + fh);
    }

    /* Spec §8.8.3.2.6. */
    if (w->line_count != ZM_LINE_COUNT_NO_MORE) {
        w->line_count++;
    }

    if (s->zork_zero_393) {
        countdown(s, w);
    }
}

/* Put text on the canvas at the cursor and move the cursor past it. A
 * front end with no canvas but a print_char gets the text in printing
 * order, as a teletype would, with no layout at all. */
/* For a front end that gets the text in printing order, which has no
 * rows to keep text apart: a line with text on it is ended before the
 * window is erased, before the cursor goes to another row, and before
 * printing moves to another window, since what comes next is not part
 * of it. Rezrov's stream does the same, so the recordings read alike.
 * On a canvas the rows do this by themselves. */
static void end_line(zm_screen *s)
{
    if (s->io->plot_text != NULL || s->io->print_char == NULL) {
        return;
    }
    flush_word(s);
    if (s->line_open) {
        s->io->print_char(s->io->context, ZM_ZSCII_NEWLINE);
        s->line_open = 0;
    }
}

static void plot(zm_screen *s, zm_window *w, const uint16_t *text, unsigned count)
{
    if (s->io->plot_text != NULL && count > 0) {
        zm_pen pen = pen_of(w);

        s->io->plot_text(s->io->context, (unsigned)w->y + w->cursor_y - 1u, (unsigned)w->x + w->cursor_x - 1u,
                         text, count, &pen);
    } else if (s->io->plot_text == NULL && s->io->print_char != NULL) {
        /* A front end without a canvas gets the text in printing order,
         * and is told the font when it changes, since a character of
         * the graphics font (Spec §16) means nothing without that. */
        if (s->io->set_font != NULL && w->font != s->told_font) {
            s->io->set_font(s->io->context, w->font);
            s->told_font = w->font;
        }
        for (unsigned i = 0; i < count; i++) {
            uint16_t zscii = zm_unicode_to_zscii(s->mem, text[i]);

            s->io->print_char(s->io->context, zscii != 0 ? zscii : '?');
        }
        if (count > 0) {
            s->line_open = 1;
        }
    }
    w->cursor_x = (uint16_t)(w->cursor_x + count * s->font_width);
}

/* One character, unbuffered: Spec §8.8.3.1.1, wrapping or clipping at
 * the right margin. */
static int at_line_start(const zm_window *w)
{
    return w->cursor_x == w->left_margin + 1u;
}

/* Plot a word, with a tab or a sentence space in it as its spaces. */
static void plot_word(zm_screen *s, zm_window *w, const uint16_t *text, unsigned count)
{
    static const uint16_t spaces[3] = { ' ', ' ', ' ' };
    unsigned start = 0;

    for (unsigned i = 0; i < count; i++) {
        if (is_spacing_code(text[i])) {
            plot(s, w, text + start, i - start);
            plot(s, w, spaces, code_units(s, text[i], at_line_start(w)) / s->font_width);
            start = i + 1;
        }
    }
    plot(s, w, text + start, count - start);
}

static void put_char(zm_screen *s, zm_window *w, uint16_t unicode)
{
    if (is_spacing_code(unicode)) {
        unsigned spaces = code_units(s, unicode, at_line_start(w)) / s->font_width;

        for (unsigned i = 0; i < spaces; i++) {
            put_char(s, w, ' ');
        }
        return;
    }
    if ((int)s->font_width > units_left(w)) {
        if (!(w->attributes & ZM_WINDOW_WRAPPING)) {
            int edge = (int)w->width - (int)w->right_margin;

            w->cursor_x = (uint16_t)(edge < 1 ? 1 : edge);
            return;
        }
        window_newline(s, w);
    }
    plot(s, w, &unicode, 1);
}

/* A whole word, which buffered printing lays out at once (Spec
 * §8.8.3.1.2.2): it goes on the next line if it would cross the right
 * margin and the window wraps, losing the spacing that led it there. */
static void put_word(zm_screen *s, zm_window *w, const uint16_t *text, unsigned count)
{
    int width = (int)text_units(s, text, count, at_line_start(w));

    if (width <= units_left(w)) {
        plot_word(s, w, text, count);
        return;
    }
    if (!(w->attributes & ZM_WINDOW_WRAPPING)) {
        for (unsigned i = 0; i < count; i++) {
            put_char(s, w, text[i]);
        }
        return;
    }
    if (is_spacing(text[0])) {
        text++;
        count--;
    }
    if (count == 0) {
        return;
    }
    window_newline(s, w);
    width = (int)text_units(s, text, count, 1);
    if (width <= units_left(w)) {
        plot_word(s, w, text, count);
    } else {
        for (unsigned i = 0; i < count; i++) {
            put_char(s, w, text[i]);
        }
    }
}

/* Store a word in a formatted stream 3 table (Spec §15 output_stream):
 * on a new line if it would not fit the width, losing the spacing in
 * front of it, as Frotz does. The word is Unicode, except that a tab or
 * a sentence space is still its ZSCII code, and goes into the table as
 * that, to print as spaces when the table is printed back. */
static void memory_word(zm_screen *s, const uint16_t *text, unsigned count)
{
    unsigned top = s->table_depth - 1u;
    unsigned width = text_units(s, text, count, s->table_line_width[top] == 0);

    if (s->table_widths[top] != ZM_TABLE_PLAIN && s->table_line_width[top] + width > s->table_widths[top]) {
        if (is_spacing(text[0])) {
            text++;
            count--;
        }
        memory_newline(s);
    }
    for (unsigned i = 0; i < count; i++) {
        uint16_t zscii = is_spacing_code(text[i]) ? text[i] : zm_unicode_to_zscii(s->mem, text[i]);

        memory_char(s, zscii != 0 ? zscii : '?');
    }
}

/* Lay out the gathered word. The buffer is emptied first, because
 * laying it out may run a new-line interrupt, which may print. */
static void flush_word(zm_screen *s)
{
    uint16_t word[ZM_WORD_MAX];
    unsigned count = s->word_length;

    if (count == 0) {
        return;
    }
    memcpy(word, s->word, count * sizeof word[0]);
    s->word_length = 0;
    if (s->streams & ZM_STREAM_MEMORY) {
        memory_word(s, word, count);
    } else {
        put_word(s, current_window(s), word, count);
    }
}

void zm_screen_flush(zm_screen *s)
{
    flush_word(s);
}

/* A change of appearance ends the word so far, which is laid out as it
 * was, unless the word is only spacing, which belongs to the word to
 * come: Infocom's ZIP test sets a style after a sentence space, and the
 * space has to go with that word when it is moved to a new line. Text
 * on its way to a table is left alone altogether, since appearance
 * means nothing there and a line is never broken inside a word. Frotz
 * keeps the change in the word in both cases. */
static void flush_text(zm_screen *s)
{
    if (s->streams & ZM_STREAM_MEMORY) {
        return;
    }
    for (unsigned i = 0; i < s->word_length; i++) {
        if (!is_spacing(s->word[i])) {
            flush_word(s);
            return;
        }
    }
}

/* Gather a character into the word, breaking words at spacing and after
 * hyphens, as Infocom's interpreters did. */
static void gather(zm_screen *s, uint16_t unicode)
{
    if (is_spacing(unicode) || (s->word_length > 0 && s->word[s->word_length - 1] == '-' && unicode != '-')) {
        flush_word(s);
    }
    if (s->word_length == ZM_WORD_MAX) {
        flush_word(s);
    }
    s->word[s->word_length++] = unicode;
}

/* A character for the Version 6 screen: zscii is what the transcript
 * gets, 0 for a character ZSCII cannot hold. */
static void v6_print(zm_screen *s, uint16_t unicode, uint16_t zscii)
{
    zm_window *w = current_window(s);

    /* Spec §8.8.3.1: attribute 2 copies the window's text to the
     * transcript, a tab or a sentence space as its spaces. */
    if ((s->streams & ZM_STREAM_TRANSCRIPT) && (w->attributes & ZM_WINDOW_TRANSCRIPT)
        && s->io->transcript_char != NULL) {
        if (is_spacing_code(zscii)) {
            unsigned spaces = code_units(s, zscii, s->word_length == 0 && at_line_start(w)) / s->font_width;

            for (unsigned i = 0; i < spaces; i++) {
                s->io->transcript_char(s->io->context, ' ');
            }
        } else {
            s->io->transcript_char(s->io->context, zscii != 0 ? zscii : '?');
        }
    }
    if (!(s->streams & ZM_STREAM_SCREEN)) {
        return;
    }
    if (zscii == ZM_ZSCII_NEWLINE) {
        flush_word(s);
        window_newline(s, w);
    } else if (w->attributes & ZM_WINDOW_BUFFERING) {
        /* Buffered text is gathered into words so that a word is never
         * broken at the margin (Spec §8.8.3.1.1), on a canvas or in a
         * front end that only gets print_char; the latter must flush
         * the screen before it stops looking, as zm_screen_flush() is
         * called before every read and at quit. */
        gather(s, unicode);
    } else {
        put_char(s, w, unicode);
    }
}

/* A character for stream 3, which in Version 6 may be formatted into
 * lines and so gathers words like a window. */
static void memory_print(zm_screen *s, uint16_t unicode, uint16_t zscii)
{
    unsigned top = s->table_depth - 1u;

    if (zscii == ZM_ZSCII_NEWLINE) {
        flush_word(s);
        memory_newline(s);
    } else if (s->table_widths[top] != ZM_TABLE_PLAIN) {
        gather(s, unicode);
    } else {
        memory_char(s, zscii != 0 ? zscii : '?');
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
        if (is_v6(s)) {
            /* A tab or a sentence space keeps its code (memory_word);
             * every other character is gathered as Unicode. */
            uint16_t unicode = zm_zscii_to_unicode(s->mem, zscii);

            memory_print(s, is_spacing_code(zscii) ? zscii : unicode != 0 ? unicode : '?', zscii);
        } else {
            memory_char(s, zscii);
        }
        return;
    }

    if (is_v6(s)) {
        uint16_t unicode = zm_zscii_to_unicode(s->mem, zscii);

        /* A tab or a sentence space keeps its code to the window
         * (code_units); every other character goes as Unicode. */
        v6_print(s, is_spacing_code(zscii) ? zscii : unicode != 0 ? unicode : '?', zscii);
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

/* Spec §3.8.5.4.5: the control characters, which no screen shows and
 * a story must not print. */
static int is_control(uint16_t unicode)
{
    return unicode < 0x20 || (unicode >= 0x7f && unicode <= 0x9f);
}

void zm_screen_print_unicode(zm_screen *s, uint16_t unicode)
{
    uint16_t zscii = zm_unicode_to_zscii(s->mem, unicode);

    /* Spec §7.5.3: a character ZSCII can hold is printed as that; in
     * memory, one it cannot becomes a question mark. */
    if (zscii != 0 || ((s->streams & ZM_STREAM_MEMORY) && !is_v6(s))) {
        zm_screen_print(s, zscii != 0 ? zscii : '?');
        return;
    }

    zm_screen_sync_transcript(s);
    if (is_v6(s)) {
        /* The canvas takes Unicode as it is; stream 3 and the transcript
         * get a question mark. */
        if (s->streams & ZM_STREAM_MEMORY) {
            memory_print(s, unicode, 0);
        } else {
            v6_print(s, unicode, 0);
        }
        return;
    }
    if (s->window == ZM_WINDOW_UPPER) {
        if (upper_window_shows(s)) {
            if (s->io->print_unicode != NULL && !is_control(unicode)) {
                s->io->print_unicode(s->io->context, unicode);
            } else {
                s->io->print_char(s->io->context, '?');
            }
            advance_upper_cursor(s, '?');
        }
        return;
    }
    if (s->streams & ZM_STREAM_SCREEN) {
        /* Spec §3.8.5.4.5: a control character is not printed, and a
         * question mark says so, as for a character the front end does
         * not have. */
        if (s->io->print_unicode != NULL && !is_control(unicode)) {
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
     * character with a ZSCII code goes both ways, unless it is a control
     * character, which is never printable; one without a code can be
     * printed if the front end prints Unicode, which a Version 6 canvas
     * always does, and typed never, since read stores ZSCII. */
    if (zm_unicode_to_zscii(s->mem, unicode) != 0) {
        return is_control(unicode) ? 2 : 3;
    }
    if (is_control(unicode)) {
        return 0;
    }
    if (is_v6(s)) {
        return s->io->plot_text != NULL ? 1 : 0;
    }
    return s->io->print_unicode != NULL ? 1 : 0;
}

void zm_screen_echo_command(zm_screen *s, const uint16_t *zscii, unsigned count)
{
    zm_screen_sync_transcript(s);

    /* Spec §7.1.1.1: in Versions 1 to 5 the command is echoed to the
     * transcript, so that it reads as the session did; in Version 6
     * that is the game's job. The screen echo is the front end's, made
     * as the player types, or in Version 6 zm_screen_show_input()'s. */
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

void zm_screen_show_input(zm_screen *s, const uint16_t *zscii, unsigned count, int newline)
{
    zm_window *w;

    if (!is_v6(s)) {
        return;
    }

    /* Spec §7.1.1.1: to stream 1 only, in the window the read is in,
     * whatever other streams are selected. The line goes down in one
     * piece, as the player saw it typed. */
    flush_word(s);
    w = current_window(s);
    while (count > 0) {
        uint16_t text[ZM_WORD_MAX];
        unsigned piece = count < ZM_WORD_MAX ? count : ZM_WORD_MAX;

        for (unsigned i = 0; i < piece; i++) {
            uint16_t unicode = zm_zscii_to_unicode(s->mem, zscii[i]);

            text[i] = unicode != 0 ? unicode : '?';
        }
        put_word(s, w, text, piece);
        zscii += piece;
        count -= piece;
    }
    if (newline) {
        window_newline(s, w);
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

/* Spec §8.8.4.1: windows 0 and 1 are tiled one above the other, window
 * 1 of the given height at the top. Each cursor keeps its place on the
 * screen if it can, and goes to its window's top left if not. */
static void v6_split(zm_screen *s, unsigned height)
{
    zm_window *upper = window_at(s, 1);
    zm_window *lower = window_at(s, 0);
    unsigned screen_height = s->height * s->font_height;
    int cursor;

    if (height > screen_height) {
        height = screen_height;
    }
    cursor = (int)upper->cursor_y + (int)upper->y - 1;
    upper->y = 1;
    upper->height = (uint16_t)height;
    if (cursor > (int)height) {
        reset_cursor(upper);
    } else {
        upper->cursor_y = (uint16_t)cursor;
    }

    cursor = (int)lower->cursor_y + (int)lower->y - 1 - (int)height;
    lower->y = (uint16_t)(1 + height);
    lower->height = (uint16_t)(screen_height - height);
    if (cursor < 1) {
        reset_cursor(lower);
    } else {
        lower->cursor_y = (uint16_t)cursor;
    }
}

void zm_screen_split(zm_screen *s, unsigned size)
{
    if (is_v6(s)) {
        flush_word(s);
        v6_split(s, size);
        return;
    }

    /* Spec §8.6.1.1, §8.7.2.1: the upper window takes the top lines of
     * the screen, and cannot sensibly take more than there are. */
    s->upper_height = size > s->height ? s->height : size;
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
    s->word_length = 0;

    /* Spec §6.1.3: the Rst header fields are reset. */
    zm_screen_write_header(s);

    if (is_v6(s)) {
        /* Spec §8.8.3.3 again, and a cleared screen. */
        init_windows(s);
        zm_screen_erase_window(s, -1);
        return;
    }

    /* The text comes out as it did at first: plain, in the default
     * colours and font, word-wrapped. Spec §8.7.3.3: the whole screen is
     * cleared, which also collapses the upper window and selects the
     * lower. */
    zm_screen_set_style(s, ZM_STYLE_ROMAN);
    zm_screen_set_colour(s, s->window, ZM_COLOUR_DEFAULT, ZM_COLOUR_DEFAULT);
    zm_screen_set_buffering(s, 1);
    s->font = 1;
    zm_screen_erase_window(s, -1);
}

void zm_screen_set_window(zm_screen *s, unsigned window)
{
    if (is_v6(s)) {
        /* Spec §8.8.3: each window keeps its own cursor, so selecting
         * one moves nothing. */
        flush_word(s);
        if (window != s->window) {
            end_line(s);
        }
        s->window = window;
        return;
    }
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

/* Spec §8.8.5.3: erase a Version 6 window to its own background. */
static void erase_v6_window(zm_screen *s, zm_window *w)
{
    if (s->io->erase_area != NULL && w->height > 0 && w->width > 0) {
        zm_pen pen = pen_of(w);

        s->io->erase_area(s->io->context, w->y, w->x, w->height, w->width, &pen);
    }
    reset_cursor(w);
    w->line_count = 0;
}

static void erase_v6_screen(zm_screen *s, const zm_window *colours)
{
    if (s->io->erase_area != NULL) {
        zm_pen pen = pen_of(colours);

        s->io->erase_area(s->io->context, 1, 1, s->height * s->font_height, s->width * s->font_width, &pen);
    }
    for (unsigned i = 0; i < ZM_WINDOWS; i++) {
        window_at(s, i)->line_count = 0;
    }
}

void zm_screen_erase_window(zm_screen *s, int16_t window)
{
    if (is_v6(s)) {
        flush_word(s);
        end_line(s);
        if (window == -1) {
            /* Spec §8.8.5.3.1: the whole screen in window 0's colours,
             * unsplit, with window 0 selected and its cursor at the top
             * left (Spec §8.7.3.3). */
            erase_v6_screen(s, window_at(s, 0));
            v6_split(s, 0);
            s->window = 0;
            reset_cursor(window_at(s, 0));
        } else if (window == -2) {
            /* Spec §8.8.5.3.2: the whole screen in the current colours,
             * and nothing else. */
            erase_v6_screen(s, current_window(s));
        } else {
            erase_v6_window(s, window_at(s, (unsigned)window));
        }
        return;
    }

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

void zm_screen_erase_line(zm_screen *s, uint16_t value)
{
    if (is_v6(s)) {
        zm_window *w = current_window(s);
        int units = (int)value - 1;
        int left;

        /* Spec §15 erase_line: 1 means to the right margin; anything
         * else that many units less one, no further than the margin.
         * Spec §8.8.5.2: the cursor stays. */
        flush_word(s);
        left = units_left(w);
        if (units <= 0 || units > left) {
            units = left;
        }
        if (units > 0 && s->io->erase_area != NULL) {
            zm_pen pen = pen_of(w);

            s->io->erase_area(s->io->context, (unsigned)w->y + w->cursor_y - 1u, (unsigned)w->x + w->cursor_x - 1u,
                              s->font_height, (unsigned)units, &pen);
        }
        return;
    }

    /* Spec §15 erase_line: only the value 1 does anything before
     * Version 6. */
    if (value == 1 && s->io->erase_line != NULL) {
        s->io->erase_line(s->io->context);
    }
}

void zm_screen_set_cursor(zm_screen *s, unsigned window, int16_t y, int16_t x)
{
    unsigned row;
    unsigned column;

    if (is_v6(s)) {
        zm_window *w = window_at(s, window);

        flush_word(s);

        /* Spec §15 set_cursor: -1 turns the cursor off, -2 on. */
        if (y < 0) {
            if (y == -1) {
                s->cursor_shown = 0;
            } else if (y == -2) {
                s->cursor_shown = 1;
            }
            return;
        }

        /* A coordinate of 0 keeps the one the window has, as it does for
         * draw_picture; a column outside the margins goes to the left
         * margin (Spec §15 set_cursor). */
        if (y == 0) {
            y = (int16_t)w->cursor_y;
        }
        if (x == 0) {
            x = (int16_t)w->cursor_x;
        }
        if (x <= (int16_t)w->left_margin || x > (int)w->width - (int)w->right_margin) {
            x = (int16_t)(w->left_margin + 1u);
        }
        if ((uint16_t)y != w->cursor_y) {
            end_line(s);
        }
        w->cursor_y = (uint16_t)y;
        w->cursor_x = (uint16_t)x;
        return;
    }

    /* Spec §8.7.2.3.1: the opcode has no effect in the lower window. */
    if (s->window != ZM_WINDOW_UPPER) {
        return;
    }
    row = y < 1 ? 1u : (unsigned)y;
    column = x < 1 ? 1u : (unsigned)x;

    /* Spec §8.7.2.3.1 makes a position outside the upper window illegal,
     * and Spec §8's Remarks note that games have often been lax about it
     * and recommend enlarging the window to contain the cursor instead
     * of halting; this does that, and keeps a column within the screen. */
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

void zm_screen_get_cursor(zm_screen *s, unsigned *y, unsigned *x)
{
    if (is_v6(s)) {
        /* Spec §8.8.3.2.7: the position is only accurate once buffered
         * text is on the screen. */
        flush_word(s);
        *y = current_window(s)->cursor_y;
        *x = current_window(s)->cursor_x;
        return;
    }

    /* Spec §8.7.2.3.2: the upper window's cursor, whichever window is
     * selected. */
    *y = s->upper_row;
    *x = s->upper_column;
}

/*
 * Appearance.
 */

void zm_screen_set_style(zm_screen *s, unsigned style)
{
    /* Spec §15 set_text_style, Standard 1.1: Roman clears every style;
     * anything else adds to those already set. */
    if (is_v6(s)) {
        zm_window *w = current_window(s);

        flush_text(s);
        w->style = (uint16_t)(style == ZM_STYLE_ROMAN ? ZM_STYLE_ROMAN : (w->style | style));
        return;
    }
    s->style = style == ZM_STYLE_ROMAN ? ZM_STYLE_ROMAN : (s->style | style);
    if (s->io->set_text_style != NULL) {
        s->io->set_text_style(s->io->context, s->style);
    }
}

static unsigned resolve_colour(int16_t requested, unsigned current, unsigned default_colour)
{
    /* Spec §8.3.1: 0 keeps the current colour and 1 asks for the
     * default. Transparent and the colour under the cursor are Version
     * 6 requests no front end grants yet (Spec §8.3.6: "Interpreters
     * not supporting transparency must ignore any attempt to select
     * colour 15"), so they keep the current colour too. */
    if (requested == ZM_COLOUR_CURRENT || requested == -1 || requested == ZM_COLOUR_TRANSPARENT) {
        return current;
    }
    if (requested == ZM_COLOUR_DEFAULT) {
        return default_colour;
    }
    return (unsigned)requested;
}

void zm_screen_set_colour(zm_screen *s, unsigned window, int16_t foreground, int16_t background)
{
    if (is_v6(s)) {
        zm_window *w = window_at(s, window);

        /* Spec §8.3: in Version 6 each window has its own pair. */
        flush_text(s);
        w->foreground = (uint16_t)resolve_colour(foreground, w->foreground, default_foreground(s));
        w->background = (uint16_t)resolve_colour(background, w->background, default_background(s));
        return;
    }
    s->foreground = resolve_colour(foreground, s->foreground, default_foreground(s));
    s->background = resolve_colour(background, s->background, default_background(s));
    if (s->io->set_colour != NULL) {
        s->io->set_colour(s->io->context, s->foreground, s->background);
    }
}


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

static int16_t colour_from_true(int16_t true_colour)
{
    /* Spec §8.3.7: -1 asks for the default and -2 for the current
     * colour, which set_colour's codes 1 and 0 already mean; -3, the
     * colour under the cursor, and -4, transparent, are kept as the
     * current colour, like their set_colour counterparts. */
    if (true_colour == -1) {
        return ZM_COLOUR_DEFAULT;
    }
    if (true_colour < 0) {
        return ZM_COLOUR_CURRENT;
    }
    return (int16_t)nearest_colour((uint16_t)true_colour);
}

void zm_screen_set_true_colour(zm_screen *s, unsigned window, int16_t foreground, int16_t background)
{
    /* Spec §8.3.7.1: the minimal implementation, which matches each
     * colour to the closest of the standard set. */
    zm_screen_set_colour(s, window, colour_from_true(foreground), colour_from_true(background));
}

unsigned zm_screen_set_font(zm_screen *s, unsigned window, unsigned font)
{
    unsigned previous;

    /* Spec §15 set_font: 0 asks which font is current and changes
     * nothing. Spec §8.1.4: the picture font is never granted. */
    if (is_v6(s)) {
        zm_window *w = window_at(s, window);

        previous = w->font;
        if (font == 0) {
            return previous;
        }
        if (font != 1 && (font == 2 || font > 31 || !(s->io->fonts & (1u << font)))) {
            return 0;
        }
        flush_text(s);
        w->font = (uint16_t)font;
        return previous;
    }

    /* A front end with fonts of its own answers for the rest; without
     * one there is only font 1 (Spec §8.1.3). */
    previous = s->font;
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
    /* Spec §15 buffer_mode leaves its effect undefined in Version 6,
     * where each window has its own buffering attribute (Spec §7.2.2).
     * Infocom's own Version 6 games use it only to hurry out a row of
     * dots while they think, which needs no help now, and taking it to
     * mean window_style would turn off the word-wrapping of a game that
     * never turns it back on. So it does nothing there. */
    if (is_v6(s)) {
        return;
    }
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

/*
 * Version 6 only.
 */

void zm_screen_move_window(zm_screen *s, unsigned window, uint16_t y, uint16_t x)
{
    zm_window *w = window_at(s, window);

    /* Spec §15 move_window: "nothing actually happens", but any future
     * plotting happens in the new place. */
    flush_word(s);
    w->y = y;
    w->x = x;
}

void zm_screen_size_window(zm_screen *s, unsigned window, uint16_t height, uint16_t width)
{
    zm_window *w = window_at(s, window);

    /* Spec §8.8.3.4: a cursor left outside goes to the top left. */
    flush_word(s);
    w->height = height;
    w->width = width;
    if (w->cursor_y > height || w->cursor_x > width) {
        reset_cursor(w);
    }
}

void zm_screen_style_window(zm_screen *s, unsigned window, uint16_t flags, unsigned operation)
{
    zm_window *w = window_at(s, window);

    /* Spec §15 window_style: set, add, remove, or flip. */
    flush_word(s);
    switch (operation) {
    case 0:
        w->attributes = (uint16_t)(flags & 15u);
        break;
    case 1:
        w->attributes |= (uint16_t)(flags & 15u);
        break;
    case 2:
        w->attributes &= (uint16_t)~(flags & 15u);
        break;
    case 3:
        w->attributes ^= (uint16_t)(flags & 15u);
        break;
    default:
        zm_fatal("window_style with operation %u; only 0 to 3 are defined", operation);
    }
}

uint16_t zm_screen_window_property(zm_screen *s, unsigned window, unsigned property)
{
    const zm_window *w;

    flush_word(s);
    w = window_at(s, window);
    switch (property) {
    case ZM_WINDOW_PROP_Y:                return w->y;
    case ZM_WINDOW_PROP_X:                return w->x;
    case ZM_WINDOW_PROP_HEIGHT:           return w->height;
    case ZM_WINDOW_PROP_WIDTH:            return w->width;
    case ZM_WINDOW_PROP_CURSOR_Y:         return w->cursor_y;
    case ZM_WINDOW_PROP_CURSOR_X:         return w->cursor_x;
    case ZM_WINDOW_PROP_LEFT_MARGIN:      return w->left_margin;
    case ZM_WINDOW_PROP_RIGHT_MARGIN:     return w->right_margin;
    case ZM_WINDOW_PROP_NEWLINE_ROUTINE:  return w->newline_routine;
    case ZM_WINDOW_PROP_COUNTDOWN:        return w->countdown;
    case ZM_WINDOW_PROP_STYLE:            return w->style;
    case ZM_WINDOW_PROP_COLOUR:
        /* Spec §8.8.3.2.4: foreground in the low byte, background in
         * the high. */
        return (uint16_t)((w->background << 8) | w->foreground);
    case ZM_WINDOW_PROP_FONT:             return w->font;
    case ZM_WINDOW_PROP_FONT_SIZE:
        /* Spec §8.8.3.2.5: height in the high byte, width in the low. */
        return (uint16_t)((s->font_height << 8) | s->font_width);
    case ZM_WINDOW_PROP_ATTRIBUTES:       return w->attributes;
    case ZM_WINDOW_PROP_LINE_COUNT:       return (uint16_t)w->line_count;
    case ZM_WINDOW_PROP_TRUE_FOREGROUND:  return true_colour_of(w->foreground);
    case ZM_WINDOW_PROP_TRUE_BACKGROUND:  return true_colour_of(w->background);
    default:
        zm_fatal("get_wind_prop: window property %u does not exist", property);
    }
}

void zm_screen_put_window_property(zm_screen *s, unsigned window, unsigned property, uint16_t value)
{
    zm_window *w;

    /* Spec §8.8.3.2: properties 0 to 15 may be written, though a game
     * should only need the interrupt routine, the countdown, and the
     * line count; the true colours may not be. */
    flush_word(s);
    w = window_at(s, window);
    switch (property) {
    case ZM_WINDOW_PROP_Y:                w->y = value; break;
    case ZM_WINDOW_PROP_X:                w->x = value; break;
    case ZM_WINDOW_PROP_HEIGHT:           w->height = value; break;
    case ZM_WINDOW_PROP_WIDTH:            w->width = value; break;
    case ZM_WINDOW_PROP_CURSOR_Y:         w->cursor_y = value; break;
    case ZM_WINDOW_PROP_CURSOR_X:         w->cursor_x = value; break;
    case ZM_WINDOW_PROP_LEFT_MARGIN:      w->left_margin = value; break;
    case ZM_WINDOW_PROP_RIGHT_MARGIN:     w->right_margin = value; break;
    case ZM_WINDOW_PROP_NEWLINE_ROUTINE:  w->newline_routine = value; break;
    case ZM_WINDOW_PROP_COUNTDOWN:        w->countdown = value; break;
    case ZM_WINDOW_PROP_STYLE:            w->style = value; break;
    case ZM_WINDOW_PROP_COLOUR:
        w->foreground = (uint16_t)(value & 0xff);
        w->background = (uint16_t)(value >> 8);
        break;
    case ZM_WINDOW_PROP_FONT:             w->font = value; break;
    case ZM_WINDOW_PROP_FONT_SIZE:        break;      /* the font is what it is */
    case ZM_WINDOW_PROP_ATTRIBUTES:       w->attributes = (uint16_t)(value & 15u); break;
    case ZM_WINDOW_PROP_LINE_COUNT:       w->line_count = (int16_t)value; break;
    case ZM_WINDOW_PROP_TRUE_FOREGROUND:
    case ZM_WINDOW_PROP_TRUE_BACKGROUND:
        zm_fatal("put_wind_prop: window property %u, a true colour, may not be written", property);
    default:
        zm_fatal("put_wind_prop: window property %u does not exist", property);
    }
}

void zm_screen_scroll_window(zm_screen *s, unsigned window, int16_t units)
{
    zm_window *w = window_at(s, window);

    /* Spec §15 scroll_window: in that window's colours. */
    flush_word(s);
    if (s->io->scroll_area != NULL && units != 0 && w->height > 0 && w->width > 0) {
        zm_pen pen = pen_of(w);

        s->io->scroll_area(s->io->context, w->y, w->x, w->height, w->width, units, &pen);
    }
}

void zm_screen_set_margins(zm_screen *s, unsigned window, uint16_t left, uint16_t right)
{
    zm_window *w = window_at(s, window);

    /* Spec §15 set_margins, §8.8.3.2.2.2: a cursor overtaken by the
     * margins goes to the left margin of its line. */
    flush_word(s);
    w->left_margin = left;
    w->right_margin = right;
    if (w->cursor_x <= left || (int)w->cursor_x > (int)w->width - (int)right) {
        w->cursor_x = (uint16_t)(left + 1u);
    }
}

/* The size a picture takes on this screen, in units. */
static void picture_size(const zm_screen *s, const zm_picture *picture, unsigned *height, unsigned *width)
{
    if (s->io->picture_size != NULL) {
        s->io->picture_size(s->io->context, picture, height, width);
    } else {
        zm_picture_scaled_size(picture, s->width * s->font_width, s->height * s->font_height, width, height);
    }
}

int zm_screen_picture_data(zm_screen *s, unsigned number, uint16_t *height, uint16_t *width)
{
    zm_picture picture;
    unsigned h;
    unsigned w;

    *height = 0;
    *width = 0;
    if (!pictures_available(s)) {
        return 0;
    }
    if (number == 0) {
        /* Spec §15 picture_data: how many pictures, and the release of
         * the picture file. */
        *height = (uint16_t)zm_blorb_picture_count(s->resources);
        *width = zm_blorb_release(s->resources);
        return *height > 0;
    }
    if (!zm_blorb_picture(s->resources, number, &picture)) {
        return 0;
    }
    picture_size(s, &picture, &h, &w);
    *height = (uint16_t)h;
    *width = (uint16_t)w;
    return 1;
}

/* Where a picture goes: (y, x) in the current window, or the cursor for
 * a coordinate of 0 (Spec §15 draw_picture), as screen coordinates. */
static void picture_position(zm_screen *s, uint16_t *y, uint16_t *x)
{
    const zm_window *w = current_window(s);

    if (*y == 0) {
        *y = w->cursor_y;
    }
    if (*x == 0) {
        *x = w->cursor_x;
    }
    *y = (uint16_t)(*y + w->y - 1u);
    *x = (uint16_t)(*x + w->x - 1u);
}

void zm_screen_draw_picture(zm_screen *s, unsigned number, uint16_t y, uint16_t x)
{
    zm_picture picture;
    unsigned height;
    unsigned width;

    flush_word(s);
    picture_position(s, &y, &x);
    if (!pictures_available(s)) {
        return;
    }
    if (!zm_blorb_picture(s->resources, number, &picture)) {
        zm_fatal("draw_picture: there is no picture %u", number);
    }

    /* Blorb §2.3: a placeholder has a size but nothing to draw. */
    if (picture.format == ZM_PICTURE_RECT) {
        return;
    }
    picture_size(s, &picture, &height, &width);
    s->io->draw_picture(s->io->context, &picture, y, x, height, width);
}

void zm_screen_erase_picture(zm_screen *s, unsigned number, uint16_t y, uint16_t x)
{
    uint16_t height;
    uint16_t width;

    /* Spec §8.8.5.1: the space the picture would take, in the current
     * window's background. */
    flush_word(s);
    picture_position(s, &y, &x);
    if (zm_screen_picture_data(s, number, &height, &width) && s->io->erase_area != NULL
        && height > 0 && width > 0) {
        zm_pen pen = pen_of(current_window(s));

        s->io->erase_area(s->io->context, y, x, height, width, &pen);
    }
}

void zm_screen_mouse_window(zm_screen *s, int window)
{
    s->mouse_window = window;
}

uint16_t zm_screen_buffer_screen(zm_screen *s, int16_t mode)
{
    /* Spec §8.8.7: the front ends so far draw straight onto the screen,
     * so this interpreter "must always act as though the mode is 0":
     * the request is remembered, nothing changes, and 0 is what the
     * old state is reported as. -1 asks for an update, which is already
     * done. */
    if (mode == 0 || mode == 1) {
        s->screen_buffering = mode;
    }
    return 0;
}
