/*
 * zm/machine.h: the running Z-machine.
 *
 * Implements Spec §5 (routines) and §6 (the game state), and executes the
 * instructions of Spec §15. The state of play (Spec §6.1) is dynamic
 * memory, the stack, the program counter, and the chain of routine calls
 * with their local variables. Memory belongs to zm/memory.h; the rest is
 * here, in the interpreter's own storage where the game cannot reach it
 * (Spec §6.1).
 *
 * Execution is a loop of decode (zm/instruction.h), fetch operands, act.
 * The instruction set is being filled in as the project advances; an
 * opcode that exists in the Version but is not yet implemented stops the
 * machine with a fault naming it.
 */

#ifndef ZM_MACHINE_H
#define ZM_MACHINE_H

#include <stdint.h>

#include "zm/io.h"
#include "zm/memory.h"
#include "zm/screen.h"
#include "zm/status.h"

/* Spec §6.3.3 asks for a stack of at least 1024 words and advises more;
 * this is the size Windows Frotz uses. */
#define ZM_STACK_WORDS 32768u

/* Routine calls can nest this deep. Spec §6.3.3's accounting (4 words per
 * call plus locals, out of 1024) implies a few hundred; this is generous
 * without being unbounded, so runaway recursion is reported rather than
 * crashing. */
#define ZM_MAX_FRAMES 4096u

/* Spec §5.2: a routine has at most 15 local variables. */
#define ZM_MAX_LOCALS 15

/* One entry in the routine call state (Spec §6.1, §6.5). */
typedef struct zm_frame {
    /* Where to continue in the caller after a return. */
    uint32_t return_pc;

    /* The stack pointer when the routine was entered. The routine sees
     * the stack as empty above this point (Spec §6.3.1) and everything
     * it pushed is discarded on return (Spec §6.3.2). */
    uint32_t stack_base;

    /* Variable to receive the return value, or -1 if the call discards
     * it (the call_*n opcodes). */
    int result_variable;

    /* Spec §6.4.4.1: how many arguments the caller supplied, which
     * check_arg_count asks about. */
    uint8_t argument_count;

    uint8_t local_count;
    uint16_t locals[ZM_MAX_LOCALS];
} zm_frame;

typedef struct zm_machine {
    zm_memory *mem;
    const zm_io *io;

    /* Byte address of the next instruction to execute. */
    uint32_t pc;

    /* The game stack (Spec §6.3): sp is the number of words in use, so
     * stack[sp - 1] is the top. */
    uint16_t *stack;
    uint32_t sp;

    /* The routine call state. frames[depth - 1] is the current routine.
     * Frame 0 is the environment execution starts in (Spec §5.5), or in
     * Version 6 the main routine (Spec §5.4); either way it cannot be
     * returned from. */
    zm_frame *frames;
    uint32_t depth;

    /* Byte address of the global variables table (Spec §6.2). */
    uint32_t globals;

    /* Byte address of the dictionary (Spec §13.1). */
    uint32_t dictionary;

    /* The output streams and the screen model (Spec §7, §8). */
    zm_screen screen;

    /* The random number generator (Spec §2.4). In "random" mode the
     * state is a 64-bit generator seeded from the clock. In
     * "predictable" mode with a seed below 1000 the generator counts
     * 1, 2, ..., seed and repeats (Spec §2 Remarks); with a larger seed
     * the 64-bit generator is seeded with it. */
    uint64_t random_state;
    uint16_t random_interval;   /* 0 unless predictable with a small seed */
    uint16_t random_counter;

    /* Cleared by quit. */
    int running;

    /* How many instructions have been executed, for diagnostics. */
    uint64_t instruction_count;
} zm_machine;

/* Prepare to run a loaded story. The memory and the I/O struct must
 * outlive the machine. Allocates the stack and call state; on failure
 * nothing is allocated. */
zm_status zm_machine_init(zm_machine *m, zm_memory *mem, const zm_io *io);

/* Release what zm_machine_init() allocated. Does not free the memory. */
void zm_machine_free(zm_machine *m);

/* Execute one instruction. */
void zm_machine_step(zm_machine *m);

/* Execute instructions until the game quits. */
void zm_machine_run(zm_machine *m);

/* Read or write a variable by number (Spec §4.2.2): 0 pops or pushes the
 * stack, 1 to 15 are the current routine's locals, 16 to 255 the
 * globals. These are the ordinary semantics an operand or store byte
 * uses; the in-place treatment of the stack that seven opcodes apply
 * (Spec §6.3.4) is internal to the machine. */
uint16_t zm_machine_read_variable(zm_machine *m, uint8_t variable);
void zm_machine_write_variable(zm_machine *m, uint8_t variable, uint16_t value);

/* Put the random number generator into "predictable" mode with a seed,
 * as the random opcode does with a negative operand (Spec §2.4.2). A
 * front end may offer this to the player for testing (Spec §2.4.3).
 * Seed 0 returns the generator to "random" mode. */
void zm_machine_seed_random(zm_machine *m, uint16_t seed);

/* Draw a number from 1 to range, which must be at least 1 (Spec
 * §2.4.1). */
uint16_t zm_machine_random(zm_machine *m, uint16_t range);

#endif /* ZM_MACHINE_H */
