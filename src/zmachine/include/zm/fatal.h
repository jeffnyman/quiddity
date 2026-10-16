/*
 * zm/fatal.h: reporting faults inside the running machine.
 *
 * Some conditions mean the machine cannot sensibly continue: an address
 * outside memory, a write into static memory (Spec §1.1.2), an instruction
 * that does not decode. These are bugs in the story file or in this
 * interpreter, not ordinary failures a caller can retry, so they are not
 * returned as status codes. Instead the library calls zm_fatal(), which
 * formats a message and hands it to a handler supplied by the front end.
 *
 * The default handler prints the message to stderr and exits. A front end
 * with a screen to tidy up, or a test that wants to prove a fault is
 * detected, installs its own with zm_set_fatal_handler(). A handler must
 * not return; if it does, the library aborts, because continuing after a
 * fault would silently corrupt the game's state.
 */

#ifndef ZM_FATAL_H
#define ZM_FATAL_H

/* Lets GCC and Clang check zm_fatal's format string against its arguments
 * the way they check printf. MSVC has no equivalent attribute. */
#if defined(__GNUC__)
#define ZM_PRINTF_LIKE(format_index, first_arg_index) \
    __attribute__((format(printf, format_index, first_arg_index)))
#else
#define ZM_PRINTF_LIKE(format_index, first_arg_index)
#endif

typedef void (*zm_fatal_handler)(const char *message);

/* Install a handler, or pass NULL to restore the default. */
void zm_set_fatal_handler(zm_fatal_handler handler);

/* Report a fault. Never returns. */
_Noreturn void zm_fatal(const char *format, ...) ZM_PRINTF_LIKE(1, 2);

#endif /* ZM_FATAL_H */
