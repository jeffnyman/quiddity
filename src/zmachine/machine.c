/*
 * machine.c: see zm/machine.h. Implements Spec §5, §6, and the opcodes of
 * Spec §15 that have been reached so far, with the arithmetic of Spec §2,
 * the object table of Spec §12, the dictionary of Spec §13, and the
 * screen of Spec §7 and §8 as the opcodes need them.
 *
 * The file is in five parts: the stack and variables (§6.2, §6.3), routine
 * calls and returns (§5, §6.4), the state of play as a whole (§6.1:
 * snapshots, restart, undo), the helpers every opcode shares (store,
 * branch, printing, arithmetic, random numbers, objects, input, the
 * screen, files), and the dispatch itself.
 */

#include "zm/machine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "zm/dictionary.h"
#include "zm/fatal.h"
#include "zm/header.h"
#include "zm/instruction.h"
#include "zm/object.h"
#include "zm/quetzal.h"
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

/* What a routine finds when it pops its own empty stack.
 *
 * Spec §6.3.1 says the stack is empty at the start of each routine, so a
 * routine cannot pull what its caller pushed, and a game that tries has a
 * bug. Infocom's games have a few. Zork I release 2 pops a routine's
 * result with "je (sp)+ 1" at $5851 and then, at $585b, pulls it again
 * from the now-empty stack. It got away with this because Infocom's
 * interpreters, and Frotz after them, keep a routine's local variables
 * on the game stack directly above its working area (see variable.c in
 * Frotz, where local n is at fp - n and pull reads *sp without a check),
 * so the pop quietly read the routine's highest-numbered local. Quiddity
 * keeps locals in the frame, so to run those games it supplies the same
 * word on purpose, one word deep. A routine with no locals has nothing
 * there, and faults. */
static uint16_t *underflow_slot(zm_machine *m)
{
    zm_frame *f = current_frame(m);

    if (f->local_count == 0) {
        zm_fatal("stack underflow at $%05x", (unsigned)m->pc);
    }
    return &f->locals[f->local_count - 1];
}

static uint16_t pop(zm_machine *m)
{
    if (m->sp <= current_frame(m)->stack_base) {
        return *underflow_slot(m);
    }
    return m->stack[--m->sp];
}

/* The top of the stack, for the opcodes that read or write it in place. */
static uint16_t *stack_top(zm_machine *m)
{
    if (m->sp <= current_frame(m)->stack_base) {
        return underflow_slot(m);
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
     * the caller's local. A routine the interpreter called returns its
     * value to the interpreter. */
    if (result_variable >= 0) {
        write_variable(m, (uint8_t)result_variable, value, 0);
    } else if (result_variable == ZM_RESULT_INTERRUPT) {
        m->interrupt_result = value;
    }
}

/*
 * The state of play (Spec §6.1).
 */

/* Put the machine at the start of the story: frame 0 with an empty
 * stack, and the PC at the first instruction (Spec §5.5) or, in Version
 * 6, inside the main routine (Spec §5.4). */
static void begin_execution(zm_machine *m)
{
    zm_header h;

    zm_header_read(m->mem, &h);
    m->sp = 0;

    /* Frame 0 is the starting environment, which has no locals and no
     * caller. Its stack base is 0, so the stack starts empty. */
    m->depth = 1;
    memset(&m->frames[0], 0, sizeof m->frames[0]);
    m->frames[0].result_variable = -1;

    if (m->mem->version == 6) {
        /* Version 6 begins by calling the main routine. It is entered
         * here directly, so that it is frame 0 and the rule that frame 0
         * cannot be returned from covers it too. */
        m->depth = 0;
        enter_routine(m, zm_unpack_routine(m->mem, h.initial_pc), NULL, 0);
    } else {
        m->pc = h.initial_pc;
    }
}

/* Replace dynamic memory wholesale while keeping Flags 2, which Spec
 * §6.1.2 and §6.1.3 have survive a restore, an undo, and a restart: its
 * bits describe the player's session (transcript on, fixed pitch
 * wanted), not the story's state. */
static void replace_memory(zm_machine *m, const uint8_t *memory)
{
    uint16_t flags2 = zm_read_word(m->mem, ZM_HEADER_FLAGS2);

    memcpy(m->mem->bytes, memory, m->mem->static_base);
    zm_write_word(m->mem, ZM_HEADER_FLAGS2, flags2);
}

void zm_machine_restart(zm_machine *m)
{
    /* Spec §15 restart, §6.1.3: back to the story file's dynamic memory,
     * an empty stack, and the first instruction; the header's Rst fields
     * are reset and the screen cleared by zm_screen_restart(). */
    replace_memory(m, m->mem->original);
    begin_execution(m);
    zm_screen_restart(&m->screen);
}

void zm_snapshot_free(zm_snapshot *snapshot)
{
    free(snapshot->memory);
    free(snapshot->stack);
    free(snapshot->frames);
    memset(snapshot, 0, sizeof *snapshot);
}

zm_status zm_machine_take_snapshot(const zm_machine *m, uint32_t pc, zm_snapshot *snapshot)
{
    memset(snapshot, 0, sizeof *snapshot);
    snapshot->memory = malloc(m->mem->static_base);
    snapshot->stack = malloc((m->sp > 0 ? m->sp : 1) * sizeof *snapshot->stack);
    snapshot->frames = malloc(m->depth * sizeof *snapshot->frames);
    if (snapshot->memory == NULL || snapshot->stack == NULL || snapshot->frames == NULL) {
        zm_snapshot_free(snapshot);
        return ZM_ERROR_OUT_OF_MEMORY;
    }
    memcpy(snapshot->memory, m->mem->bytes, m->mem->static_base);
    memcpy(snapshot->stack, m->stack, m->sp * sizeof *snapshot->stack);
    memcpy(snapshot->frames, m->frames, m->depth * sizeof *snapshot->frames);
    snapshot->sp = m->sp;
    snapshot->depth = m->depth;
    snapshot->pc = pc;
    return ZM_OK;
}

void zm_machine_apply_snapshot(zm_machine *m, const zm_snapshot *snapshot)
{
    replace_memory(m, snapshot->memory);
    memcpy(m->stack, snapshot->stack, snapshot->sp * sizeof *m->stack);
    memcpy(m->frames, snapshot->frames, snapshot->depth * sizeof *m->frames);
    m->sp = snapshot->sp;
    m->depth = snapshot->depth;
    m->pc = snapshot->pc;

    /* Spec §6.1.2.2: the game may have been saved by another
     * interpreter, so the header says who is running it now. */
    zm_screen_write_header(&m->screen);
}

/* Spec §15 save_undo: keep the state of play in memory. The ring holds
 * the last ZM_UNDO_LEVELS of them; when it is full the oldest goes. */
static int save_undo(zm_machine *m, uint32_t pc)
{
    zm_snapshot *slot = &m->undo[m->undo_next];

    zm_snapshot_free(slot);
    if (zm_machine_take_snapshot(m, pc, slot) != ZM_OK) {
        return 0;
    }
    m->undo_next = (m->undo_next + 1) % ZM_UNDO_LEVELS;
    if (m->undo_count < ZM_UNDO_LEVELS) {
        m->undo_count++;
    }
    return 1;
}

/* Spec §15 restore_undo: go back to the newest state kept, which is
 * then used up, so that a second undo goes back a step further. */
static int restore_undo(zm_machine *m)
{
    zm_snapshot *slot;

    if (m->undo_count == 0) {
        return 0;
    }
    m->undo_next = (m->undo_next + ZM_UNDO_LEVELS - 1) % ZM_UNDO_LEVELS;
    m->undo_count--;
    slot = &m->undo[m->undo_next];
    zm_machine_apply_snapshot(m, slot);
    zm_snapshot_free(slot);
    return 1;
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

/* Everything printed goes through the output streams (Spec §7). */
static void print_zscii(zm_machine *m, uint16_t zscii)
{
    zm_screen_print(&m->screen, zscii);
}

static void print_sink(void *context, uint16_t zscii)
{
    print_zscii(context, zscii);
}

static void print_string(zm_machine *m, uint32_t address)
{
    zm_text_decode(m->mem, address, print_sink, m);
}

/* A message of the interpreter's own, in ASCII, sent where the game's
 * text goes so that it lands on the screen and in the transcript. */
static void print_ascii(zm_machine *m, const char *text)
{
    for (; *text != '\0'; text++) {
        print_zscii(m, (uint16_t)(unsigned char)*text);
    }
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
 * The status line (Spec §8.2) and input (Spec §10, §13, §15 read).
 */

typedef struct {
    uint16_t zscii[256];
    unsigned count;
} zscii_buffer;

static void buffer_sink(void *context, uint16_t zscii)
{
    zscii_buffer *b = context;

    if (b->count < 256) {
        b->zscii[b->count++] = zscii;
    }
}

static void show_status_line(zm_machine *m)
{
    zscii_buffer name;
    uint16_t object;
    int time_game;

    if (m->io->show_status == NULL) {
        return;
    }

    /* Spec §8.2.2: the first global holds the location; Spec §8.2.3: the
     * next two hold the score and turns, or the hours and minutes. Spec
     * §8.2.2.1 asks interpreters to protect themselves against a global
     * that is not an object, and a fault naming it is that protection. */
    object = zm_read_word(m->mem, m->globals);
    if (object == 0 || object > zm_object_max(m->mem)) {
        zm_fatal("the status line's location, global 0, is object %u, which does not exist", (unsigned)object);
    }
    name.count = 0;
    if (zm_object_name_length(m->mem, object) > 0) {
        zm_text_decode(m->mem, zm_object_name(m->mem, object), buffer_sink, &name);
    }

    /* Spec §8.2.1: Versions 1 and 2 are score games; Version 3 says which
     * it is in Flags 1. */
    time_game = m->mem->version == 3
             && (zm_read_byte(m->mem, ZM_HEADER_FLAGS1) & ZM_FLAGS1_V3_STATUS_TIME) != 0;

    m->io->show_status(m->io->context, name.zscii, name.count, time_game,
                       as_signed(zm_read_word(m->mem, m->globals + 2u)),
                       as_signed(zm_read_word(m->mem, m->globals + 4u)));
}

/* The front end has no more input to give, so the game ends as if it
 * had quit. A story driven from a file of commands ends this way. */
static void end_of_input(zm_machine *m)
{
    zm_screen_flush(&m->screen);
    m->running = 0;
}

/* Spec §15 read: with a time and a routine, the routine is called every
 * time/10 seconds while the game waits for input, and may end the input
 * by returning true. The routine runs here, inside the read instruction,
 * on top of the game's own call stack: it is entered as any routine is,
 * instructions are stepped until it returns, and its value is collected
 * from where return_from_routine() puts it. Returns whether the input
 * is to stop. A routine that quits stops everything, and the caller
 * must look at m->running. */
static int run_interrupt(zm_machine *m, uint16_t routine)
{
    uint32_t depth = m->depth;

    if (routine == 0) {
        return 0;
    }
    enter_routine(m, zm_unpack_routine(m->mem, routine), NULL, 0)->result_variable = ZM_RESULT_INTERRUPT;
    m->interrupt_result = 0;
    while (m->running && m->depth > depth) {
        zm_machine_step(m);
    }
    return m->running && m->interrupt_result != 0;
}

/* The time and routine operands of read and read_char, which only
 * count together (Spec §15 read): a time with no routine is no timer. */
static unsigned timeout_of(const uint16_t *ops, unsigned count, unsigned time_index, uint16_t *routine)
{
    *routine = count > time_index + 1 ? ops[time_index + 1] : 0;
    return count > time_index && *routine != 0 ? ops[time_index] : 0;
}

static void read_line_opcode(zm_machine *m, const zm_instruction *inst, const uint16_t *ops, unsigned count)
{
    uint32_t text = ops[0];
    uint32_t parse = count > 1 ? ops[1] : 0;
    uint16_t routine;
    unsigned timeout = timeout_of(ops, count, 2, &routine);
    unsigned room;
    unsigned letters;
    unsigned have = 0;
    unsigned typed;
    uint16_t line[256];
    uint16_t terminator = ZM_ZSCII_NEWLINE;
    int interrupted = 0;

    if (m->io->read_line == NULL) {
        zm_fatal("%s at $%05x: the front end cannot read a line", inst->info->name, (unsigned)inst->address);
    }

    /* Spec §15 read: byte 0 of the text buffer is its room. Before
     * Version 5 it counts the zero terminator, so one fewer letter fits.
     * The Standard asks for a halt on buffers too small to be
     * plausible, since they are usually the wreckage of an overrun. */
    room = zm_read_byte(m->mem, text);
    if (m->mem->version <= 4) {
        if (room < 3) {
            zm_fatal("%s at $%05x: text buffer at $%04x has room for %u bytes",
                     inst->info->name, (unsigned)inst->address, (unsigned)text, room);
        }
        letters = room - 1u;
        if (parse == 0) {
            zm_fatal("%s at $%05x has no parse buffer", inst->info->name, (unsigned)inst->address);
        }
    } else {
        if (room < 1) {
            zm_fatal("%s at $%05x: text buffer at $%04x has room for nothing",
                     inst->info->name, (unsigned)inst->address, (unsigned)text);
        }
        letters = room;

        /* Spec §15 read: from Version 5, byte 1 may already count
         * characters left over from an interrupted read; the new ones go
         * after them. */
        have = zm_read_byte(m->mem, text + 1u);
        if (have > letters) {
            have = letters;
        }
    }
    if (parse != 0 && zm_read_byte(m->mem, parse) == 0) {
        zm_fatal("%s at $%05x: parse buffer at $%04x has room for no words",
                 inst->info->name, (unsigned)inst->address, (unsigned)parse);
    }

    for (unsigned i = 0; i < have; i++) {
        line[i] = zm_read_byte(m->mem, text + 2u + i);
    }
    typed = have;

    /* Spec §8.2.4, §10.5.1. */
    if (m->mem->version <= 3) {
        show_status_line(m);
    }

    /* Spec §15 read: the wait is broken up by the timer, if there is
     * one, and goes on after an interrupt routine that returns false.
     * Spec §8.8.3.2.7: anything still buffered is shown first. */
    zm_screen_flush(&m->screen);
    for (;;) {
        int got = m->io->read_line(m->io->context, line, &typed, letters, &terminator, timeout);

        if (typed > letters) {
            typed = letters;
        }
        if (got == ZM_INPUT_END) {
            end_of_input(m);
            return;
        }
        if (got != ZM_INPUT_TIMEOUT) {
            break;
        }
        if (run_interrupt(m, routine)) {
            interrupted = 1;
            break;
        }
        if (!m->running) {
            return;
        }
    }

    /* Spec §7.1.1.1: in Version 6 the interpreter shows the input in the
     * window, as typed; the line ends if the input did. */
    zm_screen_show_input(&m->screen, line + have, typed - have, !interrupted && terminator == ZM_ZSCII_NEWLINE);

    if (interrupted) {
        /* Spec §15 read: the input stops with terminator 0. What was
         * typed stays in the buffer, and from Version 5 is counted in
         * byte 1, so that the next read can carry on from it (the
         * "characters left over" of Spec §15 read, which 'Beyond Zork'
         * and 'Zork Zero' rely on): it is not lexed or folded. */
        terminator = 0;
    } else {
        zm_screen_echo_command(&m->screen, line + have, typed - have);

        /* Spec §15 read: the text is reduced to lower case. Only the
         * ASCII letters are folded here; what lower case means for the
         * extra characters (Spec §3.8.5) depends on the story's Unicode
         * table, and no game is known to need it. */
        for (unsigned i = 0; i < typed; i++) {
            if (line[i] >= 'A' && line[i] <= 'Z') {
                line[i] = (uint16_t)(line[i] + 32u);
            }
        }
    }

    if (m->mem->version <= 4) {
        for (unsigned i = 0; i < typed; i++) {
            zm_write_byte(m->mem, text + 1u + i, (uint8_t)line[i]);
        }
        zm_write_byte(m->mem, text + 1u + typed, 0);
    } else {
        for (unsigned i = 0; i < typed; i++) {
            zm_write_byte(m->mem, text + 2u + i, (uint8_t)line[i]);
        }
        zm_write_byte(m->mem, text + 1u, (uint8_t)typed);
    }

    /* Spec §13.6: lexical analysis, with the game's own dictionary. From
     * Version 5 a parse buffer of 0 asks for none. */
    if (parse != 0 && !interrupted) {
        zm_dictionary dict;

        zm_dictionary_read(m->mem, m->dictionary, &dict);
        zm_lexer_tokenise(m->mem, text, parse, &dict, 0);
    }

    /* Spec §15 read: from Version 5 the result is the terminating
     * character, 13 for a new-line however the keyboard spelt it, and 0
     * for an input the timer ended. */
    if (m->mem->version >= 5) {
        store_result(m, inst, terminator);
    }
}

static void read_char_opcode(zm_machine *m, const zm_instruction *inst, const uint16_t *ops, unsigned count)
{
    uint16_t routine;
    unsigned timeout = timeout_of(ops, count, 1, &routine);
    int c;

    /* Spec §15 read_char: the first operand is the input device, and
     * only the keyboard, 1, was ever built. 'Wyrmward' leaves the
     * operand out altogether, and since there is nothing else it could
     * mean, so may anyone. */
    if (count >= 1 && ops[0] != 1) {
        zm_fatal("read_char at $%05x from device %u; only device 1 exists",
                 (unsigned)inst->address, (unsigned)ops[0]);
    }
    if (m->io->read_char == NULL) {
        zm_fatal("read_char at $%05x: the front end cannot read a key", (unsigned)inst->address);
    }

    /* Spec §15 read_char: the timer works as for read, and a wait the
     * routine ends gives 0. */
    zm_screen_flush(&m->screen);
    for (;;) {
        c = m->io->read_char(m->io->context, timeout);
        if (c == ZM_INPUT_END) {
            end_of_input(m);
            return;
        }
        if (c != ZM_INPUT_TIMEOUT) {
            break;
        }
        if (run_interrupt(m, routine)) {
            c = 0;
            break;
        }
        if (!m->running) {
            return;
        }
    }
    if (c != 0) {
        zm_screen_echo_key(&m->screen, (uint16_t)c);
    }
    store_result(m, inst, (uint16_t)c);
}

/*
 * The screen and the output streams (Spec §7, §8), whose state lives in
 * zm/screen.h. The opcodes here check their operands and pass the
 * decision on. Version 6 gives most of them extra operands and other
 * meanings (Spec §8.8): a window to act on, where -3 is the current one,
 * coordinates in units instead of lines and columns, and more windows
 * than two.
 */

static int is_v6(const zm_machine *m)
{
    return m->mem->version == 6;
}

/* Spec §8.8.3: a window operand is 0 to 7, or -3 for the current window,
 * which an absent optional operand also means. */
static unsigned window_operand(zm_machine *m, const zm_instruction *inst, const uint16_t *ops, unsigned count,
                               unsigned index)
{
    int16_t window = count > index ? as_signed(ops[index]) : (int16_t)ZM_WINDOW_CURRENT;

    if (window == ZM_WINDOW_CURRENT) {
        return m->screen.window;
    }
    if (window < 0 || window >= ZM_WINDOWS) {
        zm_fatal("%s at $%05x: window %d does not exist", inst->info->name, (unsigned)inst->address, (int)window);
    }
    return (unsigned)window;
}

static void output_stream_opcode(zm_machine *m, const zm_instruction *inst, const uint16_t *ops, unsigned count)
{
    int16_t stream = count > 0 ? as_signed(ops[0]) : 0;

    if (stream < -4 || stream > 4) {
        zm_fatal("output_stream %d at $%05x: there is no such stream", (int)stream, (unsigned)inst->address);
    }
    if (stream == 3 && count < 2) {
        zm_fatal("output_stream 3 at $%05x names no table to write to", (unsigned)inst->address);
    }
    /* Spec §15 output_stream: the Version 6 form may add a width, which
     * formats the table into lines. */
    zm_screen_select_stream(&m->screen, stream, count > 1 ? ops[1] : 0,
                            count > 2 ? as_signed(ops[2]) : 0, is_v6(m) && count > 2);
}

static void set_colour_opcode(zm_machine *m, const zm_instruction *inst, const uint16_t *ops, unsigned count)
{
    unsigned window;

    if (count < 2) {
        zm_fatal("set_colour with %u operand(s) at $%05x", count, (unsigned)inst->address);
    }
    window = is_v6(m) ? window_operand(m, inst, ops, count, 2) : m->screen.window;

    /* Spec §8.3.1: codes 0 to 12, and in Version 6 also -1, the colour
     * under the cursor, and 15, transparent; see zm/screen.h on the
     * greys. 13 and 14 are reserved. */
    for (unsigned i = 0; i < 2; i++) {
        int16_t colour = as_signed(ops[i]);
        int allowed = (colour >= 0 && colour <= ZM_COLOUR_DARK_GREY)
                   || (is_v6(m) && (colour == -1 || colour == ZM_COLOUR_TRANSPARENT));

        if (!allowed) {
            zm_fatal("set_colour at $%05x: %d is not a colour%s", (unsigned)inst->address, (int)colour,
                     is_v6(m) ? "" : " before Version 6");
        }
    }
    zm_screen_set_colour(&m->screen, window, as_signed(ops[0]), as_signed(ops[1]));
}

static void set_true_colour_opcode(zm_machine *m, const zm_instruction *inst, const uint16_t *ops, unsigned count)
{
    unsigned window;

    if (count < 2) {
        zm_fatal("set_true_colour with %u operand(s) at $%05x", count, (unsigned)inst->address);
    }
    window = is_v6(m) ? window_operand(m, inst, ops, count, 2) : m->screen.window;

    /* Spec §8.3.7: -1 and -2 are requests in every Version; -3 and -4,
     * the colour under the cursor and transparent, in Version 6 only. */
    for (unsigned i = 0; i < 2; i++) {
        int16_t colour = as_signed(ops[i]);

        if (colour < (is_v6(m) ? -4 : -2)) {
            zm_fatal("set_true_colour at $%05x: %d is not a colour%s", (unsigned)inst->address, (int)colour,
                     is_v6(m) ? "" : " before Version 6");
        }
    }
    zm_screen_set_true_colour(&m->screen, window, as_signed(ops[0]), as_signed(ops[1]));
}

static void set_cursor_opcode(zm_machine *m, const zm_instruction *inst, const uint16_t *ops, unsigned count)
{
    /* Spec §15 set_cursor: the Version 6 forms that turn the cursor off
     * and on may come with the one operand ("set_cursor -1"). */
    if (count < 1 || (count < 2 && !(is_v6(m) && as_signed(ops[0]) < 0))) {
        zm_fatal("set_cursor with %u operand(s) at $%05x", count, (unsigned)inst->address);
    }
    if (is_v6(m)) {
        zm_screen_set_cursor(&m->screen, window_operand(m, inst, ops, count, 2), as_signed(ops[0]),
                             count > 1 ? as_signed(ops[1]) : 0);
        return;
    }

    /* Spec §15 set_cursor: a negative row turns the cursor off or on,
     * in Version 6 only. */
    if (as_signed(ops[0]) < 0) {
        zm_fatal("set_cursor at $%05x: row %d is only meaningful in Version 6",
                 (unsigned)inst->address, (int)as_signed(ops[0]));
    }
    zm_screen_set_cursor(&m->screen, m->screen.window, as_signed(ops[0]), as_signed(ops[1]));
}

static void erase_window_opcode(zm_machine *m, const zm_instruction *inst, const uint16_t *ops)
{
    int16_t window = as_signed(ops[0]);

    if (is_v6(m)) {
        /* Spec §15 erase_window: -1, -2, a window, or -3 for the current
         * one. */
        if (window == ZM_WINDOW_CURRENT) {
            window = (int16_t)m->screen.window;
        } else if (window < -2 || window >= ZM_WINDOWS) {
            zm_fatal("erase_window %d at $%05x: there is no such window", (int)window, (unsigned)inst->address);
        }
    } else if (window < -2 || window > ZM_WINDOW_UPPER) {
        zm_fatal("erase_window %d at $%05x: only -2, -1, 0 and 1 are meaningful before Version 6",
                 (int)window, (unsigned)inst->address);
    }
    zm_screen_erase_window(&m->screen, window);
}

static void set_window_opcode(zm_machine *m, const zm_instruction *inst, const uint16_t *ops, unsigned count)
{
    if (is_v6(m)) {
        zm_screen_set_window(&m->screen, window_operand(m, inst, ops, count, 0));
        return;
    }
    if (ops[0] > ZM_WINDOW_UPPER) {
        zm_fatal("set_window %u at $%05x: only windows 0 and 1 exist before Version 6",
                 (unsigned)ops[0], (unsigned)inst->address);
    }
    zm_screen_set_window(&m->screen, ops[0]);
}

/*
 * Version 6 windows and pictures (Spec §8.8, §15).
 */

static void picture_data_opcode(zm_machine *m, const zm_instruction *inst, const uint16_t *ops, unsigned count)
{
    uint16_t height;
    uint16_t width;
    int available;

    if (count < 2) {
        zm_fatal("picture_data with %u operand(s) at $%05x", count, (unsigned)inst->address);
    }
    /* Spec §15 picture_data: the height and width of a picture, or for
     * picture 0 the count and the release; the branch says whether
     * there was anything to report. */
    available = zm_screen_picture_data(&m->screen, ops[0], &height, &width);
    zm_write_word(m->mem, ops[1], height);
    zm_write_word(m->mem, (uint16_t)(ops[1] + 2u), width);
    branch(m, inst, available);
}

static void window_style_opcode(zm_machine *m, const zm_instruction *inst, const uint16_t *ops, unsigned count)
{
    if (count < 2) {
        zm_fatal("window_style with %u operand(s) at $%05x", count, (unsigned)inst->address);
    }
    if (count > 2 && ops[2] > 3) {
        zm_fatal("window_style at $%05x: operation %u does not exist", (unsigned)inst->address, (unsigned)ops[2]);
    }
    zm_screen_style_window(&m->screen, window_operand(m, inst, ops, count, 0), ops[1], count > 2 ? ops[2] : 0u);
}

/* Spec §15 print_form: lines of text as a formatted stream 3 wrote them,
 * each a count word and that many characters, ended by a count of 0. */
static void print_form_opcode(zm_machine *m, const uint16_t *ops)
{
    uint32_t address = ops[0];
    int first = 1;

    for (;;) {
        uint16_t length = zm_read_word(m->mem, address);

        address += 2;
        if (length == 0) {
            break;
        }
        if (!first) {
            print_zscii(m, ZM_ZSCII_NEWLINE);
        }
        for (unsigned i = 0; i < length; i++) {
            print_zscii(m, zm_read_byte(m->mem, address++));
        }
        first = 0;
    }
}

/* Spec §15 read_mouse: the mouse's position, buttons, and menu choice.
 * No front end has a mouse yet, and the header says so, so there is
 * nothing to report. */
static void read_mouse_opcode(zm_machine *m, const uint16_t *ops)
{
    for (unsigned i = 0; i < 4; i++) {
        zm_write_word(m->mem, (uint16_t)(ops[0] + 2u * i), 0);
    }
}

/*
 * User stacks (Spec §6.6): a table of words in dynamic memory whose
 * first word counts the spare slots, so that pushing fills the table
 * from its far end towards the count.
 */

static void push_stack_opcode(zm_machine *m, const zm_instruction *inst, const uint16_t *ops, unsigned count)
{
    uint16_t spare;

    if (count < 2) {
        zm_fatal("push_stack with %u operand(s) at $%05x", count, (unsigned)inst->address);
    }
    /* Spec §15 push_stack: a full stack takes nothing and the branch is
     * not made; "this is not an error condition". */
    spare = zm_read_word(m->mem, ops[1]);
    if (spare == 0) {
        branch(m, inst, 0);
        return;
    }
    zm_write_word(m->mem, (uint16_t)(ops[1] + 2u * spare), ops[0]);
    zm_write_word(m->mem, ops[1], (uint16_t)(spare - 1u));
    branch(m, inst, 1);
}

static uint16_t pull_user_stack(zm_machine *m, uint16_t table)
{
    /* Spec §6.6: "The Z-machine makes no check on stack under-flow",
     * so a pull from an empty user stack reads whatever follows it. */
    uint16_t spare = (uint16_t)(zm_read_word(m->mem, table) + 1u);

    zm_write_word(m->mem, table, spare);
    return zm_read_word(m->mem, (uint16_t)(table + 2u * spare));
}

static void pop_stack_opcode(zm_machine *m, const zm_instruction *inst, const uint16_t *ops, unsigned count)
{
    /* Spec §15 pop_stack: so many items off a user stack, or off the
     * game stack, which must have them (Spec §6.3.1). */
    if (count > 1) {
        zm_write_word(m->mem, ops[1], (uint16_t)(zm_read_word(m->mem, ops[1]) + ops[0]));
        return;
    }
    if (ops[0] > m->sp - current_frame(m)->stack_base) {
        zm_fatal("pop_stack %u at $%05x: the stack holds only %u word(s)",
                 (unsigned)ops[0], (unsigned)inst->address, (unsigned)(m->sp - current_frame(m)->stack_base));
    }
    m->sp -= ops[0];
}

/* Spec §8.8.3.2.2: the new-line interrupt routine is run like a timed
 * input's, on top of the game's stack, in the middle of whatever was
 * being printed. Its result is not wanted. */
static int run_interrupt(zm_machine *m, uint16_t routine);

static void newline_interrupt(void *context, uint16_t routine)
{
    run_interrupt(context, routine);
}

static void sound_effect_opcode(zm_machine *m, const uint16_t *ops, unsigned count)
{
    /* Spec §15 sound_effect: with no operands at all, bleep. The volume
     * word carries the repeat count in its high byte from Version 5;
     * Spec §15 suggests treating a count of 0 as playing once. The
     * routine of a Version 5 call is to be called when the sound
     * finishes, which needs a front end that plays sounds in the
     * background; none does yet, so no sound ever finishes. */
    unsigned number = count > 0 ? ops[0] : 1u;
    unsigned effect = count > 1 ? ops[1] : (unsigned)ZM_SOUND_START;
    unsigned volume = count > 2 ? (ops[2] & 0xffu) : 8u;
    unsigned repeats = count > 2 && m->mem->version >= 5 ? (ops[2] >> 8) : 0u;

    zm_screen_sound_effect(&m->screen, number, effect, volume, repeats != 0 ? repeats : 1u);
}

static void print_table_opcode(zm_machine *m, const zm_instruction *inst, const uint16_t *ops, unsigned count)
{
    unsigned width;
    unsigned height = count > 2 ? ops[2] : 1u;
    unsigned skip = count > 3 ? ops[3] : 0u;
    unsigned row;
    unsigned column;

    if (count < 2) {
        zm_fatal("print_table with %u operand(s) at $%05x", count, (unsigned)inst->address);
    }
    width = ops[1];

    /* Spec §15 print_table: each line starts under the first, which in
     * the upper window means moving the cursor back; in the lower window
     * only one line is legal, and a new-line is the nearest thing. In
     * Version 6 every window has a cursor to move, a line down. */
    zm_screen_get_cursor(&m->screen, &row, &column);
    for (unsigned r = 0; r < height; r++) {
        for (unsigned c = 0; c < width; c++) {
            print_zscii(m, zm_read_byte(m->mem, (uint16_t)(ops[0] + r * (width + skip) + c)));
        }
        if (r + 1 < height) {
            if (is_v6(m)) {
                zm_screen_set_cursor(&m->screen, m->screen.window,
                                     (int16_t)(row + (r + 1) * m->screen.font_height), (int16_t)column);
            } else if (m->screen.window == ZM_WINDOW_UPPER) {
                zm_screen_set_cursor(&m->screen, m->screen.window, (int16_t)(row + r + 1), (int16_t)column);
            } else {
                print_zscii(m, ZM_ZSCII_NEWLINE);
            }
        }
    }
}

static void scan_table_opcode(zm_machine *m, const zm_instruction *inst, const uint16_t *ops, unsigned count)
{
    unsigned form = count > 3 ? ops[3] : 0x82u;
    unsigned field = form & 0x7fu;
    int words = (form & 0x80u) != 0;
    uint16_t found = 0;

    /* Spec §15 scan_table: the form's top bit says whether the entries
     * are words, and the rest how far apart they are. */
    if (count < 3) {
        zm_fatal("scan_table with %u operand(s) at $%05x", count, (unsigned)inst->address);
    }
    if (field == 0) {
        zm_fatal("scan_table at $%05x with entries 0 bytes apart", (unsigned)inst->address);
    }
    for (unsigned i = 0; i < ops[2]; i++) {
        uint16_t address = (uint16_t)(ops[1] + i * field);
        uint16_t value = words ? zm_read_word(m->mem, address) : zm_read_byte(m->mem, address);

        if (value == ops[0]) {
            found = address;
            break;
        }
    }
    store_result(m, inst, found);
    branch(m, inst, found != 0);
}

static void copy_table_opcode(zm_machine *m, const zm_instruction *inst, const uint16_t *ops, unsigned count)
{
    uint16_t first;
    uint16_t second;
    int16_t size;
    unsigned length;

    if (count < 3) {
        zm_fatal("copy_table with %u operand(s) at $%05x", count, (unsigned)inst->address);
    }
    first = ops[0];
    second = ops[1];
    size = as_signed(ops[2]);
    length = size < 0 ? (unsigned)(-(int32_t)size) : (unsigned)size;

    /* Spec §15 copy_table: no destination means fill with zeros. */
    if (second == 0) {
        for (unsigned i = 0; i < length; i++) {
            zm_write_byte(m->mem, (uint16_t)(first + i), 0);
        }
        return;
    }

    /* Spec §15 copy_table: a positive size copies safely even if the
     * tables overlap, which means backwards when the destination starts
     * inside the source; a negative size copies forwards regardless,
     * which 'Beyond Zork' uses to fill an array with its first byte. */
    if (size > 0 && second > first && (unsigned)second < (unsigned)first + length) {
        for (unsigned i = length; i-- > 0;) {
            zm_write_byte(m->mem, (uint16_t)(second + i), zm_read_byte(m->mem, (uint16_t)(first + i)));
        }
    } else {
        for (unsigned i = 0; i < length; i++) {
            zm_write_byte(m->mem, (uint16_t)(second + i), zm_read_byte(m->mem, (uint16_t)(first + i)));
        }
    }
}

/*
 * Saving and restoring (Spec §15 save, restore, save_undo, restore_undo).
 */

/* Quetzal §5.8: a saved game records the address of the save
 * instruction's branch data (Versions 1 to 3) or store byte (Version 4
 * on), rather than of the next instruction, so that on a restore those
 * bytes can be acted on again with a different outcome. A save in those
 * Versions is a one-byte 0OP instruction, so its branch data starts at
 * the next byte; a store byte is always the last byte of its
 * instruction. */
static uint32_t save_point(const zm_machine *m, const zm_instruction *inst)
{
    return m->mem->version <= 3 ? inst->address + 1 : inst->next - 1;
}

/* Pick up after a restore, with the PC at the save point: Spec §15 save
 * has Versions 1 to 3 take the save's branch, as if it had succeeded, and
 * later Versions store 2 where the save stored its 1. */
static void resume_after_restore(zm_machine *m)
{
    if (m->mem->version <= 3) {
        zm_instruction save = { 0 };

        save.next = zm_instruction_read_branch(m->mem, m->pc, &save.branch_on_true, &save.branch_offset);
        m->pc = save.next;
        branch(m, &save, 1);
    } else {
        uint8_t variable = zm_read_byte(m->mem, m->pc++);

        write_variable(m, variable, 2, 0);
    }
}

/* The Version 5 forms of save and restore with operands (Spec §15, §7.6)
 * move bytes between a table in memory and an auxiliary file of the
 * game's own: table and bytes give the region, name a string counted by
 * its first byte (Spec §7.6.1), and prompt, if given, whether to ask the
 * player. Returns 1 or 0 for a save and the number of bytes read for a
 * restore. */
static uint16_t auxiliary_file(zm_machine *m, const zm_instruction *inst, const uint16_t *ops, unsigned count, int saving)
{
    uint32_t table = ops[0];
    uint32_t bytes;
    char name[256];
    const char *name_given = NULL;
    int prompt = -1;
    uint8_t *data;
    uint32_t size;
    uint16_t result = 0;

    if (count < 2) {
        zm_fatal("%s at $%05x names a table but not how many bytes of it",
                 inst->info->name, (unsigned)inst->address);
    }
    bytes = ops[1];
    if (count > 2 && ops[2] != 0) {
        unsigned length = zm_read_byte(m->mem, ops[2]);

        for (unsigned i = 0; i < length; i++) {
            name[i] = (char)zm_read_byte(m->mem, (uint32_t)(ops[2] + 1u + i));
        }
        name[length] = '\0';
        name_given = name;
    }
    if (count > 3) {
        prompt = ops[3] != 0;
    }

    if (saving) {
        if (m->io->write_file == NULL) {
            return 0;
        }
        data = malloc(bytes > 0 ? bytes : 1);
        if (data == NULL) {
            return 0;
        }
        for (uint32_t i = 0; i < bytes; i++) {
            data[i] = zm_read_byte(m->mem, table + i);
        }
        result = m->io->write_file(m->io->context, ZM_FILE_AUXILIARY, name_given, prompt, data, bytes) ? 1 : 0;
        free(data);
        return result;
    }

    if (m->io->read_file == NULL) {
        return 0;
    }
    data = m->io->read_file(m->io->context, ZM_FILE_AUXILIARY, name_given, prompt, &size);
    if (data == NULL) {
        return 0;
    }
    if (size > bytes) {
        size = bytes;
    }
    for (uint32_t i = 0; i < size; i++) {
        zm_write_byte(m->mem, table + i, data[i]);
    }
    free(data);
    return (uint16_t)size;
}

static void save_opcode(zm_machine *m, const zm_instruction *inst, const uint16_t *ops, unsigned count)
{
    uint8_t *data;
    uint32_t size;
    int saved = 0;

    if (count > 0) {
        store_result(m, inst, auxiliary_file(m, inst, ops, count, 1));
        return;
    }

    /* Spec §6.1.1: the whole state of play, as a Quetzal file the front
     * end puts somewhere. */
    if (m->io->write_file != NULL && zm_quetzal_write(m, save_point(m, inst), &data, &size) == ZM_OK) {
        saved = m->io->write_file(m->io->context, ZM_FILE_SAVED_GAME, NULL, -1, data, size) != 0;
        free(data);
    }
    if (m->mem->version <= 3) {
        branch(m, inst, saved);
    } else {
        store_result(m, inst, saved ? 1 : 0);
    }
}

static void restore_opcode(zm_machine *m, const zm_instruction *inst, const uint16_t *ops, unsigned count)
{
    uint8_t *data = NULL;
    uint32_t size = 0;

    if (count > 0) {
        store_result(m, inst, auxiliary_file(m, inst, ops, count, 0));
        return;
    }

    if (m->io->read_file != NULL) {
        data = m->io->read_file(m->io->context, ZM_FILE_SAVED_GAME, NULL, -1, &size);
    }
    if (data != NULL) {
        zm_status status = zm_quetzal_read(m, data, size);

        free(data);
        if (status == ZM_OK) {
            /* Spec §8.6.1.3: the upper window collapses on a restore;
             * the game draws it again if it wants one. A Version 6 game
             * lays its windows out again itself. */
            if (!is_v6(m)) {
                zm_screen_split(&m->screen, 0);
                zm_screen_set_window(&m->screen, ZM_WINDOW_LOWER);
            }
            resume_after_restore(m);
            return;
        }

        /* The game will say the restore failed, but it cannot say why,
         * and the player deserves to know. */
        print_ascii(m, "[");
        print_ascii(m, zm_status_message(status));
        print_ascii(m, "]\n");
    }

    /* Spec §15 restore: 0 for failure, which in Versions 1 to 3 means
     * the branch is not taken. */
    if (m->mem->version <= 3) {
        branch(m, inst, 0);
    } else {
        store_result(m, inst, 0);
    }
}

/* Quetzal §6.2: catch is the number of frames on the call stack, so that
 * a caught value survives in a saved game. Frotz, whose saved games are
 * the ones to agree with, does not count the environment of Spec §5.5
 * that this machine keeps as frame 0; it does count the Version 6 main
 * routine. */
static uint16_t frame_number(const zm_machine *m)
{
    return (uint16_t)(m->depth - (m->mem->version == 6 ? 0 : 1));
}

static void throw_opcode(zm_machine *m, const zm_instruction *inst, const uint16_t *ops)
{
    uint32_t depth = (uint32_t)ops[1] + (m->mem->version == 6 ? 0 : 1);

    /* Spec §15 throw: back to the routine that caught the value, which
     * then returns. A frame that is not on the stack cannot be
     * returned to. */
    if (depth < 1 || depth > m->depth) {
        zm_fatal("throw at $%05x to stack frame %u, which does not exist",
                 (unsigned)inst->address, (unsigned)ops[1]);
    }
    m->depth = depth;
    return_from_routine(m, ops[0]);
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
    case 5:  /* save */
        save_opcode(m, inst, NULL, 0);
        break;
    case 6:  /* restore */
        restore_opcode(m, inst, NULL, 0);
        break;
    case 7:  /* restart */
        zm_machine_restart(m);
        break;
    case 8:  /* ret_popped */
        return_from_routine(m, pop(m));
        break;
    case 9:  /* catch from Version 5; pop before that, which discards the
              * top of the stack */
        if (m->mem->version >= 5) {
            store_result(m, inst, frame_number(m));
        } else {
            pop(m);
        }
        break;
    case 10: /* quit: whatever is still buffered for the screen (Spec
              * §8.8.3.1.2) is shown on the way out */
        zm_screen_flush(&m->screen);
        m->running = 0;
        break;
    case 11: /* new_line */
        print_zscii(m, ZM_ZSCII_NEWLINE);
        break;
    case 12: /* show_status: Version 3 only, but Spec §15 asks that it be
              * a nop in later Versions for the sake of 'Wishbringer'
              * release 23. */
        if (m->mem->version <= 3) {
            show_status_line(m);
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
    case 18: /* get_prop_addr: the address of the data, or 0. Spec §15 asks
              * for 0 when the object has not got the property, and some
              * Inform games ('Vespers', 'Nine Dancers') ask about numbers
              * above the Version's highest, which no object has got.
              * Frotz answers 0 to those too, so faulting would only
              * stop games that run elsewhere. */
        {
            zm_property prop;
            unsigned limit = m->mem->version <= 3 ? 31u : 63u;

            if (ops[1] == 0 || ops[1] > limit) {
                store_result(m, inst, 0);
            } else {
                store_result(m, inst, find_property(m, inst, ops[0], ops[1], &prop) ? (uint16_t)prop.address : 0u);
            }
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
    case 27: /* set_colour */
        set_colour_opcode(m, inst, ops, count);
        break;
    case 28: /* throw */
        throw_opcode(m, inst, ops);
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
    case 4:  /* sread / aread */
        if (count == 0) {
            zm_fatal("%s with no text buffer at $%05x", inst->info->name, (unsigned)inst->address);
        }
        read_line_opcode(m, inst, ops, count);
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
        if (is_v6(m)) {
            /* Version 6 pulls from a user stack if one is given (Spec
             * §6.6) and stores the result. */
            store_result(m, inst, count > 0 ? pull_user_stack(m, ops[0]) : pop(m));
        } else {
            /* The pop happens first, so pulling to the stack replaces
             * the new top in place (Spec §6.3.4). */
            uint16_t value = pop(m);
            write_variable(m, variable_reference(inst, ops[0]), value, 1);
        }
        break;
    case 10: /* split_window: lines, or units in Version 6 */
        zm_screen_split(&m->screen, ops[0]);
        break;
    case 11: /* set_window */
        set_window_opcode(m, inst, ops, count);
        break;
    case 12: /* call_vs2 */
        if (count == 0) {
            zm_fatal("call_vs2 with no routine at $%05x", (unsigned)inst->address);
        }
        call_routine(m, ops[0], ops + 1, count - 1, inst->store);
        break;
    case 13: /* erase_window */
        erase_window_opcode(m, inst, ops);
        break;
    case 14: /* erase_line */
        zm_screen_erase_line(&m->screen, ops[0]);
        break;
    case 15: /* set_cursor */
        set_cursor_opcode(m, inst, ops, count);
        break;
    case 16: /* get_cursor: row into word 0 of the array, column into
              * word 1 */
        {
            unsigned row;
            unsigned column;

            zm_screen_get_cursor(&m->screen, &row, &column);
            zm_write_word(m->mem, ops[0], (uint16_t)row);
            zm_write_word(m->mem, (uint16_t)(ops[0] + 2u), (uint16_t)column);
        }
        break;
    case 17: /* set_text_style */
        if (ops[0] > (ZM_STYLE_REVERSE | ZM_STYLE_BOLD | ZM_STYLE_ITALIC | ZM_STYLE_FIXED_PITCH)) {
            zm_fatal("set_text_style %u at $%05x: styles are 0 to 15", (unsigned)ops[0], (unsigned)inst->address);
        }
        zm_screen_set_style(&m->screen, ops[0]);
        break;
    case 18: /* buffer_mode */
        zm_screen_set_buffering(&m->screen, ops[0] != 0);
        break;
    case 19: /* output_stream */
        output_stream_opcode(m, inst, ops, count);
        break;
    case 20: /* input_stream: 0 is the keyboard and 1 a file of commands
              * (Spec §10.2). Spec §10.2.2 leaves the interpreter free to
              * choose the input stream itself, and the front ends so
              * far read from wherever they were started, so the choice
              * is noted and nothing changes. */
        if (ops[0] > 1) {
            zm_fatal("input_stream %u at $%05x: only streams 0 and 1 exist", (unsigned)ops[0], (unsigned)inst->address);
        }
        break;
    case 21: /* sound_effect */
        sound_effect_opcode(m, ops, count);
        break;
    case 25: /* call_vn */
    case 26: /* call_vn2 */
        if (count == 0) {
            zm_fatal("%s with no routine at $%05x", inst->info->name, (unsigned)inst->address);
        }
        call_routine(m, ops[0], ops + 1, count - 1, -1);
        break;
    case 22: /* read_char */
        read_char_opcode(m, inst, ops, count);
        break;
    case 23: /* scan_table */
        scan_table_opcode(m, inst, ops, count);
        break;
    case 24: /* not, moved here from 1OP:15 in Version 5 */
        store_result(m, inst, (uint16_t)~ops[0]);
        break;
    case 27: /* tokenise: lexical analysis on demand, with the game's
              * dictionary or one of its own, optionally leaving words it
              * does not know alone (Spec §15 tokenise) */
        {
            zm_dictionary dict;

            if (count < 2) {
                zm_fatal("tokenise with %u operand(s) at $%05x", count, (unsigned)inst->address);
            }
            zm_dictionary_read(m->mem, count > 2 && ops[2] != 0 ? ops[2] : m->dictionary, &dict);
            zm_lexer_tokenise(m->mem, ops[0], ops[1], &dict, count > 3 && ops[3] != 0);
        }
        break;
    case 28: /* encode_text: length characters of ZSCII at text + from,
              * encoded as a dictionary word into coded-text */
        {
            uint16_t zscii[256];
            uint8_t encoded[ZM_TEXT_ENCODED_MAX];
            unsigned length;
            unsigned bytes;

            if (count < 4) {
                zm_fatal("encode_text with %u operand(s) at $%05x", count, (unsigned)inst->address);
            }
            length = ops[1] > 255 ? 255u : ops[1];
            for (unsigned i = 0; i < length; i++) {
                zscii[i] = zm_read_byte(m->mem, (uint16_t)(ops[0] + ops[2] + i));
            }
            bytes = zm_text_encode(m->mem, zscii, length, encoded);
            for (unsigned i = 0; i < bytes; i++) {
                zm_write_byte(m->mem, (uint16_t)(ops[3] + i), encoded[i]);
            }
        }
        break;
    case 29: /* copy_table */
        copy_table_opcode(m, inst, ops, count);
        break;
    case 30: /* print_table */
        print_table_opcode(m, inst, ops, count);
        break;
    case 31: /* check_arg_count: branch if the current routine was given
              * at least that many arguments (Spec §6.4.4.1) */
        branch(m, inst, ops[0] <= current_frame(m)->argument_count);
        break;
    default:
        not_implemented(m, inst);
    }
}

static void execute_ext(zm_machine *m, const zm_instruction *inst, const uint16_t *ops, unsigned count)
{
    switch (inst->number) {
    case 0:  /* save, with a table to put in an auxiliary file if given */
        save_opcode(m, inst, ops, count);
        break;
    case 1:  /* restore */
        restore_opcode(m, inst, ops, count);
        break;
    case 2:  /* log_shift */
        store_result(m, inst, shift(inst, ops[0], ops[1], 0));
        break;
    case 3:  /* art_shift */
        store_result(m, inst, shift(inst, ops[0], ops[1], 1));
        break;
    case 4:  /* set_font, for a window in Version 6 */
        store_result(m, inst, (uint16_t)zm_screen_set_font(&m->screen,
                     is_v6(m) ? window_operand(m, inst, ops, count, 1) : m->screen.window, ops[0]));
        break;
    case 5:  /* draw_picture */
        zm_screen_draw_picture(&m->screen, ops[0], count > 1 ? ops[1] : 0u, count > 2 ? ops[2] : 0u);
        break;
    case 6:  /* picture_data */
        picture_data_opcode(m, inst, ops, count);
        break;
    case 7:  /* erase_picture */
        zm_screen_erase_picture(&m->screen, ops[0], count > 1 ? ops[1] : 0u, count > 2 ? ops[2] : 0u);
        break;
    case 8:  /* set_margins */
        if (count < 2) {
            zm_fatal("set_margins with %u operand(s) at $%05x", count, (unsigned)inst->address);
        }
        zm_screen_set_margins(&m->screen, window_operand(m, inst, ops, count, 2), ops[0], ops[1]);
        break;
    case 9:  /* save_undo: 1 for saved, 0 for not; -1 would mean the
              * interpreter cannot, and it can */
        store_result(m, inst, save_undo(m, save_point(m, inst)) ? 1 : 0);
        break;
    case 10: /* restore_undo: on success the save_undo stores 2, as a
              * restored save does; with nothing to go back to, 0 */
        if (restore_undo(m)) {
            resume_after_restore(m);
        } else {
            store_result(m, inst, 0);
        }
        break;
    case 11: /* print_unicode */
        zm_screen_print_unicode(&m->screen, ops[0]);
        break;
    case 12: /* check_unicode */
        store_result(m, inst, zm_screen_check_unicode(&m->screen, ops[0]));
        break;
    case 13: /* set_true_colour */
        set_true_colour_opcode(m, inst, ops, count);
        break;
    case 16: /* move_window */
        if (count < 3) {
            zm_fatal("move_window with %u operand(s) at $%05x", count, (unsigned)inst->address);
        }
        zm_screen_move_window(&m->screen, window_operand(m, inst, ops, count, 0), ops[1], ops[2]);
        break;
    case 17: /* window_size */
        if (count < 3) {
            zm_fatal("window_size with %u operand(s) at $%05x", count, (unsigned)inst->address);
        }
        zm_screen_size_window(&m->screen, window_operand(m, inst, ops, count, 0), ops[1], ops[2]);
        break;
    case 18: /* window_style */
        window_style_opcode(m, inst, ops, count);
        break;
    case 19: /* get_wind_prop */
        if (count < 2) {
            zm_fatal("get_wind_prop with %u operand(s) at $%05x", count, (unsigned)inst->address);
        }
        store_result(m, inst, zm_screen_window_property(&m->screen, window_operand(m, inst, ops, count, 0), ops[1]));
        break;
    case 20: /* scroll_window */
        if (count < 2) {
            zm_fatal("scroll_window with %u operand(s) at $%05x", count, (unsigned)inst->address);
        }
        zm_screen_scroll_window(&m->screen, window_operand(m, inst, ops, count, 0), as_signed(ops[1]));
        break;
    case 21: /* pop_stack */
        pop_stack_opcode(m, inst, ops, count);
        break;
    case 22: /* read_mouse */
        read_mouse_opcode(m, ops);
        break;
    case 23: /* mouse_window: a window, or -1 for the whole screen */
        zm_screen_mouse_window(&m->screen, as_signed(ops[0]) == -1 ? -1 : (int)window_operand(m, inst, ops, count, 0));
        break;
    case 24: /* push_stack */
        push_stack_opcode(m, inst, ops, count);
        break;
    case 25: /* put_wind_prop */
        if (count < 3) {
            zm_fatal("put_wind_prop with %u operand(s) at $%05x", count, (unsigned)inst->address);
        }
        zm_screen_put_window_property(&m->screen, window_operand(m, inst, ops, count, 0), ops[1], ops[2]);
        break;
    case 26: /* print_form */
        print_form_opcode(m, ops);
        break;
    case 27: /* make_menu: menus "with numbers greater than 2" are the
              * front end's to show, and none shows them, so the branch
              * for success is never made */
        branch(m, inst, 0);
        break;
    case 28: /* picture_table: "the interpreter may if it wishes" cache
              * the pictures named; this one finds them quickly enough */
        break;
    case 29: /* buffer_screen */
        store_result(m, inst, zm_screen_buffer_screen(&m->screen, as_signed(ops[0])));
        break;
    default:
        not_implemented(m, inst);
    }
}

void zm_machine_use_resources(zm_machine *m, const zm_blorb *resources)
{
    zm_screen_use_resources(&m->screen, resources);
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
    case ZM_OP_EXT: execute_ext(m, &inst, ops, inst.operand_count); break;
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
    m->dictionary = h.dictionary;
    zm_screen_init(&m->screen, mem, io);
    zm_screen_set_interrupt(&m->screen, newline_interrupt, m);

    /* Spec §2.4: the generator starts in "random" mode. */
    zm_machine_seed_random(m, 0);

    begin_execution(m);
    return ZM_OK;
}

void zm_machine_free(zm_machine *m)
{
    for (unsigned i = 0; i < ZM_UNDO_LEVELS; i++) {
        zm_snapshot_free(&m->undo[i]);
    }
    free(m->stack);
    free(m->frames);
    memset(m, 0, sizeof *m);
}
