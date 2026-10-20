/*
 * display.c: see display.h.
 */

#include "display.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "files.h"
#include "font.h"
#include "zm/screen.h"
#include "zm/text.h"

/* The font's size in points at a display scale of 1, where a point is a
 * pixel. */
#define FONT_SIZE 16.0f

/* How long the window may go without being drawn while the game runs. */
#define REFRESH_MS 100

/* The window, for message boxes about faults, which may come from deep
 * inside the machine. */
static SDL_Window *report_window;

/* The colours, for now the only ones there are. */
static const SDL_Color foreground = { 0x20, 0x20, 0x20, 0xff };
static const SDL_Color background = { 0xfa, 0xfa, 0xf7, 0xff };

/*
 * Drawing.
 */

/* The glyph for a character, drawn in white the first time it is asked
 * for. A character the font has no glyph for is drawn as a question mark,
 * as Spec §3.8.5.4.3 has it for one with no letter-form. */
static SDL_Texture *glyph(display *d, uint32_t ch)
{
    static const SDL_Color white = { 0xff, 0xff, 0xff, 0xff };
    SDL_Texture **page;
    SDL_Surface *surface;

    if (ch > 0xffff || !TTF_FontHasGlyph(d->font, ch)) {
        ch = '?';
    }
    page = d->glyphs[ch >> 8];
    if (page == NULL) {
        page = SDL_calloc(256, sizeof page[0]);
        if (page == NULL) {
            return NULL;
        }
        d->glyphs[ch >> 8] = page;
    }
    if (page[ch & 0xff] == NULL) {
        surface = TTF_RenderGlyph_Blended(d->font, ch, white);
        if (surface == NULL) {
            return NULL;
        }
        page[ch & 0xff] = SDL_CreateTextureFromSurface(d->renderer, surface);
        SDL_DestroySurface(surface);
    }
    return page[ch & 0xff];
}

static void draw_cell(display *d, unsigned row, unsigned column, uint32_t ch, unsigned style)
{
    SDL_FRect box;
    SDL_Color ink = foreground;
    SDL_Texture *texture;

    box.x = (float)(d->margin + (int)column * d->cell_width);
    box.y = (float)(d->margin + (int)row * d->cell_height);
    box.w = (float)d->cell_width;
    box.h = (float)d->cell_height;
    if (style & ZM_STYLE_REVERSE) {
        SDL_SetRenderDrawColor(d->renderer, foreground.r, foreground.g, foreground.b, foreground.a);
        SDL_RenderFillRect(d->renderer, &box);
        ink = background;
    }
    if (ch == ' ') {
        return;
    }
    texture = glyph(d, ch);
    if (texture == NULL) {
        return;
    }
    SDL_GetTextureSize(texture, &box.w, &box.h);
    SDL_SetTextureColorMod(texture, ink.r, ink.g, ink.b);
    SDL_RenderTexture(d->renderer, texture, NULL, &box);
}

/* Where the character at a position of the line being typed goes: the
 * line runs on from row to row, without regard to words. */
static void typed_cell(const display *d, unsigned position, unsigned *row, unsigned *column)
{
    unsigned offset = d->typed_column + position;

    *row = d->typed_row + offset / d->grid.columns;
    *column = offset % d->grid.columns;
}

static void draw_text(display *d, unsigned row, unsigned column, const char *text, unsigned style)
{
    for (; *text != '\0' && column < d->grid.columns; text++, column++) {
        draw_cell(d, row, column, (unsigned char)*text, style);
    }
}

/* Draw the grid, and over it the line being typed with its cursor, or
 * [MORE]. */
static void draw(display *d)
{
    grid *g = &d->grid;

    SDL_SetRenderDrawColor(d->renderer, background.r, background.g, background.b, background.a);
    SDL_RenderClear(d->renderer);
    for (unsigned r = 0; r < g->rows; r++) {
        for (unsigned c = 0; c < g->columns; c++) {
            const grid_cell *cell = grid_at(g, r, c);

            draw_cell(d, r, c, cell->ch, cell->style);
        }
    }
    if (d->typing) {
        unsigned row;
        unsigned column;
        SDL_FRect caret;

        for (unsigned i = 0; i < d->typed_count; i++) {
            uint16_t unicode = zm_zscii_to_unicode(d->mem, d->typed[i]);

            typed_cell(d, i, &row, &column);
            if (row < g->rows) {
                draw_cell(d, row, column, unicode != 0 ? unicode : '?', 0);
            }
        }
        typed_cell(d, d->typed_position, &row, &column);
        caret.x = (float)(d->margin + (int)column * d->cell_width);
        caret.y = (float)(d->margin + (int)row * d->cell_height);
        caret.w = (float)(d->cell_width / 8 > 2 ? d->cell_width / 8 : 2);
        caret.h = (float)d->cell_height;
        SDL_SetRenderDrawColor(d->renderer, foreground.r, foreground.g, foreground.b, foreground.a);
        SDL_RenderFillRect(d->renderer, &caret);
    }
    if (d->more) {
        draw_text(d, g->rows - 1u, 0, "[MORE]", ZM_STYLE_REVERSE);
    }
    SDL_RenderPresent(d->renderer);
    d->drawn_at = SDL_GetTicks();
    d->printed_since = 0;
}

/*
 * Events.
 */

/* Take note of the window's own events. Returns whether the window is
 * still open. */
static int handle_window_event(display *d, const SDL_Event *event)
{
    if (event->type == SDL_EVENT_QUIT || event->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
        d->closed = 1;
    }
    return !d->closed;
}

/* Wait for an event, drawing the window first, for no longer than until
 * the deadline in SDL ticks, if there is one. Returns 0 when the time is
 * up first. */
static int next_event(display *d, SDL_Event *event, Uint64 deadline)
{
    for (;;) {
        Sint32 wait = -1;

        draw(d);
        if (deadline != 0) {
            Uint64 now = SDL_GetTicks();

            if (now >= deadline) {
                return 0;
            }
            wait = (Sint32)(deadline - now);
        }
        if (SDL_WaitEventTimeout(event, wait)) {
            return 1;
        }
    }
}

/* The deadline for a timeout in tenths of a second, or 0 for none. */
static Uint64 deadline_of(unsigned timeout)
{
    return timeout != 0 ? SDL_GetTicks() + (Uint64)timeout * 100u : 0;
}

/* Spec §10.7: a character that can be typed is one ZSCII has an input
 * code for. */
static uint16_t typed_zscii(const display *d, Uint32 unicode)
{
    uint16_t zscii;

    if (unicode > 0xffff) {
        return 0;
    }
    zscii = zm_unicode_to_zscii(d->mem, (uint16_t)unicode);
    if ((zscii >= 32 && zscii <= 126) || (zscii >= ZM_ZSCII_EXTRA_FIRST && zscii <= 251)) {
        return zscii;
    }
    return 0;
}

/* Spec §3.8.4, §10.6: the keys that are not characters, as their ZSCII
 * input codes, or 0 for one that has none. */
static uint16_t key_zscii(SDL_Keycode key)
{
    switch (key) {
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        return ZM_ZSCII_NEWLINE;
    case SDLK_BACKSPACE:
    case SDLK_DELETE:
        return ZM_ZSCII_DELETE;
    case SDLK_ESCAPE:
        return ZM_ZSCII_ESCAPE;
    case SDLK_UP:
        return 129;
    case SDLK_DOWN:
        return 130;
    case SDLK_LEFT:
        return 131;
    case SDLK_RIGHT:
        return 132;
    default:
        if (key >= SDLK_F1 && key <= SDLK_F12) {
            return (uint16_t)(133u + (key - SDLK_F1));
        }
        return 0;
    }
}

/* The game has printed a screenful the player has not seen: show it with
 * [MORE] beneath, and wait for a key or a click. */
static void more(void *context)
{
    display *d = context;
    SDL_Event event;

    if (d->closed) {
        return;
    }
    d->more = 1;
    while (next_event(d, &event, 0) && handle_window_event(d, &event)) {
        if ((event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) || event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
            break;
        }
    }
    d->more = 0;
}

/* Called as the game prints: every so often, let the window be seen to,
 * so that a game working a long time between inputs is still drawn and
 * the system does not take it for stuck. Keys typed meanwhile wait in
 * the queue for the next read. */
static void printed(display *d)
{
    if (++d->printed_since % 256u == 0 && SDL_GetTicks() - d->drawn_at >= REFRESH_MS) {
        SDL_PumpEvents();
        draw(d);
    }
}

/*
 * Output.
 */

static void display_print_char(void *context, uint16_t zscii)
{
    display *d = context;
    uint16_t unicode;

    /* Spec §3.8.2.1: null prints nothing; Spec §3.8.5.4.3: a code with
     * no letter-form is a question mark. */
    if (zscii == ZM_ZSCII_NEWLINE) {
        grid_put(&d->grid, '\n');
    } else if (zscii != ZM_ZSCII_NULL) {
        unicode = zm_zscii_to_unicode(d->mem, zscii);
        grid_put_unit(&d->grid, unicode != 0 ? unicode : '?');
    }
    printed(d);
}

static void display_print_unicode(void *context, uint16_t unicode)
{
    display *d = context;

    grid_put_unit(&d->grid, unicode);
    printed(d);
}

static void display_show_status(void *context, const uint16_t *name, unsigned name_length,
                                int time_game, int16_t score_or_hours, int16_t turns_or_minutes)
{
    display *d = context;
    uint32_t text[DISPLAY_COLUMNS];
    char right[40];
    unsigned length = 0;

    for (unsigned i = 0; i < name_length && length < DISPLAY_COLUMNS; i++) {
        uint16_t unicode = zm_zscii_to_unicode(d->mem, name[i]);

        text[length++] = unicode != 0 ? unicode : '?';
    }

    /* Spec §8.2.3: score and turns, or the time on a 24-hour clock. */
    if (time_game) {
        snprintf(right, sizeof right, "Time: %d:%02d", (int)score_or_hours, (int)turns_or_minutes);
    } else {
        snprintf(right, sizeof right, "Score: %d  Moves: %d", (int)score_or_hours, (int)turns_or_minutes);
    }
    grid_status(&d->grid, text, length, right);
}

static void display_split_window(void *context, unsigned lines)
{
    display *d = context;

    grid_split(&d->grid, lines);
}

static void display_set_window(void *context, unsigned window)
{
    display *d = context;

    grid_select(&d->grid, window);
}

static void display_erase_window(void *context, int window)
{
    display *d = context;

    grid_erase(&d->grid, window);
}

static void display_erase_line(void *context)
{
    display *d = context;

    grid_erase_line(&d->grid);
}

static void display_set_cursor(void *context, unsigned row, unsigned column)
{
    display *d = context;

    grid_set_cursor(&d->grid, row, column);
}

static void display_set_text_style(void *context, unsigned style)
{
    display *d = context;

    d->grid.style = style;
}

/* Spec §8.1.2: everything is in one fixed-pitch font, which serves as
 * both the normal font and the fixed-pitch one. The picture font and the
 * character graphics font are not to be had. */
static unsigned display_set_font(void *context, unsigned font)
{
    display *d = context;

    if (font == 1 || font == 4) {
        unsigned previous = d->font_number;

        d->font_number = font;
        return previous;
    }
    return 0;
}

static void display_set_buffering(void *context, int on)
{
    display *d = context;

    d->grid.buffering = on;
}

/*
 * Input.
 */

static int display_read_char(void *context, unsigned timeout)
{
    display *d = context;
    Uint64 deadline = deadline_of(timeout);
    SDL_Event event;

    if (d->closed) {
        return ZM_INPUT_END;
    }
    grid_seen(&d->grid);
    d->resume = 0;
    for (;;) {
        uint16_t zscii = 0;

        if (!next_event(d, &event, deadline)) {
            return ZM_INPUT_TIMEOUT;
        }
        if (!handle_window_event(d, &event)) {
            return ZM_INPUT_END;
        }
        if (event.type == SDL_EVENT_KEY_DOWN) {
            zscii = key_zscii(event.key.key);
        } else if (event.type == SDL_EVENT_TEXT_INPUT) {
            const char *text = event.text.text;

            /* A character ZSCII cannot hold is not a key the game can be
             * given, so it is passed over and the wait goes on. */
            zscii = typed_zscii(d, SDL_StepUTF8(&text, NULL));
        }
        if (zscii != 0) {
            return zscii;
        }
    }
}

/* Make sure the line being typed, and the cursor after it, fit on the
 * screen, scrolling the lower window up under them if not. */
static void make_room(display *d)
{
    grid *g = &d->grid;
    unsigned row;
    unsigned column;

    typed_cell(d, d->typed_count, &row, &column);
    while (row >= g->rows && d->typed_row > grid_lower_top(g)) {
        grid_scroll(g);
        d->typed_row--;
        row--;
    }
}

/* Write the line as typed into the grid, where it was drawn, and leave
 * the lower window's cursor after it. */
static void commit_line(display *d)
{
    grid *g = &d->grid;
    unsigned row;
    unsigned column;

    for (unsigned i = 0; i < d->typed_count; i++) {
        uint16_t unicode = zm_zscii_to_unicode(d->mem, d->typed[i]);

        typed_cell(d, i, &row, &column);
        if (row < g->rows) {
            grid_cell *cell = grid_at(g, row, column);

            cell->ch = unicode != 0 ? unicode : '?';
            cell->style = 0;
        }
    }
    typed_cell(d, d->typed_count, &row, &column);
    if (column == 0 && row > d->typed_row) {
        /* The line ended at the right edge: the cursor waits there. */
        row--;
        column = g->columns;
    }
    g->lower_row = row < g->rows ? row : g->rows - 1u;
    g->lower_column = column;
    g->printed += d->typed_count;
    d->typing = 0;
}

static void remember(display *d, const uint16_t *line, unsigned count)
{
    if (count == 0) {
        return;
    }
    if (d->history_count == DISPLAY_HISTORY) {
        memmove(d->history[0], d->history[1], (DISPLAY_HISTORY - 1u) * sizeof d->history[0]);
        memmove(d->history_length, d->history_length + 1, (DISPLAY_HISTORY - 1u) * sizeof d->history_length[0]);
        d->history_count--;
    }
    memcpy(d->history[d->history_count], line, count * sizeof line[0]);
    d->history_length[d->history_count] = count;
    d->history_count++;
}

/* Where the line begins and what of it the player may edit. Spec §15
 * read: characters already in the buffer are left over from an earlier
 * read, and the game has shown them itself, so they stay as they are and
 * typing goes on after them. But when they are the display's own, from a
 * timed read that ran out, the line is taken up again: in place, if
 * nothing has been printed since, or else drawn afresh where the cursor
 * now is, "the interpreter should notice and redraw the input line so
 * far". */
static unsigned begin_line(display *d, unsigned count)
{
    grid *g = &d->grid;
    unsigned fixed = count;

    d->typed_row = g->lower_row;
    d->typed_column = g->lower_column;
    if (d->resume && count == d->resume_count) {
        unsigned long moved = g->scrolls - d->resume_scrolls;

        fixed = d->resume_fixed;
        if (g->printed == d->resume_printed && d->resume_row >= moved
            && d->resume_row - moved >= grid_lower_top(g)) {
            d->typed_row = d->resume_row - (unsigned)moved;
            d->typed_column = d->resume_column;
            grid_clear(g, d->typed_row, d->typed_column, count - fixed);
        }
    }
    d->resume = 0;
    return fixed;
}

static int display_read_line(void *context, uint16_t *buffer, unsigned *count, unsigned max,
                             uint16_t *terminator, unsigned timeout)
{
    display *d = context;
    Uint64 deadline = deadline_of(timeout);
    unsigned fixed = begin_line(d, *count);
    unsigned position = *count;
    unsigned recalled = d->history_count;
    SDL_Event event;

    if (d->closed) {
        return ZM_INPUT_END;
    }
    if (max > DISPLAY_INPUT_MAX) {
        max = DISPLAY_INPUT_MAX;
    }
    grid_seen(&d->grid);
    d->typing = 1;
    for (;;) {
        d->typed = buffer + fixed;
        d->typed_count = *count - fixed;
        d->typed_position = position - fixed;
        make_room(d);
        if (!next_event(d, &event, deadline)) {
            /* The time is up. The line stays on the screen as it is, with
             * the cursor after it, until the read is taken up again. */
            commit_line(d);
            d->resume = 1;
            d->resume_fixed = fixed;
            d->resume_count = *count;
            d->resume_row = d->typed_row;
            d->resume_column = d->typed_column;
            d->resume_scrolls = d->grid.scrolls;
            d->resume_printed = d->grid.printed;
            return ZM_INPUT_TIMEOUT;
        }
        if (!handle_window_event(d, &event)) {
            d->typing = 0;
            return ZM_INPUT_END;
        }
        if (event.type == SDL_EVENT_TEXT_INPUT) {
            const char *text = event.text.text;
            Uint32 unicode;

            while ((unicode = SDL_StepUTF8(&text, NULL)) != 0) {
                uint16_t zscii = typed_zscii(d, unicode);

                if (zscii != 0 && *count < max) {
                    memmove(buffer + position + 1, buffer + position, (*count - position) * sizeof buffer[0]);
                    buffer[position++] = zscii;
                    (*count)++;
                }
            }
            continue;
        }
        if (event.type != SDL_EVENT_KEY_DOWN) {
            continue;
        }
        switch (event.key.key) {
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            commit_line(d);
            remember(d, buffer + fixed, *count - fixed);
            grid_put(&d->grid, '\n');
            grid_seen(&d->grid);
            *terminator = ZM_ZSCII_NEWLINE;
            return 0;
        case SDLK_BACKSPACE:
            if (position > fixed) {
                position--;
                memmove(buffer + position, buffer + position + 1, (*count - position - 1u) * sizeof buffer[0]);
                (*count)--;
            }
            break;
        case SDLK_DELETE:
            if (position < *count) {
                memmove(buffer + position, buffer + position + 1, (*count - position - 1u) * sizeof buffer[0]);
                (*count)--;
            }
            break;
        case SDLK_LEFT:
            if (position > fixed) {
                position--;
            }
            break;
        case SDLK_RIGHT:
            if (position < *count) {
                position++;
            }
            break;
        case SDLK_HOME:
            position = fixed;
            break;
        case SDLK_END:
            position = *count;
            break;
        case SDLK_ESCAPE:
            *count = fixed;
            position = fixed;
            break;
        case SDLK_UP:
        case SDLK_DOWN:
            /* Earlier commands, one at a time, and past the newest an
             * empty line again. */
            if (event.key.key == SDLK_UP && recalled > 0) {
                recalled--;
            } else if (event.key.key == SDLK_DOWN && recalled < d->history_count) {
                recalled++;
            } else {
                break;
            }
            *count = fixed;
            if (recalled < d->history_count) {
                for (unsigned i = 0; i < d->history_length[recalled] && *count < max; i++) {
                    buffer[(*count)++] = d->history[recalled][i];
                }
            }
            position = *count;
            break;
        default:
            break;
        }
    }
}

/*
 * Files (Spec §15 save and restore, §7.6).
 */

/* A file dialog's answer, which may come on another thread. */
typedef struct dialog_answer {
    SDL_AtomicInt done;
    int chosen;
    char path[DISPLAY_PATH_MAX];
} dialog_answer;

static void SDLCALL dialog_done(void *userdata, const char *const *files, int filter)
{
    dialog_answer *answer = userdata;

    (void)filter;
    if (files != NULL && files[0] != NULL && strlen(files[0]) < sizeof answer->path) {
        strcpy(answer->path, files[0]);
        answer->chosen = 1;
    }
    SDL_SetAtomicInt(&answer->done, 1);
}

/* Ask the player for a file with the system's dialog, offering the given
 * one, and keep the window, if there is one, drawn until they answer.
 * Returns 0 if they chose none. */
static int run_dialog(display *d, int writing, const SDL_DialogFileFilter *filters, int filter_count,
                      const char *offered, char *path)
{
    SDL_Window *parent = d != NULL ? d->window : NULL;
    dialog_answer answer;
    SDL_Event event;

    memset(&answer, 0, sizeof answer);
    if (writing) {
        SDL_ShowSaveFileDialog(dialog_done, &answer, parent, filters, filter_count, offered);
    } else {
        SDL_ShowOpenFileDialog(dialog_done, &answer, parent, filters, filter_count, offered, false);
    }
    while (!SDL_GetAtomicInt(&answer.done)) {
        if (SDL_WaitEventTimeout(&event, 50) && d != NULL) {
            handle_window_event(d, &event);
        }
        if (d != NULL) {
            draw(d);
        }
    }
    if (!answer.chosen) {
        return 0;
    }
    strcpy(path, answer.path);
    return 1;
}

static int choose_file(display *d, int writing, const char *offered, char *path)
{
    static const SDL_DialogFileFilter saved_games[] = {
        { "Saved games", "sav;qzl" },
        { "All files", "*" },
    };

    return run_dialog(d, writing, saved_games, 2, offered, path);
}

int display_choose_story(char *path)
{
    static const SDL_DialogFileFilter stories[] = {
        { "Stories", "z1;z2;z3;z4;z5;z6;z7;z8;zblorb;zlb" },
        { "All files", "*" },
    };

    return run_dialog(NULL, 0, stories, 2, NULL, path);
}

/* A line of the interpreter's own, in the lower window. */
static void say(display *d, const char *text)
{
    unsigned window = d->grid.window;

    d->grid.window = ZM_WINDOW_LOWER;
    for (; *text != '\0'; text++) {
        grid_put(&d->grid, (unsigned char)*text);
    }
    grid_put(&d->grid, '\n');
    d->grid.window = window;
}

/* Settle which file a save or restore means. "All questions about
 * filenames are asked by interpreters" (Spec §15 save): a saved game is
 * always asked about, the last one offered again; an auxiliary file is
 * asked about unless the game said not to, and lives beside the story. */
static int file_for(display *d, zm_file_kind kind, const char *name, int prompt, int writing, char *path)
{
    char offered[DISPLAY_PATH_MAX];
    char auxiliary[64];

    if (kind == ZM_FILE_AUXILIARY) {
        files_auxiliary_name(name, auxiliary, sizeof auxiliary);
        if (strlen(d->story_directory) + strlen(auxiliary) >= sizeof offered) {
            return 0;
        }
        strcpy(offered, d->story_directory);
        strcat(offered, auxiliary);
        if (prompt == 0) {
            strcpy(path, offered);
            return 1;
        }
        return choose_file(d, writing, offered, path);
    }
    return choose_file(d, writing, d->save_path, path);
}

static int display_write_file(void *context, zm_file_kind kind, const char *name, int prompt,
                              const uint8_t *data, uint32_t size)
{
    display *d = context;
    char path[DISPLAY_PATH_MAX];
    FILE *file;
    int ok;

    if (!file_for(d, kind, name, prompt, 1, path)) {
        return 0;
    }
    file = fopen(path, "wb");
    if (file == NULL) {
        say(d, "That file cannot be written.");
        return 0;
    }
    ok = fwrite(data, 1, size, file) == size;
    ok = fclose(file) == 0 && ok;
    if (!ok) {
        say(d, "That file could not be written in full.");
    } else if (kind == ZM_FILE_SAVED_GAME) {
        strcpy(d->save_path, path);
    }
    return ok;
}

static uint8_t *display_read_file(void *context, zm_file_kind kind, const char *name, int prompt, uint32_t *size)
{
    display *d = context;
    char path[DISPLAY_PATH_MAX];
    size_t length;
    void *data;

    if (!file_for(d, kind, name, prompt, 0, path)) {
        return NULL;
    }
    data = SDL_LoadFile(path, &length);
    if (data == NULL) {
        say(d, "That file cannot be read.");
        return NULL;
    }
    if (kind == ZM_FILE_SAVED_GAME) {
        strcpy(d->save_path, path);
    }

    /* The machine frees what it is given with free(), and SDL may not
     * allocate with malloc(), so the bytes are moved to a buffer that
     * the C library gave. */
    {
        uint8_t *copy = malloc(length > 0 ? length : 1);

        if (copy != NULL) {
            memcpy(copy, data, length);
            *size = (uint32_t)length;
        }
        SDL_free(data);
        return copy;
    }
}

/*
 * Set-up.
 */

/* The directory part of a path, with its separator, or nothing. */
static void directory_of(const char *path, char *directory, size_t size)
{
    size_t length = 0;

    for (size_t i = 0; path[i] != '\0'; i++) {
        if (path[i] == '/' || path[i] == '\\') {
            length = i + 1;
        }
    }
    if (length >= size) {
        length = 0;
    }
    memcpy(directory, path, length);
    directory[length] = '\0';
}

/* Open the font at the window's scale, and size the window to the grid.
 * Spec §8.4: the screen the game is told about is DISPLAY_COLUMNS by
 * DISPLAY_ROWS. */
static int fit_window(display *d)
{
    float scale = SDL_GetWindowDisplayScale(d->window);
    float density = SDL_GetWindowPixelDensity(d->window);
    int unused;
    int advance = 0;
    int width;
    int height;

    if (scale <= 0.0f) {
        scale = 1.0f;
    }
    if (density <= 0.0f) {
        density = 1.0f;
    }
    d->font = TTF_OpenFontIO(SDL_IOFromConstMem(font_regular, (size_t)font_regular_size), true, FONT_SIZE * scale);
    if (d->font == NULL) {
        return 0;
    }
    if (!TTF_GetGlyphMetrics(d->font, 'M', &unused, &unused, &unused, &unused, &advance)) {
        return 0;
    }
    d->cell_width = advance;
    d->cell_height = TTF_GetFontLineSkip(d->font);
    if (d->cell_height < TTF_GetFontHeight(d->font)) {
        d->cell_height = TTF_GetFontHeight(d->font);
    }
    d->margin = d->cell_width;
    width = DISPLAY_COLUMNS * d->cell_width + 2 * d->margin;
    height = DISPLAY_ROWS * d->cell_height + 2 * d->margin;
    return SDL_SetWindowSize(d->window, (int)((float)width / density + 0.5f), (int)((float)height / density + 0.5f));
}

int display_open(display *d, const zm_memory *mem, const char *story_path, zm_io *io)
{
    char name[DISPLAY_PATH_MAX];
    char title[DISPLAY_PATH_MAX + 16];

    memset(d, 0, sizeof *d);
    d->mem = mem;
    d->font_number = 1;
    directory_of(story_path, d->story_directory, sizeof d->story_directory);
    files_default_save_name(story_path, name, sizeof name);
    if (strlen(d->story_directory) + strlen(name) < sizeof d->save_path) {
        strcpy(d->save_path, d->story_directory);
        strcat(d->save_path, name);
    } else {
        strcpy(d->save_path, name);
    }

    snprintf(title, sizeof title, "Quiddity \xe2\x80\x94 %s", story_path + strlen(d->story_directory));
    d->window = SDL_CreateWindow(title, 640, 480, SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (d->window == NULL) {
        return 0;
    }
    report_window = d->window;

    /* The window takes the size the grid needs before anything draws in
     * it, so that the renderer starts out the size it will stay. */
    if (!fit_window(d)) {
        display_close(d);
        return 0;
    }
    SDL_SyncWindow(d->window);
    d->renderer = SDL_CreateRenderer(d->window, NULL);
    if (d->renderer == NULL || !grid_init(&d->grid, mem->version, DISPLAY_COLUMNS, DISPLAY_ROWS)) {
        display_close(d);
        return 0;
    }
    d->grid.more = more;
    d->grid.more_context = d;
    SDL_SetRenderVSync(d->renderer, 1);
    SDL_StartTextInput(d->window);
    SDL_ShowWindow(d->window);

    memset(io, 0, sizeof *io);
    io->context = d;
    io->screen_width = DISPLAY_COLUMNS;
    io->screen_height = DISPLAY_ROWS;
    io->timed_input = 1;
    io->styles = ZM_STYLE_REVERSE | ZM_STYLE_FIXED_PITCH;
    io->default_foreground = 2;     /* black */
    io->default_background = 9;     /* white */

    io->print_char = display_print_char;
    io->print_unicode = display_print_unicode;
    io->read_line = display_read_line;
    io->read_char = display_read_char;
    io->show_status = display_show_status;
    io->split_window = display_split_window;
    io->set_window = display_set_window;
    io->erase_window = display_erase_window;
    io->erase_line = display_erase_line;
    io->set_cursor = display_set_cursor;
    io->set_text_style = display_set_text_style;
    io->set_font = display_set_font;
    io->set_buffering = display_set_buffering;
    io->write_file = display_write_file;
    io->read_file = display_read_file;
    return 1;
}

void display_finish(display *d)
{
    SDL_Event event;

    if (d->closed) {
        return;
    }
    say(d, "");
    say(d, "[The story has ended. Press any key to close the window.]");
    grid_seen(&d->grid);
    while (next_event(d, &event, 0) && handle_window_event(d, &event)) {
        if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
            break;
        }
    }
}

void display_close(display *d)
{
    for (unsigned i = 0; i < 256; i++) {
        if (d->glyphs[i] != NULL) {
            for (unsigned k = 0; k < 256; k++) {
                if (d->glyphs[i][k] != NULL) {
                    SDL_DestroyTexture(d->glyphs[i][k]);
                }
            }
            SDL_free(d->glyphs[i]);
            d->glyphs[i] = NULL;
        }
    }
    if (d->font != NULL) {
        TTF_CloseFont(d->font);
        d->font = NULL;
    }
    grid_free(&d->grid);
    if (d->renderer != NULL) {
        SDL_DestroyRenderer(d->renderer);
        d->renderer = NULL;
    }
    if (d->window != NULL) {
        SDL_DestroyWindow(d->window);
        d->window = NULL;
    }
    report_window = NULL;
}

void display_report(const char *message)
{
    fprintf(stderr, "quiddity-sdl: %s\n", message);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Quiddity", message, report_window);
}
