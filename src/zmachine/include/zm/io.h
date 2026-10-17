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
 * and the part Quiddity is willing to vary. Every function takes the
 * context pointer first so a front end can keep its state wherever it
 * likes. A function left NULL means the front end cannot do that thing:
 * the machine says so in the header where the Standard provides a bit
 * for it (Spec §11.1.4) and otherwise behaves as if it had been done, so
 * a game never faults for want of a screen feature.
 *
 * The screen functions report decisions the core has already made (see
 * zm/screen.h); the front end's job is to make them visible.
 */

#ifndef ZM_IO_H
#define ZM_IO_H

#include <stdint.h>

/* What a file the game asks for is (Spec §15 save and restore): a saved
 * game, which the machine writes and reads in Quetzal form (zm/quetzal.h),
 * or an auxiliary file of the game's own bytes (Spec §7.6). */
typedef enum zm_file_kind {
    ZM_FILE_SAVED_GAME,
    ZM_FILE_AUXILIARY
} zm_file_kind;

/* What read_line and read_char return instead of input. */
enum {
    ZM_INPUT_END = -1,
    ZM_INPUT_TIMEOUT = -2
};

typedef struct zm_io {
    void *context;

    /*
     * How the front end describes itself.
     */

    /* The screen in characters and lines (Spec §8.4); 0 means the
     * default of 80 by 24. */
    unsigned screen_width;
    unsigned screen_height;

    /* The text styles (ZM_STYLE_* in zm/screen.h) the front end can
     * show, for the header's capability bits (Spec §11.1.4). */
    unsigned styles;

    /* The colours text starts in, as codes 2 to 9 (Spec §8.3.1); 0
     * means white on black. */
    unsigned default_foreground;
    unsigned default_background;

    /* Whether read_line and read_char honour their timeout (Spec
     * §10.5.3), for the header's timed-input bit. A front end that
     * cannot keep time leaves this 0 and ignores the timeout. */
    int timed_input;

    /*
     * Output.
     */

    /* Print one ZSCII character (Spec §3.8) to the current window. New-
     * line is ZSCII 13. The front end turns the code into something
     * visible with zm_zscii_to_unicode() or its own tables. */
    void (*print_char)(void *context, uint16_t zscii);

    /* Print a character ZSCII has no code for (Spec §15 print_unicode,
     * §3.8.5.4). Left NULL, a question mark is printed instead. */
    void (*print_unicode)(void *context, uint16_t unicode);

    /*
     * Input.
     */

    /* Read a line of input for the read opcode (Spec §15 read, §10.5).
     * buffer already holds *count ZSCII characters, left over from an
     * interrupted read or typed before a timeout; add what the player
     * types after them, up to max in all, update *count, and put the
     * terminating character in *terminator, which is ZSCII 13 for a
     * new-line. Only characters defined for input and output may be
     * stored (Spec §10.7.2). Return 0 for a line, ZM_INPUT_END when there
     * is no more input to be had, which ends the game, or, when timeout
     * is not 0 and that many tenths of a second pass first,
     * ZM_INPUT_TIMEOUT, with whatever was typed so far kept in the
     * buffer and counted; the read may then be continued with another
     * call. */
    int (*read_line)(void *context, uint16_t *buffer, unsigned *count, unsigned max,
                     uint16_t *terminator, unsigned timeout);

    /* Read one keypress for read_char (Spec §15 read_char, §10.6) as a
     * ZSCII input code, with new-line as 13, or ZM_INPUT_END at the end
     * of input, or ZM_INPUT_TIMEOUT when the timeout, in tenths of a
     * second if not 0, passes first. */
    int (*read_char)(void *context, unsigned timeout);

    /*
     * The screen (Spec §8).
     */

    /* Show the status line (Spec §8.2): the location's short name, as
     * ZSCII, and either the score and turn count or, for a time game,
     * the hours and minutes. Called before each read in Versions 1 to 3
     * and by show_status (Spec §8.2.4). A front end that leaves this
     * NULL has the "status line not available" bit set for it. */
    void (*show_status)(void *context, const uint16_t *name, unsigned name_length,
                        int time_game, int16_t score_or_hours, int16_t turns_or_minutes);

    /* Give the upper window this many lines (Spec §8.6.1.1, §8.7.2.1).
     * A front end with no upper window leaves this NULL: text printed to
     * the upper window is then dropped, and in Version 3 the header says
     * the screen cannot be split (Spec §8.6.1.2). */
    void (*split_window)(void *context, unsigned lines);

    /* Select the lower (0) or upper (1) window for printing (Spec §15
     * set_window). The upper window's cursor returns to its top left. */
    void (*set_window)(void *context, unsigned window);

    /* Clear a window (Spec §15 erase_window): 0 or 1 clears that window;
     * -1 clears the whole screen with the upper window already collapsed
     * and the lower one selected; -2 clears the screen and nothing else.
     * Cursor movements are given in Spec §8.7.3.2.1 and §8.7.3.3. */
    void (*erase_window)(void *context, int window);

    /* Clear from the cursor to the end of its line in the current window
     * (Spec §15 erase_line). */
    void (*erase_line)(void *context);

    /* Move the upper window's cursor, (1, 1) being its top left (Spec
     * §15 set_cursor). Only ever called with the upper window selected
     * and the row within it. */
    void (*set_cursor)(void *context, unsigned row, unsigned column);

    /* Change the text style to this combination of ZM_STYLE_* bits (Spec
     * §15 set_text_style, §8.7.1). */
    void (*set_text_style)(void *context, unsigned style);

    /* Change the colours, as codes 2 to 12 (Spec §15 set_colour, §8.3).
     * A front end that leaves this NULL has the colour bit cleared. */
    void (*set_colour)(void *context, unsigned foreground, unsigned background);

    /* Change the font (Spec §15 set_font, §8.1.2) and return the one it
     * replaces, or 0 if the font is unavailable and nothing changed.
     * Left NULL, only font 1 exists. */
    unsigned (*set_font)(void *context, unsigned font);

    /* Turn word-wrapping of lower-window text on or off (Spec §15
     * buffer_mode, §7.2). */
    void (*set_buffering)(void *context, int on);

    /*
     * Files (Spec §7.1.1, §7.1.2.3).
     */

    /* Start or stop the transcript, output stream 2, and return whether
     * it is now running. Called once per change, however many times the
     * game asks (Spec §7.1.1.2). Left NULL, there is no transcript. */
    int (*set_transcript)(void *context, int on);

    /* Write one ZSCII character to the transcript. */
    void (*transcript_char)(void *context, uint16_t zscii);

    /* Record a command or a keypress, as ZSCII without a terminator, to
     * the command file, output stream 4. Left NULL, stream 4 records
     * nothing. */
    void (*record_input)(void *context, const uint16_t *zscii, unsigned count);

    /* Keep a file for the save opcode (Spec §15 save). The front end
     * chooses where it goes and asks the player what it likes; "all
     * questions about filenames are asked by interpreters". For an
     * auxiliary file, name is the file name the game suggests as ASCII,
     * or NULL if it gave none, and prompt is 1 if the game wants the
     * player asked, 0 if not, and -1 if it did not say (Spec §15 save,
     * Standard 1.1). For a saved game both are always NULL and -1. Return
     * nonzero once the bytes are safely written. Left NULL, saving
     * fails, and the game says so in its own words. */
    int (*write_file)(void *context, zm_file_kind kind, const char *name, int prompt,
                      const uint8_t *data, uint32_t size);

    /* Fetch a file back for restore (Spec §15 restore), with the same
     * arguments. Return its bytes in a buffer obtained from malloc, which
     * the machine frees, and its size in *size; or NULL if there is no
     * file to be had, the player having declined or named a file that
     * does not exist. Left NULL, restoring fails. */
    uint8_t *(*read_file)(void *context, zm_file_kind kind, const char *name, int prompt, uint32_t *size);

    /*
     * Sound (Spec §9).
     */

    /* Act on a sound: numbers 1 and 2 are bleeps, higher ones samples
     * the front end finds for the game. The effect is ZM_SOUND_* from
     * zm/screen.h; volume 1 to 8 or 255 for loudest; repeats a count or
     * 255 for ever. Left NULL, the header says there is no sound. */
    void (*sound_effect)(void *context, unsigned number, unsigned effect, unsigned volume, unsigned repeats);
} zm_io;

#endif /* ZM_IO_H */
