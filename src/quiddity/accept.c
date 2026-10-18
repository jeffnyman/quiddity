/*
 * accept.c: see accept.h.
 *
 * The file is in three parts: the stream front end, which is a zm_io
 * whose output is a growing buffer and whose input is the script; the
 * play, which runs a script through a machine with that front end; and
 * the comparison, which finds the first line where a play and its
 * recording differ and says where that is in the script.
 */

#include "accept.h"

#include <errno.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "utf8.h"
#include "zm/blorb.h"
#include "zm/fatal.h"
#include "zm/io.h"
#include "zm/machine.h"
#include "zm/memory.h"
#include "zm/screen.h"
#include "zm/status.h"
#include "zm/text.h"

/* The screen the stream claims to be (Spec §8.4): 80 characters wide,
 * and 255 lines high, which Spec §8.4.1 says means infinite. The
 * Standard advises against that number, for the sake of games that send
 * the cursor to the bottom of the screen, but a stream has no bottom to
 * send it to, and a game that sizes its work by the height then rolls
 * its dice as it does on the other plain front ends that say 255, so a
 * recording made on one of those plays here unchanged. */
#define STREAM_WIDTH 80
#define STREAM_HEIGHT 255

/* Spec §8.8.1: for a Version 6 game a character is 4 units wide and 1
 * high, since Infocom's games measure sideways in pixels and downward in
 * lines when they have no pictures. */
#define STREAM_FONT_WIDTH 4
#define STREAM_FONT_HEIGHT 1

/* The most codes a line of the script can hold. */
#define LINE_CODES 512

/* Spec §16: the character graphics font, as the nearest Unicode
 * characters, since a stream of text can carry those and a map drawn in
 * it (Beyond Zork's) should read as a map. Codes 32 to 126, in order. */
static const uint16_t font3_glyphs[95] = {
    0x0020, 0x2190, 0x2192, 0x2571, 0x2572, 0x0020, 0x2500, 0x2500,
    0x2502, 0x2502, 0x2534, 0x252c, 0x251c, 0x2524, 0x2514, 0x250c,
    0x2510, 0x2518, 0x2571, 0x250c, 0x2510, 0x2518, 0x2588, 0x2580,
    0x2584, 0x258c, 0x2590, 0x2584, 0x2580, 0x258c, 0x2590, 0x2580,
    0x2584, 0x2584, 0x2580, 0x2580, 0x2584, 0x2584, 0x2580, 0x0027,
    0x002e, 0x002e, 0x0027, 0x2580, 0x2584, 0x258c, 0x2590, 0x2550,
    0x258c, 0x258c, 0x258c, 0x258c, 0x2588, 0x2588, 0x2588, 0x2588,
    0x2590, 0x258c, 0x2573, 0x253c, 0x2191, 0x2193, 0x2195, 0x25a1,
    0x003f, 0x16aa, 0x16d2, 0x16c7, 0x16de, 0x16d6, 0x16a0, 0x16b7,
    0x16bb, 0x16c1, 0x16c4, 0x16e4, 0x16da, 0x16d7, 0x16be, 0x16a9,
    0x16c8, 0x16b3, 0x16b1, 0x16cb, 0x16cf, 0x16a2, 0x16e0, 0x16b9,
    0x16c9, 0x16a3, 0x16df, 0x2593, 0x2593, 0x2592, 0x2591,
};

/* Spec §11.1.3: the interpreter numbers by name, for the INTERPRETER
 * directive. */
static const struct {
    const char *name;
    unsigned number;
} interpreter_names[] = {
    { "dec20", 1 },    { "apple2e", 2 }, { "macintosh", 3 }, { "amiga", 4 },  { "atarist", 5 }, { "ibmpc", 6 },
    { "c128", 7 },     { "c64", 8 },     { "apple2c", 9 },   { "apple2gs", 10 }, { "tandy", 11 },
};

/* An auxiliary file the game wrote (Spec §7.6), kept for the run. */
typedef struct kept_file {
    char *name;
    uint8_t *data;
    uint32_t size;
} kept_file;

/* The state of a play. It lives on the heap rather than the stack so
 * that a fault, which comes back through longjmp, finds it intact. */
typedef struct accept_play_state {
    const accept_script *script;
    zm_memory mem;
    zm_blorb resources;
    int have_resources;
    zm_machine machine;
    zm_io io;
    int display;
    int resume;

    /* The play so far. */
    char *out;
    size_t out_length;
    size_t out_capacity;

    /* Where the script has got to, and how long the play was when each
     * command was read. A command is "peeked" when a read begins and
     * taken when its text is handed over; the offset is noted, and a
     * change of seed made, at the peek, before any timer routine of the
     * read has printed anything, which is where Rezrov notes them. */
    unsigned next;
    int peeked;
    unsigned *offsets;
    unsigned offset_count;
    unsigned offset_capacity;
    int script_ended;
    int announced;

    /* The read whose timer was last allowed to run: see timer_fires(). */
    int timed_pending;
    uint32_t timed_pc;

    /* The keys of a line being handed out one read_char at a time. */
    uint16_t keys[LINE_CODES];
    unsigned key_count;
    unsigned key_next;

    /* Spec §8.1.2: the font text is being printed in. */
    unsigned font;

    /* The first half of a surrogate pair, waiting for the second. */
    uint16_t high_surrogate;

    /* The saved game and the auxiliary files, in memory. */
    uint8_t *saved;
    uint32_t saved_size;
    kept_file *files;
    unsigned file_count;

    char *message;
} accept_play_state;

/*
 * Output.
 */

static void emit_bytes(accept_play_state *p, const char *bytes, size_t count)
{
    if (p->out_length + count + 1 > p->out_capacity) {
        size_t capacity = p->out_capacity == 0 ? 4096 : p->out_capacity * 2;
        char *grown;

        while (capacity < p->out_length + count + 1) {
            capacity *= 2;
        }
        grown = realloc(p->out, capacity);
        if (grown == NULL) {
            return;
        }
        p->out = grown;
        p->out_capacity = capacity;
    }
    memcpy(p->out + p->out_length, bytes, count);
    p->out_length += count;
    p->out[p->out_length] = '\0';
    if (p->display) {
        fwrite(bytes, 1, count, stdout);
    }
}

/* One character of the play. A story that prints a character beyond
 * the Basic Multilingual Plane, an emoji say, can only do it as two
 * print_unicode calls with a surrogate pair, since the opcode takes 16
 * bits; the pair is put back together here so the record holds the
 * character and not two halves nothing can read. */
static void emit_char(accept_play_state *p, uint16_t unicode)
{
    char bytes[4];

    if (p->high_surrogate != 0) {
        uint16_t high = p->high_surrogate;

        p->high_surrogate = 0;
        if (unicode >= 0xdc00 && unicode <= 0xdfff) {
            emit_bytes(p, bytes, utf8_encode_pair(high, unicode, bytes));
            return;
        }
        emit_bytes(p, bytes, utf8_encode(high, bytes));
    }
    if (unicode >= 0xd800 && unicode <= 0xdbff) {
        p->high_surrogate = unicode;
        return;
    }
    emit_bytes(p, bytes, utf8_encode(unicode, bytes));
}

static void emit_zscii(accept_play_state *p, uint16_t zscii)
{
    uint16_t unicode;

    if (zscii == ZM_ZSCII_NEWLINE) {
        emit_char(p, '\n');
        return;
    }
    if (p->font == 3 && zscii >= 32 && zscii <= 126) {
        unicode = font3_glyphs[zscii - 32];
    } else {
        unicode = zm_zscii_to_unicode(&p->mem, zscii);
    }
    emit_char(p, unicode != 0 ? unicode : '?');
}

static void stream_print_char(void *context, uint16_t zscii)
{
    emit_zscii(context, zscii);
}

static void stream_print_unicode(void *context, uint16_t unicode)
{
    emit_char(context, unicode);
}

/* Spec §8.1.2: the normal font, the fixed-pitch one, which a stream of
 * text is in anyway, and the character graphics font, which the stream
 * spells in Unicode. Font 2 was never defined. A game that has switched
 * to the graphics font for a border needs the fixed-pitch font said yes
 * to before it can switch back, since a refused font leaves the current
 * one in force: Journey prints its command menu that way. */
static unsigned stream_set_font(void *context, unsigned font)
{
    accept_play_state *p = context;
    unsigned previous = p->font;

    if (font != 1 && font != 3 && font != 4) {
        return 0;
    }
    p->font = font;
    return previous;
}

/*
 * Input.
 */

/* Spec §10.7: the characters that can end a line of input besides the
 * return key: the function keys, which a game must have asked for, and
 * the clicks. A line of the script that ends in one is a command ended
 * by that key, as a command file records it (Spec §7.1.2.3). */
static int is_terminator(uint16_t code)
{
    return (code >= 129 && code <= 154) || (code >= 252 && code <= 254);
}

/* Reads [digits] starting at the bracket at *i, leaving *i after the
 * closing bracket, or returns 0 and leaves *i alone so that a bare
 * bracket is taken as the character it is. */
static int bracketed_code(const char *text, size_t *i, unsigned *code)
{
    size_t end = *i + 1;
    unsigned value = 0;

    while (text[end] >= '0' && text[end] <= '9') {
        value = value * 10 + (unsigned)(text[end] - '0');
        end++;
        if (value > 0xffff) {
            return 0;
        }
    }
    if (end == *i + 1 || text[end] != ']') {
        return 0;
    }
    *code = value;
    *i = end + 1;
    return 1;
}

/* One line of the script as ZSCII codes, in the format a command file
 * uses (Spec §7.1.2.3 and §7 Remarks): a plain character is itself, and
 * a code in square brackets is a ZSCII value, which is how a function
 * key, a timeout, or a click is written. Frotz writes its own hot keys
 * as codes from 1000 up, which mean nothing to any other interpreter.
 * A click is followed by its coordinates, which this front end has no
 * mouse to honour. */
static void decode_line(accept_play_state *p, const char *text, uint16_t *codes, unsigned *count)
{
    size_t i = 0;

    *count = 0;
    while (text[i] != '\0' && *count < LINE_CODES) {
        unsigned code;

        if (text[i] == '[' && bracketed_code(text, &i, &code)) {
            if (code >= 1000) {
                continue;
            }
            codes[(*count)++] = (uint16_t)code;
            if (code >= 252 && code <= 254) {
                unsigned position;

                for (unsigned k = 0; k < 2 && text[i] == '[' && bracketed_code(text, &i, &position); k++) {
                }
            }
        } else {
            uint32_t unicode = utf8_next((const unsigned char *)text, &i);

            if (unicode <= 0xffff) {
                uint16_t zscii = zm_unicode_to_zscii(&p->mem, (uint16_t)unicode);

                if (zscii != 0) {
                    codes[(*count)++] = zscii;
                }
            }
        }
    }
}

/* Whether the script has anything left: a command still to play, or
 * keys of the last line still to hand out. */
static int has_more(const accept_play_state *p)
{
    return p->key_next < p->key_count || p->next < p->script->command_count;
}

/* A read has begun, so the script's next command is looked at, once:
 * its place in the play is noted, and a change of seed the script asks
 * for before it is made. Reseeding as the command is reached means the
 * game's rolls for that command onward come from the new stream, and
 * nothing before it is touched. It is the session's kind of seed, not
 * Spec §2.4.2's predictable state. */
static void peek_line(accept_play_state *p)
{
    const accept_script *s = p->script;
    uint32_t seed;

    if (p->peeked) {
        return;
    }
    p->peeked = 1;
    if (p->next <= s->command_count && accept_script_seed_change(s, p->next, &seed)) {
        zm_machine_seed_session(&p->machine, seed);
    }
    if (p->next >= s->command_count) {
        /* Past the end for good, so a change after the last command is
         * applied once and not on every later read. */
        p->next = s->command_count + 1;
        return;
    }
    if (p->offset_count == p->offset_capacity) {
        unsigned capacity = p->offset_capacity == 0 ? 64 : p->offset_capacity * 2;
        unsigned *grown = realloc(p->offsets, capacity * sizeof *grown);

        if (grown != NULL) {
            p->offsets = grown;
            p->offset_capacity = capacity;
        }
    }
    if (p->offset_count < p->offset_capacity) {
        p->offsets[p->offset_count++] = (unsigned)p->out_length;
    }
}

/* Whether a timed read's routine is to run before the script answers
 * it. A replay cannot wait, so Rezrov's rule is followed, since the
 * recordings were made by it (Spec §10.2 is deviated from to this
 * extent): for a timed read, one interval is taken to have passed
 * before the script's input arrives, so the interrupt routine runs
 * once, and if it ends the read, the read ends with nothing typed and
 * the script is left for the next one. Without this a title screen that
 * waits a while for any key, as Arthur's does, swallows a script's first
 * command; and a routine that rolls dice while it waits, as All Roads'
 * title does, rolls them the same number of times in both interpreters,
 * which is what keeps the two plays together.
 *
 * Keys of a line still under the fingers beat the clock, and so does
 * the same key read asked again straight after its routine ended it:
 * Custard's editor ends every timed read from its routine and loops
 * back, and would otherwise never get a key at all. A timed read
 * elsewhere is a different question and waits its own interval, which
 * keeps Arthur's paced sequences on their timeline; and a line read is
 * a fresh sitting whatever a key read before it burned.
 *
 * The machine runs the routine when a read returns ZM_INPUT_TIMEOUT and
 * asks again from the same instruction if the routine let the read go
 * on, so the same read seen twice is that continuation, or, when the
 * routine's result is still standing, the game asking again. */
static int timer_fires(accept_play_state *p, unsigned timeout, int key_read)
{
    uint32_t pc = p->machine.pc;

    if (timeout == 0 || !has_more(p)) {
        return 0;
    }
    if (p->timed_pending && p->timed_pc == pc) {
        p->timed_pending = 0;
        if (key_read || p->machine.interrupt_result == 0) {
            return 0;
        }
    }
    if (key_read && p->key_next < p->key_count) {
        return 0;
    }
    p->timed_pending = 1;
    p->timed_pc = pc;
    return 1;
}

/* The next line of input: the script's next command, or, once the
 * script has run out and the game is to go on, a line from the console.
 * Returns 0 when there is nothing more, which ends the game. */
static int take_line(accept_play_state *p, uint16_t *codes, unsigned *count)
{
    const accept_script *s = p->script;

    peek_line(p);
    p->peeked = 0;
    p->timed_pending = 0;

    if (p->next >= s->command_count) {
        char line[2048];
        size_t length;

        if (!p->resume) {
            p->script_ended = 1;
            return 0;
        }
        if (!p->announced) {
            p->announced = 1;
            fputs("\nquiddity: the script has ended; the game is yours from here\n", stderr);
        }
        fflush(stdout);
        if (fgets(line, sizeof line, stdin) == NULL) {
            p->script_ended = 1;
            return 0;
        }
        length = strlen(line);
        while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r')) {
            line[--length] = '\0';
        }
        decode_line(p, line, codes, count);
        return 1;
    }

    decode_line(p, s->commands[p->next++], codes, count);
    return 1;
}

static int stream_read_line(void *context, uint16_t *buffer, unsigned *count, unsigned max, uint16_t *terminator,
                            unsigned timeout)
{
    accept_play_state *p = context;
    uint16_t codes[LINE_CODES];
    unsigned code_count;
    unsigned have = *count;

    /* Whatever a line of keys still held is left behind: a command
     * starts on a line of its own. */
    p->key_count = p->key_next = 0;
    peek_line(p);
    if (timer_fires(p, timeout, 0)) {
        return ZM_INPUT_TIMEOUT;
    }
    if (!take_line(p, codes, &code_count)) {
        return ZM_INPUT_END;
    }

    *terminator = ZM_ZSCII_NEWLINE;
    for (unsigned i = 0; i < code_count; i++) {
        if (codes[i] == 0 || is_terminator(codes[i])) {
            *terminator = codes[i];
            break;
        }
        if (*count < max) {
            buffer[(*count)++] = codes[i];
        }
    }

    /* Spec §7.1.1.1: input is echoed to the screen. A keyboard does that
     * as the player types, but nobody typed this, so the front end shows
     * what the script said, and only that: characters a game left in
     * the buffer from an interrupted command are the game's to show
     * again (Spec §15 read), and Beyond Zork, Zork Zero and Shogun all
     * do. In Version 6 the machine shows the input itself. */
    if (p->mem.version != 6) {
        for (unsigned i = have; i < *count; i++) {
            emit_zscii(p, buffer[i]);
        }
        if (*terminator == ZM_ZSCII_NEWLINE) {
            emit_char(p, '\n');
        }
    }
    return 0;
}

/* A line with nothing on it is the return key, and a line with more
 * than one code on it gives them to the reads that follow, one each,
 * before the next line is looked at. The format has one key per line,
 * so a longer line is not something a recording writes; it is what a
 * person writes for a game that takes its whole command through
 * read_char, as Bureaucracy and Custard do: the command on one line,
 * then a line with nothing on it for the return that ends it. */
static int stream_read_char(void *context, unsigned timeout)
{
    accept_play_state *p = context;

    if (p->key_next < p->key_count) {
        p->timed_pending = 0;
        return p->keys[p->key_next++];
    }
    p->key_count = p->key_next = 0;
    peek_line(p);
    if (timer_fires(p, timeout, 1)) {
        return ZM_INPUT_TIMEOUT;
    }
    if (!take_line(p, p->keys, &p->key_count)) {
        return ZM_INPUT_END;
    }
    if (p->key_count == 0) {
        return ZM_ZSCII_NEWLINE;
    }
    p->key_next = 1;
    return p->keys[0];
}

/*
 * Files: none of them touch the disk. Spec §7.6 and the remarks on §7
 * single out saving and restoring as the thing a long test wants, so a
 * saved game is kept in memory and handed back on the next restore. A
 * transcript or a command record the game turns on goes nowhere, since
 * the play itself is the transcript, but the game is told it is running
 * so that it says what it would say. Auxiliary files are kept by name
 * for the length of the run. Nothing is ever asked, because there is
 * nobody to ask.
 */

static int stream_set_transcript(void *context, int on)
{
    (void)context;
    return on;
}

static void stream_transcript_char(void *context, uint16_t zscii)
{
    (void)context;
    (void)zscii;
}

static void stream_record_input(void *context, const uint16_t *zscii, unsigned count)
{
    (void)context;
    (void)zscii;
    (void)count;
}

static kept_file *find_file(accept_play_state *p, const char *name)
{
    for (unsigned i = 0; i < p->file_count; i++) {
        if (strcmp(p->files[i].name, name) == 0) {
            return &p->files[i];
        }
    }
    return NULL;
}

static int stream_write_file(void *context, zm_file_kind kind, const char *name, int prompt, const uint8_t *data,
                             uint32_t size)
{
    accept_play_state *p = context;
    uint8_t *copy = malloc(size > 0 ? size : 1);

    (void)prompt;
    if (copy == NULL) {
        return 0;
    }
    memcpy(copy, data, size);

    if (kind == ZM_FILE_SAVED_GAME) {
        free(p->saved);
        p->saved = copy;
        p->saved_size = size;
        return 1;
    } else {
        kept_file *file = find_file(p, name != NULL ? name : "");

        if (file == NULL) {
            kept_file *grown = realloc(p->files, (p->file_count + 1) * sizeof *grown);

            if (grown == NULL) {
                free(copy);
                return 0;
            }
            p->files = grown;
            file = &p->files[p->file_count];
            file->name = malloc(strlen(name != NULL ? name : "") + 1);
            if (file->name == NULL) {
                free(copy);
                return 0;
            }
            strcpy(file->name, name != NULL ? name : "");
            file->data = NULL;
            p->file_count++;
        }
        free(file->data);
        file->data = copy;
        file->size = size;
        return 1;
    }
}

static uint8_t *stream_read_file(void *context, zm_file_kind kind, const char *name, int prompt, uint32_t *size)
{
    accept_play_state *p = context;
    const uint8_t *data;
    uint8_t *copy;

    (void)prompt;
    if (kind == ZM_FILE_SAVED_GAME) {
        data = p->saved;
        *size = p->saved_size;
    } else {
        kept_file *file = find_file(p, name != NULL ? name : "");

        data = file != NULL ? file->data : NULL;
        *size = file != NULL ? file->size : 0;
    }
    if (data == NULL) {
        return NULL;
    }
    copy = malloc(*size > 0 ? *size : 1);
    if (copy != NULL) {
        memcpy(copy, data, *size);
    }
    return copy;
}

/*
 * The play.
 */

/* A fault in the machine comes here, by way of zm_fatal(), and goes
 * back to accept_play() by way of longjmp, since zm_fatal() cannot
 * return. The play up to the fault is kept, and the fault becomes the
 * last line of the record. */
static accept_play_state *faulting_play;
static jmp_buf fault_return;

static char *copy_string(const char *s)
{
    char *copy = malloc(strlen(s) + 1);

    if (copy != NULL) {
        strcpy(copy, s);
    }
    return copy;
}

static void on_fault(const char *message)
{
    free(faulting_play->message);
    faulting_play->message = copy_string(message);
    longjmp(fault_return, 1);
}

/* Spec §11.1.3: the machine the game is told it is running on, from the
 * script's INTERPRETER directive, or 0 for the stream's own choice. */
static int interpreter_number(const char *wanted, unsigned *number, char *error, size_t size)
{
    char *end = NULL;
    unsigned long parsed;

    for (size_t i = 0; i < sizeof interpreter_names / sizeof interpreter_names[0]; i++) {
        if (strcmp(wanted, interpreter_names[i].name) == 0) {
            *number = interpreter_names[i].number;
            return 1;
        }
    }
    parsed = strtoul(wanted, &end, 10);
    if (wanted[0] >= '0' && wanted[0] <= '9' && *end == '\0' && parsed >= 1 && parsed <= 11) {
        *number = (unsigned)parsed;
        return 1;
    }
    snprintf(error, size,
             "there is no interpreter called %s; the machines are dec20, apple2e, macintosh, amiga, atarist, ibmpc, "
             "c128, c64, apple2c, apple2gs, tandy, or a number from 1 to 11",
             wanted);
    return 0;
}

static void free_play(accept_play_state *p)
{
    free(p->out);
    free(p->offsets);
    free(p->saved);
    for (unsigned i = 0; i < p->file_count; i++) {
        free(p->files[i].name);
        free(p->files[i].data);
    }
    free(p->files);
    free(p->message);
    free(p);
}

int accept_play(const accept_script *script, int display, int resume, accept_result *result)
{
    accept_play_state *p = calloc(1, sizeof *p);
    const char *name = script->name;
    unsigned number = 0;
    char error[256];
    zm_status status;

    memset(result, 0, sizeof *result);
    if (p == NULL) {
        fprintf(stderr, "quiddity: %s: out of memory\n", name);
        return 0;
    }
    p->script = script;
    p->display = display;
    p->resume = resume;
    p->font = 1;

    status = zm_memory_load_file(&p->mem, script->game_path);
    if (status == ZM_ERROR_IO && errno == ENOENT) {
        fprintf(stderr, "quiddity: %s: the game %s is not there; skipped\n", name, script->game_path);
        free_play(p);
        return ACCEPT_EXIT_SKIPPED;
    }
    if (status != ZM_OK) {
        fprintf(stderr, "quiddity: %s: %s: %s\n", name, script->game_path,
                status == ZM_ERROR_IO ? strerror(errno) : zm_status_message(status));
        free_play(p);
        return 0;
    }

    if (script->interpreter != NULL && !interpreter_number(script->interpreter, &number, error, sizeof error)) {
        fprintf(stderr, "quiddity: %s: %s\n", name, error);
        zm_memory_free(&p->mem);
        free_play(p);
        return 0;
    }

    if (script->blorb_path != NULL) {
        status = zm_blorb_load_file(&p->resources, script->blorb_path);
        if (status != ZM_OK) {
            fprintf(stderr, "quiddity: %s: %s: %s\n", name, script->blorb_path,
                    status == ZM_ERROR_IO ? strerror(errno) : zm_status_message(status));
            zm_memory_free(&p->mem);
            free_play(p);
            return 0;
        }
        p->have_resources = 1;
        if (!zm_blorb_matches(&p->resources, &p->mem)) {
            /* Blorb §6: complain righteously, then carry on without. */
            fprintf(stderr, "quiddity: %s: %s belongs to a different story and is ignored\n", name,
                    script->blorb_path);
            zm_blorb_free(&p->resources);
            p->have_resources = 0;
        }
    }

    /* The directives this front end cannot honour yet are said so,
     * rather than left to wonder why they did nothing. */
    if (script->upper) {
        fprintf(stderr, "quiddity: %s: the UPPER directive is not supported yet; the upper window is not shown\n",
                name);
    }
    if (script->pictures) {
        fprintf(stderr, "quiddity: %s: the PICTURES directive is not supported yet; the game is told it has none\n",
                name);
    }
    if (script->graphics) {
        fprintf(stderr, "quiddity: %s: the GRAPHICS directive applies to Glulx games, which are not played yet\n",
                name);
    }

    /* The stream as a zm_io; see the remarks in accept.h. */
    p->io.context = p;
    p->io.screen_width = STREAM_WIDTH;
    p->io.screen_height = STREAM_HEIGHT;
    p->io.styles = ZM_STYLE_FIXED_PITCH;
    p->io.fonts = (1u << 3) | (1u << 4);
    p->io.timed_input = 0;
    p->io.colours = 0;
    p->io.tandy = script->tandy;
    p->io.interpreter_number = number != 0 ? number : p->mem.version == 6 ? 1u : 6u;
    if (p->mem.version == 6) {
        p->io.font_width = STREAM_FONT_WIDTH;
        p->io.font_height = STREAM_FONT_HEIGHT;
    }
    p->io.print_char = stream_print_char;
    p->io.print_unicode = stream_print_unicode;
    p->io.read_line = stream_read_line;
    p->io.read_char = stream_read_char;
    p->io.set_font = stream_set_font;
    p->io.set_transcript = stream_set_transcript;
    p->io.transcript_char = stream_transcript_char;
    p->io.record_input = stream_record_input;
    p->io.write_file = stream_write_file;
    p->io.read_file = stream_read_file;

    status = zm_machine_init(&p->machine, &p->mem, &p->io);
    if (status != ZM_OK) {
        fprintf(stderr, "quiddity: %s: %s\n", name, zm_status_message(status));
        if (p->have_resources) {
            zm_blorb_free(&p->resources);
        }
        zm_memory_free(&p->mem);
        free_play(p);
        return 0;
    }
    if (p->have_resources) {
        zm_machine_use_resources(&p->machine, &p->resources);
    }
    zm_machine_seed_session(&p->machine, script->seed);

    faulting_play = p;
    zm_set_fatal_handler(on_fault);
    if (setjmp(fault_return) == 0) {
        zm_machine_run(&p->machine);
        result->ending = p->script_ended ? ACCEPT_SCRIPT_ENDED : ACCEPT_QUIT;
    } else {
        result->ending = ACCEPT_FAILED;

        /* The screen may still hold a word the fault interrupted; the
         * record keeps everything the game printed before it. */
        zm_screen_flush(&p->machine.screen);
    }
    zm_set_fatal_handler(NULL);
    faulting_play = NULL;
    if (display) {
        fflush(stdout);
    }

    /* The whole record: the output, and after it a line for a fault,
     * on a line of its own. */
    result->output_length = p->out_length;
    if (result->ending == ACCEPT_FAILED) {
        const char *message = p->message != NULL ? p->message : "";
        size_t length = p->out_length + strlen(message) + 32;
        char *text = malloc(length);

        if (text != NULL) {
            memcpy(text, p->out != NULL ? p->out : "", p->out_length);
            text[p->out_length] = '\0';
            if (p->out_length > 0 && text[p->out_length - 1] != '\n') {
                strcat(text, "\n");
            }
            strcat(text, "[quiddity: error: ");
            strcat(text, message);
            strcat(text, "]\n");
        }
        result->text = text;
        result->message = p->message;
        p->message = NULL;
    } else {
        result->text = p->out != NULL ? p->out : copy_string("");
        p->out = NULL;
    }
    result->offsets = p->offsets;
    result->offset_count = p->offset_count;
    p->offsets = NULL;

    zm_machine_free(&p->machine);
    if (p->have_resources) {
        zm_blorb_free(&p->resources);
    }
    zm_memory_free(&p->mem);
    free_play(p);
    return result->text != NULL;
}

void accept_result_free(accept_result *result)
{
    free(result->text);
    free(result->message);
    free(result->offsets);
    memset(result, 0, sizeof *result);
}

/*
 * The comparison.
 */

/* The lines of a text, with CRLF read as LF and the end of the last
 * line not counted as the start of an empty one after it. */
typedef struct lines {
    char *storage;
    char **items;
    unsigned count;
} lines;

static int split_lines(const char *text, lines *out)
{
    size_t length = strlen(text);
    size_t n = 0;

    memset(out, 0, sizeof *out);
    out->storage = malloc(length + 1);
    if (out->storage == NULL) {
        return 0;
    }
    for (size_t i = 0; i < length; i++) {
        if (text[i] == '\r' && text[i + 1] == '\n') {
            continue;
        }
        out->storage[n++] = text[i];
    }
    if (n > 0 && out->storage[n - 1] == '\n') {
        n--;
    }
    out->storage[n] = '\0';

    out->count = 1;
    for (size_t i = 0; i < n; i++) {
        if (out->storage[i] == '\n') {
            out->count++;
        }
    }
    out->items = malloc(out->count * sizeof *out->items);
    if (out->items == NULL) {
        return 0;
    }
    out->items[0] = out->storage;
    for (size_t i = 0, line = 1; i < n; i++) {
        if (out->storage[i] == '\n') {
            out->storage[i] = '\0';
            out->items[line++] = out->storage + i + 1;
        }
    }
    return 1;
}

static void free_lines(lines *l)
{
    free(l->storage);
    free(l->items);
}

static char *format(const char *fmt, ...)
{
    va_list args;
    int length;
    char *text;

    va_start(args, fmt);
    length = vsnprintf(NULL, 0, fmt, args);
    va_end(args);
    if (length < 0) {
        return NULL;
    }
    text = malloc((size_t)length + 1);
    if (text == NULL) {
        return NULL;
    }
    va_start(args, fmt);
    vsnprintf(text, (size_t)length + 1, fmt, args);
    va_end(args);
    return text;
}

int accept_first_difference(const char *expected, const char *actual, accept_difference *difference)
{
    lines e, a;
    unsigned common;
    int differ = 0;

    memset(difference, 0, sizeof *difference);
    memset(&e, 0, sizeof e);
    memset(&a, 0, sizeof a);
    if (!split_lines(expected, &e) || !split_lines(actual, &a)) {
        free_lines(&e);
        free_lines(&a);
        return 0;
    }
    common = e.count < a.count ? e.count : a.count;

    for (unsigned i = 0; i < common; i++) {
        if (strcmp(e.items[i], a.items[i]) == 0) {
            continue;
        }

        /* A recording made from a shorter script ends with a bare
         * prompt, and the longer play has a command after that prompt
         * on the same line. That is the recording running out, not the
         * play going wrong, and it is said so. */
        if (i == e.count - 1 && strncmp(a.items[i], e.items[i], strlen(e.items[i])) == 0) {
            difference->line = i + 1;
            difference->recording_ended = 1;
            difference->description =
                format("the recording ends there, and the play goes on for %u more lines\n  recorded: %s\n  played:   %s",
                       a.count - e.count, e.items[i], a.items[i]);
        } else {
            difference->line = i + 1;
            difference->description = format("  expected: %s\n  actual:   %s", e.items[i], a.items[i]);
        }
        differ = 1;
        break;
    }

    if (!differ && e.count > a.count) {
        difference->line = common + 1;
        difference->description =
            format("the play ends after line %u, but %u more were expected, the first being\n  expected: %s",
                   a.count, e.count - a.count, e.items[common]);
        differ = 1;
    } else if (!differ && a.count > e.count) {
        difference->line = common + 1;
        difference->recording_ended = 1;
        difference->description =
            format("the recording ends there, and the play goes on for %u more lines, starting with\n  played:   %s",
                   a.count - e.count, a.items[common]);
        differ = 1;
    }

    free_lines(&e);
    free_lines(&a);
    return differ;
}

void accept_difference_free(accept_difference *difference)
{
    free(difference->description);
    memset(difference, 0, sizeof *difference);
}

char *accept_describe(const accept_difference *difference, const accept_result *result, const accept_script *script)
{
    const char *expected_name = script->expected_path;
    lines l;
    size_t end = 0;
    int command = -1;
    char *where;
    char *text;

    for (const char *c = script->expected_path; *c != '\0'; c++) {
        if (*c == '/' || *c == '\\') {
            expected_name = c + 1;
        }
    }

    /* The end of the differing line in the play, and the last command
     * read before that point. The echo of a command follows the prompt
     * on the same line, so the end of the line is what places it. */
    if (split_lines(result->text, &l)) {
        for (unsigned i = 0; i < difference->line && i < l.count; i++) {
            end += strlen(l.items[i]) + 1;
        }
    }
    free_lines(&l);
    for (unsigned i = 0; i < result->offset_count && result->offsets[i] <= end; i++) {
        command = (int)i;
    }

    if (command >= 0 && (unsigned)command < script->command_count) {
        where = format("line %u of %s %s, while playing line %u of the script, \"%s\"", difference->line, expected_name,
                       difference->recording_ended ? "is its last" : "differs", script->command_lines[command],
                       script->commands[command]);
    } else if (command < 0) {
        where = format("line %u of %s %s, before the first command", difference->line, expected_name,
                       difference->recording_ended ? "is its last" : "differs");
    } else {
        where = format("line %u of %s %s", difference->line, expected_name,
                       difference->recording_ended ? "is its last" : "differs");
    }
    if (where == NULL) {
        return NULL;
    }
    text = format("%s\n%s", where, difference->description != NULL ? difference->description : "");
    free(where);
    return text;
}

/*
 * The command.
 */

static char *read_whole_file(const char *path)
{
    FILE *file = fopen(path, "rb");
    long size;
    char *text;
    size_t read;

    if (file == NULL) {
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    text = malloc((size_t)size + 1);
    if (text == NULL) {
        fclose(file);
        return NULL;
    }
    read = fread(text, 1, (size_t)size, file);
    fclose(file);
    text[read] = '\0';
    return text;
}

static int write_whole_file(const char *path, const char *text)
{
    FILE *file = fopen(path, "wb");
    size_t length = strlen(text);
    int ok;

    if (file == NULL) {
        return 0;
    }
    ok = fwrite(text, 1, length, file) == length;
    ok = fclose(file) == 0 && ok;
    return ok;
}

int accept_run(const char *script_path, accept_mode mode, const char *games_dir)
{
    accept_script script;
    accept_result result;
    accept_difference difference;
    char error[ACCEPT_ERROR_MAX];
    const char *expected_name;
    char *expected;
    int status;

    if (!accept_script_load(&script, script_path, games_dir, error)) {
        fprintf(stderr, "quiddity: %s\n", error);
        return 1;
    }

    /* The play is shown as it happens, so that someone building a
     * script up sees where a command went wrong, and the verdict comes
     * at the end. */
    status = accept_play(&script, 1, mode == ACCEPT_RESUME, &result);
    if (status != 1) {
        accept_script_free(&script);
        return status == ACCEPT_EXIT_SKIPPED ? ACCEPT_EXIT_SKIPPED : 1;
    }

    /* The rest of the record: what the interpreter noticed, which the
     * play itself does not show. */
    fputs(result.text + result.output_length, stdout);
    if (result.text[0] != '\0' && result.text[strlen(result.text) - 1] != '\n') {
        putchar('\n');
    }

    if (mode == ACCEPT_RESUME) {
        accept_result_free(&result);
        accept_script_free(&script);
        return 0;
    }

    /* A gap before the verdict. */
    putchar('\n');

    expected_name = script.expected_path;
    for (const char *c = script.expected_path; *c != '\0'; c++) {
        if (*c == '/' || *c == '\\') {
            expected_name = c + 1;
        }
    }

    expected = mode == ACCEPT_UPDATE ? NULL : read_whole_file(script.expected_path);
    if (expected == NULL) {
        /* The first run of a script has nothing to check against, so it
         * records what happened and says so; every run after compares. */
        if (!write_whole_file(script.expected_path, result.text)) {
            fprintf(stderr, "quiddity: %s: %s cannot be written: %s\n", script.name, expected_name, strerror(errno));
            status = 1;
        } else {
            printf(mode == ACCEPT_UPDATE ? "quiddity: %s: updated %s\n"
                                         : "quiddity: %s: recorded %s, which later runs will be checked against\n",
                   script.name, expected_name);
            status = 0;
        }
        accept_result_free(&result);
        accept_script_free(&script);
        return status;
    }

    if (accept_first_difference(expected, result.text, &difference)) {
        char *description = accept_describe(&difference, &result, &script);

        fprintf(stderr, "quiddity: %s: %s\n", script.name,
                difference.recording_ended ? "the recording ends before the script does"
                                           : "the play differs from what was recorded");
        fprintf(stderr, "%s\n", description != NULL ? description : "");
        fputs(difference.recording_ended ? "If the script has grown, run again with --update to record the rest.\n"
                                         : "If the change is meant, run again with --update.\n",
              stderr);
        free(description);
        accept_difference_free(&difference);
        status = 1;
    } else {
        printf("quiddity: %s: passed\n", script.name);
        status = 0;
    }

    free(expected);
    accept_result_free(&result);
    accept_script_free(&script);
    return status;
}
