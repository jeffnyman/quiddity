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

#include "zm/blorb.h"
#include "zm/machine.h"
#include "zm/memory.h"
#include "zm/status.h"

#include "accept.h"
#include "info.h"
#include "terminal.h"

/* Exit status for a command-line usage error, following the BSD sysexits
 * convention (EX_USAGE). Kept local so <sysexits.h>, which MSVC lacks, is
 * not needed. */
#define EXIT_USAGE 64

static void print_usage(FILE *out)
{
    fputs("Usage: quiddity [options] <story-file>\n"
          "       quiddity --accept <script> [--update | --resume] [--games <dir>]\n"
          "       quiddity --accept --create <script> --game <story> [--games <dir>]\n"
          "\n"
          "Options:\n"
          "  -i, --info               Describe the story file's header and exit.\n"
          "  -t, --transcript <file>  Append a transcript to <file> when the game asks for one.\n"
          "  -r, --record <file>      Append the commands typed to <file> when the game asks.\n"
          "  -s, --seed <number>      Seed the game's random numbers, so that a play repeats.\n"
          "  -h, --help               Show this help and exit.\n"
          "  -v, --version            Show the version and exit.\n"
          "\n"
          "The acceptance command plays a script and checks the play against the\n"
          "recording beside it: --update records the play afresh, and --resume hands\n"
          "the game over at the console where the script ends. A game the script\n"
          "names is looked for beside the script, then under --games or $QUIDDITY_GAMES.\n"
          "--create writes a new script naming the story given by --game, with seed 1\n"
          "and no commands, and hands the game over at once, so that the commands\n"
          "that work can be tried before they go into the file.\n",
          out);
}

/* The acceptance command (see accept.h). It stands on its own, since
 * the script says which story to play and how. */
static int accept_command(int argc, char **argv)
{
    accept_mode mode = ACCEPT_CHECK;
    const char *script = NULL;
    const char *games = getenv("QUIDDITY_GAMES");
    const char *game = NULL;
    int create = 0;

    for (int i = 2; i < argc; i++) {
        const char *arg = argv[i];

        if (strcmp(arg, "--update") == 0) {
            mode = ACCEPT_UPDATE;
        } else if (strcmp(arg, "--resume") == 0) {
            mode = ACCEPT_RESUME;
        } else if (strcmp(arg, "--create") == 0) {
            create = 1;
        } else if (strcmp(arg, "--game") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "quiddity: option '--game' needs a story file\n\n");
                print_usage(stderr);
                return EXIT_USAGE;
            }
            game = argv[++i];
        } else if (strcmp(arg, "--games") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "quiddity: option '--games' needs a directory\n\n");
                print_usage(stderr);
                return EXIT_USAGE;
            }
            games = argv[++i];
        } else if (arg[0] == '-') {
            fprintf(stderr, "quiddity: unknown option '%s'\n\n", arg);
            print_usage(stderr);
            return EXIT_USAGE;
        } else if (script == NULL) {
            script = arg;
        } else {
            fprintf(stderr, "quiddity: --accept takes one script\n\n");
            print_usage(stderr);
            return EXIT_USAGE;
        }
    }
    if (script == NULL) {
        print_usage(stderr);
        return EXIT_USAGE;
    }
    if (create != (game != NULL)) {
        fputs(create ? "quiddity: --create needs --game <story>\n\n"
                     : "quiddity: --game goes with --create\n\n",
              stderr);
        print_usage(stderr);
        return EXIT_USAGE;
    }
    if (create) {
        char error[ACCEPT_ERROR_MAX];

        if (mode == ACCEPT_UPDATE) {
            fputs("quiddity: --create and --update cannot go together\n\n", stderr);
            print_usage(stderr);
            return EXIT_USAGE;
        }
        if (!accept_create(script, game, games, error)) {
            fprintf(stderr, "quiddity: %s\n", error);
            return 1;
        }
        printf("quiddity: created %s; the game is yours from the start\n", script);
        mode = ACCEPT_RESUME;
    }
    return accept_run(script, mode, games);
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

/* Find the story's pictures and sounds (zm/blorb.h). A story file that is
 * itself a Blorb carries them; a bare story file may have a resource
 * file beside it with the same name and one of the Blorb suffixes, which
 * must belong to it (Blorb §6). Returns whether one was loaded. */
static int find_resources(const char *path, const zm_memory *mem, zm_blorb *resources)
{
    static const char *const suffixes[] = { ".blb", ".blorb", ".zblorb" };
    size_t base_length = strlen(path);
    char *sibling;

    if (zm_blorb_load_file(resources, path) == ZM_OK) {
        return 1;
    }

    /* The story's name up to its extension, if the extension is in the
     * file name rather than a directory on the way to it. */
    for (size_t i = base_length; i > 0; i--) {
        if (path[i - 1] == '/' || path[i - 1] == '\\') {
            break;
        }
        if (path[i - 1] == '.') {
            base_length = i - 1;
            break;
        }
    }
    sibling = malloc(base_length + 8);
    if (sibling == NULL) {
        return 0;
    }
    for (size_t i = 0; i < sizeof suffixes / sizeof suffixes[0]; i++) {
        memcpy(sibling, path, base_length);
        strcpy(sibling + base_length, suffixes[i]);
        if (zm_blorb_load_file(resources, sibling) == ZM_OK) {
            if (zm_blorb_matches(resources, mem)) {
                free(sibling);
                return 1;
            }
            fprintf(stderr, "quiddity: %s belongs to a different story and is ignored\n", sibling);
            zm_blorb_free(resources);
        }
    }
    free(sibling);
    return 0;
}

int main(int argc, char **argv)
{
    int show_help = 0;
    int show_version = 0;
    int show_info = 0;
    const char *transcript_path = NULL;
    const char *record_path = NULL;
    unsigned long seed = 0;
    int i = 1;
    const char *path;
    zm_memory mem;
    zm_blorb resources;
    int have_resources;
    zm_machine machine;
    terminal term;
    zm_io io;
    zm_status status;

    if (argc >= 2 && strcmp(argv[1], "--accept") == 0) {
        return accept_command(argc, argv);
    }

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
        } else if (strcmp(arg, "-t") == 0 || strcmp(arg, "--transcript") == 0
                   || strcmp(arg, "-r") == 0 || strcmp(arg, "--record") == 0) {
            /* Spec §7.1.1, §7.1.2.3: the files for output streams 2 and
             * 4, named here so the game need never ask. */
            if (i + 1 >= argc) {
                fprintf(stderr, "quiddity: option '%s' needs a file name\n\n", arg);
                print_usage(stderr);
                return EXIT_USAGE;
            }
            if (arg[1] == 't' || arg[2] == 't') {
                transcript_path = argv[++i];
            } else {
                record_path = argv[++i];
            }
        } else if (strcmp(arg, "-s") == 0 || strcmp(arg, "--seed") == 0) {
            /* Spec §2.4: the dice are the game's own, but a session can
             * be made to roll the same ones every time, which is how a
             * play is repeated and how a recorded one is reproduced. */
            char *end = NULL;

            if (i + 1 >= argc) {
                fprintf(stderr, "quiddity: option '%s' needs a number\n\n", arg);
                print_usage(stderr);
                return EXIT_USAGE;
            }
            seed = strtoul(argv[++i], &end, 10);
            if (*argv[i] == '\0' || *end != '\0' || seed == 0 || seed > 0xffffffffUL) {
                fprintf(stderr, "quiddity: the seed must be a whole number from 1 to 4294967295\n\n");
                print_usage(stderr);
                return EXIT_USAGE;
            }
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

    have_resources = find_resources(path, &mem, &resources);
    terminal_init(&term, &mem, &io, path, transcript_path, record_path);
    status = zm_machine_init(&machine, &mem, &io);
    if (status != ZM_OK) {
        fprintf(stderr, "quiddity: %s\n", zm_status_message(status));
        if (have_resources) {
            zm_blorb_free(&resources);
        }
        zm_memory_free(&mem);
        return EXIT_FAILURE;
    }
    if (have_resources) {
        zm_machine_use_resources(&machine, &resources);
    }
    if (seed != 0) {
        zm_machine_seed_session(&machine, (uint32_t)seed);
    }

    /* Runs until the game quits. A fault inside the machine goes through
     * zm_fatal(), whose default handler reports it and exits. */
    zm_machine_run(&machine);
    terminal_finish(&term);

    zm_machine_free(&machine);
    if (have_resources) {
        zm_blorb_free(&resources);
    }
    zm_memory_free(&mem);
    return EXIT_SUCCESS;
}
