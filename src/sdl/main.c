/*
 * Quiddity: an interactive fiction interpreter for the Z-Machine and Glulx.
 *
 * This file is the SDL front end's entry point. For now it opens a window
 * and waits for it to be closed, which is enough to show that SDL and the
 * libraries drawn on it build and run on each platform; the machine comes
 * into the window a piece at a time.
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

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3_image/SDL_image.h>
#include <SDL3_ttf/SDL_ttf.h>

/* Exit status for a command-line usage error, as in the terminal front
 * end (EX_USAGE). */
#define EXIT_USAGE 64

static void print_usage(FILE *out)
{
    fputs("Usage: quiddity-sdl [options]\n"
          "\n"
          "Options:\n"
          "  -h, --help     Show this help and exit.\n"
          "  -v, --version  Show the version, and those of the libraries, and exit.\n",
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

/* Clear the window to its background. */
static void draw(SDL_Renderer *renderer)
{
    SDL_SetRenderDrawColor(renderer, 0xff, 0xff, 0xff, SDL_ALPHA_OPAQUE);
    SDL_RenderClear(renderer);
    SDL_RenderPresent(renderer);
}

/* Open the window and wait, drawing it whenever it needs it, until it is
 * closed. Waiting for events rather than polling keeps the program idle
 * while nothing happens. */
static int run(void)
{
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Event event;
    int running = 1;

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "quiddity-sdl: cannot start SDL: %s\n", SDL_GetError());
        return EXIT_FAILURE;
    }
    if (!SDL_CreateWindowAndRenderer("Quiddity", 800, 600, SDL_WINDOW_RESIZABLE, &window, &renderer)) {
        fprintf(stderr, "quiddity-sdl: cannot open a window: %s\n", SDL_GetError());
        SDL_Quit();
        return EXIT_FAILURE;
    }
    draw(renderer);
    while (running && SDL_WaitEvent(&event)) {
        switch (event.type) {
        case SDL_EVENT_QUIT:
            running = 0;
            break;
        case SDL_EVENT_WINDOW_EXPOSED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            draw(renderer);
            break;
        default:
            break;
        }
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return EXIT_SUCCESS;
}

int main(int argc, char *argv[])
{
    if (argc == 1) {
        return run();
    }
    if (argc == 2 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
        print_usage(stdout);
        return EXIT_SUCCESS;
    }
    if (argc == 2 && (strcmp(argv[1], "-v") == 0 || strcmp(argv[1], "--version") == 0)) {
        print_version();
        return EXIT_SUCCESS;
    }
    print_usage(stderr);
    return EXIT_USAGE;
}
