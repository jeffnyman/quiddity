/*
 * info.h: the --info report, a human-readable dump of a story's header.
 */

#ifndef QUIDDITY_INFO_H
#define QUIDDITY_INFO_H

#include <stdio.h>

#include "zm/memory.h"

/* Print everything the header says about the story to out. */
void print_story_info(FILE *out, const char *path, const zm_memory *mem);

#endif /* QUIDDITY_INFO_H */
