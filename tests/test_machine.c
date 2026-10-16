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
#include <string.h>

#include "zm/fatal.h"
#include "zm/header.h"
#include "zm/machine.h"
#include "zm/memory.h"
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
    EMIT(&s, 0xbc);                                             /* show_status */
    expect_fault(&s, "show_status", &out);

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
             "1 2 3 0 4 2 3 0 ac 258 7 68 68 %u 1 0 0 4 5 3 2 0 -16657 255 0", (unsigned)(PROPS2 + 7));
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
    return CHECK_SUMMARY();
}
