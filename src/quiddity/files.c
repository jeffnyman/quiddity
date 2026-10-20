/*
 * files.c: see files.h.
 */

#include "files.h"

#include <ctype.h>
#include <string.h>

/* The story file's name less its directory and extension, plus .sav. */
void files_default_save_name(const char *story_path, char *name, size_t size)
{
    const char *base = story_path;
    const char *dot;
    size_t length;

    for (const char *p = story_path; *p != '\0'; p++) {
        if (*p == '/' || *p == '\\') {
            base = p + 1;
        }
    }
    dot = strrchr(base, '.');
    length = dot != NULL && dot != base ? (size_t)(dot - base) : strlen(base);
    if (length > size - 5) {
        length = size - 5;
    }
    memcpy(name, base, length);
    strcpy(name + length, ".sav");
}

/* Spec §7.6.1: an auxiliary file's name as the game gives it is made
 * safe to use: letters and digits only, in upper case, cut at the first
 * full stop, and given the extension .AUX, so that a game can neither
 * reach outside the current directory nor overwrite its own files
 * (Spec §7.6.1.2, §7.6.1.4). A name that is a device on Windows (CON,
 * AUX, NUL, and the like) is set apart with an underscore. */
void files_auxiliary_name(const char *given, char *name, size_t size)
{
    static const char *const devices[] = { "CON", "PRN", "AUX", "NUL", "COM", "LPT" };
    size_t length = 0;

    for (; given != NULL && *given != '\0' && *given != '.'; given++) {
        if (isalnum((unsigned char)*given) && length < size - 6) {
            name[length++] = (char)toupper((unsigned char)*given);
        }
    }
    name[length] = '\0';
    if (length == 0) {
        strcpy(name, "DATA");
        length = 4;
    }
    for (unsigned i = 0; i < sizeof devices / sizeof devices[0]; i++) {
        if (strncmp(name, devices[i], 3) == 0 && (length == 3 || (length == 4 && isdigit((unsigned char)name[3])))) {
            memmove(name + 1, name, length + 1);
            name[0] = '_';
            length++;
        }
    }
    strcpy(name + length, ".AUX");
}
