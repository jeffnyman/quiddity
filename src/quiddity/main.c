/*
 * Quiddity: an interactive fiction interpreter for the Z-Machine and Glulx.
 *
 * This file is the terminal front end's entry point. It handles the command
 * line, loads the story file, and either describes it or runs it on the
 * terminal.
 *
 * Copyright (C) 2026 Jeff Nyman
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version. See the LICENSE file for details.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zm/machine.h"
#include "zm/memory.h"
#include "zm/status.h"

#include "info.h"
#include "terminal.h"

/* Exit status for a command-line usage error, following the BSD sysexits
 * convention (EX_USAGE). Kept local so <sysexits.h>, which MSVC lacks, is
 * not needed. */
#define EXIT_USAGE 64

static void print_usage(FILE *out)
{
    fputs("Usage: quiddity [options] <story-file>\n"
          "\n"
          "Options:\n"
          "  -i, --info     Describe the story file's header and exit.\n"
          "  -h, --help     Show this help and exit.\n"
          "  -v, --version  Show the version and exit.\n",
          out);
}

static void print_version(void)
{
    printf("Quiddity %s\n", QUIDDITY_VERSION);
}

/* Explain a failed load. For an I/O failure the C library knows the real
 * reason, so that is added. */
static void report_load_failure(const char *path, zm_status status)
{
    if (status == ZM_ERROR_IO) {
        fprintf(stderr, "quiddity: %s: %s\n", path, strerror(errno));
    } else {
        fprintf(stderr, "quiddity: %s: %s\n", path, zm_status_message(status));
    }
}

int main(int argc, char **argv)
{
    int show_help = 0;
    int show_version = 0;
    int show_info = 0;
    int i = 1;
    const char *path;
    zm_memory mem;
    zm_machine machine;
    terminal term;
    zm_io io;
    zm_status status;

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
        } else if (strcmp(arg, "-i") == 0 || strcmp(arg, "--info") == 0) {
            show_info = 1;
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

    path = argv[i];
    status = zm_memory_load_file(&mem, path);
    if (status != ZM_OK) {
        report_load_failure(path, status);
        return EXIT_FAILURE;
    }

    if (show_info) {
        print_story_info(stdout, path, &mem);
        zm_memory_free(&mem);
        return EXIT_SUCCESS;
    }

    terminal_init(&term, &mem, &io);
    status = zm_machine_init(&machine, &mem, &io);
    if (status != ZM_OK) {
        fprintf(stderr, "quiddity: %s\n", zm_status_message(status));
        zm_memory_free(&mem);
        return EXIT_FAILURE;
    }

    /* Runs until the game quits. A fault inside the machine goes through
     * zm_fatal(), whose default handler reports it and exits. */
    zm_machine_run(&machine);
    terminal_finish(&term);

    zm_machine_free(&machine);
    zm_memory_free(&mem);
    return EXIT_SUCCESS;
}
