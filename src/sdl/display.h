/*
 * display.h: the Z-machine's I/O, realised in an SDL window.
 *
 * This supplies the zm_io functions (see zm/io.h) for the windowed front
 * end. The screen is a grid of character cells in one fixed-pitch font
 * (grid.h), drawn whenever the game stops to wait for the player, and
 * every so often while it runs.
 *
 * Everything happens on the one thread, the machine's. When the game asks
 * for input, the display takes the window's events until it has some:
 * keys for the game, and the window's own business of being redrawn or
 * closed. A timed read waits for events only until its time is up. The
 * window being closed ends the game, as the end of input does.
 *
 * A line is typed with a small editor of the display's own, which draws
 * the line over the grid until it is entered: the cursor keys move along
 * it, and up and down bring back earlier commands. Saved games and the
 * auxiliary files a game asks the player to name are chosen with the
 * system's own file dialogs.
 *
 * This is the first stage of the front end, for Versions 1 to 5. Text
 * styles other than reverse video, colours, and sound are not shown yet,
 * and the header says so. A Version 6 story is given its text in the order
 * it was printed, in the lower window, as on a teletype.
 */

#ifndef QUIDDITY_DISPLAY_H
#define QUIDDITY_DISPLAY_H

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include "grid.h"
#include "zm/io.h"
#include "zm/memory.h"

/* The screen the game is told about (Spec §8.4). */
#define DISPLAY_COLUMNS 80
#define DISPLAY_ROWS 24

#define DISPLAY_PATH_MAX 1024
#define DISPLAY_HISTORY 32
#define DISPLAY_INPUT_MAX 256

/* The colours the window is drawn in: those the system is set to, light
 * or dark, or one of them chosen. A theme lasts the session, since a
 * game is told the colours at the start (Spec §8.3) and may have chosen
 * its own by them. */
typedef enum display_theme {
    DISPLAY_THEME_SYSTEM,
    DISPLAY_THEME_LIGHT,
    DISPLAY_THEME_DARK
} display_theme;

typedef struct display {
    const zm_memory *mem;
    SDL_Window *window;
    SDL_Renderer *renderer;
    TTF_Font *font;

    /* Glyphs drawn in white, to be tinted as they are used, in pages of
     * 256 code points made as they are needed. */
    SDL_Texture **glyphs[256];

    /* A cell's size, and the margin around the grid, in pixels. */
    int cell_width;
    int cell_height;
    int margin;

    grid grid;

    /* The theme's colours, for text and for the background. */
    SDL_Color foreground;
    SDL_Color background;

    /* The font the game last chose (Spec §8.1.2), 1 or 4, which are the
     * same one here. */
    unsigned font_number;

    /* Whether the player has closed the window. */
    int closed;

    /* The line being typed (see display.c), drawn over the grid while it
     * is: the characters from fixed onward of the buffer, from a row and
     * column of the grid, with the cursor at position. */
    int typing;
    const uint16_t *typed;
    unsigned typed_count;
    unsigned typed_position;
    unsigned typed_row;
    unsigned typed_column;

    /* A line whose timed read ran out, as it was left on the screen, so
     * that the read can carry on where it was (Spec §15 read). */
    int resume;
    unsigned resume_fixed;
    unsigned resume_count;
    unsigned resume_row;
    unsigned resume_column;
    unsigned long resume_scrolls;
    unsigned long resume_printed;

    /* Earlier commands, the newest last, as ZSCII. */
    uint16_t history[DISPLAY_HISTORY][DISPLAY_INPUT_MAX];
    unsigned history_length[DISPLAY_HISTORY];
    unsigned history_count;

    /* Whether [MORE] is showing (Spec §8.4.1). */
    int more;

    /* When the window was last drawn, and how many characters have been
     * printed since it was last thought about, so that a game that runs
     * a long time is still seen to and the window still answers. */
    Uint64 drawn_at;
    unsigned printed_since;

    /* The directory the story is in, where files are offered, and the
     * file the last saved game went to or came from. */
    char story_directory[DISPLAY_PATH_MAX];
    char save_path[DISPLAY_PATH_MAX];
} display;

/* Ask the player which story to play, with the system's dialog, before
 * there is a window. path has room for DISPLAY_PATH_MAX bytes. Returns 0
 * if they chose none. */
int display_choose_story(char *path);

/* Open the window for a story, in a theme, and fill in the I/O struct to
 * point at it. SDL and SDL_ttf must already be started. Returns 0, with
 * the reason given by SDL_GetError(), if the window cannot be had. */
int display_open(display *d, const zm_memory *mem, const char *story_path, display_theme theme, zm_io *io);

/* The game is over: unless the player closed the window, leave its last
 * words up until they press a key or close it. */
void display_finish(display *d);

/* Close the window. */
void display_close(display *d);

/* Tell the player about a fault, in a message box over the window if
 * there is one, as well as on standard error. */
void display_report(const char *message);

#endif /* QUIDDITY_DISPLAY_H */
