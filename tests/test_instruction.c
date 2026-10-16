/*
 * test_instruction.c: decoding instructions.
 *
 * Covers Spec §4 and the opcode table of Spec §14 through
 * zm/instruction.h. Each test writes the bytes of an instruction into a
 * story and checks what the decoder makes of them. Several sequences are
 * the worked examples from the "On disassembly" remarks at the end of
 * Spec §4, so the expected values can be read straight from the
 * Standard.
 */

#include <stdio.h>
#include <string.h>

#include "zm/instruction.h"
#include "zm/memory.h"

#include "check.h"

#define STORY_SIZE 512u
#define CODE 0x100u

static void put_word(uint8_t *bytes, uint32_t offset, uint16_t value)
{
    bytes[offset] = (uint8_t)(value >> 8);
    bytes[offset + 1] = (uint8_t)(value & 0xff);
}

/* Load a story of the given Version whose bytes at CODE are the given
 * code, and decode the instruction there. */
static void decode_bytes(uint8_t version, const uint8_t *code, size_t length, zm_instruction *inst, zm_memory *mem)
{
    uint8_t bytes[STORY_SIZE];

    memset(bytes, 0, sizeof bytes);
    bytes[0] = version;
    put_word(bytes, 0x0e, STORY_SIZE);
    memcpy(bytes + CODE, code, length);

    CHECK_EQ_INT(zm_memory_load_bytes(mem, bytes, sizeof bytes), ZM_OK);
    zm_instruction_decode(mem, CODE, inst);
}

static void test_long_form_with_short_branch(void)
{
    /* Spec §4 remarks: @inc_chk c 0 label; is 05 02 00 d4. Long form,
     * 2OP, opcode 5, two small constants, then a one-byte branch on true
     * with offset 20. */
    static const uint8_t code[] = { 0x05, 0x02, 0x00, 0xd4 };
    zm_instruction inst;
    zm_memory mem;

    decode_bytes(3, code, sizeof code, &inst, &mem);

    CHECK_EQ_INT(inst.form, ZM_FORM_LONG);
    CHECK_EQ_INT(inst.op_class, ZM_OP_2OP);
    CHECK_EQ_INT(inst.number, 5);
    CHECK(inst.info != NULL);
    CHECK_EQ_STR(inst.info->name, "inc_chk");
    CHECK_EQ_INT(inst.operand_count, 2);
    CHECK_EQ_INT(inst.operands[0].type, ZM_OPERAND_SMALL);
    CHECK_EQ_INT(inst.operands[0].value, 2);
    CHECK_EQ_INT(inst.operands[1].type, ZM_OPERAND_SMALL);
    CHECK_EQ_INT(inst.operands[1].value, 0);
    CHECK(inst.info->branches);
    CHECK(!inst.info->stores);
    CHECK_EQ_INT(inst.branch_on_true, 1);
    CHECK_EQ_INT(inst.branch_offset, 20);
    CHECK_EQ_INT(inst.next, CODE + 4);

    zm_memory_free(&mem);
}

static void test_long_form_variable_operands(void)
{
    /* Spec §4.4.2: bit 6 types the first operand and bit 5 the second,
     * 1 meaning variable. $61 is je with two variable operands. The
     * branch byte $41 is "on false, one byte, offset 1": return true. */
    static const uint8_t code[] = { 0x61, 0x01, 0x10, 0x41 };
    zm_instruction inst;
    zm_memory mem;

    decode_bytes(3, code, sizeof code, &inst, &mem);

    CHECK_EQ_INT(inst.form, ZM_FORM_LONG);
    CHECK_EQ_STR(inst.info->name, "je");
    CHECK_EQ_INT(inst.operands[0].type, ZM_OPERAND_VARIABLE);
    CHECK_EQ_INT(inst.operands[0].value, 1);
    CHECK_EQ_INT(inst.operands[1].type, ZM_OPERAND_VARIABLE);
    CHECK_EQ_INT(inst.operands[1].value, 0x10);
    CHECK_EQ_INT(inst.branch_on_true, 0);
    CHECK_EQ_INT(inst.branch_offset, 1);
    CHECK_EQ_INT(inst.next, CODE + 4);

    zm_memory_free(&mem);
}

static void test_short_form_0op_with_text(void)
{
    /* Spec §4 remarks: @print "Hello.^"; is b2 followed by four words of
     * text, the last with its top bit set. Short form, no operand, so
     * 0OP:2, and execution continues after the text (Spec §4.8). */
    static const uint8_t code[] = { 0xb2, 0x11, 0xaa, 0x46, 0x34, 0x16, 0x45, 0x9c, 0xa5 };
    zm_instruction inst;
    zm_memory mem;

    decode_bytes(3, code, sizeof code, &inst, &mem);

    CHECK_EQ_INT(inst.form, ZM_FORM_SHORT);
    CHECK_EQ_INT(inst.op_class, ZM_OP_0OP);
    CHECK_EQ_INT(inst.number, 2);
    CHECK_EQ_STR(inst.info->name, "print");
    CHECK_EQ_INT(inst.operand_count, 0);
    CHECK(inst.info->has_text);
    CHECK_EQ_INT(inst.text, CODE + 1);
    CHECK_EQ_INT(inst.next, CODE + 9);

    zm_memory_free(&mem);
}

static void test_variable_form_2op_with_store(void)
{
    /* Spec §4 remarks: @mul 1000 c -> sp; is d6 2f 03 e8 02 00. Variable
     * form with bit 5 clear, so still 2OP (opcode 22), because a large
     * constant cannot be written in long form. */
    static const uint8_t code[] = { 0xd6, 0x2f, 0x03, 0xe8, 0x02, 0x00 };
    zm_instruction inst;
    zm_memory mem;

    decode_bytes(3, code, sizeof code, &inst, &mem);

    CHECK_EQ_INT(inst.form, ZM_FORM_VARIABLE);
    CHECK_EQ_INT(inst.op_class, ZM_OP_2OP);
    CHECK_EQ_INT(inst.number, 22);
    CHECK_EQ_STR(inst.info->name, "mul");
    CHECK_EQ_INT(inst.operand_count, 2);
    CHECK_EQ_INT(inst.operands[0].type, ZM_OPERAND_LARGE);
    CHECK_EQ_INT(inst.operands[0].value, 1000);
    CHECK_EQ_INT(inst.operands[1].type, ZM_OPERAND_VARIABLE);
    CHECK_EQ_INT(inst.operands[1].value, 2);
    CHECK(inst.info->stores);
    CHECK_EQ_INT(inst.store, 0);
    CHECK_EQ_INT(inst.next, CODE + 6);

    zm_memory_free(&mem);
}

static void test_short_form_1op_changes_meaning_with_version(void)
{
    /* Spec §4 remarks: @call_1n Message; is 8f 01 56 in Version 5. Short
     * form, large constant, 1OP:15. In Versions 1 to 4 the same number
     * is "not", which stores, so a fourth byte is read (Spec §14). */
    static const uint8_t code[] = { 0x8f, 0x01, 0x56, 0x07 };
    zm_instruction inst;
    zm_memory mem;

    decode_bytes(5, code, sizeof code, &inst, &mem);
    CHECK_EQ_INT(inst.form, ZM_FORM_SHORT);
    CHECK_EQ_INT(inst.op_class, ZM_OP_1OP);
    CHECK_EQ_INT(inst.number, 15);
    CHECK_EQ_STR(inst.info->name, "call_1n");
    CHECK_EQ_INT(inst.operand_count, 1);
    CHECK_EQ_INT(inst.operands[0].type, ZM_OPERAND_LARGE);
    CHECK_EQ_INT(inst.operands[0].value, 0x0156);
    CHECK(!inst.info->stores);
    CHECK_EQ_INT(inst.next, CODE + 3);
    zm_memory_free(&mem);

    decode_bytes(3, code, sizeof code, &inst, &mem);
    CHECK_EQ_STR(inst.info->name, "not");
    CHECK(inst.info->stores);
    CHECK_EQ_INT(inst.store, 7);
    CHECK_EQ_INT(inst.next, CODE + 4);
    zm_memory_free(&mem);
}

static void test_variable_form_var_stops_at_omitted(void)
{
    /* The first instruction Inform compiles: e0 3f 02 4f ff is call with
     * one large-constant operand, since the type byte %00111111 says
     * large, omitted, omitted, omitted (Spec §4.4.3), storing to $ff. */
    static const uint8_t code[] = { 0xe0, 0x3f, 0x02, 0x4f, 0xff };
    zm_instruction inst;
    zm_memory mem;

    decode_bytes(3, code, sizeof code, &inst, &mem);

    CHECK_EQ_INT(inst.form, ZM_FORM_VARIABLE);
    CHECK_EQ_INT(inst.op_class, ZM_OP_VAR);
    CHECK_EQ_INT(inst.number, 0);
    CHECK_EQ_STR(inst.info->name, "call");
    CHECK_EQ_INT(inst.operand_count, 1);
    CHECK_EQ_INT(inst.operands[0].value, 0x024f);
    CHECK_EQ_INT(inst.store, 0xff);
    CHECK_EQ_INT(inst.next, CODE + 5);
    zm_memory_free(&mem);

    /* Spec §14: the same opcode is called call_vs from Version 4. */
    decode_bytes(4, code, sizeof code, &inst, &mem);
    CHECK_EQ_STR(inst.info->name, "call_vs");
    zm_memory_free(&mem);
}

static void test_variable_form_with_four_operands(void)
{
    /* je in variable form with four small constants (%01010101) and a
     * branch on true with offset 0, meaning return false (Spec §4.7.1). */
    static const uint8_t code[] = { 0xc1, 0x55, 0x01, 0x02, 0x03, 0x04, 0xc0 };
    zm_instruction inst;
    zm_memory mem;

    decode_bytes(3, code, sizeof code, &inst, &mem);

    CHECK_EQ_INT(inst.op_class, ZM_OP_2OP);
    CHECK_EQ_STR(inst.info->name, "je");
    CHECK_EQ_INT(inst.operand_count, 4);
    CHECK_EQ_INT(inst.operands[3].value, 4);
    CHECK_EQ_INT(inst.branch_on_true, 1);
    CHECK_EQ_INT(inst.branch_offset, 0);
    CHECK_EQ_INT(inst.next, CODE + 7);

    zm_memory_free(&mem);
}

static void test_call_vs2_reads_two_type_bytes(void)
{
    /* Spec §4.4.3.1: VAR:12 and VAR:26 have a second byte of types. Here
     * %00010101 then %01011111 give large, small, small, small, small,
     * small: six operands, then the store byte. */
    static const uint8_t code[] = {
        0xec, 0x15, 0x5f, 0x01, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x00
    };
    zm_instruction inst;
    zm_memory mem;

    decode_bytes(5, code, sizeof code, &inst, &mem);

    CHECK_EQ_STR(inst.info->name, "call_vs2");
    CHECK_EQ_INT(inst.operand_count, 6);
    CHECK_EQ_INT(inst.operands[0].type, ZM_OPERAND_LARGE);
    CHECK_EQ_INT(inst.operands[0].value, 0x0100);
    CHECK_EQ_INT(inst.operands[1].value, 1);
    CHECK_EQ_INT(inst.operands[5].value, 5);
    CHECK_EQ_INT(inst.store, 0);
    CHECK_EQ_INT(inst.next, CODE + 11);

    zm_memory_free(&mem);
}

static void test_extended_form(void)
{
    /* Spec §4.3.4: $be followed by the opcode number and a type byte, in
     * Version 5 and later. log_shift 8 2 -> sp. */
    static const uint8_t code[] = { 0xbe, 0x02, 0x5f, 0x08, 0x02, 0x00 };
    zm_instruction inst;
    zm_memory mem;

    decode_bytes(5, code, sizeof code, &inst, &mem);
    CHECK_EQ_INT(inst.form, ZM_FORM_EXTENDED);
    CHECK_EQ_INT(inst.op_class, ZM_OP_EXT);
    CHECK_EQ_INT(inst.number, 2);
    CHECK_EQ_STR(inst.info->name, "log_shift");
    CHECK_EQ_INT(inst.operand_count, 2);
    CHECK_EQ_INT(inst.operands[0].value, 8);
    CHECK_EQ_INT(inst.operands[1].value, 2);
    CHECK_EQ_INT(inst.store, 0);
    CHECK_EQ_INT(inst.next, CODE + 6);
    zm_memory_free(&mem);

    /* Before Version 5, $be is just short form 0OP:14, which no Version
     * defines: an illegal opcode one byte long. */
    decode_bytes(3, code, sizeof code, &inst, &mem);
    CHECK_EQ_INT(inst.form, ZM_FORM_SHORT);
    CHECK_EQ_INT(inst.op_class, ZM_OP_0OP);
    CHECK_EQ_INT(inst.number, 14);
    CHECK(inst.info == NULL);
    CHECK_EQ_INT(inst.next, CODE + 1);
    zm_memory_free(&mem);

    /* Spec §14.2.1: an extended number past 29 is unknown but decodes
     * far enough to be skipped. */
    {
        static const uint8_t unknown[] = { 0xbe, 0x40, 0x5f, 0x08, 0x02 };
        decode_bytes(5, unknown, sizeof unknown, &inst, &mem);
        CHECK_EQ_INT(inst.op_class, ZM_OP_EXT);
        CHECK(inst.info == NULL);
        CHECK_EQ_INT(inst.operand_count, 2);
        CHECK_EQ_INT(inst.next, CODE + 5);
        zm_memory_free(&mem);
    }
}

static void test_long_branch_offsets(void)
{
    /* Spec §4.7: with bit 6 clear the offset is a signed 14-bit value in
     * the low six bits of the first byte and all of the second. jz with
     * a variable operand: $bf $fb is "on true", -5; $01 $2c is "on
     * false", 300. */
    static const uint8_t backward[] = { 0xa0, 0x01, 0xbf, 0xfb };
    static const uint8_t forward[]  = { 0xa0, 0x01, 0x01, 0x2c };
    zm_instruction inst;
    zm_memory mem;

    decode_bytes(3, backward, sizeof backward, &inst, &mem);
    CHECK_EQ_STR(inst.info->name, "jz");
    CHECK_EQ_INT(inst.operands[0].type, ZM_OPERAND_VARIABLE);
    CHECK_EQ_INT(inst.branch_on_true, 1);
    CHECK_EQ_INT(inst.branch_offset, -5);
    CHECK_EQ_INT(inst.next, CODE + 4);
    zm_memory_free(&mem);

    decode_bytes(3, forward, sizeof forward, &inst, &mem);
    CHECK_EQ_INT(inst.branch_on_true, 0);
    CHECK_EQ_INT(inst.branch_offset, 300);
    zm_memory_free(&mem);
}

static void test_store_then_branch(void)
{
    /* Spec §4.1: when an opcode both stores and branches, the store byte
     * comes first. get_child obj -> sp ?label. */
    static const uint8_t code[] = { 0xa2, 0x05, 0x00, 0xc3 };
    zm_instruction inst;
    zm_memory mem;

    decode_bytes(3, code, sizeof code, &inst, &mem);

    CHECK_EQ_STR(inst.info->name, "get_child");
    CHECK(inst.info->stores);
    CHECK(inst.info->branches);
    CHECK_EQ_INT(inst.store, 0);
    CHECK_EQ_INT(inst.branch_on_true, 1);
    CHECK_EQ_INT(inst.branch_offset, 3);
    CHECK_EQ_INT(inst.next, CODE + 4);

    zm_memory_free(&mem);
}

static void test_save_changes_shape_with_version(void)
{
    /* Spec §14: 0OP:5 save branches in Versions 1 to 3, stores in
     * Version 4, and does not exist from Version 5 (where save is
     * EXT:0). The byte after $b5 is read as branch data, a store byte,
     * or not at all. */
    static const uint8_t code[] = { 0xb5, 0xc5 };
    zm_instruction inst;
    zm_memory mem;

    decode_bytes(3, code, sizeof code, &inst, &mem);
    CHECK_EQ_STR(inst.info->name, "save");
    CHECK(inst.info->branches);
    CHECK_EQ_INT(inst.branch_offset, 5);
    CHECK_EQ_INT(inst.next, CODE + 2);
    zm_memory_free(&mem);

    decode_bytes(4, code, sizeof code, &inst, &mem);
    CHECK_EQ_STR(inst.info->name, "save");
    CHECK(inst.info->stores);
    CHECK_EQ_INT(inst.store, 0xc5);
    CHECK_EQ_INT(inst.next, CODE + 2);
    zm_memory_free(&mem);

    decode_bytes(5, code, sizeof code, &inst, &mem);
    CHECK(inst.info == NULL);
    CHECK_EQ_INT(inst.next, CODE + 1);
    zm_memory_free(&mem);
}

static void test_sread_becomes_aread(void)
{
    /* Spec §14: VAR:4 is sread until Version 4 and aread, which stores,
     * from Version 5. */
    static const uint8_t code[] = { 0xe4, 0x5f, 0x01, 0x02, 0x00 };
    zm_instruction inst;
    zm_memory mem;

    decode_bytes(4, code, sizeof code, &inst, &mem);
    CHECK_EQ_STR(inst.info->name, "sread");
    CHECK_EQ_INT(inst.next, CODE + 4);
    zm_memory_free(&mem);

    decode_bytes(5, code, sizeof code, &inst, &mem);
    CHECK_EQ_STR(inst.info->name, "aread");
    CHECK_EQ_INT(inst.store, 0);
    CHECK_EQ_INT(inst.next, CODE + 5);
    zm_memory_free(&mem);
}

static void test_opcode_table(void)
{
    const zm_opcode_info *info;

    /* A sample of the Version column of Spec §14. */
    CHECK(zm_opcode_lookup(3, ZM_OP_2OP, 25) == NULL);          /* call_2s is V4 */
    CHECK(zm_opcode_lookup(4, ZM_OP_2OP, 25) != NULL);
    CHECK(zm_opcode_lookup(4, ZM_OP_2OP, 26) == NULL);          /* call_2n is V5 */

    info = zm_opcode_lookup(4, ZM_OP_0OP, 9);
    CHECK_EQ_STR(info->name, "pop");
    CHECK(!info->stores);
    info = zm_opcode_lookup(5, ZM_OP_0OP, 9);
    CHECK_EQ_STR(info->name, "catch");
    CHECK(info->stores);

    CHECK(!zm_opcode_lookup(5, ZM_OP_VAR, 9)->stores);          /* pull (variable) */
    CHECK(zm_opcode_lookup(6, ZM_OP_VAR, 9)->stores);           /* pull stack -> (result) */
    CHECK(!zm_opcode_lookup(8, ZM_OP_VAR, 9)->stores);

    CHECK(zm_opcode_lookup(3, ZM_OP_VAR, 10) != NULL);          /* split_window is V3 */
    CHECK(zm_opcode_lookup(2, ZM_OP_VAR, 10) == NULL);
    CHECK(zm_opcode_lookup(5, ZM_OP_EXT, 5) == NULL);           /* draw_picture is V6 only */
    CHECK(zm_opcode_lookup(6, ZM_OP_EXT, 5) != NULL);
    CHECK(zm_opcode_lookup(8, ZM_OP_EXT, 29) == NULL);          /* buffer_screen is V6 only */
    CHECK(zm_opcode_lookup(5, ZM_OP_EXT, 9) != NULL);           /* save_undo */

    /* Numbers with no opcode. */
    CHECK(zm_opcode_lookup(3, ZM_OP_2OP, 0) == NULL);
    CHECK(zm_opcode_lookup(8, ZM_OP_2OP, 29) == NULL);
    CHECK(zm_opcode_lookup(8, ZM_OP_EXT, 14) == NULL);
    CHECK(zm_opcode_lookup(8, ZM_OP_EXT, 30) == NULL);

    /* Which opcodes both store and branch. */
    CHECK(zm_opcode_lookup(3, ZM_OP_1OP, 1)->branches);         /* get_sibling */
    CHECK(zm_opcode_lookup(3, ZM_OP_1OP, 1)->stores);
    CHECK(zm_opcode_lookup(4, ZM_OP_VAR, 23)->branches);        /* scan_table */
    CHECK(zm_opcode_lookup(4, ZM_OP_VAR, 23)->stores);

    /* Which carry text. */
    CHECK(zm_opcode_lookup(1, ZM_OP_0OP, 2)->has_text);
    CHECK(zm_opcode_lookup(1, ZM_OP_0OP, 3)->has_text);
    CHECK(!zm_opcode_lookup(1, ZM_OP_1OP, 13)->has_text);       /* print_paddr takes an address */

    CHECK_EQ_STR(zm_opcode_class_name(ZM_OP_VAR), "VAR");
    CHECK_EQ_STR(zm_form_name(ZM_FORM_EXTENDED), "extended");
}

int main(void)
{
    RUN_TEST(test_long_form_with_short_branch);
    RUN_TEST(test_long_form_variable_operands);
    RUN_TEST(test_short_form_0op_with_text);
    RUN_TEST(test_variable_form_2op_with_store);
    RUN_TEST(test_short_form_1op_changes_meaning_with_version);
    RUN_TEST(test_variable_form_var_stops_at_omitted);
    RUN_TEST(test_variable_form_with_four_operands);
    RUN_TEST(test_call_vs2_reads_two_type_bytes);
    RUN_TEST(test_extended_form);
    RUN_TEST(test_long_branch_offsets);
    RUN_TEST(test_store_then_branch);
    RUN_TEST(test_save_changes_shape_with_version);
    RUN_TEST(test_sread_becomes_aread);
    RUN_TEST(test_opcode_table);
    return CHECK_SUMMARY();
}
