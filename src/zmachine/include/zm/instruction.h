/*
 * zm/instruction.h: decoding one instruction.
 *
 * Implements Spec §4. An instruction is a variable-length record with no
 * length field: the opcode byte gives its form, the form gives the operand
 * count and where the operand types come from, the types give the size of
 * the operands, and only then is it known whether a store byte, branch
 * data, or a string follows. Decoding is therefore a fixed sequence of
 * steps, and this file performs it without executing anything. The
 * machine (zm/machine.h) decodes and then acts; a disassembler decodes
 * and prints; the tests decode and inspect.
 *
 * Knowing which opcodes store, branch, or carry text is part of decoding
 * (Spec §4.6, §4.7, §4.8), so the table of opcodes from Spec §14 lives
 * here too, with the Version differences it records.
 */

#ifndef ZM_INSTRUCTION_H
#define ZM_INSTRUCTION_H

#include <stdint.h>

#include "zm/memory.h"

/* Spec §4.3: the four forms an instruction can take. */
typedef enum zm_form {
    ZM_FORM_LONG,
    ZM_FORM_SHORT,
    ZM_FORM_VARIABLE,
    ZM_FORM_EXTENDED
} zm_form;

/* Spec §4.3: the operand count, which with the opcode number identifies
 * the operation. Extended instructions have a VAR count by §4.3.4 but
 * their own numbering (Spec §14), so they are a class of their own
 * here. */
typedef enum zm_opcode_class {
    ZM_OP_0OP,
    ZM_OP_1OP,
    ZM_OP_2OP,
    ZM_OP_VAR,
    ZM_OP_EXT
} zm_opcode_class;

/* Spec §4.2: the operand types, numbered as the two-bit fields encode
 * them. */
typedef enum zm_operand_type {
    ZM_OPERAND_LARGE    = 0,  /* 2-byte constant */
    ZM_OPERAND_SMALL    = 1,  /* 1-byte constant */
    ZM_OPERAND_VARIABLE = 2,  /* 1-byte variable number, read when executed */
    ZM_OPERAND_OMITTED  = 3
} zm_operand_type;

typedef struct zm_operand {
    zm_operand_type type;

    /* The constant, or for a variable operand the variable number (Spec
     * §4.2.2): 0 is the stack, 1 to 15 the locals, 16 to 255 the
     * globals. */
    uint16_t value;
} zm_operand;

/* What Spec §14 says about an opcode: its Inform name and which of the
 * optional sections (Spec §4.1) follow its operands. */
typedef struct zm_opcode_info {
    const char *name;
    uint8_t stores;    /* a store variable byte follows (Spec §4.6) */
    uint8_t branches;  /* branch data follows (Spec §4.7) */
    uint8_t has_text;  /* an encoded string follows (Spec §4.8) */
} zm_opcode_info;

/* Spec §4.5.1: no instruction has more than eight operands. */
#define ZM_MAX_OPERANDS 8

typedef struct zm_instruction {
    /* Byte address of the opcode byte. */
    uint32_t address;

    zm_form form;
    zm_opcode_class op_class;

    /* The opcode number within its class (Spec §4.3.1 to §4.3.4). */
    uint8_t number;

    /* NULL if the Version has no such opcode (Spec §14.2). */
    const zm_opcode_info *info;

    unsigned operand_count;
    zm_operand operands[ZM_MAX_OPERANDS];

    /* Spec §4.6: where to put the result, if the opcode stores one. */
    uint8_t store;

    /* Spec §4.7: the branch, if the opcode has one. branch_on_true is the
     * sense of the test; branch_offset is 0 or 1 to return false or true
     * from the routine, otherwise the signed offset to apply. */
    uint8_t branch_on_true;
    int16_t branch_offset;

    /* Spec §4.8: byte address of the string, if the opcode carries one. */
    uint32_t text;

    /* Byte address of the next instruction in sequence: after the
     * operands, the store byte, the branch data, and the text. For a
     * branch this is the "address after branch data" of Spec §4.7.2. */
    uint32_t next;
} zm_instruction;

/* Decode the instruction at a byte address. Never faults on an unknown
 * opcode: info is left NULL and next is set as if nothing followed the
 * operands, so a disassembler can show the bytes and carry on. Reads
 * past the end of memory are still faults. */
void zm_instruction_decode(const zm_memory *mem, uint32_t address, zm_instruction *out);

/* Decode the one or two bytes of branch data at an address (Spec §4.7)
 * without the instruction they belong to, as a restore must for the
 * branch of the save instruction it resumes at (Quetzal §5.8.1). Returns
 * the address after the data. */
uint32_t zm_instruction_read_branch(const zm_memory *mem, uint32_t address,
                                    uint8_t *branch_on_true, int16_t *branch_offset);

/* Look up an opcode in the table of Spec §14 for a given Version, or
 * NULL if that Version does not define it. */
const zm_opcode_info *zm_opcode_lookup(uint8_t version, zm_opcode_class op_class, uint8_t number);

/* A short name for a form or class, for messages. */
const char *zm_form_name(zm_form form);
const char *zm_opcode_class_name(zm_opcode_class op_class);

#endif /* ZM_INSTRUCTION_H */
