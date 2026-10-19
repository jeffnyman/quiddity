/*
 * script.h: an acceptance script, which is a game, a seed, and the
 * commands to play, in a small text file that a run can be repeated
 * from exactly.
 *
 * The file is plain text. A line beginning with ! is a directive,
 * written "! NAME=value", and these are known:
 *
 *   GAME         the story file to play
 *   SEED         the session seed that makes the game's dice repeat
 *   BLORB        a resource file to use with it
 *   INTERPRETER  the machine the game is to take itself to be on, by
 *                name or number (Spec §11.1.3), since some games behave
 *                differently by it
 *   TANDY        yes to set the Tandy bit (Spec §11.1), which a few
 *                early games read
 *   UPPER        yes to put the upper window into the play, for a game
 *                that draws its text there
 *   HEIGHT       the screen height in lines the game is told, 1 to 255,
 *                for a game that lays its upper window out by it; the
 *                stream's own height, 255, means infinite (Spec §8.4.1)
 *   PICTURES     yes to tell a Version 6 game it has pictures
 *   GRAPHICS     yes to let a Glulx game open a graphics window
 *
 * A SEED line after some commands is a change of seed that takes effect
 * when the play reaches it, so that each stretch of a long script can
 * have the seed that makes it go the way it should, and settling one
 * stretch never disturbs the ones before. A line beginning with # is a
 * comment and a blank line is nothing. A line of three backticks opens
 * a fenced section, with a label after the backticks if one helps, and
 * nothing inside it is read until a bare line of three backticks closes
 * it, or the file ends; that lets a script keep an alternative path
 * through the game, or the rest of a path still being worked out,
 * without playing it. A resumed session keeps what was typed at the
 * console under a fence labelled tried, at the end of the script, for
 * the same reason: the lines worth keeping are moved above it, and the
 * rest deleted. Every other line is a command, in the format the
 * Z-machine's command files use (Spec §7.1.2.3 and §7 Remarks): a key
 * that is not a printable character is written as its ZSCII code in
 * square brackets, or by name for the ones a menu is worked with,
 * <up>, <down>, <left>, <right>, <escape>, and <space>, one press per
 * line. A command may be written with the prompt in front, as "> look",
 * which is how a transcript reads and how a command that itself begins
 * with #, !, or < is given.
 *
 * The game's path is taken relative to the script, so that a script
 * can sit beside the games it plays and still work from anywhere; when
 * the file is not there and the runner has been given a games
 * directory, it is looked for under that instead, which is how the
 * scripts in the repository find a corpus kept outside it. The script's
 * expected output lives beside it in a file of the same name with the
 * extension .expected, which the runner writes on the first run and
 * compares against on every run after.
 *
 * This is the same format as Rezrov's, Quiddity's C# sibling, and a
 * script written for one plays on the other.
 */

#ifndef QUIDDITY_SCRIPT_H
#define QUIDDITY_SCRIPT_H

#include <stdint.h>

/* Room for the explanation of a script that cannot be used. */
#define ACCEPT_ERROR_MAX 512

/* A change of seed before the command at an index. An index equal to
 * the number of commands is a change after the last of them, for a game
 * that goes on from where the script ends. */
typedef struct accept_seed_change {
    unsigned index;
    uint32_t seed;
} accept_seed_change;

typedef struct accept_script {
    char *path;              /* the script file, as given */
    char *name;              /* its file name alone, for messages */
    char *expected_path;     /* beside it, with the extension .expected */
    char *game_path;         /* the story file, resolved */
    char *blorb_path;        /* the resource file, resolved, or NULL */
    char *interpreter;       /* the INTERPRETER directive as written, or NULL */
    int tandy;
    int upper;
    int pictures;
    int graphics;
    unsigned height;         /* the HEIGHT directive, or 0 for the stream's own */
    uint32_t seed;           /* the seed at the start */

    char **commands;         /* the commands to play, in order */
    unsigned *command_lines; /* the line of the script each came from, from 1 */
    unsigned command_count;

    accept_seed_change *seed_changes;
    unsigned seed_change_count;

    int ends_fenced;         /* the file ended inside a fenced section */
} accept_script;

/* Read a script from a file. Returns 1, or 0 with an explanation in
 * error, which has ACCEPT_ERROR_MAX bytes, and nothing left to free.
 * games_dir may be NULL. */
int accept_script_load(accept_script *s, const char *path, const char *games_dir, char *error);

/* Parse a script's text, as if read from path, with the same results. */
int accept_script_parse(accept_script *s, const char *text, const char *path, const char *games_dir, char *error);

void accept_script_free(accept_script *s);

/* The seed to change to before the command at index, if the script has
 * one there. */
int accept_script_seed_change(const accept_script *s, unsigned index, uint32_t *seed);

#endif /* QUIDDITY_SCRIPT_H */
