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
 * variables at $40 (Spec §6.2), code from $240, routines at $300, $340,
 * $380, and $3c0 (all divisible by 8, so they can be packed addresses in
 * any Version, Spec §5.1), and a string at $400.
 */

#include <setjmp.h>
#include <stdio.h>
#include <string.h>

#include "zm/fatal.h"
#include "zm/header.h"
#include "zm/machine.h"
#include "zm/memory.h"
#include "zm/text.h"

#include "check.h"

#define STORY_SIZE 2048u
#define GLOBALS 0x40u
#define CODE 0x240u
#define R1 0x300u
#define R2 0x340u
#define R3 0x380u
#define R4 0x3c0u
#define STRING 0x400u

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

static zm_io io = { NULL, collect };

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

    /* Spec §4.2.2: a local the routine does not have. */
    story_init(&s, 3);
    EMIT(&s, 0xe6, 0xbf, L1);                                   /* print_num L1 */
    expect_fault(&s, "local variable 1", &out);

    /* Spec §14.2: an opcode the Version does not define. */
    story_init(&s, 3);
    EMIT(&s, 0xbe);
    expect_fault(&s, "illegal opcode", &out);

    /* An opcode that exists but is not implemented yet is named. */
    story_init(&s, 3);
    EMIT(&s, 0xd6, 0x2f, 0x03, 0xe8, G0, 0x00);                 /* mul 1000 G0 -> sp */
    expect_fault(&s, "mul", &out);

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
    return CHECK_SUMMARY();
}
