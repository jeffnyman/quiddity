/*
 * machine.c: see zm/machine.h. Implements Spec §5, §6, and the opcodes of
 * Spec §15 that have been reached so far, with the arithmetic of Spec §2
 * and the object table of Spec §12 as the opcodes need them.
 *
 * The file is in four parts: the stack and variables (§6.2, §6.3), routine
 * calls and returns (§5, §6.4), the helpers every opcode shares (store,
 * branch, printing, arithmetic, random numbers, objects), and the
 * dispatch itself.
 */

#include "zm/machine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "zm/fatal.h"
#include "zm/header.h"
#include "zm/instruction.h"
#include "zm/object.h"
#include "zm/text.h"

/*
 * The stack and variables.
 */

static zm_frame *current_frame(zm_machine *m)
{
    return &m->frames[m->depth - 1];
}

static void push(zm_machine *m, uint16_t value)
{
    if (m->sp >= ZM_STACK_WORDS) {
        zm_fatal("stack overflow at $%05x", (unsigned)m->pc);
    }
    m->stack[m->sp++] = value;
}

static uint16_t pop(zm_machine *m)
{
    /* Spec §6.3.1: the stack is empty at the start of each routine, so a
     * routine cannot pull what its caller pushed. */
    if (m->sp <= current_frame(m)->stack_base) {
        zm_fatal("stack underflow at $%05x", (unsigned)m->pc);
    }
    return m->stack[--m->sp];
}

/* The top of the stack, for the opcodes that read or write it in place. */
static uint16_t *stack_top(zm_machine *m)
{
    if (m->sp <= current_frame(m)->stack_base) {
        zm_fatal("stack is empty at $%05x", (unsigned)m->pc);
    }
    return &m->stack[m->sp - 1];
}

/* Spec §4.2.2: locals are 1 to 15, but only as many as the routine
 * declared. */
static uint16_t *local_slot(zm_machine *m, uint8_t variable)
{
    zm_frame *f = current_frame(m);

    if (variable > f->local_count) {
        zm_fatal("local variable %u does not exist in a routine with %u at $%05x",
                 (unsigned)variable, (unsigned)f->local_count, (unsigned)m->pc);
    }
    return &f->locals[variable - 1];
}

/* Read a variable. With in_place set, variable 0 is the top of the stack
 * read without popping (Spec §6.3.4); otherwise it pops (Spec §6.3). */
static uint16_t read_variable(zm_machine *m, uint8_t variable, int in_place)
{
    if (variable == 0) {
        return in_place ? *stack_top(m) : pop(m);
    }
    if (variable < 16) {
        return *local_slot(m, variable);
    }
    /* Spec §6.2: globals are a table of 240 words in dynamic memory. */
    return zm_read_word(m->mem, m->globals + 2u * (variable - 16u));
}

static void write_variable(zm_machine *m, uint8_t variable, uint16_t value, int in_place)
{
    if (variable == 0) {
        if (in_place) {
            *stack_top(m) = value;
        } else {
            push(m, value);
        }
    } else if (variable < 16) {
        *local_slot(m, variable) = value;
    } else {
        zm_write_word(m->mem, m->globals + 2u * (variable - 16u), value);
    }
}

uint16_t zm_machine_read_variable(zm_machine *m, uint8_t variable)
{
    return read_variable(m, variable, 0);
}

void zm_machine_write_variable(zm_machine *m, uint8_t variable, uint16_t value)
{
    write_variable(m, variable, value, 0);
}

/*
 * Routine calls (Spec §5, §6.4).
 */

/* Enter the routine at a byte address, creating its frame. The frame's
 * return address and result variable are the caller's to fill in. */
static zm_frame *enter_routine(zm_machine *m, uint32_t address, const uint16_t *args, unsigned arg_count)
{
    zm_frame *f;
    uint8_t local_count;

    if (m->depth >= ZM_MAX_FRAMES) {
        zm_fatal("routine calls nested more than %u deep at $%05x",
                 (unsigned)ZM_MAX_FRAMES, (unsigned)m->pc);
    }

    /* Spec §5.2: the routine starts with its number of locals. */
    local_count = zm_read_byte(m->mem, address++);
    if (local_count > ZM_MAX_LOCALS) {
        zm_fatal("routine at $%05x claims %u local variables", (unsigned)(address - 1), (unsigned)local_count);
    }

    f = &m->frames[m->depth++];
    f->return_pc = m->pc;
    f->stack_base = m->sp;
    f->result_variable = -1;
    f->argument_count = (uint8_t)(arg_count > 255 ? 255 : arg_count);
    f->local_count = local_count;

    /* Spec §5.2.1, §6.4.4: initial values come from the routine header
     * in Versions 1 to 4 and are zero from Version 5. Then the arguments
     * overwrite the first locals; spare arguments are dropped (Spec
     * §6.4.4.1). */
    for (unsigned i = 0; i < local_count; i++) {
        if (m->mem->version <= 4) {
            f->locals[i] = zm_read_word(m->mem, address);
            address += 2;
        } else {
            f->locals[i] = 0;
        }
    }
    for (unsigned i = 0; i < arg_count && i < local_count; i++) {
        f->locals[i] = args[i];
    }

    /* Spec §5.3: execution begins after the header. */
    m->pc = address;
    return f;
}

/* Call a routine by packed address with the given arguments, arranging
 * for its return value to go to result_variable, or nowhere if that is
 * -1. */
static void call_routine(zm_machine *m, uint16_t packed, const uint16_t *args, unsigned arg_count, int result_variable)
{
    zm_frame *f;

    /* Spec §6.4.3: calling address 0 does nothing and returns false. */
    if (packed == 0) {
        if (result_variable >= 0) {
            write_variable(m, (uint8_t)result_variable, 0, 0);
        }
        return;
    }

    f = enter_routine(m, zm_unpack_routine(m->mem, packed), args, arg_count);
    f->result_variable = result_variable;
}

static void return_from_routine(zm_machine *m, uint16_t value)
{
    zm_frame *f = current_frame(m);
    int result_variable = f->result_variable;

    /* Spec §5.4, §5.5: there is nowhere to return to from the first
     * frame; a game must quit instead. */
    if (m->depth == 1) {
        zm_fatal("return from the main routine at $%05x; quit must be used instead", (unsigned)m->pc);
    }

    /* Spec §6.3.2: whatever the routine left on the stack goes. */
    m->sp = f->stack_base;
    m->pc = f->return_pc;
    m->depth--;

    /* The store happens in the caller's frame, so a result sent to the
     * stack lands on the caller's stack and one sent to a local goes to
     * the caller's local. */
    if (result_variable >= 0) {
        write_variable(m, (uint8_t)result_variable, value, 0);
    }
}

/*
 * Shared pieces of instructions.
 */

static void store_result(zm_machine *m, const zm_instruction *inst, uint16_t value)
{
    write_variable(m, inst->store, value, 0);
}

/* Spec §4.7: act on the outcome of a test. */
static void branch(zm_machine *m, const zm_instruction *inst, int condition)
{
    if ((condition != 0) != (inst->branch_on_true != 0)) {
        return;
    }

    /* Spec §4.7.1: offsets 0 and 1 are returns. Spec §4.7.2: anything
     * else moves to the address after the branch data, plus the offset,
     * less two. */
    if (inst->branch_offset == 0) {
        return_from_routine(m, 0);
    } else if (inst->branch_offset == 1) {
        return_from_routine(m, 1);
    } else {
        m->pc = (uint32_t)((int32_t)inst->next + inst->branch_offset - 2);
    }
}

static void print_zscii(zm_machine *m, uint16_t zscii)
{
    m->io->print_char(m->io->context, zscii);
}

static void print_sink(void *context, uint16_t zscii)
{
    print_zscii(context, zscii);
}

static void print_string(zm_machine *m, uint32_t address)
{
    zm_text_decode(m->mem, address, print_sink, m);
}

/* Spec §2.2.1, §15 print_num: numbers print as signed decimals. */
static void print_number(zm_machine *m, uint16_t value)
{
    char digits[8];
    int length = snprintf(digits, sizeof digits, "%d", (int)(int16_t)value);

    for (int i = 0; i < length; i++) {
        print_zscii(m, (uint16_t)digits[i]);
    }
}

/* Spec §6.3.4: inc, dec, load, store, and pull name a variable rather
 * than supplying a value, and treat variable 0 as the top of the stack
 * in place. The variable number arrives as an ordinary operand value. */
static uint8_t variable_reference(const zm_instruction *inst, uint16_t operand)
{
    if (operand > 255) {
        zm_fatal("%s at $%05x refers to variable %u, which does not exist",
                 inst->info->name, (unsigned)inst->address, (unsigned)operand);
    }
    return (uint8_t)operand;
}

static void not_implemented(zm_machine *m, const zm_instruction *inst)
{
    (void)m;
    zm_fatal("opcode %s (%s:%u) at $%05x is not implemented yet",
             inst->info->name, zm_opcode_class_name(inst->op_class),
             (unsigned)inst->number, (unsigned)inst->address);
}

/*
 * Arithmetic (Spec §2).
 *
 * Operands arrive as the 16-bit words they are stored as. Spec §2.2.1
 * makes comparison, multiplication, addition, subtraction, division, and
 * remainder signed, and bitwise operations unsigned, so the signed ones
 * reinterpret their operands first. Results are reduced modulo $10000
 * (Spec §2.3.2), which is what converting back to a 16-bit word does.
 */

static int16_t as_signed(uint16_t word)
{
    return (int16_t)word;
}

static uint16_t as_word(int32_t value)
{
    return (uint16_t)value;
}

static uint16_t divide(const zm_instruction *inst, uint16_t a, uint16_t b, int remainder)
{
    int32_t x = as_signed(a);
    int32_t y = as_signed(b);

    /* Spec §2.3.1. */
    if (y == 0) {
        zm_fatal("%s by zero at $%05x", remainder ? "remainder of division" : "division", (unsigned)inst->address);
    }

    /* Spec §2 Remarks: -11 / 2 = -5 and -13 % 5 = -3, which is C's
     * truncation toward zero. The one overflow, -32768 / -1, gives 32768
     * in the wider type and $8000 once reduced. */
    return as_word(remainder ? x % y : x / y);
}

/* Spec §15 art_shift and log_shift: a positive count shifts left, a
 * negative one right; arithmetic shifts carry the sign bit down. */
static uint16_t shift(const zm_instruction *inst, uint16_t value, uint16_t places, int arithmetic)
{
    int count = as_signed(places);

    if (count > 15 || count < -15) {
        zm_fatal("%s by %d places at $%05x", inst->info->name, count, (unsigned)inst->address);
    }
    if (count >= 0) {
        return (uint16_t)((uint32_t)value << count);
    }
    if (arithmetic && (value & 0x8000u)) {
        /* Right-shifting a negative number is implementation-defined in
         * C, so shift its complement, which is positive, and complement
         * the result: the ones shifted in become the copies of the sign
         * bit that an arithmetic shift wants. */
        return (uint16_t)~((uint16_t)~value >> -count);
    }
    return (uint16_t)(value >> -count);
}

/*
 * Random numbers (Spec §2.4).
 *
 * Spec §2 Remarks warns against the C library's rand(). The generator
 * here is xorshift64*, a small, well-studied generator whose high bits
 * are the good ones, which is why the draw below uses them. It is
 * seeded through a mixing function so that seeds that differ in a bit
 * or two give unrelated sequences (Spec §2.4.2).
 */

static uint64_t mix64(uint64_t z)
{
    z += 0x9e3779b97f4a7c15u;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9u;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebu;
    return z ^ (z >> 31);
}

static uint64_t next_random(zm_machine *m)
{
    uint64_t x = m->random_state;

    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    m->random_state = x;
    return x * 0x2545f4914f6cdd1du;
}

void zm_machine_seed_random(zm_machine *m, uint16_t seed)
{
    m->random_interval = 0;
    m->random_counter = 0;

    if (seed == 0) {
        /* "Random" mode (Spec §2.4): the clock, with the address of the
         * machine stirred in so that two machines started in the same
         * second differ. mix64() never yields 0, which xorshift needs. */
        m->random_state = mix64((uint64_t)time(NULL) ^ ((uint64_t)clock() << 32) ^ (uint64_t)(uintptr_t)m);
    } else if (seed < 1000) {
        /* Spec §2 Remarks: a small seed asks for the sequence 1, 2, ...,
         * seed, repeating, so that every value turns up in order. */
        m->random_interval = seed;
    } else {
        m->random_state = mix64(seed);
    }
}

uint16_t zm_machine_random(zm_machine *m, uint16_t range)
{
    if (range == 0) {
        zm_fatal("random number from an empty range");
    }

    if (m->random_interval != 0) {
        uint16_t value = (uint16_t)(m->random_counter % range + 1u);

        m->random_counter = (uint16_t)((m->random_counter + 1u) % m->random_interval);
        return value;
    }

    return (uint16_t)((next_random(m) >> 32) % range + 1u);
}

/*
 * Objects (Spec §12). The table is zm/object.h's; what belongs here is
 * the machine's treatment of object 0.
 *
 * Spec §12.3: object 0 means "nothing", and there is formally no such
 * object. Games nonetheless ask about it now and then, by taking the
 * parent of nothing or testing an attribute of nothing, and a halt there
 * would leave some Infocom games unplayable. So the enquiries treat
 * nothing as an object with no parent, no children, no attributes, and
 * no properties of its own, which is what such code expects. Anything
 * that would change object 0, or print its name, is a fault, as there is
 * nothing to change (and Spec §15 print_obj asks for a halt).
 */

/* Look up a property for get_prop, get_prop_addr, and put_prop. Faults
 * on a property number no object can have; returns 1 if the object
 * provides the property, 0 if it falls back on the default. */
static int find_property(zm_machine *m, const zm_instruction *inst, uint16_t object, uint16_t number, zm_property *prop)
{
    unsigned limit = m->mem->version <= 3 ? 31u : 63u;

    if (number == 0 || number > limit) {
        zm_fatal("%s at $%05x asks for property %u, which does not exist in Version %u",
                 inst->info->name, (unsigned)inst->address, (unsigned)number, (unsigned)m->mem->version);
    }
    if (object == 0) {
        return 0;
    }
    return zm_object_find_property(m->mem, object, number, prop);
}

/* Fault unless an object operand names a real object. */
static uint16_t object_to_change(zm_machine *m, const zm_instruction *inst, uint16_t object)
{
    (void)m;
    if (object == 0) {
        zm_fatal("%s at $%05x applied to object 0, which is nothing", inst->info->name, (unsigned)inst->address);
    }
    return object;
}

static void print_object_name(zm_machine *m, uint16_t object)
{
    /* Spec §12.4: an empty name has a text-length of 0 and no text to
     * decode. */
    if (zm_object_name_length(m->mem, object) > 0) {
        print_string(m, zm_object_name(m->mem, object));
    }
}

/* Spec §15 get_prop and put_prop: only properties of 1 or 2 bytes may be
 * read or written as values. */
static void check_property_is_a_value(const zm_instruction *inst, uint16_t object, const zm_property *prop)
{
    if (prop->length > 2) {
        zm_fatal("%s at $%05x on property %u of object %u, which has %u bytes rather than 1 or 2",
                 inst->info->name, (unsigned)inst->address, (unsigned)prop->number,
                 (unsigned)object, (unsigned)prop->length);
    }
}

/*
 * Dispatch. Each function receives the decoded instruction and its
 * operand values, already fetched in order (Spec §4.5.2).
 */

static void execute_0op(zm_machine *m, const zm_instruction *inst)
{
    switch (inst->number) {
    case 0:  /* rtrue */
        return_from_routine(m, 1);
        break;
    case 1:  /* rfalse */
        return_from_routine(m, 0);
        break;
    case 2:  /* print */
        print_string(m, inst->text);
        break;
    case 3:  /* print_ret: print, new-line, return true */
        print_string(m, inst->text);
        print_zscii(m, ZM_ZSCII_NEWLINE);
        return_from_routine(m, 1);
        break;
    case 4:  /* nop */
        break;
    case 8:  /* ret_popped */
        return_from_routine(m, pop(m));
        break;
    case 9:  /* pop (Versions 1 to 4): discard the top of the stack */
        pop(m);
        break;
    case 10: /* quit */
        m->running = 0;
        break;
    case 11: /* new_line */
        print_zscii(m, ZM_ZSCII_NEWLINE);
        break;
    case 12: /* show_status: Version 3 only, but Spec §15 asks that it be
              * a nop in later Versions for the sake of 'Wishbringer'
              * release 23. The status line itself comes with the screen
              * model. */
        if (m->mem->version <= 3) {
            not_implemented(m, inst);
        }
        break;
    case 13: /* verify: compare the checksum of the file with the header's
              * (Spec §15 verify; the sum is defined there and computed by
              * the header module). */
        branch(m, inst, zm_header_compute_checksum(m->mem) == zm_read_word(m->mem, ZM_HEADER_CHECKSUM));
        break;
    case 15: /* piracy: "interpreters are asked to be gullible" */
        branch(m, inst, 1);
        break;
    default:
        not_implemented(m, inst);
    }
}

static void execute_1op(zm_machine *m, const zm_instruction *inst, const uint16_t *ops)
{
    uint8_t variable;

    switch (inst->number) {
    case 0:  /* jz */
        branch(m, inst, ops[0] == 0);
        break;
    case 1:  /* get_sibling: store, then branch if there is one */
    case 2:  /* get_child */
    case 3:  /* get_parent: store only */
        {
            uint16_t result = 0;

            if (ops[0] != 0) {
                result = inst->number == 1 ? zm_object_sibling(m->mem, ops[0])
                       : inst->number == 2 ? zm_object_child(m->mem, ops[0])
                                           : zm_object_parent(m->mem, ops[0]);
            }
            store_result(m, inst, result);
            if (inst->number != 3) {
                branch(m, inst, result != 0);
            }
        }
        break;
    case 4:  /* get_prop_len */
        store_result(m, inst, zm_property_length_at(m->mem, ops[0]));
        break;
    case 5:  /* inc: signed, so $ffff becomes 0 */
        variable = variable_reference(inst, ops[0]);
        write_variable(m, variable, (uint16_t)(read_variable(m, variable, 1) + 1u), 1);
        break;
    case 6:  /* dec */
        variable = variable_reference(inst, ops[0]);
        write_variable(m, variable, (uint16_t)(read_variable(m, variable, 1) - 1u), 1);
        break;
    case 7:  /* print_addr */
        print_string(m, ops[0]);
        break;
    case 8:  /* call_1s */
        call_routine(m, ops[0], NULL, 0, inst->store);
        break;
    case 9:  /* remove_obj */
        zm_object_remove(m->mem, object_to_change(m, inst, ops[0]));
        break;
    case 10: /* print_obj */
        print_object_name(m, object_to_change(m, inst, ops[0]));
        break;
    case 11: /* ret */
        return_from_routine(m, ops[0]);
        break;
    case 12: /* jump: the operand is a signed offset applied as a branch
              * offset would be (Spec §15 jump), with no condition and no
              * special meaning for 0 and 1. */
        m->pc = (uint32_t)((int32_t)inst->next + (int16_t)ops[0] - 2);
        break;
    case 13: /* print_paddr */
        print_string(m, zm_unpack_string(m->mem, ops[0]));
        break;
    case 14: /* load */
        variable = variable_reference(inst, ops[0]);
        store_result(m, inst, read_variable(m, variable, 1));
        break;
    case 15: /* call_1n from Version 5; not (bitwise, Spec §2.2.1) before
              * that, where it has room because call_1n does not exist */
        if (m->mem->version >= 5) {
            call_routine(m, ops[0], NULL, 0, -1);
        } else {
            store_result(m, inst, (uint16_t)~ops[0]);
        }
        break;
    default:
        not_implemented(m, inst);
    }
}

static void execute_2op(zm_machine *m, const zm_instruction *inst, const uint16_t *ops, unsigned count)
{
    switch (inst->number) {
    case 1:  /* je: branch if the first operand equals any of the rest.
              * In variable form it may have up to four operands. */
        if (count < 2) {
            zm_fatal("je with one operand at $%05x", (unsigned)inst->address);
        }
        {
            int equal = 0;
            for (unsigned i = 1; i < count; i++) {
                if (ops[i] == ops[0]) {
                    equal = 1;
                }
            }
            branch(m, inst, equal);
        }
        break;
    case 2:  /* jl: signed (Spec §2.2.1) */
        branch(m, inst, as_signed(ops[0]) < as_signed(ops[1]));
        break;
    case 3:  /* jg */
        branch(m, inst, as_signed(ops[0]) > as_signed(ops[1]));
        break;
    case 4:  /* dec_chk: decrement the variable, in place for the stack
              * (Spec §6.3.4), then branch if it is now less than the value */
    case 5:  /* inc_chk: the same, branching if now greater */
        {
            uint8_t variable = variable_reference(inst, ops[0]);
            uint16_t value = read_variable(m, variable, 1);

            value = (uint16_t)(inst->number == 4 ? value - 1u : value + 1u);
            write_variable(m, variable, value, 1);
            branch(m, inst, inst->number == 4 ? as_signed(value) < as_signed(ops[1])
                                              : as_signed(value) > as_signed(ops[1]));
        }
        break;
    case 6:  /* jin: branch if the first object's parent is the second */
        branch(m, inst, (ops[0] == 0 ? 0 : zm_object_parent(m->mem, ops[0])) == ops[1]);
        break;
    case 7:  /* test: branch if every flag in the second operand is set
              * in the first */
        branch(m, inst, (ops[0] & ops[1]) == ops[1]);
        break;
    case 8:  /* or: bitwise, so unsigned (Spec §2.2.1) */
        store_result(m, inst, ops[0] | ops[1]);
        break;
    case 9:  /* and */
        store_result(m, inst, ops[0] & ops[1]);
        break;
    case 10: /* test_attr */
        branch(m, inst, ops[0] != 0 && zm_object_test_attribute(m->mem, ops[0], ops[1]));
        break;
    case 11: /* set_attr */
        zm_object_set_attribute(m->mem, object_to_change(m, inst, ops[0]), ops[1]);
        break;
    case 12: /* clear_attr */
        zm_object_clear_attribute(m->mem, object_to_change(m, inst, ops[0]), ops[1]);
        break;
    case 13: /* store */
        write_variable(m, variable_reference(inst, ops[0]), ops[1], 1);
        break;
    case 14: /* insert_obj */
        zm_object_insert(m->mem, object_to_change(m, inst, ops[0]), object_to_change(m, inst, ops[1]));
        break;
    case 15: /* loadw: the word at array + 2 * index. Addresses are 16
              * bits, so the sum wraps, which also lets an index be
              * negative. */
        store_result(m, inst, zm_read_word(m->mem, (uint16_t)(ops[0] + 2u * ops[1])));
        break;
    case 16: /* loadb */
        store_result(m, inst, zm_read_byte(m->mem, (uint16_t)(ops[0] + ops[1])));
        break;
    case 17: /* get_prop: the value, or the default if the object has no
              * such property (Spec §12.2) */
        {
            zm_property prop;

            if (!find_property(m, inst, ops[0], ops[1], &prop)) {
                store_result(m, inst, zm_property_default(m->mem, ops[1]));
            } else {
                check_property_is_a_value(inst, ops[0], &prop);
                store_result(m, inst, prop.length == 1 ? zm_read_byte(m->mem, prop.address)
                                                       : zm_read_word(m->mem, prop.address));
            }
        }
        break;
    case 18: /* get_prop_addr: the address of the data, or 0 */
        {
            zm_property prop;

            store_result(m, inst, find_property(m, inst, ops[0], ops[1], &prop) ? (uint16_t)prop.address : 0u);
        }
        break;
    case 19: /* get_next_prop: the number after the given one in the
              * object's list, the first for 0, and 0 at the end */
        {
            zm_property prop;

            if (ops[0] == 0) {
                store_result(m, inst, 0);
            } else if (ops[1] == 0) {
                zm_property_decode(m->mem, zm_object_first_property(m->mem, ops[0]), &prop);
                store_result(m, inst, prop.number);
            } else if (!find_property(m, inst, ops[0], ops[1], &prop)) {
                /* Spec §15 get_next_prop: illegal, and cheap to check. */
                zm_fatal("get_next_prop at $%05x asks what follows property %u of object %u, which has no such property",
                         (unsigned)inst->address, (unsigned)ops[1], (unsigned)ops[0]);
            } else {
                zm_property_decode(m->mem, prop.next, &prop);
                store_result(m, inst, prop.number);
            }
        }
        break;
    case 20: /* add */
        store_result(m, inst, as_word(as_signed(ops[0]) + as_signed(ops[1])));
        break;
    case 21: /* sub */
        store_result(m, inst, as_word(as_signed(ops[0]) - as_signed(ops[1])));
        break;
    case 22: /* mul */
        store_result(m, inst, as_word(as_signed(ops[0]) * as_signed(ops[1])));
        break;
    case 23: /* div */
        store_result(m, inst, divide(inst, ops[0], ops[1], 0));
        break;
    case 24: /* mod */
        store_result(m, inst, divide(inst, ops[0], ops[1], 1));
        break;
    case 25: /* call_2s */
        call_routine(m, ops[0], ops + 1, 1, inst->store);
        break;
    case 26: /* call_2n */
        call_routine(m, ops[0], ops + 1, 1, -1);
        break;
    default:
        not_implemented(m, inst);
    }
}

static void execute_var(zm_machine *m, const zm_instruction *inst, const uint16_t *ops, unsigned count)
{
    switch (inst->number) {
    case 0:  /* call / call_vs */
        if (count == 0) {
            zm_fatal("call with no routine at $%05x", (unsigned)inst->address);
        }
        call_routine(m, ops[0], ops + 1, count - 1, inst->store);
        break;
    case 1:  /* storew: dynamic memory only, which zm_write_word enforces */
        zm_write_word(m->mem, (uint16_t)(ops[0] + 2u * ops[1]), ops[2]);
        break;
    case 2:  /* storeb */
        zm_write_byte(m->mem, (uint16_t)(ops[0] + ops[1]), (uint8_t)ops[2]);
        break;
    case 3:  /* put_prop: a 1-byte property takes the low byte */
        {
            zm_property prop;

            if (!find_property(m, inst, ops[0], ops[1], &prop)) {
                /* Spec §15 put_prop: there is no default to write to. */
                zm_fatal("put_prop at $%05x to object %u, which has no property %u",
                         (unsigned)inst->address, (unsigned)ops[0], (unsigned)ops[1]);
            }
            check_property_is_a_value(inst, ops[0], &prop);
            if (prop.length == 1) {
                zm_write_byte(m->mem, prop.address, (uint8_t)ops[2]);
            } else {
                zm_write_word(m->mem, prop.address, ops[2]);
            }
        }
        break;
    case 5:  /* print_char */
        print_zscii(m, ops[0]);
        break;
    case 6:  /* print_num */
        print_number(m, ops[0]);
        break;
    case 7:  /* random: a positive range draws; a negative one seeds the
              * predictable sequence and gives 0; 0 returns to random mode
              * (Spec §15 random) */
        if (as_signed(ops[0]) > 0) {
            store_result(m, inst, zm_machine_random(m, ops[0]));
        } else {
            zm_machine_seed_random(m, as_word(-as_signed(ops[0])));
            store_result(m, inst, 0);
        }
        break;
    case 8:  /* push */
        push(m, ops[0]);
        break;
    case 9:  /* pull */
        if (m->mem->version == 6) {
            /* Version 6 pulls from a user stack if one is given (Spec
             * §6.6) and stores the result. */
            if (count > 0) {
                not_implemented(m, inst);
            }
            store_result(m, inst, pop(m));
        } else {
            /* The pop happens first, so pulling to the stack replaces
             * the new top in place (Spec §6.3.4). */
            uint16_t value = pop(m);
            write_variable(m, variable_reference(inst, ops[0]), value, 1);
        }
        break;
    case 12: /* call_vs2 */
        if (count == 0) {
            zm_fatal("call_vs2 with no routine at $%05x", (unsigned)inst->address);
        }
        call_routine(m, ops[0], ops + 1, count - 1, inst->store);
        break;
    case 25: /* call_vn */
    case 26: /* call_vn2 */
        if (count == 0) {
            zm_fatal("%s with no routine at $%05x", inst->info->name, (unsigned)inst->address);
        }
        call_routine(m, ops[0], ops + 1, count - 1, -1);
        break;
    case 24: /* not, moved here from 1OP:15 in Version 5 */
        store_result(m, inst, (uint16_t)~ops[0]);
        break;
    case 31: /* check_arg_count: branch if the current routine was given
              * at least that many arguments (Spec §6.4.4.1) */
        branch(m, inst, ops[0] <= current_frame(m)->argument_count);
        break;
    default:
        not_implemented(m, inst);
    }
}

static void execute_ext(zm_machine *m, const zm_instruction *inst, const uint16_t *ops)
{
    switch (inst->number) {
    case 2:  /* log_shift */
        store_result(m, inst, shift(inst, ops[0], ops[1], 0));
        break;
    case 3:  /* art_shift */
        store_result(m, inst, shift(inst, ops[0], ops[1], 1));
        break;
    default:
        not_implemented(m, inst);
    }
}

void zm_machine_step(zm_machine *m)
{
    zm_instruction inst;
    uint16_t ops[ZM_MAX_OPERANDS];

    zm_instruction_decode(m->mem, m->pc, &inst);

    if (inst.info == NULL) {
        /* Spec §14.2.1: unknown extended opcodes are ignored. Anything
         * else unknown is illegal in this Version (Spec §14.2). */
        if (inst.op_class == ZM_OP_EXT) {
            m->pc = inst.next;
            m->instruction_count++;
            return;
        }
        zm_fatal("illegal opcode %s:%u (%s form, byte $%02x) at $%05x for Version %u",
                 zm_opcode_class_name(inst.op_class), (unsigned)inst.number,
                 zm_form_name(inst.form), (unsigned)zm_read_byte(m->mem, inst.address),
                 (unsigned)inst.address, (unsigned)m->mem->version);
    }

    /* The PC moves on before the instruction acts, so calls record the
     * right return address and branches have the right base. */
    m->pc = inst.next;
    m->instruction_count++;

    /* Spec §4.5.2: operands are evaluated first to last, which matters
     * when more than one of them pops the stack. */
    for (unsigned i = 0; i < inst.operand_count; i++) {
        const zm_operand *op = &inst.operands[i];
        ops[i] = op->type == ZM_OPERAND_VARIABLE ? read_variable(m, (uint8_t)op->value, 0) : op->value;
    }

    switch (inst.op_class) {
    case ZM_OP_0OP: execute_0op(m, &inst); break;
    case ZM_OP_1OP: execute_1op(m, &inst, ops); break;
    case ZM_OP_2OP: execute_2op(m, &inst, ops, inst.operand_count); break;
    case ZM_OP_VAR: execute_var(m, &inst, ops, inst.operand_count); break;
    case ZM_OP_EXT: execute_ext(m, &inst, ops); break;
    }
}

void zm_machine_run(zm_machine *m)
{
    while (m->running) {
        zm_machine_step(m);
    }
}

/*
 * Set-up.
 */

zm_status zm_machine_init(zm_machine *m, zm_memory *mem, const zm_io *io)
{
    zm_header h;

    memset(m, 0, sizeof *m);

    m->stack = malloc(ZM_STACK_WORDS * sizeof *m->stack);
    m->frames = malloc(ZM_MAX_FRAMES * sizeof *m->frames);
    if (m->stack == NULL || m->frames == NULL) {
        free(m->stack);
        free(m->frames);
        memset(m, 0, sizeof *m);
        return ZM_ERROR_OUT_OF_MEMORY;
    }

    m->mem = mem;
    m->io = io;
    m->running = 1;

    zm_header_read(mem, &h);
    m->globals = h.globals;

    /* Spec §2.4: the generator starts in "random" mode. */
    zm_machine_seed_random(m, 0);

    /* Frame 0 is the starting environment, which has no locals and no
     * caller (Spec §5.5). Its stack base is 0, so the stack starts
     * empty. */
    m->depth = 1;
    memset(&m->frames[0], 0, sizeof m->frames[0]);
    m->frames[0].result_variable = -1;

    if (mem->version == 6) {
        /* Spec §5.4: Version 6 begins by calling the main routine. It is
         * entered here directly, so that it is frame 0 and the rule that
         * frame 0 cannot be returned from covers it too. */
        m->depth = 0;
        enter_routine(m, zm_unpack_routine(mem, h.initial_pc), NULL, 0);
    } else {
        /* Spec §5.5: elsewhere the header gives the first instruction. */
        m->pc = h.initial_pc;
    }

    return ZM_OK;
}

void zm_machine_free(zm_machine *m)
{
    free(m->stack);
    free(m->frames);
    memset(m, 0, sizeof *m);
}
