/*
 * accept.h: the acceptance runner, which plays a script (script.h)
 * through the interpreter and checks the play against the recording
 * beside it.
 *
 * The play uses the plainest front end there is: a stream of text, in
 * which the lower window is everything and nothing already printed can
 * change. The script's commands are the game's input and nothing stands
 * behind them, so a script that runs out ends the game rather than
 * waiting. The random numbers come from the script's seed, so the play
 * is the same every time, which is the whole point. Its text, with a
 * line for an abnormal ending after it, is what an expected file holds,
 * so that a game that starts failing, or stops failing, changes the
 * text and shows up.
 *
 * The stream has no clock, but a game that reads with a timer still has
 * its interrupt routine run once before the script answers, as if the
 * player had taken an interval to type; accept.c says why, and why that
 * is the rule Rezrov follows too.
 *
 * The front end is a stream rather than a screen for a reason: a
 * recording should hold what the game said, not how a particular
 * display arranged it. So the text is not wrapped, no status line is
 * drawn, and the upper window is not shown; the game is told as much
 * through the header (Spec §11.1.4), sees a screen 80 characters wide
 * and 255 lines high (Spec §8.4.1), has the fixed-pitch and character
 * graphics fonts (Spec §8.1.2, §16), and is told it is an IBM PC, or
 * for a Version 6 game without pictures a DEC-20, the machines whose
 * interpreters shipped plain text (Spec §11.1.3). Those are the choices
 * Rezrov's recordings were made with, so that a script and its
 * recording can be shared between the two interpreters.
 */

#ifndef QUIDDITY_ACCEPT_H
#define QUIDDITY_ACCEPT_H

#include <stddef.h>

#include "script.h"

/* What the runner does with a script once it has played it. */
typedef enum accept_mode {
    ACCEPT_CHECK,   /* compare with the expected file, recording it if there is none */
    ACCEPT_UPDATE,  /* record the expected file afresh */
    ACCEPT_RESUME   /* hand the game to the player where the script ends */
} accept_mode;

/* The runner's exit status for a script whose game is not there, which
 * CTest is told means the test was skipped rather than failed. */
#define ACCEPT_EXIT_SKIPPED 77

/* How a run came to an end. */
typedef enum accept_ending {
    ACCEPT_QUIT,          /* the game quit */
    ACCEPT_SCRIPT_ENDED,  /* the script ran out while the game wanted more */
    ACCEPT_FAILED         /* the game or the interpreter faulted */
} accept_ending;

/* What a run produced. */
typedef struct accept_result {
    char *text;             /* the whole record: the play, then a line for a fault */
    size_t output_length;   /* how much of it the game printed, the commands included */
    accept_ending ending;
    char *message;          /* why, for a fault */
    unsigned *offsets;      /* how long the output was when each command was read */
    unsigned offset_count;
    char **typed;           /* the lines typed at the console in a resumed session */
    unsigned typed_count;
} accept_result;

/* Where an expected text and a play first part company. */
typedef struct accept_difference {
    unsigned line;          /* of the expected text, from 1 */
    char *description;      /* what was expected there and what came instead */
    int recording_ended;    /* the two agree as far as the recording goes and the play carries on */
} accept_difference;

/* Play the script and fill in the result. Returns 1; or 0 after saying
 * on stderr why the game could not be loaded; or ACCEPT_EXIT_SKIPPED
 * when the game file is not there. With display, the play is shown on
 * stdout as it happens; with resume, the game goes on from where the
 * script ends, taking commands from the console. */
int accept_play(const accept_script *script, int display, int resume, accept_result *result);

void accept_result_free(accept_result *result);

/* Find the first place two texts differ, line by line, filling in the
 * difference and returning 1, or return 0 when they are the same. Line
 * endings do not count, since an expected file may have been through a
 * tool that changed them. */
int accept_first_difference(const char *expected, const char *actual, accept_difference *difference);

void accept_difference_free(accept_difference *difference);

/* Say where a difference is in both files: the line of the expected
 * file, and the line of the script whose command the play was answering
 * at that point. Returns a string to free. */
char *accept_describe(const accept_difference *difference, const accept_result *result, const accept_script *script);

/* The --accept command: play the script at script_path and return the
 * program's exit status, 0 when the play matched or was recorded, or
 * the resumed game ended; 1 when the play differed or the script could
 * not be run; ACCEPT_EXIT_SKIPPED when its game is not there. games_dir
 * may be NULL. */
int accept_run(const char *script_path, accept_mode mode, const char *games_dir);

/* Keep the lines typed in a resumed session at the end of the script,
 * under a fence labelled tried, which the runner never plays, so that
 * the ones that worked can be moved above it. A line that would read
 * as a comment, a directive, or a fence is written with the prompt in
 * front, so that it is the command it was when it moves. With
 * close_fence, a fence the script ended inside is closed first. Nothing
 * is written for no lines. Returns 1, or 0 after filling in error. */
int accept_append_tried(const char *script_path, int close_fence, char *const *lines, unsigned count, char *error);

/* The --create option: write a new script at script_path that names
 * game, with seed 1 and no commands, so that a run with --resume hands
 * the game over from its start and the commands that work can be put
 * into the file. The file is written with LF line endings on every
 * platform. A script that is there already is left alone, and a game
 * that cannot be found leaves no file behind. Returns 1, or 0 after
 * filling in error. games_dir may be NULL. */
int accept_create(const char *script_path, const char *game, const char *games_dir, char *error);

#endif /* QUIDDITY_ACCEPT_H */
