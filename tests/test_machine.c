/*
 * test_machine.c: running instructions.
 *
 * Covers Spec §5 and §6, and the opcodes implemented so far from Spec
 * §15, through zm/machine.h. Each test assembles a small story by hand,
 * runs it with a sink that records what it prints, and checks the
 * output and the machine's state afterwards. The instruction encodings
 * follow Spec §4; the comment beside each line gives the assembly.
 *
 * Every hand-built story has the same shape: the header, the global
 * variables at $40 (Spec §6.2), room for an object table at $240, code
 * from $400, routines at $4c0, $500, $540, and $580 (all divisible by 8,
 * so they can be packed addresses in any Version, Spec §5.1), and a
 * string at $5c0.
 */

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zm/blorb.h"
#include "zm/fatal.h"
#include "zm/header.h"
#include "zm/machine.h"
#include "zm/memory.h"
#include "zm/screen.h"
#include "zm/text.h"

#include "check.h"

#define STORY_SIZE 2048u
#define GLOBALS 0x40u
#define OBJECTS 0x240u
#define CODE 0x400u
#define R1 0x4c0u
#define R2 0x500u
#define R3 0x540u
#define R4 0x580u
#define STRING 0x5c0u

/* Variable numbers (Spec §4.2.2). */
#define SP 0x00
#define L1 0x01
#define L2 0x02
#define L3 0x03
#define L7 0x07
#define G0 0x10
#define G1 0x11
#define G2 0x12

/* A sink that keeps everything printed, as in test_text.c. */
typedef struct {
    char text[512];
    int count;
} output;

static void collect(void *context, uint16_t zscii)
{
    output *out = context;

    if (out->count < 511) {
        if (zscii >= 32 && zscii <= 126) {
            out->text[out->count] = (char)zscii;
        } else if (zscii == ZM_ZSCII_NEWLINE) {
            out->text[out->count] = '\n';
        } else {
            out->text[out->count] = '?';
        }
        out->count++;
        out->text[out->count] = '\0';
    }
}

static zm_io io = { .print_char = collect };

/* Scripted input: lines and keys the "player" types, and a record of
 * the status lines shown. */
static const char *script_lines[8];
static unsigned script_next;
static int script_keys[8];
static unsigned script_key_next;

typedef struct {
    char name[64];
    int time_game;
    int a, b;
    unsigned shown;
} status_record;
static status_record status;

/* The clock, as scripted: a timed read reports script_timeouts_each
 * timeouts before each line or key it delivers, and on timeout number
 * script_partial_at of the run the characters of script_partial, if
 * any, arrive as if typed before the clock ran out. The timeout asked
 * for is noted. */
static unsigned script_timeouts_each;
static unsigned script_timeouts;
static const char *script_partial;
static unsigned script_partial_at;
static unsigned timeouts_total;
static unsigned timeout_seen;

static void script_clock(unsigned each, const char *partial, unsigned partial_at)
{
    script_timeouts_each = each;
    script_timeouts = each;
    script_partial = partial;
    script_partial_at = partial_at;
    timeouts_total = 0;
    timeout_seen = 0;
}

static int script_timed_out(unsigned timeout, uint16_t *buffer, unsigned *count, unsigned max)
{
    if (timeout == 0 || script_timeouts == 0) {
        return 0;
    }
    timeout_seen = timeout;
    script_timeouts--;
    timeouts_total++;
    if (buffer != NULL && script_partial != NULL && timeouts_total == script_partial_at) {
        for (const char *p = script_partial; *p != '\0' && *count < max; p++) {
            buffer[(*count)++] = (uint8_t)*p;
        }
    }
    return 1;
}

static int script_read_line(void *context, uint16_t *buffer, unsigned *count, unsigned max,
                            uint16_t *terminator, unsigned timeout)
{
    const char *line;

    (void)context;
    if (script_timed_out(timeout, buffer, count, max)) {
        return ZM_INPUT_TIMEOUT;
    }
    line = script_lines[script_next];
    if (line == NULL) {
        return ZM_INPUT_END;
    }
    script_next++;
    script_timeouts = script_timeouts_each;
    for (unsigned i = 0; line[i] != '\0' && *count < max; i++) {
        buffer[(*count)++] = (uint8_t)line[i];
    }
    *terminator = ZM_ZSCII_NEWLINE;
    return 0;
}

static int script_read_char(void *context, unsigned timeout)
{
    (void)context;
    if (script_timed_out(timeout, NULL, NULL, 0)) {
        return ZM_INPUT_TIMEOUT;
    }
    if (script_keys[script_key_next] == 0) {
        return ZM_INPUT_END;
    }
    script_timeouts = script_timeouts_each;
    return script_keys[script_key_next++];
}

static void record_status(void *context, const uint16_t *name, unsigned name_length,
                          int time_game, int16_t a, int16_t b)
{
    (void)context;
    memset(status.name, 0, sizeof status.name);
    for (unsigned i = 0; i < name_length && i < sizeof status.name - 1; i++) {
        status.name[i] = (char)name[i];
    }
    status.time_game = time_game;
    status.a = a;
    status.b = b;
    status.shown++;
}

/* Install scripted lines and keys, or remove them with NULL, NULL. */
static void script(const char *const *lines, const int *keys)
{
    for (unsigned i = 0; i < 8; i++) {
        script_lines[i] = NULL;
        script_keys[i] = 0;
    }
    memset(&status, 0, sizeof status);
    script_next = 0;
    script_key_next = 0;
    script_clock(0, NULL, 0);
    if (lines != NULL) {
        for (unsigned i = 0; lines[i] != NULL && i < 7; i++) {
            script_lines[i] = lines[i];
        }
    }
    if (keys != NULL) {
        for (unsigned i = 0; keys[i] != 0 && i < 7; i++) {
            script_keys[i] = keys[i];
        }
    }
    io.read_line = lines != NULL ? script_read_line : NULL;
    io.read_char = keys != NULL ? script_read_char : NULL;
    io.show_status = lines != NULL || keys != NULL ? record_status : NULL;
}


/* Screen hooks that log what the front end is told, and collectors for
 * the transcript and the command record (Spec §7). */
static char screen_log[512];
static char transcript_text[128];
static char record_text[128];
static int transcript_allowed;

static void log_call(const char *text)
{
    if (strlen(screen_log) + strlen(text) + 2 < sizeof screen_log) {
        if (screen_log[0] != '\0') {
            strcat(screen_log, ";");
        }
        strcat(screen_log, text);
    }
}

static void hook_split_window(void *context, unsigned lines)
{
    char text[32];
    (void)context;
    snprintf(text, sizeof text, "split %u", lines);
    log_call(text);
}

static void hook_set_window(void *context, unsigned window)
{
    char text[32];
    (void)context;
    snprintf(text, sizeof text, "window %u", window);
    log_call(text);
}

static void hook_erase_window(void *context, int window)
{
    char text[32];
    (void)context;
    snprintf(text, sizeof text, "erase %d", window);
    log_call(text);
}

static void hook_erase_line(void *context)
{
    (void)context;
    log_call("erase_line");
}

static void hook_set_cursor(void *context, unsigned row, unsigned column)
{
    char text[32];
    (void)context;
    snprintf(text, sizeof text, "cursor %u,%u", row, column);
    log_call(text);
}

static void hook_set_text_style(void *context, unsigned style)
{
    char text[32];
    (void)context;
    snprintf(text, sizeof text, "style %u", style);
    log_call(text);
}

static void hook_set_colour(void *context, unsigned foreground, unsigned background)
{
    char text[32];
    (void)context;
    snprintf(text, sizeof text, "colour %u/%u", foreground, background);
    log_call(text);
}

static void hook_set_buffering(void *context, int on)
{
    (void)context;
    log_call(on ? "buffer on" : "buffer off");
}

static void hook_sound_effect(void *context, unsigned number, unsigned effect, unsigned volume, unsigned repeats)
{
    char text[48];
    (void)context;
    snprintf(text, sizeof text, "sound %u %u %u %u", number, effect, volume, repeats);
    log_call(text);
}

static void hook_print_unicode(void *context, uint16_t unicode)
{
    char text[32];
    (void)context;
    snprintf(text, sizeof text, "unicode %04x", (unsigned)unicode);
    log_call(text);
}

static int hook_set_transcript(void *context, int on)
{
    (void)context;
    log_call(on ? "transcript on" : "transcript off");
    return transcript_allowed;
}

static void hook_transcript_char(void *context, uint16_t zscii)
{
    size_t n = strlen(transcript_text);
    (void)context;
    if (n + 1 < sizeof transcript_text) {
        transcript_text[n] = zscii == ZM_ZSCII_NEWLINE ? '\n' : (char)zscii;
        transcript_text[n + 1] = '\0';
    }
}

static void hook_record_input(void *context, const uint16_t *zscii, unsigned count)
{
    size_t n = strlen(record_text);
    (void)context;
    for (unsigned i = 0; i < count && n + 1 < sizeof record_text; i++) {
        record_text[n++] = (char)zscii[i];
    }
    if (n + 1 < sizeof record_text) {
        record_text[n++] = '|';
    }
    record_text[n] = '\0';
}

/* Install every screen and file hook, or remove them all. */
static void screen_hooks(int on)
{
    screen_log[0] = '\0';
    transcript_text[0] = '\0';
    record_text[0] = '\0';
    transcript_allowed = 1;
    io.split_window = on ? hook_split_window : NULL;
    io.set_window = on ? hook_set_window : NULL;
    io.erase_window = on ? hook_erase_window : NULL;
    io.erase_line = on ? hook_erase_line : NULL;
    io.set_cursor = on ? hook_set_cursor : NULL;
    io.set_text_style = on ? hook_set_text_style : NULL;
    io.set_colour = on ? hook_set_colour : NULL;
    io.set_buffering = on ? hook_set_buffering : NULL;
    io.sound_effect = on ? hook_sound_effect : NULL;
    io.print_unicode = on ? hook_print_unicode : NULL;
    io.set_transcript = on ? hook_set_transcript : NULL;
    io.transcript_char = on ? hook_transcript_char : NULL;
    io.record_input = on ? hook_record_input : NULL;
    io.styles = on ? (ZM_STYLE_BOLD | ZM_STYLE_ITALIC) : 0;
    io.timed_input = on;
    io.screen_width = 0;
    io.screen_height = 0;
}

/* The Version 6 canvas (Spec §8.8): a small screen of character cells
 * the machine plots into, the pen it last used, and a log of the
 * erasing, scrolling, and picture drawing it asked for. */
#define CANVAS_WIDTH 40
#define CANVAS_HEIGHT 8

static char canvas[CANVAS_HEIGHT][CANVAS_WIDTH + 1];
static zm_pen last_pen;

static void canvas_clear(void)
{
    for (unsigned r = 0; r < CANVAS_HEIGHT; r++) {
        memset(canvas[r], ' ', CANVAS_WIDTH);
        canvas[r][CANVAS_WIDTH] = '\0';
    }
}

/* A row of the canvas without its trailing blanks. */
static const char *canvas_row(unsigned row)
{
    static char text[CANVAS_WIDTH + 1];
    size_t end = CANVAS_WIDTH;

    memcpy(text, canvas[row - 1], CANVAS_WIDTH + 1);
    while (end > 0 && text[end - 1] == ' ') {
        end--;
    }
    text[end] = '\0';
    return text;
}

static void hook_plot_text(void *context, unsigned y, unsigned x, const uint16_t *unicode, unsigned count,
                           const zm_pen *pen)
{
    (void)context;
    last_pen = *pen;
    for (unsigned i = 0; i < count; i++) {
        if (y >= 1 && y <= CANVAS_HEIGHT && x + i >= 1 && x + i <= CANVAS_WIDTH) {
            canvas[y - 1][x + i - 1] = unicode[i] < 127 ? (char)unicode[i] : '?';
        }
    }
}

static void hook_erase_area(void *context, unsigned y, unsigned x, unsigned height, unsigned width,
                            const zm_pen *pen)
{
    char text[48];
    (void)context;
    snprintf(text, sizeof text, "erase %u,%u %ux%u bg%u", y, x, height, width, pen->background);
    log_call(text);
    for (unsigned r = y; r < y + height && r <= CANVAS_HEIGHT; r++) {
        for (unsigned c = x; c < x + width && c <= CANVAS_WIDTH; c++) {
            canvas[r - 1][c - 1] = ' ';
        }
    }
}

static void hook_scroll_area(void *context, unsigned y, unsigned x, unsigned height, unsigned width, int units,
                             const zm_pen *pen)
{
    char text[48];
    (void)context;
    (void)pen;
    snprintf(text, sizeof text, "scroll %u,%u %ux%u by %d", y, x, height, width, units);
    log_call(text);
    if (units > 0) {
        for (unsigned r = y; r + (unsigned)units < y + height && r + (unsigned)units <= CANVAS_HEIGHT; r++) {
            memcpy(canvas[r - 1] + x - 1, canvas[r - 1 + (unsigned)units] + x - 1, width);
        }
        for (unsigned r = y + height - (unsigned)units; r < y + height && r <= CANVAS_HEIGHT; r++) {
            memset(canvas[r - 1] + x - 1, ' ', width);
        }
    }
}

static void hook_draw_picture(void *context, const zm_picture *picture, unsigned y, unsigned x,
                              unsigned height, unsigned width)
{
    char text[48];
    (void)context;
    snprintf(text, sizeof text, "picture %u at %u,%u %ux%u", picture->number, y, x, height, width);
    log_call(text);
}

/* Install the canvas, with colours and the fixed-pitch font on offer, or
 * remove it. */
static void canvas_hooks(int on)
{
    screen_log[0] = '\0';
    canvas_clear();
    memset(&last_pen, 0, sizeof last_pen);
    io.plot_text = on ? hook_plot_text : NULL;
    io.erase_area = on ? hook_erase_area : NULL;
    io.scroll_area = on ? hook_scroll_area : NULL;
    io.draw_picture = on ? hook_draw_picture : NULL;
    io.picture_size = NULL;
    io.transcript_char = on ? hook_transcript_char : NULL;
    io.set_transcript = on ? hook_set_transcript : NULL;
    io.colours = on;
    io.fonts = on ? (1u << 4) : 0;
    io.screen_width = on ? CANVAS_WIDTH : 0;
    io.screen_height = on ? CANVAS_HEIGHT : 0;
    transcript_text[0] = '\0';
    transcript_allowed = 1;
}

/* Files kept in memory for the save and restore hooks (Spec §15): one
 * saved game, handed back as many times as saved_game_reads allows, and
 * one auxiliary file, with the name and prompt the game gave for it. */
static uint8_t saved_game[4096];
static uint32_t saved_game_size;
static int saved_game_reads;
static uint8_t aux_data[64];
static uint32_t aux_size;
static char aux_name[64];
static int aux_prompt;

static int hook_write_file(void *context, zm_file_kind kind, const char *name, int prompt,
                           const uint8_t *data, uint32_t size)
{
    (void)context;
    if (kind == ZM_FILE_SAVED_GAME) {
        if (size > sizeof saved_game) {
            return 0;
        }
        memcpy(saved_game, data, size);
        saved_game_size = size;
        return 1;
    }
    if (size > sizeof aux_data) {
        return 0;
    }
    memcpy(aux_data, data, size);
    aux_size = size;
    snprintf(aux_name, sizeof aux_name, "%s", name != NULL ? name : "");
    aux_prompt = prompt;
    return 1;
}

static uint8_t *hook_read_file(void *context, zm_file_kind kind, const char *name, int prompt, uint32_t *size)
{
    uint8_t *copy;

    (void)context;
    if (kind == ZM_FILE_SAVED_GAME) {
        if (saved_game_reads <= 0) {
            return NULL;
        }
        saved_game_reads--;
        copy = malloc(saved_game_size > 0 ? saved_game_size : 1);
        memcpy(copy, saved_game, saved_game_size);
        *size = saved_game_size;
        return copy;
    }
    snprintf(aux_name, sizeof aux_name, "%s", name != NULL ? name : "");
    aux_prompt = prompt;
    copy = malloc(aux_size > 0 ? aux_size : 1);
    memcpy(copy, aux_data, aux_size);
    *size = aux_size;
    return copy;
}

static void file_hooks(int on)
{
    io.write_file = on ? hook_write_file : NULL;
    io.read_file = on ? hook_read_file : NULL;
    saved_game_size = 0;
    saved_game_reads = 0;
    aux_size = 0;
    aux_name[0] = '\0';
    aux_prompt = -2;
}

/* A handler that records the fault and jumps back to the test. */
static jmp_buf fault_jump;
static char fault_message[256];

static void trap_fault(const char *message)
{
    strncpy(fault_message, message, sizeof fault_message - 1);
    fault_message[sizeof fault_message - 1] = '\0';
    longjmp(fault_jump, 1);
}

/*
 * Assembling stories.
 */

typedef struct {
    uint8_t bytes[STORY_SIZE];
    uint32_t at;
    uint8_t version;
} story;

static void put_word(uint8_t *bytes, uint32_t offset, uint16_t value)
{
    bytes[offset] = (uint8_t)(value >> 8);
    bytes[offset + 1] = (uint8_t)(value & 0xff);
}

static void story_init(story *s, uint8_t version)
{
    memset(s, 0, sizeof *s);
    s->version = version;
    s->bytes[ZM_HEADER_VERSION] = version;
    put_word(s->bytes, ZM_HEADER_HIGH_BASE, CODE);
    put_word(s->bytes, ZM_HEADER_INITIAL_PC, CODE);
    put_word(s->bytes, ZM_HEADER_GLOBALS, GLOBALS);
    put_word(s->bytes, ZM_HEADER_STATIC_BASE, CODE);
    s->at = CODE;
}

/* Continue assembling at an address. */
static void at(story *s, uint32_t address)
{
    s->at = address;
}

static void emit_bytes(story *s, const uint8_t *bytes, size_t count)
{
    memcpy(s->bytes + s->at, bytes, count);
    s->at += (uint32_t)count;
}

/* Write literal bytes: EMIT(s, 0xe5, 0x7f, 0x20). */
#define EMIT(s, ...) \
    emit_bytes((s), (const uint8_t[]){ __VA_ARGS__ }, sizeof((const uint8_t[]){ __VA_ARGS__ }))

/* Write the packed address (Spec §1.2.3) of a byte address as a word. The
 * hand-built stories keep the Version 6 and 7 offsets at zero. */
static void emit_packed(story *s, uint32_t address)
{
    uint32_t divisor = s->version <= 3 ? 2 : s->version <= 7 ? 4 : 8;
    uint8_t word[2];

    put_word(word, 0, (uint16_t)(address / divisor));
    emit_bytes(s, word, 2);
}

/* "Hello.^" as Spec §4's example encodes it: Z-characters 4 13 10 17 17
 * 20 5 18 5 7 5 5, the last word with its top bit set. */
static const uint8_t hello[] = { 0x11, 0xaa, 0x46, 0x34, 0x16, 0x45, 0x9c, 0xa5 };

/* Load the story, set up the machine, and run it to its quit. */
static void run(const story *s, output *out, zm_machine *m, zm_memory *mem)
{
    memset(out, 0, sizeof *out);
    screen_log[0] = '\0';
    transcript_text[0] = '\0';
    record_text[0] = '\0';
    canvas_clear();
    io.context = out;

    CHECK_EQ_INT(zm_memory_load_bytes(mem, s->bytes, STORY_SIZE), ZM_OK);
    CHECK_EQ_INT(zm_machine_init(m, mem, &io), ZM_OK);
    zm_machine_run(m);
}

/* Run a story that is expected to fault, and check the message. */
static void expect_fault(const story *s, const char *needle, output *out)
{
    static zm_machine m;
    static zm_memory mem;

    memset(out, 0, sizeof *out);
    io.context = out;
    CHECK_EQ_INT(zm_memory_load_bytes(&mem, s->bytes, STORY_SIZE), ZM_OK);
    CHECK_EQ_INT(zm_machine_init(&m, &mem, &io), ZM_OK);

    zm_set_fatal_handler(trap_fault);
    if (setjmp(fault_jump) == 0) {
        zm_machine_run(&m);
        fprintf(stderr, "    expected a fault mentioning \"%s\", but the story quit normally\n", needle);
        CHECK(0 && "story did not fault");
    } else {
        if (strstr(fault_message, needle) == NULL) {
            fprintf(stderr, "    fault was: %s\n", fault_message);
        }
        CHECK(strstr(fault_message, needle) != NULL);
    }
    zm_set_fatal_handler(NULL);

    zm_machine_free(&m);
    zm_memory_free(&mem);
}

/*
 * Tests.
 */

static void test_simple_test_files_say_hello(void)
{
    static const char *const files[] = {
        "/simple-test-r0-s260727.z1",
        "/simple-test-r0-s260727.z2",
        "/simple-test-r1-s260727.z3",
        "/simple-test-r1-s260727.z4",
        "/simple-test-r1-s260727.z5",
        "/simple-test-r1-s260727.z6",
        "/simple-test-r1-s260727.z7",
        "/simple-test-r1-s260727.z8",
    };

    for (size_t i = 0; i < sizeof files / sizeof files[0]; i++) {
        char path[512];
        zm_memory mem;
        zm_machine m;
        output out;

        strcpy(path, QUIDDITY_TEST_DATA_DIR);
        strcat(path, files[i]);
        printf("    %s\n", files[i] + 1);

        memset(&out, 0, sizeof out);
        io.context = &out;
        CHECK_EQ_INT(zm_memory_load_file(&mem, path), ZM_OK);
        CHECK_EQ_INT(zm_machine_init(&m, &mem, &io), ZM_OK);
        zm_machine_run(&m);

        /* The whole program is call Main, print_paddr, quit: three
         * instructions in every Version. */
        CHECK_EQ_STR(out.text, "hello from all z machine versions");
        CHECK_EQ_INT(m.instruction_count, 3);
        CHECK_EQ_INT(m.running, 0);

        zm_machine_free(&m);
        zm_memory_free(&mem);
    }
}

/* Spec §5.2.1, §6.4.4: a routine's locals start from the header's
 * defaults before Version 5 and from zero after, and then the arguments
 * overwrite the first of them. */
static void check_call_locals(uint8_t version, const char *expected)
{
    story s;
    output out;
    zm_machine m;
    zm_memory mem;

    story_init(&s, version);
    EMIT(&s, 0xe0, 0x1f); emit_packed(&s, R1); EMIT(&s, 0x07, G0);  /* call R1 7 -> G0 */
    EMIT(&s, 0xe5, 0x7f, ' ');                                       /* print_char ' ' */
    EMIT(&s, 0xe6, 0xbf, G0);                                        /* print_num G0 */
    EMIT(&s, 0xba);                                                  /* quit */

    at(&s, R1);
    if (version <= 4) {
        EMIT(&s, 0x03, 0x00, 0x0a, 0x00, 0x14, 0x00, 0x1e);          /* 3 locals: 10, 20, 30 */
    } else {
        EMIT(&s, 0x03);                                              /* 3 locals, all 0 */
    }
    EMIT(&s, 0xe6, 0xbf, L1);                                        /* print_num L1 */
    EMIT(&s, 0xe5, 0x7f, ' ');
    EMIT(&s, 0xe6, 0xbf, L2);                                        /* print_num L2 */
    EMIT(&s, 0xe5, 0x7f, ' ');
    EMIT(&s, 0xe6, 0xbf, L3);                                        /* print_num L3 */
    EMIT(&s, 0x9b, 99);                                              /* ret 99 */

    run(&s, &out, &m, &mem);
    CHECK_EQ_STR(out.text, expected);

    /* Back in the first frame with an empty stack. */
    CHECK_EQ_INT(m.depth, 1);
    CHECK_EQ_INT(m.sp, 0);

    zm_machine_free(&m);
    zm_memory_free(&mem);
}

static void test_call_initialises_locals_from_header_before_version_5(void)
{
    check_call_locals(3, "7 20 30 99");
    check_call_locals(4, "7 20 30 99");
}

static void test_call_initialises_locals_to_zero_from_version_5(void)
{
    check_call_locals(5, "7 0 0 99");
    check_call_locals(8, "7 0 0 99");
}

static void test_call_to_address_zero_returns_false(void)
{
    story s;
    output out;
    zm_machine m;
    zm_memory mem;

    /* Spec §6.4.3. G0 is set first so that the 0 it ends up holding is
     * seen to come from the call. */
    story_init(&s, 5);
    EMIT(&s, 0x0d, G0, 0x05);                       /* store G0 5 */
    EMIT(&s, 0xe0, 0x1f, 0x00, 0x00, 0x07, G0);     /* call_vs 0 7 -> G0 */
    EMIT(&s, 0xe6, 0xbf, G0);                       /* print_num G0 */
    EMIT(&s, 0x8f, 0x00, 0x00);                     /* call_1n 0 */
    EMIT(&s, 0xda, 0x1f, 0x00, 0x00, 0x01);         /* call_2n 0 1 */
    EMIT(&s, 0xe5, 0x7f, '!');                      /* print_char '!' */
    EMIT(&s, 0xba);                                 /* quit */

    run(&s, &out, &m, &mem);
    CHECK_EQ_STR(out.text, "0!");
    CHECK_EQ_INT(m.sp, 0);

    zm_machine_free(&m);
    zm_memory_free(&mem);
}

static void test_operands_are_evaluated_first_to_last(void)
{
    story s;
    output out;
    zm_machine m;
    zm_memory mem;

    /* Spec §4.5.2: with two stack operands, the first pops the top. The
     * routine also leaves values on the stack, which the return throws
     * away (Spec §6.3.2). */
    story_init(&s, 3);
    EMIT(&s, 0xe8, 0x7f, 10);                                        /* push 10 */
    EMIT(&s, 0xe8, 0x7f, 3);                                         /* push 3 */
    EMIT(&s, 0xe0, 0x2b); emit_packed(&s, R1); EMIT(&s, SP, SP, G0); /* call R1 sp sp -> G0 */
    EMIT(&s, 0xe6, 0xbf, G0);                                        /* print_num G0 */
    EMIT(&s, 0xba);                                                  /* quit */

    at(&s, R1);
    EMIT(&s, 0x02, 0x00, 0x00, 0x00, 0x00);                          /* 2 locals */
    EMIT(&s, 0xe6, 0xbf, L1);                                        /* print_num L1 */
    EMIT(&s, 0xe5, 0x7f, ' ');
    EMIT(&s, 0xe6, 0xbf, L2);                                        /* print_num L2 */
    EMIT(&s, 0xe5, 0x7f, ' ');
    EMIT(&s, 0xe8, 0x7f, 99);                                        /* push 99 */
    EMIT(&s, 0xe8, 0x7f, 100);                                       /* push 100 */
    EMIT(&s, 0x9b, 42);                                              /* ret 42 */

    run(&s, &out, &m, &mem);
    CHECK_EQ_STR(out.text, "3 10 42");
    CHECK_EQ_INT(m.sp, 0);

    zm_machine_free(&m);
    zm_memory_free(&mem);
}

static void test_stack_is_read_and_written_in_place_by_reference(void)
{
    story s;
    output out;
    zm_machine m;
    zm_memory mem;

    /* Spec §6.3.4: inc, load, store, and pull given variable 0 act on the
     * top of the stack without changing its depth. */
    story_init(&s, 3);
    EMIT(&s, 0xe8, 0x7f, 5);            /* push 5                   stack: 5 */
    EMIT(&s, 0xe8, 0x7f, 7);            /* push 7                   stack: 5 7 */
    EMIT(&s, 0x95, SP);                 /* inc sp                   stack: 5 8 */
    EMIT(&s, 0x9e, SP, G0);             /* load sp -> G0  (G0 = 8)  stack: 5 8 */
    EMIT(&s, 0x0d, SP, 9);              /* store sp 9               stack: 5 9 */
    EMIT(&s, 0xe9, 0x7f, G1);           /* pull G1        (G1 = 9)  stack: 5 */
    EMIT(&s, 0xe9, 0x7f, G2);           /* pull G2        (G2 = 5)  stack: empty */
    EMIT(&s, 0xe6, 0xbf, G0);           /* print_num G0 */
    EMIT(&s, 0xe5, 0x7f, ' ');
    EMIT(&s, 0xe6, 0xbf, G1);           /* print_num G1 */
    EMIT(&s, 0xe5, 0x7f, ' ');
    EMIT(&s, 0xe6, 0xbf, G2);           /* print_num G2 */
    EMIT(&s, 0xe5, 0x7f, ' ');
    EMIT(&s, 0xe8, 0x7f, 1);            /* push 1                   stack: 1 */
    EMIT(&s, 0xe8, 0x7f, 2);            /* push 2                   stack: 1 2 */
    EMIT(&s, 0xe9, 0x7f, SP);           /* pull sp: pop 2, then write it over the new top: 2 */
    EMIT(&s, 0xe6, 0xbf, SP);           /* print_num sp (pops)      stack: empty */
    EMIT(&s, 0xba);                     /* quit */

    run(&s, &out, &m, &mem);
    CHECK_EQ_STR(out.text, "8 9 5 2");
    CHECK_EQ_INT(m.sp, 0);

    /* The globals are also reachable from outside the machine. */
    CHECK_EQ_INT(zm_machine_read_variable(&m, G0), 8);
    CHECK_EQ_INT(zm_machine_read_variable(&m, G1), 9);
    CHECK_EQ_INT(zm_machine_read_variable(&m, G2), 5);
    zm_machine_write_variable(&m, SP, 1234);
    CHECK_EQ_INT(m.sp, 1);
    CHECK_EQ_INT(zm_machine_read_variable(&m, SP), 1234);
    CHECK_EQ_INT(m.sp, 0);

    zm_machine_free(&m);
    zm_memory_free(&mem);
}

static void test_branches_and_jumps(void)
{
    story s;
    output out;
    zm_machine m;
    zm_memory mem;

    story_init(&s, 3);
    /* Spec §4.7: short branches skip forward over a 3-byte print. */
    EMIT(&s, 0x90, 0x00, 0xc5);                             /* jz 0 ?+5 (taken) */
    EMIT(&s, 0xe5, 0x7f, 'X');                              /* skipped */
    EMIT(&s, 0xc1, 0x55, 1, 2, 3, 1, 0x80, 0x05);           /* je 1 2 3 1 ?+5, long form (taken) */
    EMIT(&s, 0xe5, 0x7f, 'Y');                              /* skipped */
    EMIT(&s, 0x01, 1, 2, 0x45);                             /* je 1 2 ?~+5 (not equal, so taken) */
    EMIT(&s, 0xe5, 0x7f, 'Z');                              /* skipped */
    EMIT(&s, 0xe5, 0x7f, 'A');
    /* Spec §15 jump: an unconditional signed offset with the same
     * arithmetic. */
    EMIT(&s, 0x8c, 0x00, 0x05);                             /* jump +5 */
    EMIT(&s, 0xe5, 0x7f, 'Q');                              /* skipped */
    EMIT(&s, 0xe5, 0x7f, 'B');
    /* A loop: the long branch form is the only one that can go backward.
     * From the byte after the branch, the top of the loop is 9 bytes
     * back, so the offset is -7 (Spec §4.7.2). */
    EMIT(&s, 0x0d, G0, 3);                                  /* store G0 3 */
    EMIT(&s, 0xe5, 0x7f, 'x');                              /* loop: print_char 'x' */
    EMIT(&s, 0x96, G0);                                     /* dec G0 */
    EMIT(&s, 0xa0, G0, 0x3f, 0xf9);                         /* jz G0 ?~loop */
    /* Spec §4.7.1: offsets 0 and 1 return from the routine. */
    EMIT(&s, 0xe0, 0x3f); emit_packed(&s, R1); EMIT(&s, G0); /* call R1 -> G0 */
    EMIT(&s, 0xe6, 0xbf, G0);                               /* print_num G0 */
    EMIT(&s, 0xe0, 0x3f); emit_packed(&s, R2); EMIT(&s, G0); /* call R2 -> G0 */
    EMIT(&s, 0xe6, 0xbf, G0);                               /* print_num G0 */
    EMIT(&s, 0xe0, 0x3f); emit_packed(&s, R3); EMIT(&s, G0); /* call R3 -> G0 */
    EMIT(&s, 0xe6, 0xbf, G0);                               /* print_num G0 */
    EMIT(&s, 0xba);                                         /* quit */

    at(&s, R1);
    EMIT(&s, 0x00, 0x90, 0x00, 0xc0);                       /* jz 0 ?rfalse */
    at(&s, R2);
    EMIT(&s, 0x00, 0x90, 0x00, 0xc1);                       /* jz 0 ?rtrue */
    at(&s, R3);
    EMIT(&s, 0x00, 0x90, 0x05, 0xc0, 0x9b, 7);              /* jz 5 ?rfalse (not taken); ret 7 */

    run(&s, &out, &m, &mem);
    CHECK_EQ_STR(out.text, "ABxxx017");

    zm_machine_free(&m);
    zm_memory_free(&mem);
}

static void test_print_opcodes(void)
{
    story s;
    output out;
    zm_machine m;
    zm_memory mem;

    story_init(&s, 3);
    EMIT(&s, 0xb2); emit_bytes(&s, hello, sizeof hello);     /* print "Hello.^" */
    EMIT(&s, 0xe0, 0x3f); emit_packed(&s, R1); EMIT(&s, G0); /* call R1 -> G0 */
    EMIT(&s, 0xe6, 0xbf, G0);                               /* print_num G0 (1, from print_ret) */
    EMIT(&s, 0xbb);                                         /* new_line */
    EMIT(&s, 0x87); emit_bytes(&s, (const uint8_t[]){ STRING >> 8, STRING & 0xff }, 2); /* print_addr STRING */
    EMIT(&s, 0x8d); emit_packed(&s, STRING);                /* print_paddr STRING */
    EMIT(&s, 0xe6, 0x3f, 0xff, 0xff);                       /* print_num -1 */
    EMIT(&s, 0xe5, 0x7f, ' ');
    EMIT(&s, 0xe6, 0x3f, 0x7f, 0xff);                       /* print_num 32767 */
    EMIT(&s, 0xe5, 0x7f, ' ');
    EMIT(&s, 0xe6, 0x3f, 0x80, 0x00);                       /* print_num -32768 */
    EMIT(&s, 0xe5, 0x7f, ' ');
    EMIT(&s, 0xe6, 0x7f, 0);                                /* print_num 0 */
    EMIT(&s, 0xba);                                         /* quit */

    at(&s, R1);
    EMIT(&s, 0x00, 0xb3); emit_bytes(&s, hello, sizeof hello); /* print_ret "Hello.^" */
    at(&s, STRING);
    emit_bytes(&s, hello, sizeof hello);

    run(&s, &out, &m, &mem);
    CHECK_EQ_STR(out.text, "Hello.\nHello.\n\n1\nHello.\nHello.\n-1 32767 -32768 0");

    zm_machine_free(&m);
    zm_memory_free(&mem);
}

static void test_return_opcodes(void)
{
    story s;
    output out;
    zm_machine m;
    zm_memory mem;

    story_init(&s, 4);
    EMIT(&s, 0x88); emit_packed(&s, R1); EMIT(&s, G0);          /* call_1s R1 -> G0 */
    EMIT(&s, 0xe6, 0xbf, G0);                                   /* print_num G0 */
    EMIT(&s, 0xe5, 0x7f, ' ');
    EMIT(&s, 0xd9, 0x1f); emit_packed(&s, R2); EMIT(&s, 5, G0); /* call_2s R2 5 -> G0 */
    EMIT(&s, 0xe6, 0xbf, G0);
    EMIT(&s, 0xe5, 0x7f, ' ');
    EMIT(&s, 0xe0, 0x3f); emit_packed(&s, R3); EMIT(&s, G0);    /* call_vs R3 -> G0 */
    EMIT(&s, 0xe6, 0xbf, G0);
    EMIT(&s, 0xe0, 0x3f); emit_packed(&s, R4); EMIT(&s, G0);    /* call_vs R4 -> G0 */
    EMIT(&s, 0xe6, 0xbf, G0);
    EMIT(&s, 0xba);

    at(&s, R1);
    EMIT(&s, 0x00, 0xe8, 0x7f, 42, 0xb8);                       /* push 42; ret_popped */
    at(&s, R2);
    EMIT(&s, 0x01, 0x00, 0x00);                                 /* 1 local */
    EMIT(&s, 0xb4);                                             /* nop */
    EMIT(&s, 0xe8, 0x7f, 1);                                    /* push 1 */
    EMIT(&s, 0xb9);                                             /* pop */
    EMIT(&s, 0xab, L1);                                         /* ret L1 */
    at(&s, R3);
    EMIT(&s, 0x00, 0xb0);                                       /* rtrue */
    at(&s, R4);
    EMIT(&s, 0x00, 0xb1);                                       /* rfalse */

    run(&s, &out, &m, &mem);
    CHECK_EQ_STR(out.text, "42 5 10");
    CHECK_EQ_INT(m.sp, 0);

    zm_machine_free(&m);
    zm_memory_free(&mem);
}

static void test_version_5_call_variants(void)
{
    story s;
    output out;
    zm_machine m;
    zm_memory mem;

    /* call_vn discards the result; call_vs2 and call_vn2 take up to
     * seven arguments across two type bytes (Spec §4.4.3.1); spare
     * arguments to a routine with fewer locals are dropped (Spec
     * §6.4.4.1). */
    story_init(&s, 5);
    EMIT(&s, 0xf9, 0x15); emit_packed(&s, R1); EMIT(&s, 1, 2, 3);                   /* call_vn R1 1 2 3 */
    EMIT(&s, 0xec, 0x15, 0x55); emit_packed(&s, R2); EMIT(&s, 1, 2, 3, 4, 5, 6, 7, G0); /* call_vs2 R2 1..7 -> G0 */
    EMIT(&s, 0xe6, 0xbf, G0);                                                       /* print_num G0 */
    EMIT(&s, 0xfa, 0x15, 0x5f); emit_packed(&s, R1); EMIT(&s, 1, 2, 3, 4, 5);       /* call_vn2 R1 1 2 3 4 5 */
    EMIT(&s, 0xba);

    at(&s, R1);
    EMIT(&s, 0x03);                                             /* 3 locals */
    EMIT(&s, 0xe6, 0xbf, L1, 0xe6, 0xbf, L2, 0xe6, 0xbf, L3);   /* print_num L1 L2 L3 */
    EMIT(&s, 0xb1);                                             /* rfalse */
    at(&s, R2);
    EMIT(&s, 0x07);                                             /* 7 locals */
    EMIT(&s, 0xe6, 0xbf, L7);                                   /* print_num L7 */
    EMIT(&s, 0x9b, 8);                                          /* ret 8 */

    run(&s, &out, &m, &mem);
    CHECK_EQ_STR(out.text, "12378123");
    CHECK_EQ_INT(m.depth, 1);
    CHECK_EQ_INT(m.sp, 0);

    zm_machine_free(&m);
    zm_memory_free(&mem);
}

static void test_version_6_starts_in_main_routine(void)
{
    story s;
    output out;
    zm_memory mem;
    zm_machine m;

    /* Spec §5.4: the header names a routine to call. Its locals are
     * created as for any call (two of them here, zero in Version 6 by
     * Spec §5.2.1) and it must not return. */
    story_init(&s, 6);
    put_word(s.bytes, ZM_HEADER_INITIAL_PC, (uint16_t)(R1 / 4));
    at(&s, R1);
    EMIT(&s, 0x02);                                             /* 2 locals */
    EMIT(&s, 0xe6, 0xbf, L1, 0xe5, 0x7f, ' ', 0xe6, 0xbf, L2);  /* print_num L1 ' ' L2 */
    EMIT(&s, 0xb0);                                             /* rtrue */

    CHECK_EQ_INT(zm_memory_load_bytes(&mem, s.bytes, STORY_SIZE), ZM_OK);
    CHECK_EQ_INT(zm_machine_init(&m, &mem, &io), ZM_OK);
    CHECK_EQ_INT(m.depth, 1);
    CHECK_EQ_INT(m.frames[0].local_count, 2);
    CHECK_EQ_INT(m.pc, R1 + 1);
    zm_machine_free(&m);
    zm_memory_free(&mem);

    expect_fault(&s, "main routine", &out);
    CHECK_EQ_STR(out.text, "0 0");
}

static void test_faults(void)
{
    story s;
    output out;

    /* Spec §5.5: returning from the first frame. */
    story_init(&s, 3);
    EMIT(&s, 0xb0);                                             /* rtrue */
    expect_fault(&s, "main routine", &out);

    /* Spec §6.3.1: pulling from an empty stack. */
    story_init(&s, 3);
    EMIT(&s, 0xe9, 0x7f, G0);                                   /* pull G0 */
    expect_fault(&s, "underflow", &out);

    /* The same, from inside a routine whose caller has pushed: the
     * routine cannot see its caller's stack. */
    story_init(&s, 3);
    EMIT(&s, 0xe8, 0x7f, 1);                                    /* push 1 */
    EMIT(&s, 0xe0, 0x3f); emit_packed(&s, R1); EMIT(&s, G0);    /* call R1 -> G0 */
    at(&s, R1);
    EMIT(&s, 0x00, 0xb8);                                       /* ret_popped */
    expect_fault(&s, "underflow", &out);

    /* A routine with locals that pops its empty stack reads its last
     * local instead, as Infocom's interpreters did (see underflow_slot
     * in machine.c): Zork I release 2 depends on it. Here L2 starts as 77
     * from the routine header, and "pull L1" on an empty stack copies it. */
    {
        zm_machine m;
        zm_memory mem;

        story_init(&s, 3);
        EMIT(&s, 0xe0, 0x3f); emit_packed(&s, R1); EMIT(&s, G0);    /* call R1 -> G0 */
        EMIT(&s, 0xe6, 0xbf, G0);                                   /* print_num G0 */
        EMIT(&s, 0xba);
        at(&s, R1);
        EMIT(&s, 0x02, 0x00, 0x00, 0x00, 77);                       /* 2 locals: 0, 77 */
        EMIT(&s, 0xe9, 0x7f, L1);                                   /* pull L1 (stack empty) */
        EMIT(&s, 0xab, L1);                                         /* ret L1 */
        run(&s, &out, &m, &mem);
        CHECK_EQ_STR(out.text, "77");
        zm_machine_free(&m);
        zm_memory_free(&mem);
    }

    /* Spec §4.2.2: a local the routine does not have. */
    story_init(&s, 3);
    EMIT(&s, 0xe6, 0xbf, L1);                                   /* print_num L1 */
    expect_fault(&s, "local variable 1", &out);

    /* Spec §14.2: an opcode the Version does not define. */
    story_init(&s, 3);
    EMIT(&s, 0xbe);
    expect_fault(&s, "illegal opcode", &out);

    /* Spec §15 je: one operand is not permitted. */
    story_init(&s, 3);
    EMIT(&s, 0xc1, 0x7f, 0x01, 0xc0);                           /* je 1 ?rfalse */
    expect_fault(&s, "je", &out);

    /* Pushing without end. */
    story_init(&s, 3);
    EMIT(&s, 0xe8, 0x7f, 1);                                    /* loop: push 1 */
    EMIT(&s, 0x8c, 0xff, 0xfc);                                 /* jump loop */
    expect_fault(&s, "overflow", &out);

    /* Calling without end. */
    story_init(&s, 5);
    EMIT(&s, 0x8f); emit_packed(&s, R1);                        /* call_1n R1 */
    at(&s, R1);
    EMIT(&s, 0x00, 0x8f); emit_packed(&s, R1);                  /* call_1n R1 */
    expect_fault(&s, "nested", &out);

    /* Spec §14.2.1: an unknown extended opcode is skipped, not a
     * fault, so this story reaches its quit. */
    {
        zm_machine m;
        zm_memory mem;

        story_init(&s, 5);
        EMIT(&s, 0xbe, 0x40, 0x5f, 0x08, 0x02);                 /* EXT:64 8 2 */
        EMIT(&s, 0xe5, 0x7f, 'k');
        EMIT(&s, 0xba);
        run(&s, &out, &m, &mem);
        CHECK_EQ_STR(out.text, "k");
        zm_machine_free(&m);
        zm_memory_free(&mem);
    }
}

/*
 * Arithmetic, logic, and memory (Spec §2, §15).
 */

static void test_arithmetic_is_signed_and_wraps(void)
{
    story s;
    output out;
    zm_machine m;
    zm_memory mem;

    /* Spec §2.2.1 and the worked examples in Spec §2 Remarks. Two large
     * constants need the variable form (Spec §4.4.2), type byte $0f. */
    story_init(&s, 3);
    EMIT(&s, 0x14, 3, 4, G0);                                   /* add 3 4 -> G0 */
    EMIT(&s, 0xe6, 0xbf, G0, 0xe5, 0x7f, ' ');                  /* print_num G0; print_char ' ' */
    EMIT(&s, 0x15, 3, 10, G0);                                  /* sub 3 10 -> G0 */
    EMIT(&s, 0xe6, 0xbf, G0, 0xe5, 0x7f, ' ');
    EMIT(&s, 0xd6, 0x0f, 0x03, 0xe8, 0x03, 0xe8, G0);           /* mul 1000 1000 -> G0 */
    EMIT(&s, 0xe6, 0xbf, G0, 0xe5, 0x7f, ' ');
    EMIT(&s, 0xd7, 0x0f, 0xff, 0xf5, 0x00, 0x02, G0);           /* div -11 2 -> G0 */
    EMIT(&s, 0xe6, 0xbf, G0, 0xe5, 0x7f, ' ');
    EMIT(&s, 0xd7, 0x0f, 0xff, 0xf5, 0xff, 0xfe, G0);           /* div -11 -2 -> G0 */
    EMIT(&s, 0xe6, 0xbf, G0, 0xe5, 0x7f, ' ');
    EMIT(&s, 0xd7, 0x0f, 0x00, 0x0b, 0xff, 0xfe, G0);           /* div 11 -2 -> G0 */
    EMIT(&s, 0xe6, 0xbf, G0, 0xe5, 0x7f, ' ');
    EMIT(&s, 0xd8, 0x0f, 0xff, 0xf3, 0x00, 0x05, G0);           /* mod -13 5 -> G0 */
    EMIT(&s, 0xe6, 0xbf, G0, 0xe5, 0x7f, ' ');
    EMIT(&s, 0xd8, 0x0f, 0x00, 0x0d, 0xff, 0xfb, G0);           /* mod 13 -5 -> G0 */
    EMIT(&s, 0xe6, 0xbf, G0, 0xe5, 0x7f, ' ');
    EMIT(&s, 0xd8, 0x0f, 0xff, 0xf3, 0xff, 0xfb, G0);           /* mod -13 -5 -> G0 */
    EMIT(&s, 0xe6, 0xbf, G0, 0xe5, 0x7f, ' ');
    EMIT(&s, 0xd7, 0x0f, 0x80, 0x00, 0xff, 0xff, G0);           /* div -32768 -1 -> G0 (Spec §2.3.2) */
    EMIT(&s, 0xe6, 0xbf, G0, 0xe5, 0x7f, ' ');
    EMIT(&s, 0xd4, 0x0f, 0x7f, 0xff, 0x00, 0x01, G0);           /* add 32767 1 -> G0 */
    EMIT(&s, 0xe6, 0xbf, G0);
    EMIT(&s, 0xba);                                             /* quit */

    run(&s, &out, &m, &mem);
    CHECK_EQ_STR(out.text, "7 -7 16960 -5 5 -5 -3 3 -3 -32768 -32768");

    zm_machine_free(&m);
    zm_memory_free(&mem);
}

static void test_comparisons_and_bitwise_logic(void)
{
    story s;
    output out;
    zm_machine m;
    zm_memory mem;

    /* Each branch, when it goes the way the Standard says, skips a
     * print_char; a letter in the output names the test that failed.
     * Branch byte $c5 is "on true, forward 5", $45 "on false". */
    story_init(&s, 3);
    EMIT(&s, 0xc2, 0x1f, 0xff, 0xff, 0x01, 0xc5);               /* jl -1 1 ?+5 (signed: taken) */
    EMIT(&s, 0xe5, 0x7f, 'a');
    EMIT(&s, 0xc2, 0x4f, 0x01, 0xff, 0xff, 0x45);               /* jl 1 -1 ?~+5 (not less: taken) */
    EMIT(&s, 0xe5, 0x7f, 'b');
    EMIT(&s, 0xc3, 0x4f, 0x01, 0xff, 0xff, 0xc5);               /* jg 1 -1 ?+5 */
    EMIT(&s, 0xe5, 0x7f, 'c');
    EMIT(&s, 0x03, 5, 5, 0x45);                                 /* jg 5 5 ?~+5 */
    EMIT(&s, 0xe5, 0x7f, 'd');
    EMIT(&s, 0x07, 0x0f, 0x05, 0xc5);                           /* test $0f $05 ?+5 (all flags set) */
    EMIT(&s, 0xe5, 0x7f, 'e');
    EMIT(&s, 0x07, 0x0f, 0x10, 0x45);                           /* test $0f $10 ?~+5 */
    EMIT(&s, 0xe5, 0x7f, 'f');
    EMIT(&s, 0x08, 0x0f, 0xf0, G0);                             /* or $0f $f0 -> G0 */
    EMIT(&s, 0xe6, 0xbf, G0, 0xe5, 0x7f, ' ');
    EMIT(&s, 0x09, 0x0f, 0x3c, G0);                             /* and $0f $3c -> G0 */
    EMIT(&s, 0xe6, 0xbf, G0, 0xe5, 0x7f, ' ');
    EMIT(&s, 0x9f, 0x00, G0);                                   /* not 0 -> G0 (1OP:15 before Version 5) */
    EMIT(&s, 0xe6, 0xbf, G0);
    EMIT(&s, 0xba);

    run(&s, &out, &m, &mem);
    CHECK_EQ_STR(out.text, "255 12 -1");
    zm_machine_free(&m);
    zm_memory_free(&mem);

    /* From Version 5, not is VAR:24 (Spec §14, §15 not). */
    story_init(&s, 5);
    EMIT(&s, 0xf8, 0x3f, 0x0f, 0x0f, G0);                       /* not $0f0f -> G0 */
    EMIT(&s, 0xe6, 0xbf, G0);
    EMIT(&s, 0xba);

    run(&s, &out, &m, &mem);
    CHECK_EQ_STR(out.text, "-3856");
    zm_machine_free(&m);
    zm_memory_free(&mem);
}

static void test_inc_chk_and_dec_chk(void)
{
    story s;
    output out;
    zm_machine m;
    zm_memory mem;

    story_init(&s, 3);
    EMIT(&s, 0x0d, G0, 5);                                      /* store G0 5 */
    EMIT(&s, 0x05, G0, 5, 0xc5);                                /* inc_chk G0 5 ?+5   (6 > 5) */
    EMIT(&s, 0xe5, 0x7f, 'a');
    EMIT(&s, 0x05, G0, 10, 0x45);                               /* inc_chk G0 10 ?~+5 (7 > 10 is false) */
    EMIT(&s, 0xe5, 0x7f, 'b');
    EMIT(&s, 0x04, G0, 7, 0xc5);                                /* dec_chk G0 7 ?+5   (6 < 7) */
    EMIT(&s, 0xe5, 0x7f, 'c');
    EMIT(&s, 0x04, G0, 0, 0x45);                                /* dec_chk G0 0 ?~+5  (5 < 0 is false) */
    EMIT(&s, 0xe5, 0x7f, 'd');
    EMIT(&s, 0xe6, 0xbf, G0, 0xe5, 0x7f, ' ');                  /* print_num G0 (5) */
    /* The comparison is signed (Spec §2.2.1): 32767 + 1 is -32768. */
    EMIT(&s, 0xcd, 0x4f, G0, 0x7f, 0xff);                       /* store G0 32767 */
    EMIT(&s, 0x05, G0, 0, 0x45);                                /* inc_chk G0 0 ?~+5 (-32768 > 0 is false) */
    EMIT(&s, 0xe5, 0x7f, 'e');
    EMIT(&s, 0xe6, 0xbf, G0, 0xe5, 0x7f, ' ');                  /* print_num G0 (-32768) */
    /* Spec §6.3.4: on the stack, in place. */
    EMIT(&s, 0xe8, 0x7f, 1);                                    /* push 1 */
    EMIT(&s, 0x05, SP, 1, 0xc5);                                /* inc_chk sp 1 ?+5 (2 > 1) */
    EMIT(&s, 0xe5, 0x7f, 'f');
    EMIT(&s, 0xe6, 0xbf, SP);                                   /* print_num sp (2, popping it) */
    EMIT(&s, 0xba);

    run(&s, &out, &m, &mem);
    CHECK_EQ_STR(out.text, "5 -32768 2");
    CHECK_EQ_INT(m.sp, 0);

    zm_machine_free(&m);
    zm_memory_free(&mem);
}

#define TABLE 0x220u

static void test_memory_access(void)
{
    story s;
    output out;
    zm_machine m;
    zm_memory mem;

    story_init(&s, 3);
    EMIT(&s, 0xe1, 0x13, 0x02, 0x20, 1, 0x12, 0x34);            /* storew TABLE 1 $1234 */
    EMIT(&s, 0xe2, 0x17, 0x02, 0x20, 1, 0xab);                  /* storeb TABLE 1 $ab */
    EMIT(&s, 0xcf, 0x1f, 0x02, 0x20, 1, G0);                    /* loadw TABLE 1 -> G0 */
    EMIT(&s, 0xe6, 0xbf, G0, 0xe5, 0x7f, ' ');
    EMIT(&s, 0xd0, 0x1f, 0x02, 0x20, 1, G0);                    /* loadb TABLE 1 -> G0 */
    EMIT(&s, 0xe6, 0xbf, G0, 0xe5, 0x7f, ' ');
    EMIT(&s, 0xd0, 0x1f, 0x02, 0x20, 3, G0);                    /* loadb TABLE 3 -> G0 */
    EMIT(&s, 0xe6, 0xbf, G0, 0xe5, 0x7f, ' ');
    /* A negative index reaches back, since addresses wrap at 16 bits. */
    EMIT(&s, 0xcf, 0x0f, 0x02, 0x24, 0xff, 0xff, G0);           /* loadw TABLE+4 -1 -> G0 */
    EMIT(&s, 0xe6, 0xbf, G0);
    EMIT(&s, 0xba);

    run(&s, &out, &m, &mem);
    CHECK_EQ_STR(out.text, "4660 171 52 4660");
    CHECK_EQ_INT(zm_read_byte(&mem, TABLE), 0x00);
    CHECK_EQ_INT(zm_read_byte(&mem, TABLE + 1), 0xab);
    CHECK_EQ_INT(zm_read_word(&mem, TABLE + 2), 0x1234);
    zm_machine_free(&m);
    zm_memory_free(&mem);

    /* Spec §15 storew: the address must be in dynamic memory. */
    story_init(&s, 3);
    EMIT(&s, 0xe1, 0x17, CODE >> 8, CODE & 0xff, 0, 1);         /* storew CODE 0 1 */
    expect_fault(&s, "outside dynamic memory", &out);
}

/*
 * Objects (Spec §12, §15).
 */

/* Property tables for the objects below. */
#define PROPS1 0x2c0u
#define PROPS2 0x2e0u
#define PROPS3 0x300u
#define PROPS4 0x320u

/* A Version 3 object table at OBJECTS: 1 ("a") holds 2 ("b") and 3
 * ("c"); 4 ("d") stands alone. Object 2 has attribute 3 set and
 * properties 5 (word $0102), 3 (byte 7), and 2 (four bytes). Property 4
 * defaults to $44. */
static void build_objects(story *s)
{
    static const uint8_t entries[4][9] = {
        /* attributes            parent sibling child  properties */
        { 0x00, 0x00, 0x00, 0x00, 0,     0,      2,     PROPS1 >> 8, PROPS1 & 0xff },
        { 0x10, 0x00, 0x00, 0x00, 1,     3,      0,     PROPS2 >> 8, PROPS2 & 0xff },
        { 0x00, 0x00, 0x00, 0x00, 1,     0,      0,     PROPS3 >> 8, PROPS3 & 0xff },
        { 0x00, 0x00, 0x00, 0x00, 0,     0,      0,     PROPS4 >> 8, PROPS4 & 0xff },
    };

    put_word(s->bytes, ZM_HEADER_OBJECTS, OBJECTS);
    put_word(s->bytes, OBJECTS + 2 * 3, 0x0044);                /* default for property 4 */
    memcpy(s->bytes + OBJECTS + 62, entries, sizeof entries);   /* Spec §12.3.1: after 31 defaults */

    /* Names are one-word Z-strings: "a" is Z-characters 6 5 5 with the
     * end bit, and so on through the alphabet (Spec §3.5.3). */
    at(s, PROPS1); EMIT(s, 0x01, 0x98, 0xa5, 0x00);
    at(s, PROPS2); EMIT(s, 0x01, 0x9c, 0xa5,
                        0x25, 0x01, 0x02,                       /* property 5, 2 bytes */
                        0x03, 0x07,                             /* property 3, 1 byte */
                        0x62, 0x0a, 0x0b, 0x0c, 0x0d,           /* property 2, 4 bytes */
                        0x00);
    at(s, PROPS3); EMIT(s, 0x01, 0xa0, 0xa5, 0x00);
    at(s, PROPS4); EMIT(s, 0x01, 0xa4, 0xa5, 0x00);
    at(s, CODE);
}

static void test_object_opcodes(void)
{
    story s;
    output out;
    zm_machine m;
    zm_memory mem;
    char expected[256];

    story_init(&s, 3);
    build_objects(&s);

    /* The tree as built. */
    EMIT(&s, 0x93, 2, G0);                                      /* get_parent 2 -> G0 */
    EMIT(&s, 0xe6, 0xbf, G0, 0xe5, 0x7f, ' ');                  /* 1 */
    EMIT(&s, 0x92, 1, G0, 0xc5);                                /* get_child 1 -> G0 ?+5 */
    EMIT(&s, 0xe5, 0x7f, 'a');
    EMIT(&s, 0xe6, 0xbf, G0, 0xe5, 0x7f, ' ');                  /* 2 */
    EMIT(&s, 0x91, 2, G0, 0xc5);                                /* get_sibling 2 -> G0 ?+5 */
    EMIT(&s, 0xe5, 0x7f, 'b');
    EMIT(&s, 0xe6, 0xbf, G0, 0xe5, 0x7f, ' ');                  /* 3 */
    EMIT(&s, 0x91, 3, G0, 0x45);                                /* get_sibling 3 -> G0 ?~+5 (none) */
    EMIT(&s, 0xe5, 0x7f, 'c');
    EMIT(&s, 0xe6, 0xbf, G0, 0xe5, 0x7f, ' ');                  /* 0 */
    EMIT(&s, 0x06, 2, 1, 0xc5);                                 /* jin 2 1 ?+5 */
    EMIT(&s, 0xe5, 0x7f, 'd');
    EMIT(&s, 0x06, 3, 2, 0x45);                                 /* jin 3 2 ?~+5 */
    EMIT(&s, 0xe5, 0x7f, 'e');

    /* Attributes. */
    EMIT(&s, 0x0a, 2, 3, 0xc5);                                 /* test_attr 2 3 ?+5 */
    EMIT(&s, 0xe5, 0x7f, 'f');
    EMIT(&s, 0x0a, 2, 4, 0x45);                                 /* test_attr 2 4 ?~+5 */
    EMIT(&s, 0xe5, 0x7f, 'g');
    EMIT(&s, 0x0b, 2, 4);                                       /* set_attr 2 4 */
    EMIT(&s, 0x0a, 2, 4, 0xc5);                                 /* test_attr 2 4 ?+5 */
    EMIT(&s, 0xe5, 0x7f, 'h');
    EMIT(&s, 0x0c, 2, 3);                                       /* clear_attr 2 3 */
    EMIT(&s, 0x0a, 2, 3, 0x45);                                 /* test_attr 2 3 ?~+5 */
    EMIT(&s, 0xe5, 0x7f, 'i');

    /* Moving objects. */
    EMIT(&s, 0x0e, 4, 1);                                       /* insert_obj 4 1 */
    EMIT(&s, 0x92, 1, G0, 0xc5);                                /* get_child 1 -> G0 ?+5 */
    EMIT(&s, 0xe5, 0x7f, 'j');
    EMIT(&s, 0xe6, 0xbf, G0, 0xe5, 0x7f, ' ');                  /* 4 */
    EMIT(&s, 0x91, 4, G0, 0xc5);                                /* get_sibling 4 -> G0 ?+5 */
    EMIT(&s, 0xe5, 0x7f, 'k');
    EMIT(&s, 0xe6, 0xbf, G0, 0xe5, 0x7f, ' ');                  /* 2 */
    EMIT(&s, 0x99, 2);                                          /* remove_obj 2 */
    EMIT(&s, 0x91, 4, G0, 0xc5);                                /* get_sibling 4 -> G0 ?+5 */
    EMIT(&s, 0xe5, 0x7f, 'l');
    EMIT(&s, 0xe6, 0xbf, G0, 0xe5, 0x7f, ' ');                  /* 3 */
    EMIT(&s, 0x93, 2, G0);                                      /* get_parent 2 -> G0 */
    EMIT(&s, 0xe6, 0xbf, G0, 0xe5, 0x7f, ' ');                  /* 0 */

    /* Names. */
    EMIT(&s, 0x9a, 1);                                          /* print_obj 1 */
    EMIT(&s, 0x9a, 3);                                          /* print_obj 3 */
    EMIT(&s, 0xe5, 0x7f, ' ');                                  /* "ac " */

    /* Properties. */
    EMIT(&s, 0x11, 2, 5, G0);                                   /* get_prop 2 5 -> G0 */
    EMIT(&s, 0xe6, 0xbf, G0, 0xe5, 0x7f, ' ');                  /* 258 */
    EMIT(&s, 0x11, 2, 3, G0);                                   /* get_prop 2 3 -> G0 */
    EMIT(&s, 0xe6, 0xbf, G0, 0xe5, 0x7f, ' ');                  /* 7 */
    EMIT(&s, 0x11, 2, 4, G0);                                   /* get_prop 2 4 -> G0 (default) */
    EMIT(&s, 0xe6, 0xbf, G0, 0xe5, 0x7f, ' ');                  /* 68 */
    EMIT(&s, 0x11, 0, 4, G0);                                   /* get_prop 0 4 -> G0 (nothing: default) */
    EMIT(&s, 0xe6, 0xbf, G0, 0xe5, 0x7f, ' ');                  /* 68 */
    EMIT(&s, 0x12, 2, 3, G0);                                   /* get_prop_addr 2 3 -> G0 */
    EMIT(&s, 0xe6, 0xbf, G0, 0xe5, 0x7f, ' ');                  /* PROPS2 + 7 */
    EMIT(&s, 0xa4, G0, G0);                                     /* get_prop_len G0 -> G0 */
    EMIT(&s, 0xe6, 0xbf, G0, 0xe5, 0x7f, ' ');                  /* 1 */
    EMIT(&s, 0x12, 2, 4, G0);                                   /* get_prop_addr 2 4 -> G0 (absent) */
    EMIT(&s, 0xe6, 0xbf, G0, 0xe5, 0x7f, ' ');                  /* 0 */
    EMIT(&s, 0xa4, G0, G0);                                     /* get_prop_len 0 -> G0 */
    EMIT(&s, 0xe6, 0xbf, G0, 0xe5, 0x7f, ' ');                  /* 0 */
    EMIT(&s, 0x12, 2, 2, G0);                                   /* get_prop_addr 2 2 -> G0 */
    EMIT(&s, 0xa4, G0, G0);                                     /* get_prop_len -> G0 */
    EMIT(&s, 0xe6, 0xbf, G0, 0xe5, 0x7f, ' ');                  /* 4 */
    EMIT(&s, 0x13, 2, 0, G0);                                   /* get_next_prop 2 0 -> G0 */
    EMIT(&s, 0xe6, 0xbf, G0, 0xe5, 0x7f, ' ');                  /* 5 */
    EMIT(&s, 0x13, 2, 5, G0);                                   /* get_next_prop 2 5 -> G0 */
    EMIT(&s, 0xe6, 0xbf, G0, 0xe5, 0x7f, ' ');                  /* 3 */
    EMIT(&s, 0x13, 2, 3, G0);                                   /* get_next_prop 2 3 -> G0 */
    EMIT(&s, 0xe6, 0xbf, G0, 0xe5, 0x7f, ' ');                  /* 2 */
    EMIT(&s, 0x13, 2, 2, G0);                                   /* get_next_prop 2 2 -> G0 */
    EMIT(&s, 0xe6, 0xbf, G0, 0xe5, 0x7f, ' ');                  /* 0 */
    EMIT(&s, 0xe3, 0x53, 2, 5, 0xbe, 0xef);                     /* put_prop 2 5 $beef */
    EMIT(&s, 0x11, 2, 5, G0);                                   /* get_prop 2 5 -> G0 */
    EMIT(&s, 0xe6, 0xbf, G0, 0xe5, 0x7f, ' ');                  /* -16657 */
    EMIT(&s, 0xe3, 0x53, 2, 3, 0x01, 0xff);                     /* put_prop 2 3 $1ff (low byte kept) */
    EMIT(&s, 0x11, 2, 3, G0);                                   /* get_prop 2 3 -> G0 */
    EMIT(&s, 0xe6, 0xbf, G0, 0xe5, 0x7f, ' ');                  /* 255 */
    EMIT(&s, 0x12, 2, 40, G0);                                  /* get_prop_addr 2 40 -> G0 (no such number) */
    EMIT(&s, 0xe6, 0xbf, G0, 0xe5, 0x7f, ' ');                  /* 0 */

    /* Enquiries about nothing. */
    EMIT(&s, 0x93, 0, G0);                                      /* get_parent 0 -> G0 */
    EMIT(&s, 0xe6, 0xbf, G0);                                   /* 0 */
    EMIT(&s, 0x92, 0, G0, 0x45);                                /* get_child 0 -> G0 ?~+5 */
    EMIT(&s, 0xe5, 0x7f, 'm');
    EMIT(&s, 0x0a, 0, 3, 0x45);                                 /* test_attr 0 3 ?~+5 */
    EMIT(&s, 0xe5, 0x7f, 'n');
    EMIT(&s, 0xba);

    run(&s, &out, &m, &mem);
    snprintf(expected, sizeof expected,
             "1 2 3 0 4 2 3 0 ac 258 7 68 68 %u 1 0 0 4 5 3 2 0 -16657 255 0 0", (unsigned)(PROPS2 + 7));
    CHECK_EQ_STR(out.text, expected);

    /* The table itself reflects the moves and writes. */
    CHECK_EQ_INT(zm_read_byte(&mem, OBJECTS + 62 + 9 + 4), 0);         /* object 2's parent */
    CHECK_EQ_INT(zm_read_byte(&mem, OBJECTS + 62 + 27 + 4), 1);        /* object 4's parent */
    CHECK_EQ_INT(zm_read_byte(&mem, OBJECTS + 62 + 9), 0x08);          /* object 2: attribute 4 only */
    CHECK_EQ_INT(zm_read_word(&mem, PROPS2 + 4), 0xbeef);
    CHECK_EQ_INT(zm_read_byte(&mem, PROPS2 + 7), 0xff);

    zm_machine_free(&m);
    zm_memory_free(&mem);
}

static void test_object_faults(void)
{
    story s;
    output out;

    /* Spec §15 print_obj: an invalid object halts. */
    story_init(&s, 3);
    build_objects(&s);
    EMIT(&s, 0x9a, 0);                                          /* print_obj 0 */
    expect_fault(&s, "print_obj", &out);

    /* Spec §15 put_prop: no such property. */
    story_init(&s, 3);
    build_objects(&s);
    EMIT(&s, 0xe3, 0x57, 2, 4, 1);                              /* put_prop 2 4 1 */
    expect_fault(&s, "no property 4", &out);

    /* Spec §15 get_prop: longer than two bytes. */
    story_init(&s, 3);
    build_objects(&s);
    EMIT(&s, 0x11, 2, 2, G0);                                   /* get_prop 2 2 -> G0 */
    expect_fault(&s, "4 bytes", &out);

    /* Spec §15 get_next_prop: following a property the object lacks. */
    story_init(&s, 3);
    build_objects(&s);
    EMIT(&s, 0x13, 2, 4, G0);                                   /* get_next_prop 2 4 -> G0 */
    expect_fault(&s, "get_next_prop", &out);

    /* Property numbers that no object can have. */
    story_init(&s, 3);
    build_objects(&s);
    EMIT(&s, 0x11, 2, 40, G0);                                  /* get_prop 2 40 -> G0 */
    expect_fault(&s, "property 40", &out);

    /* Changing nothing. */
    story_init(&s, 3);
    build_objects(&s);
    EMIT(&s, 0x0e, 0, 1);                                       /* insert_obj 0 1 */
    expect_fault(&s, "object 0", &out);

    story_init(&s, 3);
    build_objects(&s);
    EMIT(&s, 0x0b, 1, 32);                                      /* set_attr 1 32 */
    expect_fault(&s, "attribute 32", &out);

    /* Spec §2.3.1. */
    story_init(&s, 3);
    EMIT(&s, 0x17, 1, 0, G0);                                   /* div 1 0 -> G0 */
    expect_fault(&s, "division by zero", &out);

    story_init(&s, 3);
    EMIT(&s, 0x18, 1, 0, G0);                                   /* mod 1 0 -> G0 */
    expect_fault(&s, "remainder", &out);
}

/*
 * Random numbers (Spec §2.4), shifts, and the odd ones out.
 */

static void test_random_numbers(void)
{
    story s;
    output out;
    zm_machine m;
    zm_memory mem;
    int seen[7] = { 0 };

    /* Spec §2 Remarks: a seed below 1000 gives the sequence 1, 2, ...,
     * seed, each reduced to the range asked for. Seeding stores 0. */
    story_init(&s, 3);
    EMIT(&s, 0xe7, 0x3f, 0xff, 0xfb, G0);                       /* random -5 -> G0 */
    EMIT(&s, 0xe6, 0xbf, G0);                                   /* 0 */
    for (int i = 0; i < 8; i++) {
        EMIT(&s, 0xe7, 0x7f, 3, G0);                            /* random 3 -> G0 */
        EMIT(&s, 0xe6, 0xbf, G0);
    }
    EMIT(&s, 0xe7, 0x7f, 0, G0);                                /* random 0 -> G0 (back to random mode) */
    EMIT(&s, 0xe6, 0xbf, G0);                                   /* 0 */
    EMIT(&s, 0xba);

    run(&s, &out, &m, &mem);
    CHECK_EQ_STR(out.text, "0123121230");
    CHECK_EQ_INT(m.random_interval, 0);

    /* In random mode every draw is in range, and over enough draws every
     * value turns up (Spec §2.4.1). */
    for (int i = 0; i < 1000; i++) {
        uint16_t value = zm_machine_random(&m, 6);

        if (value >= 1 && value <= 6) {
            seen[value] = 1;
        } else {
            CHECK(0 && "random value out of range");
        }
    }
    CHECK(seen[1] && seen[2] && seen[3] && seen[4] && seen[5] && seen[6]);

    /* Spec §2.4.2: the same large seed gives the same sequence. */
    {
        uint16_t first[8], second[8];

        zm_machine_seed_random(&m, 4321);
        for (int i = 0; i < 8; i++) first[i] = zm_machine_random(&m, 1000);
        zm_machine_seed_random(&m, 4321);
        for (int i = 0; i < 8; i++) second[i] = zm_machine_random(&m, 1000);
        CHECK(memcmp(first, second, sizeof first) == 0);

        zm_machine_seed_random(&m, 4322);
        for (int i = 0; i < 8; i++) second[i] = zm_machine_random(&m, 1000);
        CHECK(memcmp(first, second, sizeof first) != 0);
    }

    zm_machine_free(&m);
    zm_memory_free(&mem);
}

static void test_shifts_and_argument_counts(void)
{
    story s;
    output out;
    zm_machine m;
    zm_memory mem;

    story_init(&s, 5);
    EMIT(&s, 0xbe, 0x02, 0x5f, 1, 3, G0);                       /* log_shift 1 3 -> G0 */
    EMIT(&s, 0xe6, 0xbf, G0, 0xe5, 0x7f, ' ');                  /* 8 */
    EMIT(&s, 0xbe, 0x02, 0x0f, 0x80, 0x00, 0xff, 0xff, G0);     /* log_shift $8000 -1 -> G0 */
    EMIT(&s, 0xe6, 0xbf, G0, 0xe5, 0x7f, ' ');                  /* 16384 */
    EMIT(&s, 0xbe, 0x03, 0x0f, 0x80, 0x00, 0xff, 0xff, G0);     /* art_shift $8000 -1 -> G0 */
    EMIT(&s, 0xe6, 0xbf, G0, 0xe5, 0x7f, ' ');                  /* -16384 */
    EMIT(&s, 0xbe, 0x03, 0x0f, 0xff, 0xf8, 0xff, 0xfe, G0);     /* art_shift -8 -2 -> G0 */
    EMIT(&s, 0xe6, 0xbf, G0, 0xe5, 0x7f, ' ');                  /* -2 */
    EMIT(&s, 0xbe, 0x03, 0x5f, 1, 15, G0);                      /* art_shift 1 15 -> G0 */
    EMIT(&s, 0xe6, 0xbf, G0, 0xe5, 0x7f, ' ');                  /* -32768 */
    EMIT(&s, 0xf9, 0x17); emit_packed(&s, R1); EMIT(&s, 1, 2);  /* call_vn R1 1 2 */
    EMIT(&s, 0xbf, 0xc5);                                       /* piracy ?+5 */
    EMIT(&s, 0xe5, 0x7f, 'p');
    EMIT(&s, 0xba);

    at(&s, R1);
    EMIT(&s, 0x03);                                             /* 3 locals */
    EMIT(&s, 0xff, 0x7f, 2, 0xc5);                              /* check_arg_count 2 ?+5 */
    EMIT(&s, 0xe5, 0x7f, 'x');
    EMIT(&s, 0xff, 0x7f, 3, 0x45);                              /* check_arg_count 3 ?~+5 */
    EMIT(&s, 0xe5, 0x7f, 'y');
    EMIT(&s, 0xe5, 0x7f, '!');
    EMIT(&s, 0xb0);                                             /* rtrue */

    run(&s, &out, &m, &mem);
    CHECK_EQ_STR(out.text, "8 16384 -16384 -2 -32768 !");

    zm_machine_free(&m);
    zm_memory_free(&mem);
}

static void test_verify_checks_the_checksum(void)
{
    story s;
    output out;
    zm_machine m;
    zm_memory mem;
    uint32_t sum = 0;

    /* Spec §15 verify: the sum of the bytes from $40 to the file length
     * in the header, which here is the whole story. The branch is "on
     * false", so 'v' prints only when the checksum agrees. */
    story_init(&s, 3);
    put_word(s.bytes, ZM_HEADER_FILE_LENGTH, STORY_SIZE / 2);
    /* Writing to dynamic memory first must not disturb the sum, which is
     * of the file as loaded. */
    EMIT(&s, 0xe1, 0x13, 0x02, 0x20, 0, 0x12, 0x34);            /* storew TABLE 0 $1234 */
    EMIT(&s, 0xbd, 0x45);                                       /* verify ?~+5 */
    EMIT(&s, 0xe5, 0x7f, 'v');
    EMIT(&s, 0xba);
    for (uint32_t i = 0x40; i < STORY_SIZE; i++) {
        sum += s.bytes[i];
    }
    put_word(s.bytes, ZM_HEADER_CHECKSUM, (uint16_t)sum);

    run(&s, &out, &m, &mem);
    CHECK_EQ_STR(out.text, "v");
    zm_machine_free(&m);
    zm_memory_free(&mem);

    put_word(s.bytes, ZM_HEADER_CHECKSUM, (uint16_t)(sum + 1));
    run(&s, &out, &m, &mem);
    CHECK_EQ_STR(out.text, "");
    zm_machine_free(&m);
    zm_memory_free(&mem);
}

/*
 * Input (Spec §10, §13, §15 read) and the status line (Spec §8.2).
 */

#define DICT 0x600u
#define PARSE 0x2a8u

/* The dictionary of test_dictionary.c: separators '.' ',' '"', and the
 * words "go", "look", "open" with three data bytes each. */
static void build_dictionary(story *s)
{
    static const uint8_t v3[] = {
        3, '.', ',', '"', 7, 0x00, 0x03,
        0x32, 0x85, 0x94, 0xa5, 0, 0, 0,                   /* go   at DICT + 7 */
        0x46, 0x94, 0xc0, 0xa5, 0, 0, 0,                   /* look at DICT + 14 */
        0x52, 0xaa, 0xcc, 0xa5, 0, 0, 0,                   /* open at DICT + 21 */
    };
    static const uint8_t v5[] = {
        3, '.', ',', '"', 9, 0x00, 0x03,
        0x32, 0x85, 0x14, 0xa5, 0x94, 0xa5, 0, 0, 0,       /* go   at DICT + 7 */
        0x46, 0x94, 0x40, 0xa5, 0x94, 0xa5, 0, 0, 0,       /* look at DICT + 16 */
        0x52, 0xaa, 0x4c, 0xa5, 0x94, 0xa5, 0, 0, 0,       /* open at DICT + 25 */
    };

    put_word(s->bytes, ZM_HEADER_DICTIONARY, DICT);
    if (s->version <= 3) {
        memcpy(s->bytes + DICT, v3, sizeof v3);
    } else {
        memcpy(s->bytes + DICT, v5, sizeof v5);
    }
}

static void test_read_in_version_3(void)
{
    static const char *const lines[] = { "Open mailbox, LOOK", NULL };
    story s;
    output out;
    zm_machine m;
    zm_memory mem;

    story_init(&s, 3);
    build_objects(&s);
    build_dictionary(&s);
    s.bytes[TABLE] = 20;                                        /* text buffer: 19 letters and the terminator */
    s.bytes[PARSE] = 4;                                         /* parse buffer: 4 words */
    EMIT(&s, 0x0d, G0, 1);                                      /* store G0 1 (the location, object "a") */
    EMIT(&s, 0x0d, G1, 5);                                      /* store G1 5 (score) */
    EMIT(&s, 0x0d, G2, 7);                                      /* store G2 7 (turns) */
    EMIT(&s, 0xe4, 0x0f, TABLE >> 8, TABLE & 0xff, PARSE >> 8, PARSE & 0xff);  /* sread TEXT PARSE */
    EMIT(&s, 0xe5, 0x7f, '!');
    EMIT(&s, 0xe4, 0x0f, TABLE >> 8, TABLE & 0xff, PARSE >> 8, PARSE & 0xff);  /* sread again: input runs out */
    EMIT(&s, 0xe5, 0x7f, '?');
    EMIT(&s, 0xba);

    script(lines, NULL);
    run(&s, &out, &m, &mem);

    /* The second read found no input, so the game ended there. */
    CHECK_EQ_STR(out.text, "!");
    CHECK_EQ_INT(m.running, 0);

    /* Spec §15 read: lower-cased text from byte 1, zero-terminated. */
    CHECK(memcmp(mem.bytes + TABLE + 1, "open mailbox, look", 19) == 0);

    /* Spec §13.6.3: four words, with positions counted from byte 0. */
    CHECK_EQ_INT(zm_read_byte(&mem, PARSE + 1), 4);
    CHECK_EQ_INT(zm_read_word(&mem, PARSE + 2), DICT + 21);     /* open */
    CHECK_EQ_INT(zm_read_byte(&mem, PARSE + 4), 4);
    CHECK_EQ_INT(zm_read_byte(&mem, PARSE + 5), 1);
    CHECK_EQ_INT(zm_read_word(&mem, PARSE + 6), 0);             /* mailbox */
    CHECK_EQ_INT(zm_read_byte(&mem, PARSE + 8), 7);
    CHECK_EQ_INT(zm_read_byte(&mem, PARSE + 9), 6);
    CHECK_EQ_INT(zm_read_word(&mem, PARSE + 10), 0);            /* , */
    CHECK_EQ_INT(zm_read_byte(&mem, PARSE + 12), 1);
    CHECK_EQ_INT(zm_read_byte(&mem, PARSE + 13), 13);
    CHECK_EQ_INT(zm_read_word(&mem, PARSE + 14), DICT + 14);    /* look */
    CHECK_EQ_INT(zm_read_byte(&mem, PARSE + 16), 4);
    CHECK_EQ_INT(zm_read_byte(&mem, PARSE + 17), 15);

    /* Spec §8.2.4: the status line was shown before each read, from the
     * first three globals. */
    CHECK_EQ_INT(status.shown, 2);
    CHECK_EQ_STR(status.name, "a");
    CHECK_EQ_INT(status.time_game, 0);
    CHECK_EQ_INT(status.a, 5);
    CHECK_EQ_INT(status.b, 7);

    /* Spec §11.1.4: a front end with a status line has bit 4 clear. */
    CHECK_EQ_INT(zm_read_byte(&mem, ZM_HEADER_FLAGS1) & ZM_FLAGS1_V3_NO_STATUS_LINE, 0);

    script(NULL, NULL);
    zm_machine_free(&m);
    zm_memory_free(&mem);
}

static void test_read_in_version_5(void)
{
    static const char *const lines[] = { "look", "Hi There", "xyz", NULL };
    story s;
    output out;
    zm_machine m;
    zm_memory mem;

    story_init(&s, 5);
    build_dictionary(&s);
    s.bytes[TABLE] = 20;
    s.bytes[PARSE] = 4;
    EMIT(&s, 0xe4, 0x0f, TABLE >> 8, TABLE & 0xff, PARSE >> 8, PARSE & 0xff, G0);  /* aread TEXT PARSE -> G0 */
    EMIT(&s, 0xe6, 0xbf, G0, 0xe5, 0x7f, ' ');
    /* Spec §15 read: byte 1 counts characters left over from an
     * interrupted read, which the new input follows, so a game clears
     * it before an ordinary read, as the Inform library does. */
    EMIT(&s, 0xe2, 0x17, TABLE >> 8, TABLE & 0xff, 1, 0);      /* storeb TEXT 1 0 */
    EMIT(&s, 0xe4, 0x1f, TABLE >> 8, TABLE & 0xff, 0, G0);     /* aread TEXT 0 -> G0: no lexing */
    EMIT(&s, 0xe6, 0xbf, G0, 0xe5, 0x7f, ' ');
    EMIT(&s, 0xe2, 0x17, TABLE >> 8, TABLE & 0xff, 1, 2);      /* storeb TEXT 1 2: pretend "hi" is left over */
    EMIT(&s, 0xe4, 0x1f, TABLE >> 8, TABLE & 0xff, 0, G0);     /* aread TEXT 0 -> G0 */
    EMIT(&s, 0xe6, 0xbf, G0);
    EMIT(&s, 0xba);

    script(lines, NULL);
    run(&s, &out, &m, &mem);

    /* Each read stores its terminator, 13 for a new-line. */
    CHECK_EQ_STR(out.text, "13 13 13");

    /* After the last read: "hi" left over plus "xyz". */
    CHECK_EQ_INT(zm_read_byte(&mem, TABLE + 1), 5);
    CHECK(memcmp(mem.bytes + TABLE + 2, "hixyz", 5) == 0);

    /* The parse buffer still holds the first read's analysis, since the
     * later reads asked for none: one word, "look", at position 2. */
    CHECK_EQ_INT(zm_read_byte(&mem, PARSE + 1), 1);
    CHECK_EQ_INT(zm_read_word(&mem, PARSE + 2), DICT + 16);
    CHECK_EQ_INT(zm_read_byte(&mem, PARSE + 4), 4);
    CHECK_EQ_INT(zm_read_byte(&mem, PARSE + 5), 2);

    /* No status line in Version 5 (Spec §8.2). */
    CHECK_EQ_INT(status.shown, 0);

    script(NULL, NULL);
    zm_machine_free(&m);
    zm_memory_free(&mem);
}

static void test_read_char_tokenise_and_encode_text(void)
{
    static const int keys[] = { 'y', 0 };
    static const uint8_t look_encoded[6] = { 0x46, 0x94, 0x40, 0xa5, 0x94, 0xa5 };
    story s;
    output out;
    zm_machine m;
    zm_memory mem;

    story_init(&s, 5);
    build_dictionary(&s);
    s.bytes[TABLE] = 20;
    s.bytes[TABLE + 1] = 7;
    memcpy(s.bytes + TABLE + 2, "go look", 7);
    s.bytes[PARSE] = 4;
    EMIT(&s, 0xf6, 0x7f, 1, G0);                                /* read_char 1 -> G0 */
    EMIT(&s, 0xe6, 0xbf, G0);                                   /* 121 */
    EMIT(&s, 0xfb, 0x0f, TABLE >> 8, TABLE & 0xff, PARSE >> 8, PARSE & 0xff);  /* tokenise TEXT PARSE */
    EMIT(&s, 0xfc, 0x14, (TABLE + 2) >> 8, (TABLE + 2) & 0xff, 4, 3, 0x02, 0x30);  /* encode_text TEXT+2 4 3 $230 */
    EMIT(&s, 0xf6, 0x7f, 1, G0);                                /* read_char 1 -> G0: input runs out */
    EMIT(&s, 0xe5, 0x7f, '?');
    EMIT(&s, 0xba);

    script(NULL, keys);
    run(&s, &out, &m, &mem);
    CHECK_EQ_STR(out.text, "121");
    CHECK_EQ_INT(m.running, 0);

    CHECK_EQ_INT(zm_read_byte(&mem, PARSE + 1), 2);
    CHECK_EQ_INT(zm_read_word(&mem, PARSE + 2), DICT + 7);      /* go */
    CHECK_EQ_INT(zm_read_byte(&mem, PARSE + 5), 2);
    CHECK_EQ_INT(zm_read_word(&mem, PARSE + 6), DICT + 16);     /* look */
    CHECK_EQ_INT(zm_read_byte(&mem, PARSE + 9), 5);

    /* "look" as the dictionary stores it. */
    CHECK(memcmp(mem.bytes + 0x230, look_encoded, 6) == 0);

    script(NULL, NULL);
    zm_machine_free(&m);
    zm_memory_free(&mem);
}

/* An interrupt routine for timed reads: counts its calls in G2, prints
 * 'T', and asks for the input to stop on its second call. */
static void build_interrupt(story *s)
{
    at(s, R1);
    EMIT(s, 0x00);                                              /* no locals */
    EMIT(s, 0x95, G2);                                          /* inc G2 */
    EMIT(s, 0xe5, 0x7f, 'T');                                   /* print_char 'T' */
    EMIT(s, 0x41, G2, 2, 0xc1);                                 /* je G2 2 ?rtrue */
    EMIT(s, 0xb1);                                              /* rfalse */
    at(s, CODE);
}

static void test_timed_input(void)
{
    static const char *const lines[] = { "look", "xyz", NULL };
    static const int keys[] = { 'q', 0 };
    story s;
    output out;
    zm_machine m;
    zm_memory mem;

    /* Spec §15 read: every time/10 seconds the routine runs, and the
     * read goes on if it returns false; its output is simply printed.
     * When it returns true the read ends with terminator 0, leaving what
     * was typed in the buffer, counted in byte 1 and not folded, for the
     * next read to carry on from; that read then stores the whole. The
     * parse buffer is untouched by the interrupted read. */
    story_init(&s, 5);
    build_dictionary(&s);
    build_interrupt(&s);
    s.bytes[TABLE] = 20;
    s.bytes[PARSE] = 4;
    EMIT(&s, 0xe4, 0x04, TABLE >> 8, TABLE & 0xff, PARSE >> 8, PARSE & 0xff, 10); emit_packed(&s, R1); EMIT(&s, G0);
                                                                /* aread TEXT PARSE 10 R1 -> G0 */
    EMIT(&s, 0xe6, 0xbf, G0, 0xe5, 0x7f, ' ');                  /* print_num G0 ' ' */
    EMIT(&s, 0xe2, 0x17, TABLE >> 8, TABLE & 0xff, 1, 0);       /* storeb TEXT 1 0 */
    EMIT(&s, 0xe4, 0x04, TABLE >> 8, TABLE & 0xff, PARSE >> 8, PARSE & 0xff, 10); emit_packed(&s, R1); EMIT(&s, G0);
                                                                /* aread TEXT PARSE 10 R1 -> G0 */
    EMIT(&s, 0xe6, 0xbf, G0, 0xe5, 0x7f, ' ');                  /* print_num G0 ' ' */
    EMIT(&s, 0xe4, 0x1f, TABLE >> 8, TABLE & 0xff, 0, G0);      /* aread TEXT 0 -> G0 */
    EMIT(&s, 0xe6, 0xbf, G0);                                   /* print_num G0 */
    EMIT(&s, 0xba);                                             /* quit */
    script(lines, NULL);
    script_clock(1, "AB", 2);
    run(&s, &out, &m, &mem);
    CHECK_EQ_STR(out.text, "T13 T0 13");
    CHECK_EQ_INT(timeout_seen, 10);
    CHECK_EQ_INT(zm_read_byte(&mem, TABLE + 1), 5);
    CHECK(memcmp(mem.bytes + TABLE + 2, "abxyz", 5) == 0);
    CHECK_EQ_INT(zm_read_byte(&mem, PARSE + 1), 1);
    CHECK_EQ_INT(m.sp, 0);
    CHECK_EQ_INT(m.depth, 1);
    zm_machine_free(&m);
    zm_memory_free(&mem);

    /* The interrupted read left "AB" as typed. */
    script(lines, NULL);
    script_clock(1, "AB", 1);
    story_init(&s, 5);
    build_dictionary(&s);
    build_interrupt(&s);
    s.bytes[TABLE] = 20;
    s.bytes[PARSE] = 4;
    EMIT(&s, 0x0d, G2, 1);                                      /* store G2 1: stop at the first call */
    EMIT(&s, 0xe4, 0x04, TABLE >> 8, TABLE & 0xff, PARSE >> 8, PARSE & 0xff, 10); emit_packed(&s, R1); EMIT(&s, G0);
    EMIT(&s, 0xe6, 0xbf, G0);                                   /* print_num G0 */
    EMIT(&s, 0xba);
    run(&s, &out, &m, &mem);
    CHECK_EQ_STR(out.text, "T0");
    CHECK_EQ_INT(zm_read_byte(&mem, TABLE + 1), 2);
    CHECK(memcmp(mem.bytes + TABLE + 2, "AB", 2) == 0);
    CHECK_EQ_INT(zm_read_byte(&mem, PARSE + 1), 0);
    zm_machine_free(&m);
    zm_memory_free(&mem);

    /* Spec §15 read_char: the same, with 0 for a wait the routine
     * ended. */
    story_init(&s, 5);
    build_interrupt(&s);
    EMIT(&s, 0xf6, 0x53, 1, 5); emit_packed(&s, R1); EMIT(&s, G0);   /* read_char 1 5 R1 -> G0 */
    EMIT(&s, 0xe6, 0xbf, G0, 0xe5, 0x7f, ' ');                  /* print_num G0 ' ' */
    EMIT(&s, 0xf6, 0x53, 1, 5); emit_packed(&s, R1); EMIT(&s, G0);   /* read_char 1 5 R1 -> G0 */
    EMIT(&s, 0xe6, 0xbf, G0);                                   /* print_num G0 */
    EMIT(&s, 0xba);
    script(NULL, keys);
    script_clock(1, NULL, 0);
    run(&s, &out, &m, &mem);
    CHECK_EQ_STR(out.text, "T113 T0");
    CHECK_EQ_INT(timeout_seen, 5);
    zm_machine_free(&m);
    zm_memory_free(&mem);

    /* Spec §15 read: a time without a routine is no timer, and before
     * Version 5 an interrupted read leaves a zero-terminated text with
     * nothing stored. */
    story_init(&s, 4);
    build_dictionary(&s);
    build_interrupt(&s);
    s.bytes[TABLE] = 20;
    s.bytes[PARSE] = 4;
    EMIT(&s, 0x0d, G2, 1);                                      /* store G2 1 */
    EMIT(&s, 0xe4, 0x07, TABLE >> 8, TABLE & 0xff, PARSE >> 8, PARSE & 0xff, 10);
                                                                /* sread TEXT PARSE 10 */
    EMIT(&s, 0xe5, 0x7f, 'a');                                  /* print_char 'a' */
    EMIT(&s, 0xe4, 0x04, TABLE >> 8, TABLE & 0xff, PARSE >> 8, PARSE & 0xff, 10); emit_packed(&s, R1);
                                                                /* sread TEXT PARSE 10 R1 */
    EMIT(&s, 0xe5, 0x7f, 'b');                                  /* print_char 'b' */
    EMIT(&s, 0xba);
    script(lines, NULL);
    script_clock(1, "AB", 1);
    run(&s, &out, &m, &mem);
    CHECK_EQ_STR(out.text, "aTb");
    CHECK(memcmp(mem.bytes + TABLE + 1, "AB\0", 3) == 0);
    CHECK_EQ_INT(zm_read_byte(&mem, PARSE + 1), 1);
    zm_machine_free(&m);
    zm_memory_free(&mem);

    /* A routine that quits ends the game from inside the read. */
    story_init(&s, 5);
    EMIT(&s, 0xf6, 0x53, 1, 5); emit_packed(&s, R1); EMIT(&s, G0);   /* read_char 1 5 R1 -> G0 */
    EMIT(&s, 0xe5, 0x7f, 'x');                                  /* print_char 'x' */
    EMIT(&s, 0xba);
    at(&s, R1);
    EMIT(&s, 0x00, 0xba);                                       /* quit */
    script(NULL, keys);
    script_clock(1, NULL, 0);
    run(&s, &out, &m, &mem);
    CHECK_EQ_STR(out.text, "");
    CHECK_EQ_INT(m.running, 0);
    zm_machine_free(&m);
    zm_memory_free(&mem);

    script(NULL, NULL);
}

static void test_header_flags_are_set_at_start(void)
{
    story s;
    output out;
    zm_machine m;
    zm_memory mem;

    /* Spec §11.1.4: without a status line, bit 4 of Flags 1 is set; the
     * screen-splitting and variable-pitch bits are cleared. */
    story_init(&s, 3);
    s.bytes[ZM_HEADER_FLAGS1] = 0x60;
    EMIT(&s, 0xba);
    script(NULL, NULL);
    run(&s, &out, &m, &mem);
    CHECK_EQ_INT(zm_read_byte(&mem, ZM_HEADER_FLAGS1), ZM_FLAGS1_V3_NO_STATUS_LINE);
    zm_machine_free(&m);
    zm_memory_free(&mem);

    /* Spec §8.6.1.2: a front end with an upper window sets bit 5. */
    screen_hooks(1);
    story_init(&s, 3);
    EMIT(&s, 0xba);
    run(&s, &out, &m, &mem);
    CHECK_EQ_INT(zm_read_byte(&mem, ZM_HEADER_FLAGS1), ZM_FLAGS1_V3_NO_STATUS_LINE | ZM_FLAGS1_V3_SCREEN_SPLITTING);
    zm_machine_free(&m);
    zm_memory_free(&mem);
    screen_hooks(0);

    /* Spec §11.1.4, §11.1.2: from Version 4, every capability bit says
     * what the front end can do, which here is nothing; the requests in
     * Flags 2 the interpreter cannot grant are cleared while the others
     * stay (undo among them, Spec §6.1.4), and the transcript bit is
     * cleared because no transcript can be made (Spec §7.4). */
    story_init(&s, 5);
    s.bytes[ZM_HEADER_FLAGS1] = 0xff;
    put_word(s.bytes, ZM_HEADER_FLAGS2, 0x01fb);
    EMIT(&s, 0xba);
    run(&s, &out, &m, &mem);
    CHECK_EQ_INT(zm_read_byte(&mem, ZM_HEADER_FLAGS1), 0x40);
    CHECK_EQ_INT(zm_read_word(&mem, ZM_HEADER_FLAGS2), ZM_FLAGS2_FORCE_FIXED | ZM_FLAGS2_UNDO | ZM_FLAGS2_COLOURS);

    /* Spec §11.1.3, §8.4, §8.4.3, §8.1.1, §8.3.3, §11.1.5: the fields
     * that describe the interpreter and its screen. */
    CHECK_EQ_INT(zm_read_byte(&mem, ZM_HEADER_INTERPRETER_NUMBER), 1);
    CHECK_EQ_INT(zm_read_byte(&mem, ZM_HEADER_INTERPRETER_VERSION), 'Q');
    CHECK_EQ_INT(zm_read_byte(&mem, ZM_HEADER_SCREEN_HEIGHT), 24);
    CHECK_EQ_INT(zm_read_byte(&mem, ZM_HEADER_SCREEN_WIDTH), 80);
    CHECK_EQ_INT(zm_read_word(&mem, ZM_HEADER_SCREEN_WIDTH_UNITS), 80);
    CHECK_EQ_INT(zm_read_word(&mem, ZM_HEADER_SCREEN_HEIGHT_UNITS), 24);
    CHECK_EQ_INT(zm_read_byte(&mem, ZM_HEADER_FONT_BYTE_26), 1);
    CHECK_EQ_INT(zm_read_byte(&mem, ZM_HEADER_FONT_BYTE_27), 1);
    CHECK_EQ_INT(zm_read_byte(&mem, ZM_HEADER_DEFAULT_BACKGROUND), ZM_COLOUR_BLACK);
    CHECK_EQ_INT(zm_read_byte(&mem, ZM_HEADER_DEFAULT_FOREGROUND), ZM_COLOUR_WHITE);
    CHECK_EQ_INT(zm_read_byte(&mem, ZM_HEADER_STANDARD_MAJOR), 1);
    CHECK_EQ_INT(zm_read_byte(&mem, ZM_HEADER_STANDARD_MINOR), 1);
    zm_machine_free(&m);
    zm_memory_free(&mem);

    /* With a front end that has colours, bold and italic, and a screen
     * of its own size, the header says so. */
    screen_hooks(1);
    io.screen_width = 60;
    io.screen_height = 14;
    io.default_foreground = ZM_COLOUR_BLUE;
    io.default_background = ZM_COLOUR_YELLOW;
    story_init(&s, 5);
    put_word(s.bytes, ZM_HEADER_FLAGS2, 0x01fb);
    EMIT(&s, 0xba);
    run(&s, &out, &m, &mem);
    CHECK_EQ_INT(zm_read_byte(&mem, ZM_HEADER_FLAGS1),
                 ZM_FLAGS1_COLOURS | ZM_FLAGS1_BOLD | ZM_FLAGS1_ITALIC | ZM_FLAGS1_SOUND | ZM_FLAGS1_TIMED_INPUT);
    CHECK_EQ_INT(zm_read_word(&mem, ZM_HEADER_FLAGS2),
                 ZM_FLAGS2_TRANSCRIPT | ZM_FLAGS2_FORCE_FIXED | ZM_FLAGS2_UNDO | ZM_FLAGS2_COLOURS | ZM_FLAGS2_SOUND);
    CHECK_EQ_INT(zm_read_byte(&mem, ZM_HEADER_SCREEN_HEIGHT), 14);
    CHECK_EQ_INT(zm_read_byte(&mem, ZM_HEADER_SCREEN_WIDTH), 60);
    CHECK_EQ_INT(zm_read_byte(&mem, ZM_HEADER_DEFAULT_BACKGROUND), ZM_COLOUR_YELLOW);
    CHECK_EQ_INT(zm_read_byte(&mem, ZM_HEADER_DEFAULT_FOREGROUND), ZM_COLOUR_BLUE);
    CHECK_EQ_STR(screen_log, "transcript on");
    zm_machine_free(&m);
    zm_memory_free(&mem);
    io.default_foreground = 0;
    io.default_background = 0;
    screen_hooks(0);
}

static void test_input_faults(void)
{
    static const char *const lines[] = { "look", NULL };
    story s;
    output out;

    script(lines, NULL);

    /* Spec §15 read: implausibly small buffers halt. */
    story_init(&s, 3);
    build_dictionary(&s);
    s.bytes[TABLE] = 2;
    s.bytes[PARSE] = 4;
    EMIT(&s, 0xe4, 0x0f, TABLE >> 8, TABLE & 0xff, PARSE >> 8, PARSE & 0xff);
    expect_fault(&s, "room for 2 bytes", &out);

    story_init(&s, 3);
    build_dictionary(&s);
    s.bytes[TABLE] = 20;
    s.bytes[PARSE] = 0;
    EMIT(&s, 0xe4, 0x0f, TABLE >> 8, TABLE & 0xff, PARSE >> 8, PARSE & 0xff);
    expect_fault(&s, "no words", &out);

    /* Spec §8.2.2.1: the status line needs a real object in global 0. */
    story_init(&s, 3);
    build_objects(&s);
    build_dictionary(&s);
    s.bytes[TABLE] = 20;
    s.bytes[PARSE] = 4;
    EMIT(&s, 0xe4, 0x0f, TABLE >> 8, TABLE & 0xff, PARSE >> 8, PARSE & 0xff);
    expect_fault(&s, "global 0, is object 0", &out);

    /* Spec §15 read_char: only device 1. */
    story_init(&s, 5);
    EMIT(&s, 0xf6, 0x7f, 2, G0);                                /* read_char 2 -> G0 */
    expect_fault(&s, "device 2", &out);

    script(NULL, NULL);

    /* A front end with no keyboard at all. */
    story_init(&s, 5);
    build_dictionary(&s);
    s.bytes[TABLE] = 20;
    EMIT(&s, 0xe4, 0x1f, TABLE >> 8, TABLE & 0xff, 0, G0);
    expect_fault(&s, "cannot read a line", &out);
}


/* Dynamic memory free for the screen tests, above the property tables. */
#define STREAM1 0x340u
#define STREAM2 0x360u
#define ARRAY 0x380u
#define TABLE2 0x3a0u

static void test_output_streams(void)
{
    static const char *const lines[] = { "Look", NULL };
    story s;
    output out;
    zm_machine m;
    zm_memory mem;

    screen_hooks(1);
    script(lines, NULL);

    /* Spec §7.1.2.1: stream 3 takes everything, counts it, and shows
     * nothing on the screen; Spec §3.8.2.1: null is not counted. Spec
     * §7.1.2.1.1: a second selection nests inside the first. */
    story_init(&s, 5);
    EMIT(&s, 0xf3, 0x4f, 0x03, STREAM1 >> 8, STREAM1 & 0xff);  /* output_stream 3 STREAM1 */
    EMIT(&s, 0xe5, 0x7f, 'H');                                  /* print_char 'H' */
    EMIT(&s, 0xe5, 0x7f, 0x00);                                 /* print_char 0 */
    EMIT(&s, 0xf3, 0x4f, 0x03, STREAM2 >> 8, STREAM2 & 0xff);  /* output_stream 3 STREAM2 */
    EMIT(&s, 0xe5, 0x7f, 'i');                                  /* print_char 'i' */
    EMIT(&s, 0xf3, 0x3f, 0xff, 0xfd);                           /* output_stream -3 */
    EMIT(&s, 0xbb);                                             /* new_line */
    EMIT(&s, 0xf3, 0x3f, 0xff, 0xfd);                           /* output_stream -3 */
    EMIT(&s, 0xe5, 0x7f, 'X');                                  /* print_char 'X' */

    /* Spec §7.1.1: stream 1 can be turned off and on. */
    EMIT(&s, 0xf3, 0x3f, 0xff, 0xff);                           /* output_stream -1 */
    EMIT(&s, 0xe5, 0x7f, 'Z');                                  /* print_char 'Z' */
    EMIT(&s, 0xf3, 0x7f, 0x01);                                 /* output_stream 1 */
    EMIT(&s, 0xe5, 0x7f, 'Y');                                  /* print_char 'Y' */

    /* Spec §7.4: stream 2 by opcode or by the header bit, and the bit
     * always says which; Spec §7.1.1.1: the command is echoed to it. */
    EMIT(&s, 0xf3, 0x7f, 0x02);                                 /* output_stream 2 */
    EMIT(&s, 0xe5, 0x7f, 'T');                                  /* print_char 'T' */
    EMIT(&s, 0xf3, 0x3f, 0xff, 0xfe);                           /* output_stream -2 */
    EMIT(&s, 0xe5, 0x7f, 'U');                                  /* print_char 'U' */
    EMIT(&s, 0xe2, 0x17, 0x00, 0x10, 0x01, 0x01);               /* storeb $10 1 1 */
    EMIT(&s, 0xe5, 0x7f, 'V');                                  /* print_char 'V' */
    EMIT(&s, 0xf3, 0x7f, 0x04);                                 /* output_stream 4 */
    s.bytes[TABLE] = 20;
    EMIT(&s, 0xe4, 0x1f, TABLE >> 8, TABLE & 0xff, 0x00, G1);   /* aread TABLE 0 -> G1 */
    EMIT(&s, 0xe2, 0x17, 0x00, 0x10, 0x01, 0x00);               /* storeb $10 1 0 */
    EMIT(&s, 0xe5, 0x7f, 'W');                                  /* print_char 'W' */
    EMIT(&s, 0xba);
    run(&s, &out, &m, &mem);
    CHECK_EQ_STR(out.text, "XYTUVW");
    CHECK_EQ_INT(zm_read_word(&mem, STREAM1), 2);
    CHECK_EQ_INT(zm_read_byte(&mem, STREAM1 + 2), 'H');
    CHECK_EQ_INT(zm_read_byte(&mem, STREAM1 + 3), 13);
    CHECK_EQ_INT(zm_read_word(&mem, STREAM2), 1);
    CHECK_EQ_INT(zm_read_byte(&mem, STREAM2 + 2), 'i');
    CHECK_EQ_STR(transcript_text, "TVLook\n");
    CHECK_EQ_STR(record_text, "Look|");
    CHECK_EQ_STR(screen_log, "transcript on;transcript off;transcript on;transcript off");
    CHECK_EQ_INT(zm_read_word(&mem, ZM_HEADER_FLAGS2) & ZM_FLAGS2_TRANSCRIPT, 0);
    zm_machine_free(&m);
    zm_memory_free(&mem);

    /* A front end that declines the transcript leaves the bit clear, so
     * the game can tell (Spec §7.4). */
    transcript_allowed = 0;
    story_init(&s, 5);
    EMIT(&s, 0xf3, 0x7f, 0x02);                                 /* output_stream 2 */
    EMIT(&s, 0xe5, 0x7f, 'T');                                  /* print_char 'T' */
    EMIT(&s, 0xba);
    run(&s, &out, &m, &mem);
    CHECK_EQ_STR(out.text, "T");
    CHECK_EQ_STR(transcript_text, "");
    CHECK_EQ_INT(zm_read_word(&mem, ZM_HEADER_FLAGS2) & ZM_FLAGS2_TRANSCRIPT, 0);
    zm_machine_free(&m);
    zm_memory_free(&mem);

    script(NULL, NULL);
    screen_hooks(0);
}

static void test_windows_and_the_cursor(void)
{
    story s;
    output out;
    zm_machine m;
    zm_memory mem;

    screen_hooks(1);

    /* Spec §8.7.2: selecting the upper window puts its cursor at the top
     * left, and printing moves it; Spec §8.7.2.3: set_cursor and
     * get_cursor work on the upper window, whichever is selected, and
     * a row below the split enlarges the window (Spec §8 Remarks). */
    story_init(&s, 5);
    EMIT(&s, 0xea, 0x7f, 0x02);                                 /* split_window 2 */
    EMIT(&s, 0xeb, 0x7f, 0x01);                                 /* set_window 1 */
    EMIT(&s, 0xe5, 0x7f, 'A');                                  /* print_char 'A' */
    EMIT(&s, 0xe5, 0x7f, 'B');                                  /* print_char 'B' */
    EMIT(&s, 0xf0, 0x3f, ARRAY >> 8, ARRAY & 0xff);             /* get_cursor ARRAY */
    EMIT(&s, 0xbb);                                             /* new_line */
    EMIT(&s, 0xf0, 0x3f, (ARRAY + 4) >> 8, (ARRAY + 4) & 0xff); /* get_cursor ARRAY+4 */
    EMIT(&s, 0xef, 0x5f, 0x01, 0x05);                           /* set_cursor 1 5 */
    EMIT(&s, 0xef, 0x5f, 0x04, 0x02);                           /* set_cursor 4 2 */
    EMIT(&s, 0xeb, 0x7f, 0x00);                                 /* set_window 0 */
    EMIT(&s, 0xef, 0x5f, 0x09, 0x09);                           /* set_cursor 9 9 (ignored) */
    EMIT(&s, 0xf0, 0x3f, (ARRAY + 8) >> 8, (ARRAY + 8) & 0xff); /* get_cursor ARRAY+8 */
    EMIT(&s, 0xe5, 0x7f, 'C');                                  /* print_char 'C' */
    EMIT(&s, 0xee, 0x7f, 0x01);                                 /* erase_line 1 */
    EMIT(&s, 0xee, 0x7f, 0x02);                                 /* erase_line 2 (nothing) */
    EMIT(&s, 0xed, 0x7f, 0x01);                                 /* erase_window 1 */
    EMIT(&s, 0xf0, 0x3f, (ARRAY + 12) >> 8, (ARRAY + 12) & 0xff); /* get_cursor ARRAY+12 */
    EMIT(&s, 0xed, 0x3f, 0xff, 0xff);                           /* erase_window -1 */
    EMIT(&s, 0xba);
    run(&s, &out, &m, &mem);
    CHECK_EQ_STR(out.text, "AB\nC");
    CHECK_EQ_INT(zm_read_word(&mem, ARRAY), 1);
    CHECK_EQ_INT(zm_read_word(&mem, ARRAY + 2), 3);
    CHECK_EQ_INT(zm_read_word(&mem, ARRAY + 4), 2);
    CHECK_EQ_INT(zm_read_word(&mem, ARRAY + 6), 1);
    CHECK_EQ_INT(zm_read_word(&mem, ARRAY + 8), 4);
    CHECK_EQ_INT(zm_read_word(&mem, ARRAY + 10), 2);
    CHECK_EQ_INT(zm_read_word(&mem, ARRAY + 12), 1);
    CHECK_EQ_INT(zm_read_word(&mem, ARRAY + 14), 1);
    CHECK_EQ_STR(screen_log, "split 2;window 1;cursor 1,5;split 4;cursor 4,2;window 0;erase_line;erase 1;erase -1");
    CHECK_EQ_INT(m.screen.upper_height, 0);
    CHECK_EQ_INT(m.screen.window, ZM_WINDOW_LOWER);
    zm_machine_free(&m);
    zm_memory_free(&mem);

    /* Spec §8.7.2.1: the upper window cannot be taller than the screen;
     * Spec §8.7.2.1.1: a split that leaves the upper cursor outside
     * moves it to the top left. */
    story_init(&s, 5);
    EMIT(&s, 0xea, 0x7f, 0x30);                                 /* split_window 48 */
    EMIT(&s, 0xeb, 0x7f, 0x01);                                 /* set_window 1 */
    EMIT(&s, 0xef, 0x5f, 0x0a, 0x01);                           /* set_cursor 10 1 */
    EMIT(&s, 0xea, 0x7f, 0x03);                                 /* split_window 3 */
    EMIT(&s, 0xf0, 0x3f, ARRAY >> 8, ARRAY & 0xff);             /* get_cursor ARRAY */
    EMIT(&s, 0xba);
    run(&s, &out, &m, &mem);
    CHECK_EQ_INT(zm_read_word(&mem, ARRAY), 1);
    CHECK_EQ_INT(zm_read_word(&mem, ARRAY + 2), 1);
    CHECK_EQ_STR(screen_log, "split 24;window 1;cursor 10,1;split 3;cursor 1,1");
    zm_machine_free(&m);
    zm_memory_free(&mem);

    /* Spec §8.6.1.1.2: in Version 3 a split clears the upper window. */
    story_init(&s, 3);
    EMIT(&s, 0xea, 0x7f, 0x01);                                 /* split_window 1 */
    EMIT(&s, 0xba);
    run(&s, &out, &m, &mem);
    CHECK_EQ_STR(screen_log, "split 1;erase 1");
    zm_machine_free(&m);
    zm_memory_free(&mem);
    screen_hooks(0);

    /* A front end with no upper window: what is printed there is lost,
     * and the lower window is untouched. */
    story_init(&s, 5);
    EMIT(&s, 0xea, 0x7f, 0x01);                                 /* split_window 1 */
    EMIT(&s, 0xeb, 0x7f, 0x01);                                 /* set_window 1 */
    EMIT(&s, 0xe5, 0x7f, 'Q');                                  /* print_char 'Q' */
    EMIT(&s, 0xeb, 0x7f, 0x00);                                 /* set_window 0 */
    EMIT(&s, 0xe5, 0x7f, 'R');                                  /* print_char 'R' */
    EMIT(&s, 0xba);
    run(&s, &out, &m, &mem);
    CHECK_EQ_STR(out.text, "R");
    zm_machine_free(&m);
    zm_memory_free(&mem);
}

static void test_styles_colours_fonts_and_sound(void)
{
    story s;
    output out;
    zm_machine m;
    zm_memory mem;

    screen_hooks(1);

    /* Spec §15 set_text_style: styles add, Roman clears. Spec §15
     * set_colour and §8.3.1: 0 keeps a colour, 1 restores the default.
     * Spec §15 set_true_colour: the nearest standard colour. */
    story_init(&s, 5);
    EMIT(&s, 0xf1, 0x7f, 0x02);                                 /* set_text_style 2 */
    EMIT(&s, 0xf1, 0x7f, 0x04);                                 /* set_text_style 4 */
    EMIT(&s, 0xf1, 0x7f, 0x00);                                 /* set_text_style 0 */
    EMIT(&s, 0x1b, 0x03, 0x04);                                 /* set_colour 3 4 */
    EMIT(&s, 0x1b, 0x00, 0x01);                                 /* set_colour 0 1 */
    EMIT(&s, 0x1b, 0x0a, 0x02);                                 /* set_colour 10 2 */
    EMIT(&s, 0xbe, 0x0d, 0x0f, 0xff, 0xff, 0x03, 0xbd);         /* set_true_colour -1 $03bd */
    EMIT(&s, 0xf2, 0x7f, 0x00);                                 /* buffer_mode 0 */
    EMIT(&s, 0xf5, 0x7f, 0x01);                                 /* sound_effect 1 */
    EMIT(&s, 0xf5, 0xff);                                       /* sound_effect */
    EMIT(&s, 0xf5, 0x13, 0x00, 0x03, 0x02, 0x02, 0x05);         /* sound_effect 3 2 $0205 */
    EMIT(&s, 0xba);
    run(&s, &out, &m, &mem);
    CHECK_EQ_STR(screen_log, "style 2;style 6;style 0;colour 3/4;colour 3/2;colour 10/2;colour 9/5;"
                             "buffer off;sound 1 2 8 1;sound 1 2 8 1;sound 3 2 5 2");
    CHECK_EQ_INT(m.screen.style, 0);
    CHECK_EQ_INT(m.screen.foreground, ZM_COLOUR_WHITE);
    CHECK_EQ_INT(m.screen.background, ZM_COLOUR_YELLOW);
    CHECK_EQ_INT(m.screen.buffering, 0);
    zm_machine_free(&m);
    zm_memory_free(&mem);
    screen_hooks(0);

    /* Spec §15 set_font: without fonts of its own the front end has font
     * 1 only; 0 asks which is current. */
    story_init(&s, 5);
    EMIT(&s, 0xbe, 0x04, 0x7f, 0x04, G0);                       /* set_font 4 -> G0 */
    EMIT(&s, 0xbe, 0x04, 0x7f, 0x01, G1);                       /* set_font 1 -> G1 */
    EMIT(&s, 0xbe, 0x04, 0x7f, 0x00, G2);                       /* set_font 0 -> G2 */
    EMIT(&s, 0xf1, 0x7f, 0x08);                                 /* set_text_style 8 (no hook) */
    EMIT(&s, 0xf5, 0x7f, 0x01);                                 /* sound_effect 1 (no hook) */
    EMIT(&s, 0xba);
    run(&s, &out, &m, &mem);
    CHECK_EQ_INT(zm_read_word(&mem, GLOBALS), 0);
    CHECK_EQ_INT(zm_read_word(&mem, GLOBALS + 2), 1);
    CHECK_EQ_INT(zm_read_word(&mem, GLOBALS + 4), 1);
    CHECK_EQ_INT(m.screen.style, ZM_STYLE_FIXED_PITCH);
    zm_machine_free(&m);
    zm_memory_free(&mem);
}

static void test_table_opcodes(void)
{
    story s;
    output out;
    zm_machine m;
    zm_memory mem;

    /* Spec §15 scan_table: words by default, or bytes with a form. */
    story_init(&s, 5);
    put_word(s.bytes, TABLE2, 0x1111);
    put_word(s.bytes, TABLE2 + 2, 0x2222);
    put_word(s.bytes, TABLE2 + 4, 0x3333);
    EMIT(&s, 0xf7, 0x07, 0x22, 0x22, TABLE2 >> 8, TABLE2 & 0xff, 0x03, G0, 0xc5); /* scan_table $2222 TABLE2 3 -> G0 ?+5 */
    EMIT(&s, 0xe5, 0x7f, 'n');                                  /* print_char 'n' (skipped) */
    EMIT(&s, 0xf7, 0x07, 0x44, 0x44, TABLE2 >> 8, TABLE2 & 0xff, 0x03, G1, 0xc5); /* scan_table $4444 ... -> G1 ?+5 */
    EMIT(&s, 0xe5, 0x7f, 'm');                                  /* print_char 'm' (printed) */
    EMIT(&s, 0xf7, 0x45, 0x33, TABLE2 >> 8, TABLE2 & 0xff, 0x06, 0x01, G2, 0xc5); /* scan_table $33 TABLE2 6 1 -> G2 ?+5 */
    EMIT(&s, 0xe5, 0x7f, 'o');                                  /* print_char 'o' (skipped) */
    EMIT(&s, 0xba);
    run(&s, &out, &m, &mem);
    CHECK_EQ_STR(out.text, "m");
    CHECK_EQ_INT(zm_read_word(&mem, GLOBALS), TABLE2 + 2);
    CHECK_EQ_INT(zm_read_word(&mem, GLOBALS + 2), 0);
    CHECK_EQ_INT(zm_read_word(&mem, GLOBALS + 4), TABLE2 + 4);
    zm_machine_free(&m);
    zm_memory_free(&mem);

    /* Spec §15 copy_table: overlapping copies forward safely with a
     * positive size and corruptingly with a negative one; a zero
     * destination clears. */
    story_init(&s, 5);
    EMIT(&s, 0x01, 0x02, 0x03, 0x04, 0x05);                      /* data, not code: see below */
    at(&s, TABLE2);
    EMIT(&s, 0x01, 0x02, 0x03, 0x04, 0x05);
    at(&s, CODE);
    EMIT(&s, 0xfd, 0x07, TABLE2 >> 8, TABLE2 & 0xff, (TABLE2 + 1) >> 8, (TABLE2 + 1) & 0xff, 0x04); /* copy_table T T+1 4 */
    EMIT(&s, 0xfd, 0x07, (TABLE2 + 8) >> 8, (TABLE2 + 8) & 0xff, 0x00, 0x00, 0x02); /* copy_table T+8 0 2 */
    EMIT(&s, 0xba);
    put_word(s.bytes, TABLE2 + 8, 0xabcd);
    run(&s, &out, &m, &mem);
    CHECK_EQ_INT(zm_read_byte(&mem, TABLE2), 1);
    CHECK_EQ_INT(zm_read_byte(&mem, TABLE2 + 1), 1);
    CHECK_EQ_INT(zm_read_byte(&mem, TABLE2 + 2), 2);
    CHECK_EQ_INT(zm_read_byte(&mem, TABLE2 + 3), 3);
    CHECK_EQ_INT(zm_read_byte(&mem, TABLE2 + 4), 4);
    CHECK_EQ_INT(zm_read_word(&mem, TABLE2 + 8), 0);
    zm_machine_free(&m);
    zm_memory_free(&mem);

    story_init(&s, 5);
    at(&s, TABLE2);
    EMIT(&s, 0x01, 0x02, 0x03, 0x04, 0x05);
    at(&s, CODE);
    EMIT(&s, 0xfd, 0x03, TABLE2 >> 8, TABLE2 & 0xff, (TABLE2 + 1) >> 8, (TABLE2 + 1) & 0xff, 0xff, 0xfc); /* copy_table T T+1 -4 */
    EMIT(&s, 0xba);
    run(&s, &out, &m, &mem);
    for (unsigned i = 0; i < 5; i++) {
        CHECK_EQ_INT(zm_read_byte(&mem, TABLE2 + i), 1);
    }
    zm_machine_free(&m);
    zm_memory_free(&mem);

    /* Spec §15 print_table: rows of width characters, skip apart. */
    story_init(&s, 5);
    at(&s, TABLE2);
    EMIT(&s, 'A', 'B', 'C', 'D', 'E', 'F');
    at(&s, CODE);
    EMIT(&s, 0xfe, 0x15, TABLE2 >> 8, TABLE2 & 0xff, 0x02, 0x02, 0x01); /* print_table TABLE2 2 2 1 */
    EMIT(&s, 0xba);
    run(&s, &out, &m, &mem);
    CHECK_EQ_STR(out.text, "AB\nDE");
    zm_machine_free(&m);
    zm_memory_free(&mem);
}

static void test_unicode_printing(void)
{
    story s;
    output out;
    zm_machine m;
    zm_memory mem;

    /* Spec §15 print_unicode, §7.5.3: a character with a ZSCII code is
     * printed as that code (e acute is ZSCII 170 in the default table,
     * Spec §3.8.5.3); one without becomes a question mark in memory and
     * goes to the front end's Unicode printer on the screen, or is a
     * question mark there too without one. Spec §15 check_unicode. */
    story_init(&s, 5);
    EMIT(&s, 0xf3, 0x4f, 0x03, STREAM1 >> 8, STREAM1 & 0xff);  /* output_stream 3 STREAM1 */
    EMIT(&s, 0xbe, 0x0b, 0x3f, 0x00, 0xe9);                     /* print_unicode $e9 */
    EMIT(&s, 0xbe, 0x0b, 0x3f, 0x4e, 0x2d);                     /* print_unicode $4e2d */
    EMIT(&s, 0xf3, 0x3f, 0xff, 0xfd);                           /* output_stream -3 */
    EMIT(&s, 0xbe, 0x0b, 0x3f, 0x4e, 0x2d);                     /* print_unicode $4e2d */
    EMIT(&s, 0xbe, 0x0b, 0x3f, 0x00, 'A');                      /* print_unicode 'A' */
    EMIT(&s, 0xbe, 0x0c, 0x3f, 0x00, 0xe9, G0);                 /* check_unicode $e9 -> G0 */
    EMIT(&s, 0xbe, 0x0c, 0x3f, 0x4e, 0x2d, G1);                 /* check_unicode $4e2d -> G1 */
    EMIT(&s, 0xba);
    run(&s, &out, &m, &mem);
    CHECK_EQ_STR(out.text, "?A");
    CHECK_EQ_INT(zm_read_word(&mem, STREAM1), 2);
    CHECK_EQ_INT(zm_read_byte(&mem, STREAM1 + 2), 170);
    CHECK_EQ_INT(zm_read_byte(&mem, STREAM1 + 3), '?');
    CHECK_EQ_INT(zm_read_word(&mem, GLOBALS), 3);
    CHECK_EQ_INT(zm_read_word(&mem, GLOBALS + 2), 0);
    zm_machine_free(&m);
    zm_memory_free(&mem);

    screen_hooks(1);
    story_init(&s, 5);
    EMIT(&s, 0xbe, 0x0b, 0x3f, 0x4e, 0x2d);                     /* print_unicode $4e2d */
    EMIT(&s, 0xbe, 0x0c, 0x3f, 0x4e, 0x2d, G1);                 /* check_unicode $4e2d -> G1 */
    EMIT(&s, 0xba);
    run(&s, &out, &m, &mem);
    CHECK_EQ_STR(out.text, "");
    CHECK_EQ_STR(screen_log, "unicode 4e2d");
    CHECK_EQ_INT(zm_read_word(&mem, GLOBALS + 2), 1);
    zm_machine_free(&m);
    zm_memory_free(&mem);
    screen_hooks(0);
}

static void test_screen_faults(void)
{
    story s;
    output out;

    /* Spec §7.1.2.1.1: the seventeenth nested stream 3 halts. */
    story_init(&s, 5);
    for (unsigned i = 0; i < 17; i++) {
        EMIT(&s, 0xf3, 0x4f, 0x03, STREAM1 >> 8, STREAM1 & 0xff); /* output_stream 3 STREAM1 */
    }
    expect_fault(&s, "17 times", &out);

    story_init(&s, 5);
    EMIT(&s, 0xf3, 0x7f, 0x05);                                 /* output_stream 5 */
    expect_fault(&s, "no such stream", &out);

    story_init(&s, 5);
    EMIT(&s, 0xf3, 0x7f, 0x03);                                 /* output_stream 3 (no table) */
    expect_fault(&s, "no table", &out);

    /* Spec §8.7.2: two windows before Version 6. */
    story_init(&s, 5);
    EMIT(&s, 0xeb, 0x7f, 0x02);                                 /* set_window 2 */
    expect_fault(&s, "only windows 0 and 1", &out);

    story_init(&s, 5);
    EMIT(&s, 0xed, 0x3f, 0xff, 0xfd);                           /* erase_window -3 */
    expect_fault(&s, "erase_window -3", &out);

    /* Spec §8.3.1, §8.3.7: Version 6 colour requests. */
    story_init(&s, 5);
    EMIT(&s, 0x1b, 0x0f, 0x02);                                 /* set_colour 15 2 */
    expect_fault(&s, "15 is not a colour before Version 6", &out);

    story_init(&s, 5);
    EMIT(&s, 0xbe, 0x0d, 0x0f, 0xff, 0xfd, 0x00, 0x00);         /* set_true_colour -3 0 */
    expect_fault(&s, "-3 is not a colour before Version 6", &out);

    story_init(&s, 5);
    EMIT(&s, 0xf1, 0x7f, 0x10);                                 /* set_text_style 16 */
    expect_fault(&s, "styles are 0 to 15", &out);
}

/*
 * Version 6 (Spec §8.8).
 */

/* A Version 6 story with no locals in its main routine, assembling at
 * R1 (Spec §5.4). */
static void story_init_v6(story *s)
{
    story_init(s, 6);
    put_word(s->bytes, ZM_HEADER_INITIAL_PC, (uint16_t)(R1 / 4));
    at(s, R1);
    EMIT(s, 0x00);
}

/* print_char for each character of a string. */
static void emit_text(story *s, const char *text)
{
    for (; *text != '\0'; text++) {
        EMIT(s, 0xe5, 0x7f, (uint8_t)*text);
    }
}

static void test_v6_text_is_laid_out_in_windows(void)
{
    story s;
    output out;
    zm_machine m;
    zm_memory mem;

    canvas_hooks(1);

    /* Spec §8.8.3.3: window 0 is the whole screen, wrapping and
     * scrolling, with buffered printing; its cursor is observable
     * (Spec §8.8.3.2, properties 4 and 5) once the buffer is flushed
     * (Spec §8.8.3.2.7). Spec §8.8.3.1.2.2: a word that would cross the
     * right margin goes on the next line. */
    story_init_v6(&s);
    emit_text(&s, "The quick brown fox jumps over the lazy dog");
    EMIT(&s, 0xbe, 0x13, 0x1f, 0xff, 0xfd, 0x04, G0);           /* get_wind_prop -3 4 -> G0 */
    EMIT(&s, 0xbe, 0x13, 0x1f, 0xff, 0xfd, 0x05, G1);           /* get_wind_prop -3 5 -> G1 */
    EMIT(&s, 0xbb);                                             /* new_line */
    EMIT(&s, 0xbe, 0x13, 0x1f, 0xff, 0xfd, 0x0f, G2);           /* get_wind_prop -3 15 -> G2 */
    EMIT(&s, 0xba);
    run(&s, &out, &m, &mem);
    CHECK_EQ_STR(canvas_row(1), "The quick brown fox jumps over the lazy");
    CHECK_EQ_STR(canvas_row(2), "dog");
    CHECK_EQ_INT(zm_read_word(&mem, GLOBALS), 2);
    CHECK_EQ_INT(zm_read_word(&mem, GLOBALS + 2), 4);
    CHECK_EQ_INT(zm_read_word(&mem, GLOBALS + 4), 2);
    CHECK_EQ_INT(m.screen.windows[0].cursor_y, 3);
    CHECK_EQ_INT(m.screen.windows[0].cursor_x, 1);
    CHECK_EQ_STR(out.text, "");
    zm_machine_free(&m);
    zm_memory_free(&mem);

    /* Spec §8.8.3.1.1: without wrapping, text is clipped at the right
     * margin and the cursor stays there; Spec §15 window_style turns
     * wrapping on; Spec §8.8.3.4: a window is moved and sized in units;
     * Spec §8.8.3.2.1: margins narrow the text. */
    story_init_v6(&s);
    EMIT(&s, 0xbe, 0x10, 0x57, 0x01, 0x02, 0x14);               /* move_window 1 2 20 */
    EMIT(&s, 0xbe, 0x11, 0x57, 0x01, 0x03, 0x0a);               /* window_size 1 3 10 */
    EMIT(&s, 0xeb, 0x7f, 0x01);                                 /* set_window 1 */
    emit_text(&s, "abc def ghi");
    EMIT(&s, 0xbe, 0x13, 0x5f, 0x01, 0x05, G0);                 /* get_wind_prop 1 5 -> G0 */
    EMIT(&s, 0xbe, 0x12, 0x57, 0x01, 0x01, 0x01);               /* window_style 1 1 1 (wrapping on) */
    EMIT(&s, 0xed, 0x7f, 0x01);                                 /* erase_window 1 */
    emit_text(&s, "abc def ghi");
    EMIT(&s, 0xf0, 0x3f, ARRAY >> 8, ARRAY & 0xff);             /* get_cursor ARRAY */
    EMIT(&s, 0xbe, 0x08, 0x57, 0x02, 0x01, 0x01);               /* set_margins 2 1 1 */
    EMIT(&s, 0xbb);                                             /* new_line */
    emit_text(&s, "mnopqrs");
    EMIT(&s, 0xf0, 0x3f, (ARRAY + 4) >> 8, (ARRAY + 4) & 0xff); /* get_cursor ARRAY+4 */
    EMIT(&s, 0xba);
    run(&s, &out, &m, &mem);
    CHECK_EQ_INT(zm_read_word(&mem, GLOBALS), 10);
    CHECK_EQ_STR(canvas_row(2), "                   abc def");
    CHECK_EQ_STR(canvas_row(3), "                   ghi");
    CHECK_EQ_INT(zm_read_word(&mem, ARRAY), 2);
    CHECK_EQ_INT(zm_read_word(&mem, ARRAY + 2), 4);
    CHECK_EQ_STR(canvas_row(4), "                     mnopqrs");
    CHECK_EQ_INT(zm_read_word(&mem, ARRAY + 4), 3);
    CHECK_EQ_INT(zm_read_word(&mem, ARRAY + 6), 10);
    CHECK_EQ_INT(m.screen.windows[1].attributes, ZM_WINDOW_WRAPPING | ZM_WINDOW_BUFFERING);
    CHECK_EQ_STR(screen_log, "erase 2,20 3x10 bg2");
    zm_machine_free(&m);
    zm_memory_free(&mem);

    /* Spec §8.8.3.1: a scrolling window scrolls when a new-line would
     * leave no room, by the height of a line, in its own area; one that
     * does not scroll starts again at the top. Spec §8.8.3.2.6: the
     * line count counts the new-lines unless it is -999. */
    story_init_v6(&s);
    EMIT(&s, 0xbe, 0x11, 0x57, 0x00, 0x03, 0x08);               /* window_size 0 3 8 */
    emit_text(&s, "one");
    EMIT(&s, 0xbb);                                             /* new_line */
    emit_text(&s, "two");
    EMIT(&s, 0xbb);
    emit_text(&s, "three");
    EMIT(&s, 0xbb);
    emit_text(&s, "four");
    EMIT(&s, 0xbe, 0x13, 0x5f, 0x00, 0x0f, G0);                 /* get_wind_prop 0 15 -> G0 */
    EMIT(&s, 0xbe, 0x19, 0x53, 0x00, 0x0f, 0xfc, 0x19);         /* put_wind_prop 0 15 -999 */
    EMIT(&s, 0xbb);
    EMIT(&s, 0xbe, 0x13, 0x5f, 0x00, 0x0f, G1);                 /* get_wind_prop 0 15 -> G1 */
    EMIT(&s, 0xbe, 0x12, 0x57, 0x00, 0x02, 0x02);               /* window_style 0 2 2 (scrolling off) */
    EMIT(&s, 0xbb);
    emit_text(&s, "topmost");
    EMIT(&s, 0xbe, 0x13, 0x5f, 0x00, 0x04, G2);                 /* get_wind_prop 0 4 -> G2 */
    EMIT(&s, 0xba);
    run(&s, &out, &m, &mem);
    CHECK_EQ_STR(canvas_row(1), "topmost");
    CHECK_EQ_STR(canvas_row(2), "four");
    CHECK_EQ_STR(canvas_row(3), "");
    CHECK_EQ_INT(zm_read_word(&mem, GLOBALS), 3);
    CHECK_EQ_INT((int16_t)zm_read_word(&mem, GLOBALS + 2), -999);
    CHECK_EQ_INT(zm_read_word(&mem, GLOBALS + 4), 1);
    CHECK_EQ_STR(screen_log, "scroll 1,1 3x8 by 1;scroll 1,1 3x8 by 1");
    zm_machine_free(&m);
    zm_memory_free(&mem);

    /* Spec §15 erase_line: to the right margin, or so many units less
     * one; Spec §15 erase_window -1 and -2; Spec §8.8.4.1: split_window
     * tiles windows 0 and 1. */
    story_init_v6(&s);
    emit_text(&s, "abcdefghij");
    EMIT(&s, 0xef, 0x5f, 0x01, 0x03);                           /* set_cursor 1 3 */
    EMIT(&s, 0xee, 0x7f, 0x04);                                 /* erase_line 4 */
    EMIT(&s, 0xef, 0x5f, 0x01, 0x08);                           /* set_cursor 1 8 */
    EMIT(&s, 0xee, 0x7f, 0x01);                                 /* erase_line 1 */
    EMIT(&s, 0xea, 0x7f, 0x02);                                 /* split_window 2 */
    EMIT(&s, 0xbe, 0x13, 0x5f, 0x00, 0x00, G0);                 /* get_wind_prop 0 0 -> G0 */
    EMIT(&s, 0xbe, 0x13, 0x5f, 0x00, 0x02, G1);                 /* get_wind_prop 0 2 -> G1 */
    EMIT(&s, 0xbe, 0x13, 0x5f, 0x01, 0x02, G2);                 /* get_wind_prop 1 2 -> G2 */
    EMIT(&s, 0xed, 0x3f, 0xff, 0xfe);                           /* erase_window -2 */
    EMIT(&s, 0xed, 0x3f, 0xff, 0xff);                           /* erase_window -1 */
    EMIT(&s, 0xba);
    run(&s, &out, &m, &mem);
    CHECK_EQ_STR(screen_log, "erase 1,3 1x3 bg2;erase 1,8 1x33 bg2;erase 1,1 8x40 bg2;erase 1,1 8x40 bg2");
    CHECK_EQ_INT(zm_read_word(&mem, GLOBALS), 3);
    CHECK_EQ_INT(zm_read_word(&mem, GLOBALS + 2), 6);
    CHECK_EQ_INT(zm_read_word(&mem, GLOBALS + 4), 2);
    CHECK_EQ_INT(m.screen.windows[0].y, 1);
    CHECK_EQ_INT(m.screen.windows[0].height, 8);
    CHECK_EQ_INT(m.screen.windows[1].height, 0);
    CHECK_EQ_INT(m.screen.window, 0);
    zm_machine_free(&m);
    zm_memory_free(&mem);
    canvas_hooks(0);
}

static void test_v6_window_properties_and_appearance(void)
{
    story s;
    output out;
    zm_machine m;
    zm_memory mem;

    canvas_hooks(1);

    /* Spec §8.3: each window has its own colours, which set_colour may
     * name a window for; Spec §8.8.3.2.4: property 11 packs them;
     * Spec §8.8.3.2.8: properties 16 and 17 give the true colours; Spec
     * §15 set_font: a window may be given a font the front end has,
     * and refuses one it has not; Spec §8.8.3.2.3: the style property
     * is the combination in use; the pen carries all of it. */
    story_init_v6(&s);
    EMIT(&s, 0xdb, 0x57, 0x03, 0x04, 0x01);                     /* set_colour 3 4 1 */
    EMIT(&s, 0x1b, 0x06, 0x00);                                 /* set_colour 6 0 (current window) */
    EMIT(&s, 0xbe, 0x13, 0x5f, 0x01, 0x0b, G0);                 /* get_wind_prop 1 11 -> G0 */
    EMIT(&s, 0xbe, 0x13, 0x5f, 0x00, 0x0b, G1);                 /* get_wind_prop 0 11 -> G1 */
    EMIT(&s, 0xbe, 0x13, 0x5f, 0x00, 0x10, G2);                 /* get_wind_prop 0 16 -> G2 */
    EMIT(&s, 0xbe, 0x04, 0x5f, 0x04, 0x01, 0x13);               /* set_font 4 1 -> G3 */
    EMIT(&s, 0xbe, 0x04, 0x5f, 0x03, 0x00, 0x14);               /* set_font 3 0 -> G4 */
    EMIT(&s, 0xbe, 0x04, 0x7f, 0x00, 0x15);                     /* set_font 0 -> G5 */
    EMIT(&s, 0xf1, 0x7f, 0x02);                                 /* set_text_style 2 */
    EMIT(&s, 0xf1, 0x7f, 0x01);                                 /* set_text_style 1 */
    EMIT(&s, 0xbe, 0x13, 0x1f, 0xff, 0xfd, 0x0a, 0x16);         /* get_wind_prop -3 10 -> G6 */
    EMIT(&s, 0xbe, 0x13, 0x5f, 0x01, 0x0c, 0x17);               /* get_wind_prop 1 12 -> G7 */
    EMIT(&s, 0xbe, 0x13, 0x5f, 0x01, 0x0d, 0x18);               /* get_wind_prop 1 13 -> G8 */
    emit_text(&s, "x");
    EMIT(&s, 0xbe, 0x0d, 0x4f, 0x01, 0x03, 0xbd);               /* set_true_colour 1 $03bd (yellow) */
    EMIT(&s, 0xbe, 0x13, 0x5f, 0x00, 0x11, 0x19);               /* get_wind_prop 0 17 -> G9 */
    EMIT(&s, 0xba);
    run(&s, &out, &m, &mem);
    CHECK_EQ_INT(zm_read_word(&mem, GLOBALS), 0x0403);
    CHECK_EQ_INT(zm_read_word(&mem, GLOBALS + 2), 0x0206);
    CHECK_EQ_INT(zm_read_word(&mem, GLOBALS + 4), 0x59a0);
    CHECK_EQ_INT(zm_read_word(&mem, GLOBALS + 6), 1);
    CHECK_EQ_INT(zm_read_word(&mem, GLOBALS + 8), 0);
    CHECK_EQ_INT(zm_read_word(&mem, GLOBALS + 10), 1);
    CHECK_EQ_INT(zm_read_word(&mem, GLOBALS + 12), 3);
    CHECK_EQ_INT(zm_read_word(&mem, GLOBALS + 14), 4);
    CHECK_EQ_INT(zm_read_word(&mem, GLOBALS + 16), 0x0101);
    CHECK_EQ_INT(zm_read_word(&mem, GLOBALS + 18), 0x03bd);
    CHECK_EQ_INT(last_pen.style, 3);
    CHECK_EQ_INT(last_pen.foreground, ZM_COLOUR_BLUE);
    CHECK_EQ_INT(last_pen.background, ZM_COLOUR_BLACK);
    CHECK_EQ_INT(last_pen.font, 1);
    CHECK_EQ_INT(m.screen.windows[0].background, ZM_COLOUR_YELLOW);
    zm_machine_free(&m);
    zm_memory_free(&mem);

    /* Spec §15 put_wind_prop and the opcodes with nothing behind them
     * yet: scroll_window reaches the front end; mouse_window is noted;
     * read_mouse reports nothing; make_menu fails; buffer_screen is
     * always 0; picture_data finds no pictures without a resource file;
     * picture_table is nothing; set_cursor -1 and -2 hide and show the
     * cursor. Spec §15 move_window and window_size put a cursor left
     * outside its window at the top left. */
    story_init_v6(&s);
    EMIT(&s, 0xbe, 0x19, 0x57, 0x02, 0x09, 0x07);               /* put_wind_prop 2 9 7 */
    EMIT(&s, 0xbe, 0x13, 0x5f, 0x02, 0x09, G0);                 /* get_wind_prop 2 9 -> G0 */
    EMIT(&s, 0xbe, 0x14, 0x4f, 0x00, 0xff, 0xfe);               /* scroll_window 0 -2 */
    EMIT(&s, 0xbe, 0x17, 0x7f, 0x03);                           /* mouse_window 3 */
    EMIT(&s, 0xbe, 0x16, 0x3f, ARRAY >> 8, ARRAY & 0xff);       /* read_mouse ARRAY */
    EMIT(&s, 0xbe, 0x1b, 0x5f, 0x03, 0x00, 0x40 | 5);           /* make_menu 3 0 ?~+3 */
    EMIT(&s, 0xe5, 0x7f, 'M');                                  /* print_char 'M' (skipped) */
    EMIT(&s, 0xbe, 0x1d, 0x7f, 0x01, G1);                       /* buffer_screen 1 -> G1 */
    EMIT(&s, 0xbe, 0x1d, 0x3f, 0xff, 0xff, G2);                 /* buffer_screen -1 -> G2 */
    EMIT(&s, 0xbe, 0x06, 0x4f, 0x00, TABLE >> 8, TABLE & 0xff, 0x40 | 5); /* picture_data 0 TABLE ?~+3 */
    EMIT(&s, 0xe5, 0x7f, 'P');                                  /* print_char 'P' (skipped) */
    EMIT(&s, 0xbe, 0x1c, 0x3f, TABLE >> 8, TABLE & 0xff);       /* picture_table TABLE */
    EMIT(&s, 0xef, 0x3f, 0xff, 0xff);                           /* set_cursor -1 */
    EMIT(&s, 0xef, 0x5f, 0x02, 0x05);                           /* set_cursor 2 5 */
    EMIT(&s, 0xbe, 0x11, 0x57, 0x00, 0x01, 0x03);               /* window_size 0 1 3 */
    EMIT(&s, 0xf0, 0x3f, (ARRAY + 8) >> 8, (ARRAY + 8) & 0xff); /* get_cursor ARRAY+8 */
    EMIT(&s, 0xba);
    put_word(s.bytes, TABLE, 0x1234);
    run(&s, &out, &m, &mem);
    CHECK_EQ_INT(zm_read_word(&mem, GLOBALS), 7);
    CHECK_EQ_INT(m.screen.mouse_window, 3);
    CHECK_EQ_INT(zm_read_word(&mem, ARRAY), 0);
    CHECK_EQ_INT(zm_read_word(&mem, ARRAY + 6), 0);
    CHECK_EQ_INT(zm_read_word(&mem, GLOBALS + 2), 0);
    CHECK_EQ_INT(zm_read_word(&mem, GLOBALS + 4), 0);
    CHECK_EQ_INT(zm_read_word(&mem, TABLE), 0);
    CHECK_EQ_INT(zm_read_word(&mem, TABLE + 2), 0);
    CHECK_EQ_INT(m.screen.cursor_shown, 0);
    CHECK_EQ_INT(zm_read_word(&mem, ARRAY + 8), 1);
    CHECK_EQ_INT(zm_read_word(&mem, ARRAY + 10), 1);
    CHECK_EQ_STR(out.text, "");
    CHECK_EQ_STR(canvas_row(1), "");
    CHECK_EQ_STR(screen_log, "scroll 1,1 8x40 by -2");
    zm_machine_free(&m);
    zm_memory_free(&mem);

    /* Spec §11.1 for Version 6: the IBM PC's interpreter number, the
     * screen in units, the font bytes the other way round, colours on
     * and pictures off; bit 6 belongs to nobody and is left alone. */
    story_init_v6(&s);
    s.bytes[ZM_HEADER_FLAGS1] = 0xff;
    put_word(s.bytes, ZM_HEADER_FLAGS2, 0x01f8);
    EMIT(&s, 0xba);
    run(&s, &out, &m, &mem);
    CHECK_EQ_INT(zm_read_byte(&mem, ZM_HEADER_FLAGS1), ZM_FLAGS1_COLOURS | 0x40);
    CHECK_EQ_INT(zm_read_word(&mem, ZM_HEADER_FLAGS2), ZM_FLAGS2_UNDO | ZM_FLAGS2_COLOURS);
    CHECK_EQ_INT(zm_read_byte(&mem, ZM_HEADER_INTERPRETER_NUMBER), 6);
    CHECK_EQ_INT(zm_read_byte(&mem, ZM_HEADER_SCREEN_HEIGHT), CANVAS_HEIGHT);
    CHECK_EQ_INT(zm_read_byte(&mem, ZM_HEADER_SCREEN_WIDTH), CANVAS_WIDTH);
    CHECK_EQ_INT(zm_read_word(&mem, ZM_HEADER_SCREEN_WIDTH_UNITS), CANVAS_WIDTH);
    CHECK_EQ_INT(zm_read_word(&mem, ZM_HEADER_SCREEN_HEIGHT_UNITS), CANVAS_HEIGHT);
    CHECK_EQ_INT(zm_read_byte(&mem, ZM_HEADER_FONT_BYTE_26), 1);
    CHECK_EQ_INT(zm_read_byte(&mem, ZM_HEADER_FONT_BYTE_27), 1);
    zm_machine_free(&m);
    zm_memory_free(&mem);
    canvas_hooks(0);
}

static void test_v6_newline_interrupt(void)
{
    story s;
    output out;
    zm_machine m;
    zm_memory mem;

    canvas_hooks(1);

    /* Spec §8.8.3.2.2: with the countdown set, each new-line counts it
     * down, and at zero the routine named in property 8 runs before
     * printing goes on. R2 moves the left margin in, so the next lines
     * start further right. */
    story_init_v6(&s);
    EMIT(&s, 0xbe, 0x19, 0x53, 0x00, 0x08);                     /* put_wind_prop 0 8 R2 */
    emit_packed(&s, R2);
    EMIT(&s, 0xbe, 0x19, 0x57, 0x00, 0x09, 0x02);               /* put_wind_prop 0 9 2 */
    emit_text(&s, "a");
    EMIT(&s, 0xbb);                                             /* new_line (countdown 1) */
    emit_text(&s, "b");
    EMIT(&s, 0xbb);                                             /* new_line (countdown 0: R2) */
    emit_text(&s, "c");
    EMIT(&s, 0xbb);
    emit_text(&s, "d");
    EMIT(&s, 0xbe, 0x13, 0x5f, 0x00, 0x09, G0);                 /* get_wind_prop 0 9 -> G0 */
    EMIT(&s, 0xba);
    at(&s, R2);
    EMIT(&s, 0x00);                                             /* no locals */
    EMIT(&s, 0xbe, 0x08, 0x57, 0x04, 0x00, 0x00);               /* set_margins 4 0 0 */
    EMIT(&s, 0x95, G1);                                         /* inc G1 */
    EMIT(&s, 0xb0);                                             /* rtrue */
    run(&s, &out, &m, &mem);
    CHECK_EQ_STR(canvas_row(1), "a");
    CHECK_EQ_STR(canvas_row(2), "b");
    CHECK_EQ_STR(canvas_row(3), "    c");
    CHECK_EQ_STR(canvas_row(4), "    d");
    CHECK_EQ_INT(zm_read_word(&mem, GLOBALS), 0);
    CHECK_EQ_INT(zm_read_word(&mem, GLOBALS + 2), 1);
    CHECK_EQ_INT(m.depth, 1);
    zm_machine_free(&m);
    zm_memory_free(&mem);
    canvas_hooks(0);
}

static void test_v6_user_stacks(void)
{
    story s;
    output out;
    zm_machine m;
    zm_memory mem;

    /* Spec §6.6: a user stack's first word counts its spare slots;
     * Spec §15 push_stack fills it from the far end and branches while
     * there is room; pull with a stack operand takes from it; pop_stack
     * discards from it or from the game stack. */
    story_init_v6(&s);
    put_word(s.bytes, TABLE, 2);                                /* two slots */
    EMIT(&s, 0xbe, 0x18, 0x4f, 0x0a, TABLE >> 8, TABLE & 0xff, 0x80 | 0x40 | 5); /* push_stack 10 TABLE ?+3 */
    EMIT(&s, 0xe5, 0x7f, '!');
    EMIT(&s, 0xbe, 0x18, 0x4f, 0x0b, TABLE >> 8, TABLE & 0xff, 0x80 | 0x40 | 5); /* push_stack 11 TABLE ?+3 */
    EMIT(&s, 0xe5, 0x7f, '!');
    EMIT(&s, 0xbe, 0x18, 0x4f, 0x0c, TABLE >> 8, TABLE & 0xff, 0x40 | 5); /* push_stack 12 TABLE ?~+3 (full) */
    EMIT(&s, 0xe5, 0x7f, '!');
    EMIT(&s, 0xe9, 0x3f, TABLE >> 8, TABLE & 0xff, G0);         /* pull TABLE -> G0 */
    EMIT(&s, 0xe8, 0x7f, 0x05);                                 /* push 5 */
    EMIT(&s, 0xe8, 0x7f, 0x06);                                 /* push 6 */
    EMIT(&s, 0xe8, 0x7f, 0x07);                                 /* push 7 */
    EMIT(&s, 0xbe, 0x15, 0x7f, 0x02);                           /* pop_stack 2 */
    EMIT(&s, 0xe9, 0xff, G1);                                   /* pull -> G1 (game stack) */
    EMIT(&s, 0xbe, 0x15, 0x4f, 0x01, TABLE >> 8, TABLE & 0xff); /* pop_stack 1 TABLE */
    EMIT(&s, 0xba);
    run(&s, &out, &m, &mem);
    CHECK_EQ_STR(out.text, "");
    CHECK_EQ_INT(zm_read_word(&mem, GLOBALS), 11);
    CHECK_EQ_INT(zm_read_word(&mem, GLOBALS + 2), 5);
    CHECK_EQ_INT(zm_read_word(&mem, TABLE), 2);
    CHECK_EQ_INT(zm_read_word(&mem, TABLE + 4), 10);
    CHECK_EQ_INT(zm_read_word(&mem, TABLE + 2), 11);
    CHECK_EQ_INT(m.sp, 0);
    zm_machine_free(&m);
    zm_memory_free(&mem);

    /* Spec §6.3.1: the game stack cannot give up more than the routine
     * pushed. */
    story_init_v6(&s);
    EMIT(&s, 0xe8, 0x7f, 0x05);                                 /* push 5 */
    EMIT(&s, 0xbe, 0x15, 0x7f, 0x02);                           /* pop_stack 2 */
    expect_fault(&s, "holds only 1", &out);
}

static void test_v6_formatted_stream_and_print_form(void)
{
    story s;
    output out;
    zm_machine m;
    zm_memory mem;

    canvas_hooks(1);

    /* Spec §15 output_stream: with a width, stream 3 formats the text
     * into lines, each a count and its characters, ended by a zero
     * count (Spec §15 print_form), and the header word at $30 has the
     * width of all of it (Spec §7.1.2.1); print_form prints it back,
     * each line on its own. */
    story_init_v6(&s);
    EMIT(&s, 0xf3, 0x43, 0x03, TABLE >> 8, TABLE & 0xff, 0xff, 0xf4); /* output_stream 3 TABLE -12 */
    emit_text(&s, "one two three four");
    EMIT(&s, 0xf3, 0x3f, 0xff, 0xfd);                           /* output_stream -3 */
    EMIT(&s, 0x0f, 0x00, 0x18, G0);                             /* loadw 0 $18 -> G0 (header $30) */
    EMIT(&s, 0xbe, 0x1a, 0x3f, TABLE >> 8, TABLE & 0xff);       /* print_form TABLE */
    EMIT(&s, 0xba);
    run(&s, &out, &m, &mem);
    CHECK_EQ_INT(zm_read_word(&mem, TABLE), 7);
    CHECK(memcmp(mem.bytes + TABLE + 2, "one two", 7) == 0);
    CHECK_EQ_INT(zm_read_word(&mem, TABLE + 9), 10);
    CHECK(memcmp(mem.bytes + TABLE + 11, "three four", 10) == 0);
    CHECK_EQ_INT(zm_read_word(&mem, TABLE + 21), 0);
    CHECK_EQ_INT(zm_read_word(&mem, GLOBALS), 17);
    CHECK_EQ_STR(canvas_row(1), "one two");
    CHECK_EQ_STR(canvas_row(2), "three four");
    zm_machine_free(&m);
    zm_memory_free(&mem);

    /* A plain stream 3 in Version 6 measures its last line in $30,
     * which is how a game centres text. */
    story_init_v6(&s);
    EMIT(&s, 0xf3, 0x4f, 0x03, TABLE >> 8, TABLE & 0xff);       /* output_stream 3 TABLE */
    emit_text(&s, "abcdef");
    EMIT(&s, 0xbb);
    emit_text(&s, "xyz");
    EMIT(&s, 0xf3, 0x3f, 0xff, 0xfd);                           /* output_stream -3 */
    EMIT(&s, 0x0f, 0x00, 0x18, G0);                             /* loadw 0 $18 -> G0 */
    EMIT(&s, 0xba);
    run(&s, &out, &m, &mem);
    CHECK_EQ_INT(zm_read_word(&mem, TABLE), 10);
    CHECK_EQ_INT(zm_read_byte(&mem, TABLE + 8), 13);
    CHECK_EQ_INT(zm_read_word(&mem, GLOBALS), 3);
    zm_machine_free(&m);
    zm_memory_free(&mem);
    canvas_hooks(0);
}

/* A resource file with one picture, 16 by 8 pixels, drawn for a window
 * the size of the test canvas (Blorb §11.2), release 9. */
static void build_resources(uint8_t *bytes, uint32_t *size)
{
    static const uint8_t png[24] = {
        0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a, 0, 0, 0, 13, 'I', 'H', 'D', 'R', 0, 0, 0, 16, 0, 0, 0, 8
    };
    uint32_t n = 0;

    memset(bytes, 0, 160);
    memcpy(bytes, "FORM", 4);
    memcpy(bytes + 8, "IFRS", 4);
    memcpy(bytes + 12, "RIdx", 4);
    bytes[19] = 16;
    bytes[23] = 1;
    memcpy(bytes + 24, "Pict", 4);
    bytes[31] = 1;
    bytes[35] = 36;
    n = 36;
    memcpy(bytes + n, "PNG ", 4);
    bytes[n + 7] = sizeof png;
    memcpy(bytes + n + 8, png, sizeof png);
    n += 8 + sizeof png;
    memcpy(bytes + n, "RelN", 4);
    bytes[n + 7] = 2;
    bytes[n + 9] = 9;
    n += 10;
    memcpy(bytes + n, "Reso", 4);
    bytes[n + 7] = 24 + 28;
    bytes[n + 11] = CANVAS_WIDTH;
    bytes[n + 15] = CANVAS_HEIGHT;
    bytes[n + 35] = 1;                                          /* picture 1 */
    bytes[n + 39] = 1;                                          /* ratio 1/1 */
    bytes[n + 43] = 1;
    n += 8 + 24 + 28;
    bytes[7] = (uint8_t)(n - 8);
    *size = n;
}

static void test_v6_pictures(void)
{
    story s;
    output out;
    zm_machine m;
    zm_memory mem;
    zm_blorb resources;
    uint8_t bytes[160];
    uint32_t size;

    canvas_hooks(1);
    build_resources(bytes, &size);
    CHECK_EQ_INT(zm_blorb_load_bytes(&resources, bytes, size), ZM_OK);

    /* Spec §15 picture_data: the count and release for picture 0, the
     * size in units for a picture, and no branch for one that is not
     * there; Spec §15 draw_picture at a position or at the cursor, and
     * erase_picture over the same area; Spec §11.1.4: the header says
     * there are pictures. */
    story_init_v6(&s);
    EMIT(&s, 0xbe, 0x06, 0x4f, 0x00, TABLE >> 8, TABLE & 0xff, 0x80 | 0x40 | 5); /* picture_data 0 TABLE ?+3 */
    EMIT(&s, 0xe5, 0x7f, '0');                                  /* print_char '0' (skipped) */
    EMIT(&s, 0xbe, 0x06, 0x4f, 0x01, (TABLE + 4) >> 8, (TABLE + 4) & 0xff, 0x80 | 0x40 | 5);
    EMIT(&s, 0xe5, 0x7f, '1');                                  /* (skipped) */
    EMIT(&s, 0xbe, 0x06, 0x4f, 0x02, (TABLE + 8) >> 8, (TABLE + 8) & 0xff, 0x40 | 5);
    EMIT(&s, 0xe5, 0x7f, '2');                                  /* (skipped) */
    EMIT(&s, 0xbe, 0x05, 0x57, 0x01, 0x02, 0x03);               /* draw_picture 1 2 3 */
    EMIT(&s, 0xef, 0x5f, 0x04, 0x05);                           /* set_cursor 4 5 */
    EMIT(&s, 0xbe, 0x05, 0x7f, 0x01);                           /* draw_picture 1 */
    EMIT(&s, 0xbe, 0x07, 0x57, 0x01, 0x02, 0x03);               /* erase_picture 1 2 3 */
    EMIT(&s, 0xba);
    CHECK_EQ_INT(zm_memory_load_bytes(&mem, s.bytes, STORY_SIZE), ZM_OK);
    CHECK_EQ_INT(zm_machine_init(&m, &mem, &io), ZM_OK);
    CHECK_EQ_INT(zm_read_byte(&mem, ZM_HEADER_FLAGS1) & ZM_FLAGS1_PICTURES, 0);
    zm_machine_use_resources(&m, &resources);
    CHECK_EQ_INT(zm_read_byte(&mem, ZM_HEADER_FLAGS1) & ZM_FLAGS1_PICTURES, ZM_FLAGS1_PICTURES);
    memset(&out, 0, sizeof out);
    io.context = &out;
    zm_machine_run(&m);
    CHECK_EQ_STR(out.text, "");
    CHECK_EQ_INT(zm_read_word(&mem, TABLE), 1);
    CHECK_EQ_INT(zm_read_word(&mem, TABLE + 2), 9);
    CHECK_EQ_INT(zm_read_word(&mem, TABLE + 4), 8);
    CHECK_EQ_INT(zm_read_word(&mem, TABLE + 6), 16);
    CHECK_EQ_INT(zm_read_word(&mem, TABLE + 8), 0);
    CHECK_EQ_STR(screen_log, "picture 1 at 2,3 8x16;picture 1 at 4,5 8x16;erase 2,3 8x16 bg2");
    zm_machine_free(&m);
    zm_memory_free(&mem);

    /* Spec §15 draw_picture: "It is illegal to call this with an invalid
     * picture number." */
    story_init_v6(&s);
    EMIT(&s, 0xbe, 0x05, 0x57, 0x07, 0x01, 0x01);               /* draw_picture 7 1 1 */
    CHECK_EQ_INT(zm_memory_load_bytes(&mem, s.bytes, STORY_SIZE), ZM_OK);
    CHECK_EQ_INT(zm_machine_init(&m, &mem, &io), ZM_OK);
    zm_machine_use_resources(&m, &resources);
    zm_set_fatal_handler(trap_fault);
    if (setjmp(fault_jump) == 0) {
        zm_machine_run(&m);
        CHECK(0 && "draw_picture of a missing picture did not fault");
    } else {
        CHECK(strstr(fault_message, "no picture 7") != NULL);
    }
    zm_set_fatal_handler(NULL);
    zm_machine_free(&m);
    zm_memory_free(&mem);
    zm_blorb_free(&resources);
    canvas_hooks(0);
}

static void test_v6_input_is_shown_by_the_interpreter(void)
{
    static const char *const lines[] = { "Take Lamp", NULL };
    story s;
    output out;
    zm_machine m;
    zm_memory mem;

    canvas_hooks(1);
    script(lines, NULL);

    /* Spec §7.1.1.1: in Version 6 the interpreter shows what was typed
     * in the window, and only there: the transcript gets the window's
     * own text but not the command. The buffer still holds the text in
     * lower case (Spec §15 read). */
    story_init_v6(&s);
    put_word(s.bytes, ZM_HEADER_FLAGS2, ZM_FLAGS2_TRANSCRIPT);
    s.bytes[TABLE2] = 20;
    EMIT(&s, 0xe5, 0x7f, '>');                                  /* print_char '>' */
    EMIT(&s, 0xe4, 0x1f, TABLE2 >> 8, TABLE2 & 0xff, 0x00, G0); /* aread TABLE2 0 -> G0 */
    emit_text(&s, "Taken.");
    EMIT(&s, 0xba);
    run(&s, &out, &m, &mem);
    CHECK_EQ_STR(canvas_row(1), ">Take Lamp");
    CHECK_EQ_STR(canvas_row(2), "Taken.");
    CHECK_EQ_INT(zm_read_byte(&mem, TABLE2 + 1), 9);
    CHECK(memcmp(mem.bytes + TABLE2 + 2, "take lamp", 9) == 0);
    CHECK_EQ_INT(zm_read_word(&mem, GLOBALS), 13);
    CHECK_EQ_STR(transcript_text, ">Taken.");
    zm_machine_free(&m);
    zm_memory_free(&mem);
    canvas_hooks(0);
    script(NULL, NULL);
}

/*
 * The state of play (Spec §6.1): save, restore, restart, undo.
 */

static void test_save_and_restore_in_version_3(void)
{
    story s;
    output out;
    zm_machine m;
    zm_memory mem;

    file_hooks(1);

    /* Spec §15 save: a branch, taken when the save succeeds; Spec §15
     * restore, Quetzal §5.8.1: a restore picks up at that branch and
     * takes it again. The game sets a Flags 2 bit before restoring, and
     * finds it still set after (Spec §6.1.2), while the global it
     * changed is back to its saved value. The second restore finds no
     * file and so is not taken. */
    story_init(&s, 3);
    EMIT(&s, 0x0d, G0, 7);                                      /* store G0 7 */
    EMIT(&s, 0xb5, 0xc3);                                       /* save ?saved */
    EMIT(&s, 0xba);                                             /* quit */
    EMIT(&s, 0xe6, 0xbf, G0);                                   /* saved: print_num G0 */
    EMIT(&s, 0xe5, 0x7f, '/');                                  /* print_char '/' */
    EMIT(&s, 0x10, 0x00, 0x11, SP);                             /* loadb 0 $11 -> sp */
    EMIT(&s, 0xe6, 0xbf, SP);                                   /* print_num sp */
    EMIT(&s, 0xe5, 0x7f, ' ');                                  /* print_char ' ' */
    EMIT(&s, 0x0d, G0, 9);                                      /* store G0 9 */
    EMIT(&s, 0xe2, 0x57, 0x00, 0x11, 0x02);                     /* storeb 0 $11 2 */
    EMIT(&s, 0xb6, 0xc2);                                       /* restore ?next */
    EMIT(&s, 0xe6, 0xbf, G0);                                   /* next: print_num G0 */
    EMIT(&s, 0xba);                                             /* quit */
    saved_game_reads = 1;
    run(&s, &out, &m, &mem);
    CHECK_EQ_STR(out.text, "7/0 7/2 9");

    /* The file is Quetzal, and records the branch's address. */
    CHECK(saved_game_size > 36);
    CHECK(memcmp(saved_game, "FORM", 4) == 0);
    CHECK(memcmp(saved_game + 8, "IFZS", 4) == 0);
    CHECK(memcmp(saved_game + 30, "\x00\x04\x04", 3) == 0);
    zm_machine_free(&m);
    zm_memory_free(&mem);

    file_hooks(0);
}

static void test_save_and_restore_in_version_5(void)
{
    story s;
    output out;
    zm_machine m;
    zm_memory mem;

    file_hooks(1);
    screen_hooks(1);

    /* Spec §15 save: 1 when saved, 2 when restored to this point. Spec
     * §8.6.1.3: a restore collapses the upper window. Spec §14: in
     * Version 4 these are the 0OP opcodes, storing rather than
     * branching. */
    story_init(&s, 4);
    EMIT(&s, 0x0d, G0, 7);                                      /* store G0 7 */
    EMIT(&s, 0xb5, G1);                                         /* save -> G1 */
    EMIT(&s, 0xe6, 0xbf, G1);                                   /* print_num G1 */
    EMIT(&s, 0x41, G1, 2, 0xca);                                /* je G1 2 ?done */
    EMIT(&s, 0x0d, G0, 9);                                      /* store G0 9 */
    EMIT(&s, 0xb6, G2);                                         /* restore -> G2 */
    EMIT(&s, 0xe5, 0x7f, 'F');                                  /* print_char 'F' */
    EMIT(&s, 0xe6, 0xbf, G0);                                   /* done: print_num G0 */
    EMIT(&s, 0xba);                                             /* quit */
    saved_game_reads = 1;
    run(&s, &out, &m, &mem);
    CHECK_EQ_STR(out.text, "127");
    CHECK(memcmp(saved_game + 30, "\x00\x04\x04", 3) == 0);
    CHECK(strstr(screen_log, "split 0;window 0") != NULL);
    zm_machine_free(&m);
    zm_memory_free(&mem);

    /* Spec §14: from Version 5 they are the extended opcodes, here
     * without their optional operands. */
    story_init(&s, 5);
    EMIT(&s, 0x0d, G0, 7);                                      /* store G0 7 */
    EMIT(&s, 0xbe, 0x00, 0xff, G1);                             /* save -> G1 */
    EMIT(&s, 0xe6, 0xbf, G1);                                   /* print_num G1 */
    EMIT(&s, 0x41, G1, 2, 0xcc);                                /* je G1 2 ?done */
    EMIT(&s, 0x0d, G0, 9);                                      /* store G0 9 */
    EMIT(&s, 0xbe, 0x01, 0xff, G2);                             /* restore -> G2 */
    EMIT(&s, 0xe5, 0x7f, 'F');                                  /* print_char 'F' */
    EMIT(&s, 0xe6, 0xbf, G0);                                   /* done: print_num G0 */
    EMIT(&s, 0xba);                                             /* quit */
    saved_game_reads = 1;
    run(&s, &out, &m, &mem);
    CHECK_EQ_STR(out.text, "127");
    CHECK(memcmp(saved_game + 30, "\x00\x04\x06", 3) == 0);
    zm_machine_free(&m);
    zm_memory_free(&mem);

    /* Spec §15 restore: 0 when there is no file; and when the file is
     * wrong, with a word on why. */
    story_init(&s, 5);
    EMIT(&s, 0xbe, 0x01, 0xff, G2);                             /* restore -> G2 */
    EMIT(&s, 0xe6, 0xbf, G2);                                   /* print_num G2 */
    EMIT(&s, 0xba);                                             /* quit */
    saved_game_reads = 0;
    run(&s, &out, &m, &mem);
    CHECK_EQ_STR(out.text, "0");
    zm_machine_free(&m);
    zm_memory_free(&mem);

    saved_game_reads = 1;
    saved_game[21] = 0x01;                                      /* another release */
    run(&s, &out, &m, &mem);
    CHECK(strstr(out.text, "different story") != NULL);
    CHECK_EQ_INT(out.text[out.count - 1], '0');
    zm_machine_free(&m);
    zm_memory_free(&mem);

    saved_game_reads = 1;
    saved_game[21] = 0x00;
    saved_game[0] = 'X';                                        /* not IFF at all */
    run(&s, &out, &m, &mem);
    CHECK(strstr(out.text, "not a Quetzal") != NULL);
    CHECK_EQ_INT(out.text[out.count - 1], '0');
    zm_machine_free(&m);
    zm_memory_free(&mem);

    saved_game_reads = 1;
    saved_game[0] = 'F';
    saved_game_size = 40;                                       /* cut short */
    run(&s, &out, &m, &mem);
    CHECK(strstr(out.text, "damaged") != NULL);
    CHECK_EQ_INT(out.text[out.count - 1], '0');
    zm_machine_free(&m);
    zm_memory_free(&mem);

    screen_hooks(0);
    file_hooks(0);
}

static void test_auxiliary_files(void)
{
    story s;
    output out;
    zm_machine m;
    zm_memory mem;

    file_hooks(1);

    /* Spec §15 save and restore with operands, §7.6: four bytes of a
     * table go to a file named by the game, which asks not to be
     * prompted; cleared, they come back from it, and restore returns
     * how many. */
    story_init(&s, 5);
    s.bytes[ARRAY] = 1;
    s.bytes[ARRAY + 1] = 2;
    s.bytes[ARRAY + 2] = 3;
    s.bytes[ARRAY + 3] = 4;
    s.bytes[TABLE2] = 4;
    memcpy(s.bytes + TABLE2 + 1, "NOTE", 4);
    EMIT(&s, 0xbe, 0x00, 0x11, ARRAY >> 8, ARRAY & 0xff, 0x04, TABLE2 >> 8, TABLE2 & 0xff, 0x00, G1);
                                                                /* save ARRAY 4 TABLE2 0 -> G1 */
    EMIT(&s, 0xe2, 0x17, ARRAY >> 8, ARRAY & 0xff, 0x00, 0x00); /* storeb ARRAY 0 0 */
    EMIT(&s, 0xe2, 0x17, ARRAY >> 8, ARRAY & 0xff, 0x03, 0x00); /* storeb ARRAY 3 0 */
    EMIT(&s, 0xbe, 0x01, 0x11, ARRAY >> 8, ARRAY & 0xff, 0x04, TABLE2 >> 8, TABLE2 & 0xff, 0x00, G2);
                                                                /* restore ARRAY 4 TABLE2 0 -> G2 */
    EMIT(&s, 0xe6, 0xbf, G1, 0xe5, 0x7f, ' ');                  /* print_num G1 ' ' */
    EMIT(&s, 0xe6, 0xbf, G2, 0xe5, 0x7f, ' ');                  /* print_num G2 ' ' */
    EMIT(&s, 0xd0, 0x1f, ARRAY >> 8, ARRAY & 0xff, 0x00, SP);   /* loadb ARRAY 0 -> sp */
    EMIT(&s, 0xe6, 0xbf, SP, 0xe5, 0x7f, ' ');                  /* print_num sp ' ' */
    EMIT(&s, 0xd0, 0x1f, ARRAY >> 8, ARRAY & 0xff, 0x03, SP);   /* loadb ARRAY 3 -> sp */
    EMIT(&s, 0xe6, 0xbf, SP);                                   /* print_num sp */
    EMIT(&s, 0xba);                                             /* quit */
    run(&s, &out, &m, &mem);
    CHECK_EQ_STR(out.text, "1 4 1 4");
    CHECK_EQ_STR(aux_name, "NOTE");
    CHECK_EQ_INT(aux_prompt, 0);
    CHECK_EQ_INT(aux_size, 4);
    CHECK(memcmp(aux_data, "\x01\x02\x03\x04", 4) == 0);
    zm_machine_free(&m);
    zm_memory_free(&mem);

    /* Without a front end that keeps files, both fail. */
    file_hooks(0);
    run(&s, &out, &m, &mem);
    CHECK_EQ_STR(out.text, "0 0 0 0");
    zm_machine_free(&m);
    zm_memory_free(&mem);
}

static void test_restart(void)
{
    story s;
    output out;
    zm_machine m;
    zm_memory mem;

    screen_hooks(1);

    /* Spec §15 restart, §6.1.3: memory comes back from the story file
     * and the stack empties, but Flags 2 survives, which is how the
     * game here tells its second start from its first. The Rst header
     * fields are reset (Spec §6.1.2.2) and the screen cleared. */
    story_init(&s, 3);
    EMIT(&s, 0x95, G0);                                         /* inc G0 */
    EMIT(&s, 0xe6, 0xbf, G0);                                   /* print_num G0 */
    EMIT(&s, 0x10, 0x00, 0x11, SP);                             /* loadb 0 $11 -> sp */
    EMIT(&s, 0xa0, SP, 0xc6);                                   /* jz sp ?first */
    EMIT(&s, 0xe5, 0x7f, 'R');                                  /* print_char 'R' */
    EMIT(&s, 0xba);                                             /* quit */
    EMIT(&s, 0xe2, 0x57, 0x00, 0x11, 0x02);                     /* first: storeb 0 $11 2 */
    EMIT(&s, 0x0d, G0, 50);                                     /* store G0 50 */
    EMIT(&s, 0xe8, 0x7f, 1);                                    /* push 1 */
    EMIT(&s, 0xb7);                                             /* restart */
    run(&s, &out, &m, &mem);
    CHECK_EQ_STR(out.text, "11R");
    CHECK_EQ_INT(m.sp, 0);
    CHECK_EQ_INT(zm_read_word(&mem, GLOBALS), 1);
    CHECK_EQ_INT(zm_read_byte(&mem, ZM_HEADER_FLAGS2 + 1), 2);
    CHECK_EQ_INT(zm_read_byte(&mem, ZM_HEADER_STANDARD_MAJOR), 1);
    CHECK(strstr(screen_log, "style 0;colour 9/2;buffer on;erase -1") != NULL);
    zm_machine_free(&m);
    zm_memory_free(&mem);

    screen_hooks(0);
}

static void test_undo(void)
{
    story s;
    output out;
    zm_machine m;
    zm_memory mem;

    /* Spec §15 save_undo and restore_undo: each save_undo stores 1, and
     * 2 again when undone to; a second undo goes back a step further;
     * with nothing left, restore_undo stores 0. */
    story_init(&s, 5);
    EMIT(&s, 0x0d, G0, 1);                                      /* store G0 1 */
    EMIT(&s, 0xbe, 0x09, 0xff, G1);                             /* save_undo -> G1 */
    EMIT(&s, 0x41, G1, 2, 0xdb);                                /* je G1 2 ?undone1 */
    EMIT(&s, 0x0d, G0, 2);                                      /* store G0 2 */
    EMIT(&s, 0xbe, 0x09, 0xff, G1);                             /* save_undo -> G1 */
    EMIT(&s, 0x41, G1, 2, 0xc9);                                /* je G1 2 ?undone2 */
    EMIT(&s, 0x0d, G0, 3);                                      /* store G0 3 */
    EMIT(&s, 0xbe, 0x0a, 0xff, G2);                             /* restore_undo -> G2 */
    EMIT(&s, 0xe6, 0xbf, G0);                                   /* undone2: print_num G0 */
    EMIT(&s, 0xbe, 0x0a, 0xff, G2);                             /* restore_undo -> G2 */
    EMIT(&s, 0xe6, 0xbf, G0);                                   /* undone1: print_num G0 */
    EMIT(&s, 0xbe, 0x0a, 0xff, G2);                             /* restore_undo -> G2 */
    EMIT(&s, 0xe6, 0xbf, G2);                                   /* print_num G2 */
    EMIT(&s, 0xba);                                             /* quit */
    run(&s, &out, &m, &mem);
    CHECK_EQ_STR(out.text, "210");
    zm_machine_free(&m);
    zm_memory_free(&mem);

    /* More saves than the ring holds: the newest is still there. */
    story_init(&s, 5);
    EMIT(&s, 0xbe, 0x09, 0xff, G1);                             /* loop: save_undo -> G1 */
    EMIT(&s, 0x41, G1, 2, 0xcd);                                /* je G1 2 ?out */
    EMIT(&s, 0x95, G0);                                         /* inc G0 */
    EMIT(&s, 0x42, G0, 40, 0xbf, 0xf3);                         /* jl G0 40 ?loop */
    EMIT(&s, 0xbe, 0x0a, 0xff, G2);                             /* restore_undo -> G2 */
    EMIT(&s, 0xe6, 0xbf, G0);                                   /* out: print_num G0 */
    EMIT(&s, 0xba);                                             /* quit */
    run(&s, &out, &m, &mem);
    CHECK_EQ_STR(out.text, "39");
    CHECK_EQ_INT(m.undo_count, ZM_UNDO_LEVELS - 1);
    zm_machine_free(&m);
    zm_memory_free(&mem);
}

static void test_catch_and_throw(void)
{
    story s;
    output out;
    zm_machine m;
    zm_memory mem;

    /* Spec §15 catch and throw, Quetzal §6.2: catch counts the frames
     * on the call stack, not counting the top level, so the first
     * routine called sees 1; throw returns from that routine with the
     * value, discarding the frames and stack words above it. */
    story_init(&s, 5);
    EMIT(&s, 0xe0, 0x3f); emit_packed(&s, R1); EMIT(&s, G0);    /* call_vs R1 -> G0 */
    EMIT(&s, 0xe6, 0xbf, G0);                                   /* print_num G0 */
    EMIT(&s, 0xba);                                             /* quit */
    at(&s, R1);
    EMIT(&s, 0x01);                                             /* 1 local */
    EMIT(&s, 0xb9, L1);                                         /* catch -> L1 */
    EMIT(&s, 0xe6, 0xbf, L1, 0xe5, 0x7f, ' ');                  /* print_num L1 ' ' */
    EMIT(&s, 0xf9, 0x2f); emit_packed(&s, R2); EMIT(&s, L1);    /* call_vn R2 L1 */
    EMIT(&s, 0x9b, 5);                                          /* ret 5 */
    at(&s, R2);
    EMIT(&s, 0x01);                                             /* 1 local */
    EMIT(&s, 0xe8, 0x7f, 7);                                    /* push 7 */
    EMIT(&s, 0xf9, 0x2f); emit_packed(&s, R3); EMIT(&s, L1);    /* call_vn R3 L1 */
    EMIT(&s, 0xb0);                                             /* rtrue */
    at(&s, R3);
    EMIT(&s, 0x01);                                             /* 1 local */
    EMIT(&s, 0xe8, 0x7f, 8);                                    /* push 8 */
    EMIT(&s, 0x3c, 99, L1);                                     /* throw 99 L1 */
    run(&s, &out, &m, &mem);
    CHECK_EQ_STR(out.text, "1 99");
    CHECK_EQ_INT(m.sp, 0);
    CHECK_EQ_INT(m.depth, 1);
    zm_machine_free(&m);
    zm_memory_free(&mem);

    /* A frame that is not there. */
    story_init(&s, 5);
    EMIT(&s, 0x1c, 5, 7);                                       /* throw 5 7 */
    expect_fault(&s, "throw", &out);
}

int main(void)
{
    RUN_TEST(test_simple_test_files_say_hello);
    RUN_TEST(test_call_initialises_locals_from_header_before_version_5);
    RUN_TEST(test_call_initialises_locals_to_zero_from_version_5);
    RUN_TEST(test_call_to_address_zero_returns_false);
    RUN_TEST(test_operands_are_evaluated_first_to_last);
    RUN_TEST(test_stack_is_read_and_written_in_place_by_reference);
    RUN_TEST(test_branches_and_jumps);
    RUN_TEST(test_print_opcodes);
    RUN_TEST(test_return_opcodes);
    RUN_TEST(test_version_5_call_variants);
    RUN_TEST(test_version_6_starts_in_main_routine);
    RUN_TEST(test_faults);
    RUN_TEST(test_arithmetic_is_signed_and_wraps);
    RUN_TEST(test_comparisons_and_bitwise_logic);
    RUN_TEST(test_inc_chk_and_dec_chk);
    RUN_TEST(test_memory_access);
    RUN_TEST(test_object_opcodes);
    RUN_TEST(test_object_faults);
    RUN_TEST(test_random_numbers);
    RUN_TEST(test_shifts_and_argument_counts);
    RUN_TEST(test_verify_checks_the_checksum);
    RUN_TEST(test_read_in_version_3);
    RUN_TEST(test_read_in_version_5);
    RUN_TEST(test_read_char_tokenise_and_encode_text);
    RUN_TEST(test_timed_input);
    RUN_TEST(test_header_flags_are_set_at_start);
    RUN_TEST(test_input_faults);
    RUN_TEST(test_output_streams);
    RUN_TEST(test_windows_and_the_cursor);
    RUN_TEST(test_styles_colours_fonts_and_sound);
    RUN_TEST(test_table_opcodes);
    RUN_TEST(test_unicode_printing);
    RUN_TEST(test_screen_faults);
    RUN_TEST(test_v6_text_is_laid_out_in_windows);
    RUN_TEST(test_v6_window_properties_and_appearance);
    RUN_TEST(test_v6_newline_interrupt);
    RUN_TEST(test_v6_user_stacks);
    RUN_TEST(test_v6_formatted_stream_and_print_form);
    RUN_TEST(test_v6_pictures);
    RUN_TEST(test_v6_input_is_shown_by_the_interpreter);
    RUN_TEST(test_save_and_restore_in_version_3);
    RUN_TEST(test_save_and_restore_in_version_5);
    RUN_TEST(test_auxiliary_files);
    RUN_TEST(test_restart);
    RUN_TEST(test_undo);
    RUN_TEST(test_catch_and_throw);
    return CHECK_SUMMARY();
}
