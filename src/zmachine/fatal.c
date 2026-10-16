/*
 * fatal.c: see zm/fatal.h.
 */

#include "zm/fatal.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

static zm_fatal_handler current_handler = NULL;

void zm_set_fatal_handler(zm_fatal_handler handler)
{
    current_handler = handler;
}

_Noreturn void zm_fatal(const char *format, ...)
{
    char message[256];
    va_list args;

    va_start(args, format);
    vsnprintf(message, sizeof message, format, args);
    va_end(args);

    if (current_handler != NULL) {
        current_handler(message);
        /* The handler was required not to return. Continuing would run
         * the machine in a corrupt state, so stop here instead. */
        abort();
    }

    fprintf(stderr, "quiddity: fatal: %s\n", message);
    exit(EXIT_FAILURE);
}
