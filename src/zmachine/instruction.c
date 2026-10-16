/*
 * instruction.c: see zm/instruction.h. Implements Spec §4 and the opcode
 * table of Spec §14.
 */

#include "zm/instruction.h"

#include <stddef.h>

/*
 * The opcode table (Spec §14).
 *
 * One row per opcode per Version range in which it has a given meaning.
 * Most opcodes mean the same thing in every Version and have one row with
 * the range 1 to 8. Where the Standard's table gives a Version column
 * (save turns from a branch into a store in Version 4, 1OP:15 is "not"
 * until Version 5 and call_1n after, and so on) there are several rows,
 * and the lookup picks the one whose range holds the Version. A number
 * with no row for a Version is illegal there (Spec §14.2).
 */

typedef struct {
    zm_opcode_class op_class;
    uint8_t number;
    uint8_t from, to;      /* Versions, inclusive */
    zm_opcode_info info;
} opcode_row;

#define ST 1, 0, 0        /* stores */
#define BR 0, 1, 0        /* branches */
#define STBR 1, 1, 0      /* both; store byte comes first (Spec §4.1) */
#define TEXT 0, 0, 1      /* followed by a string */
#define PLAIN 0, 0, 0

static const opcode_row table[] = {
    /* Two-operand opcodes. */
    { ZM_OP_2OP,  1, 1, 8, { "je",            BR } },
    { ZM_OP_2OP,  2, 1, 8, { "jl",            BR } },
    { ZM_OP_2OP,  3, 1, 8, { "jg",            BR } },
    { ZM_OP_2OP,  4, 1, 8, { "dec_chk",       BR } },
    { ZM_OP_2OP,  5, 1, 8, { "inc_chk",       BR } },
    { ZM_OP_2OP,  6, 1, 8, { "jin",           BR } },
    { ZM_OP_2OP,  7, 1, 8, { "test",          BR } },
    { ZM_OP_2OP,  8, 1, 8, { "or",            ST } },
    { ZM_OP_2OP,  9, 1, 8, { "and",           ST } },
    { ZM_OP_2OP, 10, 1, 8, { "test_attr",     BR } },
    { ZM_OP_2OP, 11, 1, 8, { "set_attr",      PLAIN } },
    { ZM_OP_2OP, 12, 1, 8, { "clear_attr",    PLAIN } },
    { ZM_OP_2OP, 13, 1, 8, { "store",         PLAIN } },
    { ZM_OP_2OP, 14, 1, 8, { "insert_obj",    PLAIN } },
    { ZM_OP_2OP, 15, 1, 8, { "loadw",         ST } },
    { ZM_OP_2OP, 16, 1, 8, { "loadb",         ST } },
    { ZM_OP_2OP, 17, 1, 8, { "get_prop",      ST } },
    { ZM_OP_2OP, 18, 1, 8, { "get_prop_addr", ST } },
    { ZM_OP_2OP, 19, 1, 8, { "get_next_prop", ST } },
    { ZM_OP_2OP, 20, 1, 8, { "add",           ST } },
    { ZM_OP_2OP, 21, 1, 8, { "sub",           ST } },
    { ZM_OP_2OP, 22, 1, 8, { "mul",           ST } },
    { ZM_OP_2OP, 23, 1, 8, { "div",           ST } },
    { ZM_OP_2OP, 24, 1, 8, { "mod",           ST } },
    { ZM_OP_2OP, 25, 4, 8, { "call_2s",       ST } },
    { ZM_OP_2OP, 26, 5, 8, { "call_2n",       PLAIN } },
    { ZM_OP_2OP, 27, 5, 8, { "set_colour",    PLAIN } },
    { ZM_OP_2OP, 28, 5, 8, { "throw",         PLAIN } },

    /* One-operand opcodes. */
    { ZM_OP_1OP,  0, 1, 8, { "jz",            BR } },
    { ZM_OP_1OP,  1, 1, 8, { "get_sibling",   STBR } },
    { ZM_OP_1OP,  2, 1, 8, { "get_child",     STBR } },
    { ZM_OP_1OP,  3, 1, 8, { "get_parent",    ST } },
    { ZM_OP_1OP,  4, 1, 8, { "get_prop_len",  ST } },
    { ZM_OP_1OP,  5, 1, 8, { "inc",           PLAIN } },
    { ZM_OP_1OP,  6, 1, 8, { "dec",           PLAIN } },
    { ZM_OP_1OP,  7, 1, 8, { "print_addr",    PLAIN } },
    { ZM_OP_1OP,  8, 4, 8, { "call_1s",       ST } },
    { ZM_OP_1OP,  9, 1, 8, { "remove_obj",    PLAIN } },
    { ZM_OP_1OP, 10, 1, 8, { "print_obj",     PLAIN } },
    { ZM_OP_1OP, 11, 1, 8, { "ret",           PLAIN } },
    { ZM_OP_1OP, 12, 1, 8, { "jump",          PLAIN } },
    { ZM_OP_1OP, 13, 1, 8, { "print_paddr",   PLAIN } },
    { ZM_OP_1OP, 14, 1, 8, { "load",          ST } },
    { ZM_OP_1OP, 15, 1, 4, { "not",           ST } },
    { ZM_OP_1OP, 15, 5, 8, { "call_1n",       PLAIN } },

    /* Zero-operand opcodes. */
    { ZM_OP_0OP,  0, 1, 8, { "rtrue",         PLAIN } },
    { ZM_OP_0OP,  1, 1, 8, { "rfalse",        PLAIN } },
    { ZM_OP_0OP,  2, 1, 8, { "print",         TEXT } },
    { ZM_OP_0OP,  3, 1, 8, { "print_ret",     TEXT } },
    { ZM_OP_0OP,  4, 1, 8, { "nop",           PLAIN } },
    { ZM_OP_0OP,  5, 1, 3, { "save",          BR } },
    { ZM_OP_0OP,  5, 4, 4, { "save",          ST } },
    { ZM_OP_0OP,  6, 1, 3, { "restore",       BR } },
    { ZM_OP_0OP,  6, 4, 4, { "restore",       ST } },
    { ZM_OP_0OP,  7, 1, 8, { "restart",       PLAIN } },
    { ZM_OP_0OP,  8, 1, 8, { "ret_popped",    PLAIN } },
    { ZM_OP_0OP,  9, 1, 4, { "pop",           PLAIN } },
    { ZM_OP_0OP,  9, 5, 8, { "catch",         ST } },
    { ZM_OP_0OP, 10, 1, 8, { "quit",          PLAIN } },
    { ZM_OP_0OP, 11, 1, 8, { "new_line",      PLAIN } },
    { ZM_OP_0OP, 12, 1, 3, { "show_status",   PLAIN } },
    { ZM_OP_0OP, 13, 3, 8, { "verify",        BR } },
    /* 0OP:14 is the first byte of an extended opcode in Version 5 and
     * later (Spec §4.3) and illegal before that. */
    { ZM_OP_0OP, 15, 5, 8, { "piracy",        BR } },

    /* Variable-operand opcodes. */
    { ZM_OP_VAR,  0, 1, 3, { "call",          ST } },
    { ZM_OP_VAR,  0, 4, 8, { "call_vs",       ST } },
    { ZM_OP_VAR,  1, 1, 8, { "storew",        PLAIN } },
    { ZM_OP_VAR,  2, 1, 8, { "storeb",        PLAIN } },
    { ZM_OP_VAR,  3, 1, 8, { "put_prop",      PLAIN } },
    { ZM_OP_VAR,  4, 1, 4, { "sread",         PLAIN } },
    { ZM_OP_VAR,  4, 5, 8, { "aread",         ST } },
    { ZM_OP_VAR,  5, 1, 8, { "print_char",    PLAIN } },
    { ZM_OP_VAR,  6, 1, 8, { "print_num",     PLAIN } },
    { ZM_OP_VAR,  7, 1, 8, { "random",        ST } },
    { ZM_OP_VAR,  8, 1, 8, { "push",          PLAIN } },
    { ZM_OP_VAR,  9, 1, 5, { "pull",          PLAIN } },
    { ZM_OP_VAR,  9, 6, 6, { "pull",          ST } },
    { ZM_OP_VAR,  9, 7, 8, { "pull",          PLAIN } },
    { ZM_OP_VAR, 10, 3, 8, { "split_window",  PLAIN } },
    { ZM_OP_VAR, 11, 3, 8, { "set_window",    PLAIN } },
    { ZM_OP_VAR, 12, 4, 8, { "call_vs2",      ST } },
    { ZM_OP_VAR, 13, 4, 8, { "erase_window",  PLAIN } },
    { ZM_OP_VAR, 14, 4, 8, { "erase_line",    PLAIN } },
    { ZM_OP_VAR, 15, 4, 8, { "set_cursor",    PLAIN } },
    { ZM_OP_VAR, 16, 4, 8, { "get_cursor",    PLAIN } },
    { ZM_OP_VAR, 17, 4, 8, { "set_text_style", PLAIN } },
    { ZM_OP_VAR, 18, 4, 8, { "buffer_mode",   PLAIN } },
    { ZM_OP_VAR, 19, 3, 8, { "output_stream", PLAIN } },
    { ZM_OP_VAR, 20, 3, 8, { "input_stream",  PLAIN } },
    { ZM_OP_VAR, 21, 3, 8, { "sound_effect",  PLAIN } },
    { ZM_OP_VAR, 22, 4, 8, { "read_char",     ST } },
    { ZM_OP_VAR, 23, 4, 8, { "scan_table",    STBR } },
    { ZM_OP_VAR, 24, 5, 8, { "not",           ST } },
    { ZM_OP_VAR, 25, 5, 8, { "call_vn",       PLAIN } },
    { ZM_OP_VAR, 26, 5, 8, { "call_vn2",      PLAIN } },
    { ZM_OP_VAR, 27, 5, 8, { "tokenise",      PLAIN } },
    { ZM_OP_VAR, 28, 5, 8, { "encode_text",   PLAIN } },
    { ZM_OP_VAR, 29, 5, 8, { "copy_table",    PLAIN } },
    { ZM_OP_VAR, 30, 5, 8, { "print_table",   PLAIN } },
    { ZM_OP_VAR, 31, 5, 8, { "check_arg_count", BR } },

    /* Extended opcodes, Version 5 and later only. */
    { ZM_OP_EXT,  0, 5, 8, { "save",          ST } },
    { ZM_OP_EXT,  1, 5, 8, { "restore",       ST } },
    { ZM_OP_EXT,  2, 5, 8, { "log_shift",     ST } },
    { ZM_OP_EXT,  3, 5, 8, { "art_shift",     ST } },
    { ZM_OP_EXT,  4, 5, 8, { "set_font",      ST } },
    { ZM_OP_EXT,  5, 6, 6, { "draw_picture",  PLAIN } },
    { ZM_OP_EXT,  6, 6, 6, { "picture_data",  BR } },
    { ZM_OP_EXT,  7, 6, 6, { "erase_picture", PLAIN } },
    { ZM_OP_EXT,  8, 6, 6, { "set_margins",   PLAIN } },
    { ZM_OP_EXT,  9, 5, 8, { "save_undo",     ST } },
    { ZM_OP_EXT, 10, 5, 8, { "restore_undo",  ST } },
    { ZM_OP_EXT, 11, 5, 8, { "print_unicode", PLAIN } },
    { ZM_OP_EXT, 12, 5, 8, { "check_unicode", ST } },
    { ZM_OP_EXT, 13, 5, 8, { "set_true_colour", PLAIN } },
    { ZM_OP_EXT, 16, 6, 6, { "move_window",   PLAIN } },
    { ZM_OP_EXT, 17, 6, 6, { "window_size",   PLAIN } },
    { ZM_OP_EXT, 18, 6, 6, { "window_style",  PLAIN } },
    { ZM_OP_EXT, 19, 6, 6, { "get_wind_prop", ST } },
    { ZM_OP_EXT, 20, 6, 6, { "scroll_window", PLAIN } },
    { ZM_OP_EXT, 21, 6, 6, { "pop_stack",     PLAIN } },
    { ZM_OP_EXT, 22, 6, 6, { "read_mouse",    PLAIN } },
    { ZM_OP_EXT, 23, 6, 6, { "mouse_window",  PLAIN } },
    { ZM_OP_EXT, 24, 6, 6, { "push_stack",    BR } },
    { ZM_OP_EXT, 25, 6, 6, { "put_wind_prop", PLAIN } },
    { ZM_OP_EXT, 26, 6, 6, { "print_form",    PLAIN } },
    { ZM_OP_EXT, 27, 6, 6, { "make_menu",     BR } },
    { ZM_OP_EXT, 28, 6, 6, { "picture_table", PLAIN } },
    { ZM_OP_EXT, 29, 6, 6, { "buffer_screen", ST } },
};

#undef ST
#undef BR
#undef STBR
#undef TEXT
#undef PLAIN

const zm_opcode_info *zm_opcode_lookup(uint8_t version, zm_opcode_class op_class, uint8_t number)
{
    for (size_t i = 0; i < sizeof table / sizeof table[0]; i++) {
        const opcode_row *row = &table[i];

        if (row->op_class == op_class && row->number == number
            && version >= row->from && version <= row->to) {
            return &row->info;
        }
    }
    return NULL;
}

const char *zm_form_name(zm_form form)
{
    switch (form) {
    case ZM_FORM_LONG:     return "long";
    case ZM_FORM_SHORT:    return "short";
    case ZM_FORM_VARIABLE: return "variable";
    case ZM_FORM_EXTENDED: return "extended";
    }
    return "?";
}

const char *zm_opcode_class_name(zm_opcode_class op_class)
{
    switch (op_class) {
    case ZM_OP_0OP: return "0OP";
    case ZM_OP_1OP: return "1OP";
    case ZM_OP_2OP: return "2OP";
    case ZM_OP_VAR: return "VAR";
    case ZM_OP_EXT: return "EXT";
    }
    return "?";
}

/*
 * Decoding (Spec §4.1): opcode, operand types, operands, store, branch,
 * text, in that order.
 */

/* Spec §4.4.3: read a byte of four two-bit operand types, most
 * significant field first, stopping at the first "omitted". Returns the
 * number of operands found. */
static unsigned read_type_byte(uint8_t byte, zm_operand *operands, unsigned start)
{
    unsigned count = start;

    for (int shift = 6; shift >= 0; shift -= 2) {
        zm_operand_type type = (zm_operand_type)((byte >> shift) & 3);

        if (type == ZM_OPERAND_OMITTED) {
            break;
        }
        operands[count++].type = type;
    }
    return count;
}

/* Spec §4.5: fetch each operand's bytes according to its type. Returns
 * the address after the last one. */
static uint32_t read_operands(const zm_memory *mem, uint32_t address, zm_operand *operands, unsigned count)
{
    for (unsigned i = 0; i < count; i++) {
        if (operands[i].type == ZM_OPERAND_LARGE) {
            /* Spec §4.2.1: high byte first. */
            operands[i].value = zm_read_word(mem, address);
            address += 2;
        } else {
            operands[i].value = zm_read_byte(mem, address);
            address += 1;
        }
    }
    return address;
}

/* Spec §4.7: one or two bytes of branch data. */
static uint32_t read_branch(const zm_memory *mem, uint32_t address, zm_instruction *inst)
{
    uint8_t first = zm_read_byte(mem, address++);

    /* Bit 7 gives the sense of the test. */
    inst->branch_on_true = (uint8_t)((first & 0x80) != 0);

    if (first & 0x40) {
        /* Bit 6 set: a single byte, with an unsigned offset of 0 to 63 in
         * the bottom six bits. This form can only branch forward. */
        inst->branch_offset = (int16_t)(first & 0x3f);
    } else {
        /* Bit 6 clear: a signed 14-bit offset from the bottom six bits
         * of this byte and all eight of the next. Bit 13 is the sign
         * bit, so a value with it set is 16384 less than it reads. */
        int raw = ((first & 0x3f) << 8) | zm_read_byte(mem, address++);
        inst->branch_offset = (int16_t)((raw & 0x2000) ? raw - 0x4000 : raw);
    }
    return address;
}

/* Spec §4.8, §3.2: skip the words of a string up to and including the
 * one with the top bit set. */
static uint32_t skip_text(const zm_memory *mem, uint32_t address)
{
    uint16_t word;

    do {
        word = zm_read_word(mem, address);
        address += 2;
    } while ((word & 0x8000) == 0);
    return address;
}

void zm_instruction_decode(const zm_memory *mem, uint32_t address, zm_instruction *inst)
{
    uint8_t opcode;
    uint32_t at = address;

    inst->address = address;
    inst->operand_count = 0;
    inst->info = NULL;
    inst->store = 0;
    inst->branch_on_true = 0;
    inst->branch_offset = 0;
    inst->text = 0;

    opcode = zm_read_byte(mem, at++);

    /* Spec §4.3: the form. The test for $be must come before the test on
     * the top two bits, because $be itself has top bits %10. */
    if (opcode == 0xbe && mem->version >= 5) {
        /* Spec §4.3.4, §4.4.3: extended form, opcode number in the next
         * byte, then a byte of operand types. */
        inst->form = ZM_FORM_EXTENDED;
        inst->op_class = ZM_OP_EXT;
        inst->number = zm_read_byte(mem, at++);
        inst->operand_count = read_type_byte(zm_read_byte(mem, at++), inst->operands, 0);
    } else if ((opcode & 0xc0) == 0xc0) {
        /* Spec §4.3.3: variable form. Bit 5 chooses 2OP or VAR; the
         * number is the bottom five bits. Operand types follow in a
         * byte (Spec §4.4.3), or two bytes for call_vs2 and call_vn2
         * (Spec §4.4.3.1), whose eight operands need eight fields. */
        inst->form = ZM_FORM_VARIABLE;
        inst->op_class = (opcode & 0x20) ? ZM_OP_VAR : ZM_OP_2OP;
        inst->number = (uint8_t)(opcode & 0x1f);
        inst->operand_count = read_type_byte(zm_read_byte(mem, at++), inst->operands, 0);
        if (inst->op_class == ZM_OP_VAR && (inst->number == 12 || inst->number == 26)) {
            inst->operand_count = read_type_byte(zm_read_byte(mem, at++), inst->operands, inst->operand_count);
        }
    } else if ((opcode & 0xc0) == 0x80) {
        /* Spec §4.3.1, §4.4.1: short form. Bits 4 and 5 are the type of
         * the single operand, or %11 for no operand at all; the number
         * is the bottom four bits. */
        zm_operand_type type = (zm_operand_type)((opcode >> 4) & 3);

        inst->form = ZM_FORM_SHORT;
        inst->number = (uint8_t)(opcode & 0x0f);
        if (type == ZM_OPERAND_OMITTED) {
            inst->op_class = ZM_OP_0OP;
        } else {
            inst->op_class = ZM_OP_1OP;
            inst->operands[0].type = type;
            inst->operand_count = 1;
        }
    } else {
        /* Spec §4.3.2, §4.4.2: long form, always 2OP, with one type bit
         * per operand: 0 for a small constant, 1 for a variable. There
         * is no way to write a large constant in this form. */
        inst->form = ZM_FORM_LONG;
        inst->op_class = ZM_OP_2OP;
        inst->number = (uint8_t)(opcode & 0x1f);
        inst->operands[0].type = (opcode & 0x40) ? ZM_OPERAND_VARIABLE : ZM_OPERAND_SMALL;
        inst->operands[1].type = (opcode & 0x20) ? ZM_OPERAND_VARIABLE : ZM_OPERAND_SMALL;
        inst->operand_count = 2;
    }

    /* Spec §4.5: the operands themselves. */
    at = read_operands(mem, at, inst->operands, inst->operand_count);

    /* Spec §4.6 to §4.8: what follows depends on which opcode this is. */
    inst->info = zm_opcode_lookup(mem->version, inst->op_class, inst->number);
    if (inst->info != NULL) {
        if (inst->info->stores) {
            inst->store = zm_read_byte(mem, at++);
        }
        if (inst->info->branches) {
            at = read_branch(mem, at, inst);
        }
        if (inst->info->has_text) {
            inst->text = at;
            at = skip_text(mem, at);
        }
    }

    inst->next = at;
}
