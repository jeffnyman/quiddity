/*
 * Quiddity: an interactive fiction interpreter for the Z-Machine and Glulx.
 *
 * This file is the SDL front end's entry point. It handles the command
 * line, loads the story file, asking for one if none was named, and plays
 * it in a window (display.h).
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

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3_image/SDL_image.h>
#include <SDL3_ttf/SDL_ttf.h>

#include "display.h"
#include "zm/fatal.h"
#include "zm/machine.h"
#include "zm/memory.h"
#include "zm/status.h"

/* Exit status for a command-line usage error, as in the terminal front
 * end (EX_USAGE). */
#define EXIT_USAGE 64

static void print_usage(FILE *out)
{
    fputs("Usage: quiddity-sdl [options] [<story-file>]\n"
          "\n"
          "Options:\n"
          "  -s, --seed <number>  Seed the game's random numbers, so that a play repeats.\n"
          "  -h, --help           Show this help and exit.\n"
          "  -v, --version        Show the version, and those of the libraries, and exit.\n"
          "\n"
          "Without a story file, one is asked for.\n",
          out);
}

/* The libraries are named with the versions linked in rather than those
 * compiled against, which for a static build are the same, and for a
 * build on the system's own libraries are what is actually running. */
static void print_version(void)
{
    int sdl = SDL_GetVersion();
    int ttf = TTF_Version();
    int image = IMG_Version();

    printf("quiddity-sdl %s\n", QUIDDITY_VERSION);
    printf("SDL %d.%d.%d, SDL_ttf %d.%d.%d, SDL_image %d.%d.%d\n",
           SDL_VERSIONNUM_MAJOR(sdl), SDL_VERSIONNUM_MINOR(sdl), SDL_VERSIONNUM_MICRO(sdl),
           SDL_VERSIONNUM_MAJOR(ttf), SDL_VERSIONNUM_MINOR(ttf), SDL_VERSIONNUM_MICRO(ttf),
           SDL_VERSIONNUM_MAJOR(image), SDL_VERSIONNUM_MINOR(image), SDL_VERSIONNUM_MICRO(image));
}

/* A fault inside the machine (zm/fatal.h), which must not return. */
static void fatal(const char *message)
{
    display_report(message);
    exit(EXIT_FAILURE);
}

/* Load the story and play it in a window until it ends or the window is
 * closed. */
static int play(const char *path, unsigned long seed)
{
    zm_memory mem;
    zm_machine machine;
    display d;
    zm_io io;
    zm_status status;
    char message[DISPLAY_PATH_MAX + 64];

    status = zm_memory_load_file(&mem, path);
    if (status != ZM_OK) {
        snprintf(message, sizeof message, "%s: %s", path,
                 status == ZM_ERROR_IO ? strerror(errno) : zm_status_message(status));
        display_report(message);
        return EXIT_FAILURE;
    }
    if (!display_open(&d, &mem, path, &io)) {
        snprintf(message, sizeof message, "cannot open a window: %s", SDL_GetError());
        display_report(message);
        zm_memory_free(&mem);
        return EXIT_FAILURE;
    }
    status = zm_machine_init(&machine, &mem, &io);
    if (status != ZM_OK) {
        display_report(zm_status_message(status));
        display_close(&d);
        zm_memory_free(&mem);
        return EXIT_FAILURE;
    }
    if (seed != 0) {
        zm_machine_seed_session(&machine, (uint32_t)seed);
    }
    zm_set_fatal_handler(fatal);

    /* Runs until the game quits, or the window is closed, which ends the
     * game as the end of input would. */
    zm_machine_run(&machine);
    display_finish(&d);

    zm_set_fatal_handler(NULL);
    zm_machine_free(&machine);
    display_close(&d);
    zm_memory_free(&mem);
    return EXIT_SUCCESS;
}

int main(int argc, char *argv[])
{
    unsigned long seed = 0;
    const char *path = NULL;
    char chosen[DISPLAY_PATH_MAX];
    int result;
    int i = 1;

    for (; i < argc && argv[i][0] == '-'; i++) {
        const char *arg = argv[i];

        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            print_usage(stdout);
            return EXIT_SUCCESS;
        }
        if (strcmp(arg, "-v") == 0 || strcmp(arg, "--version") == 0) {
            print_version();
            return EXIT_SUCCESS;
        }
        if (strcmp(arg, "-s") == 0 || strcmp(arg, "--seed") == 0) {
            /* Spec §2.4, as in the terminal front end. */
            char *end = NULL;

            if (i + 1 >= argc) {
                fprintf(stderr, "quiddity-sdl: option '%s' needs a number\n\n", arg);
                print_usage(stderr);
                return EXIT_USAGE;
            }
            seed = strtoul(argv[++i], &end, 10);
            if (*argv[i] == '\0' || *end != '\0' || seed == 0 || seed > 0xffffffffUL) {
                fprintf(stderr, "quiddity-sdl: the seed must be a whole number from 1 to 4294967295\n\n");
                print_usage(stderr);
                return EXIT_USAGE;
            }
            continue;
        }
        fprintf(stderr, "quiddity-sdl: unknown option '%s'\n\n", arg);
        print_usage(stderr);
        return EXIT_USAGE;
    }
    if (i < argc) {
        path = argv[i++];
    }
    if (i < argc) {
        print_usage(stderr);
        return EXIT_USAGE;
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "quiddity-sdl: cannot start SDL: %s\n", SDL_GetError());
        return EXIT_FAILURE;
    }
    if (!TTF_Init()) {
        fprintf(stderr, "quiddity-sdl: cannot start SDL_ttf: %s\n", SDL_GetError());
        SDL_Quit();
        return EXIT_FAILURE;
    }

    /* Started without a story, as a program opened from the desktop is,
     * it asks for one. */
    if (path == NULL) {
        if (!display_choose_story(chosen)) {
            TTF_Quit();
            SDL_Quit();
            return EXIT_SUCCESS;
        }
        path = chosen;
    }
    result = play(path, seed);

    TTF_Quit();
    SDL_Quit();
    return result;
}
