/*
 * Quiddity: an interactive fiction interpreter for the Z-Machine and Glulx.
 *
 * This file is the terminal front end's entry point. For now it does nothing
 * but recognise its own command line; story file loading arrives with the
 * Z-Machine header work in the next increment.
 *
 * Copyright (C) 2026 Jeff Nyman
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version. See the LICENSE file for details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Exit status for a command-line usage error, following the BSD sysexits
 * convention (EX_USAGE). Kept local so <sysexits.h>, which MSVC lacks, is
 * not needed. */
#define EXIT_USAGE 64

static void print_usage(FILE *out)
{
    fputs("Usage: quiddity [options] <story-file>\n"
          "\n"
          "Options:\n"
          "  -h, --help     Show this help and exit.\n"
          "  -v, --version  Show the version and exit.\n",
          out);
}

static void print_version(void)
{
    printf("Quiddity %s\n", QUIDDITY_VERSION);
}

int main(int argc, char **argv)
{
    int show_help = 0;
    int show_version = 0;
    int i = 1;

    /* Options come first and are only recorded here. The loop stops at the
     * first argument that does not begin with a dash, which is taken to be
     * the story file. Acting on the options after the loop keeps the
     * parsing and the behaviour separate. */
    for (; i < argc; i++) {
        const char *arg = argv[i];

        if (arg[0] != '-') {
            break;
        }

        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            show_help = 1;
        } else if (strcmp(arg, "-v") == 0 || strcmp(arg, "--version") == 0) {
            show_version = 1;
        } else {
            fprintf(stderr, "quiddity: unknown option '%s'\n\n", arg);
            print_usage(stderr);
            return EXIT_USAGE;
        }
    }

    if (show_help) {
        print_usage(stdout);
        return EXIT_SUCCESS;
    }

    if (show_version) {
        print_version();
        return EXIT_SUCCESS;
    }

    if (i >= argc) {
        print_usage(stderr);
        return EXIT_USAGE;
    }

    /* There is nothing to load a story file with yet, so say so plainly
     * rather than pretend. */
    fprintf(stderr, "quiddity: story file loading is not implemented yet (%s)\n", argv[i]);
    return EXIT_FAILURE;
}
