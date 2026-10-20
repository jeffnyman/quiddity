/*
 * files.h: the names a front end gives the files a game asks for.
 *
 * Every front end keeps saved games and auxiliary files (Spec §7.6) in
 * files of the player's, and offers or chooses their names by the same
 * rules, which are kept here so that the terminal and the window agree.
 */

#ifndef QUIDDITY_FILES_H
#define QUIDDITY_FILES_H

#include <stddef.h>

/* The name first offered for a saved game: the story file's own name,
 * without its directory, with its extension replaced by .sav. */
void files_default_save_name(const char *story_path, char *name, size_t size);

/* Spec §7.6.1: the name for an auxiliary file the game names, made safe
 * to use: letters and digits only, in upper case, cut at the first full
 * stop, with the extension .AUX, and set apart with an underscore if it
 * names a device on Windows. A game that gives no name, NULL, gets DATA. */
void files_auxiliary_name(const char *given, char *name, size_t size);

#endif /* QUIDDITY_FILES_H */
