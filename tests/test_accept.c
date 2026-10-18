/*
 * test_accept.c: acceptance scripts.
 *
 * Covers src/quiddity/script.h and accept.h: how a script is read, how a
 * play is compared with its recording and a difference placed in both
 * files, and that a play of one of the stories in tests/data comes out
 * as a record. The scripts that play real games live under acceptance/
 * and are tests of their own.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "accept.h"
#include "script.h"

#include "check.h"

#define SCRIPT_PATH QUIDDITY_TEST_DATA_DIR "/test.accept"

static int ends_with(const char *s, const char *suffix)
{
    size_t n = strlen(s), m = strlen(suffix);

    return n >= m && strcmp(s + n - m, suffix) == 0;
}

static int contains(const char *s, const char *needle)
{
    return strstr(s, needle) != NULL;
}

static void test_a_script_has_directives_comments_and_commands(void)
{
    accept_script s;
    char error[ACCEPT_ERROR_MAX];

    CHECK(accept_script_parse(&s, "# Zork, briefly.\n! SEED=20\n!GAME = ../games/zork1.z3\n\nn. n. u\n> get egg\nlook[13]\n>#help\n",
                              SCRIPT_PATH, NULL, error));
    CHECK_EQ_INT(s.seed, 20);
    CHECK(ends_with(s.game_path, "/../games/zork1.z3"));
    CHECK(strncmp(s.game_path, QUIDDITY_TEST_DATA_DIR, strlen(QUIDDITY_TEST_DATA_DIR)) == 0);
    CHECK(s.blorb_path == NULL);
    CHECK_EQ_INT(s.command_count, 4);
    CHECK_EQ_STR(s.commands[0], "n. n. u");
    CHECK_EQ_STR(s.commands[1], "get egg");
    CHECK_EQ_STR(s.commands[2], "look[13]");
    CHECK_EQ_STR(s.commands[3], "#help");
    CHECK_EQ_INT(s.command_lines[0], 5);
    CHECK_EQ_INT(s.command_lines[1], 6);
    CHECK_EQ_INT(s.command_lines[2], 7);
    CHECK_EQ_INT(s.command_lines[3], 8);
    CHECK_EQ_STR(s.expected_path, QUIDDITY_TEST_DATA_DIR "/test.expected");
    CHECK_EQ_STR(s.name, "test.accept");
    accept_script_free(&s);
}

static void test_a_seed_after_commands_is_a_change_of_seed(void)
{
    accept_script s;
    char error[ACCEPT_ERROR_MAX];
    uint32_t seed = 0;

    CHECK(accept_script_parse(&s, "! SEED=1000\n! GAME=zork1.z3\nd. w\nkill troll\n! SEED=1010\ns\n! SEED=1020\n! SEED=1021\nget all\n! SEED=1030\n",
                              SCRIPT_PATH, NULL, error));
    CHECK_EQ_INT(s.seed, 1000);
    CHECK_EQ_INT(s.command_count, 4);
    CHECK_EQ_INT(s.seed_change_count, 3);
    CHECK(accept_script_seed_change(&s, 2, &seed));
    CHECK_EQ_INT(seed, 1010);
    CHECK(accept_script_seed_change(&s, 3, &seed));
    CHECK_EQ_INT(seed, 1021);
    CHECK(accept_script_seed_change(&s, 4, &seed));
    CHECK_EQ_INT(seed, 1030);
    CHECK(!accept_script_seed_change(&s, 1, &seed));
    accept_script_free(&s);

    CHECK(!accept_script_parse(&s, "! GAME=zork1.z3\nlook\n! SEED=1000\n", SCRIPT_PATH, NULL, error));
    CHECK_EQ_STR(error, "test.accept, line 3: the first SEED directive must come before the first command");
}

static void test_a_script_can_name_keys_and_touch_a_window(void)
{
    accept_script s;
    char error[ACCEPT_ERROR_MAX];
    static const char *const refused[] = { "<click 3>", "<click 3,>", "<click left,1>", "<link>", "<link 0>",
                                           "<link two>", "<home>" };
    static const char *const complaints[] = { "is not a click", "is not a click", "is not a click", "is not a key",
                                              "is not a link", "is not a link", "is not a key" };

    CHECK(accept_script_parse(&s, "! SEED=1\n! GAME=windowtest.ulx\n<click 3,1>\n<CLICK 0, 0>\n<link 5>\n<up>\n<Escape>\n<space>\n",
                              SCRIPT_PATH, NULL, error));
    CHECK_EQ_INT(s.command_count, 6);
    CHECK_EQ_STR(s.commands[0], "[click 3,1]");
    CHECK_EQ_STR(s.commands[1], "[click 0,0]");
    CHECK_EQ_STR(s.commands[2], "[link 5]");
    CHECK_EQ_STR(s.commands[3], "[129]");
    CHECK_EQ_STR(s.commands[4], "[27]");
    CHECK_EQ_STR(s.commands[5], "[32]");
    accept_script_free(&s);

    /* A mistyped key or click would otherwise be typed into the game as
     * text, which is worse than refusing the script. */
    for (size_t i = 0; i < sizeof refused / sizeof refused[0]; i++) {
        char text[128];

        snprintf(text, sizeof text, "! SEED=1\n! GAME=windowtest.ulx\n%s\n", refused[i]);
        CHECK(!accept_script_parse(&s, text, SCRIPT_PATH, NULL, error));
        CHECK(strncmp(error, "test.accept, line 3: ", 21) == 0);
        CHECK(contains(error, complaints[i]));
    }
}

static void test_a_fenced_section_is_not_read(void)
{
    accept_script s;
    char error[ACCEPT_ERROR_MAX];

    CHECK(accept_script_parse(&s, "! SEED=1\n! GAME=zork1.z3\ngo north\n``` PATH 1\nkill troll\n! SEED=99\n```\nsave troll\n```\ntake axe\n",
                              SCRIPT_PATH, NULL, error));
    CHECK_EQ_INT(s.seed, 1);
    CHECK_EQ_INT(s.command_count, 2);
    CHECK_EQ_STR(s.commands[0], "go north");
    CHECK_EQ_STR(s.commands[1], "save troll");
    CHECK_EQ_INT(s.command_lines[0], 3);
    CHECK_EQ_INT(s.command_lines[1], 8);
    CHECK_EQ_INT(s.seed_change_count, 0);
    accept_script_free(&s);

    CHECK(!accept_script_parse(&s, "! SEED=1\n! GAME=zork1.z3\n``` PATH 1\nkill troll\n``` PATH 2\ntake axe\n",
                               SCRIPT_PATH, NULL, error));
    CHECK_EQ_STR(error, "test.accept, line 5: a fenced section is still open from line 3; close it with a bare ``` first");
}

static void test_paths_are_relative_to_the_script_or_the_games_directory(void)
{
    accept_script s;
    char error[ACCEPT_ERROR_MAX];

    /* A path that is not under the script's directory falls back to the
     * games directory; one that is stays where it is. */
    CHECK(accept_script_parse(&s, "! SEED=1\n! GAME=simple-test-r1-s260727.z3\n! BLORB=sounds/lurking.blb\n",
                              "/nowhere/test.accept", QUIDDITY_TEST_DATA_DIR, error));
    CHECK_EQ_STR(s.game_path, QUIDDITY_TEST_DATA_DIR "/simple-test-r1-s260727.z3");
    CHECK_EQ_STR(s.blorb_path, QUIDDITY_TEST_DATA_DIR "/sounds/lurking.blb");
    accept_script_free(&s);

    CHECK(accept_script_parse(&s, "! SEED=1\n! GAME=simple-test-r1-s260727.z3\n", SCRIPT_PATH, "/elsewhere", error));
    CHECK_EQ_STR(s.game_path, QUIDDITY_TEST_DATA_DIR "/simple-test-r1-s260727.z3");
    accept_script_free(&s);

    CHECK(accept_script_parse(&s, "! SEED=1\n! GAME=/absolute/zork1.z3\n", SCRIPT_PATH, "/elsewhere", error));
    CHECK_EQ_STR(s.game_path, "/absolute/zork1.z3");
    accept_script_free(&s);
}

static void test_a_script_that_cannot_be_used_says_why(void)
{
    static const struct {
        const char *text;
        const char *reason;
    } cases[] = {
        { "! SEED=20\n", "no GAME directive" },
        { "! GAME=zork1.z3\n", "no SEED directive" },
        { "! GAME=zork1.z3\n! SEED=0\n", "line 2: SEED must be" },
        { "! GAME=zork1.z3\n! SEED=soon\n", "line 2: SEED must be" },
        { "! GAME=zork1.z3\n! SEED=1\n! SAVE=here\n", "line 3: there is no SAVE directive" },
        { "! GAME=zork1.z3\n! SEED=1\n! hello\n", "line 3: a directive is written" },
        { "! GAME=zork1.z3\n! SEED=1\n! TANDY=maybe\n", "line 3: TANDY must be yes or no" },
    };
    accept_script s;
    char error[ACCEPT_ERROR_MAX];

    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        CHECK(!accept_script_parse(&s, cases[i].text, SCRIPT_PATH, NULL, error));
        CHECK(strncmp(error, "test.accept", 11) == 0);
        CHECK(contains(error, cases[i].reason));
    }

    CHECK(accept_script_parse(&s, "! GAME=zork1.z3\n! SEED=1\n! TANDY=yes\n! UPPER=On\n! PICTURES=1\n! INTERPRETER=amiga\n",
                              SCRIPT_PATH, NULL, error));
    CHECK_EQ_INT(s.tandy, 1);
    CHECK_EQ_INT(s.upper, 1);
    CHECK_EQ_INT(s.pictures, 1);
    CHECK_EQ_INT(s.graphics, 0);
    CHECK_EQ_STR(s.interpreter, "amiga");
    accept_script_free(&s);
}

static void test_the_first_difference_is_found_by_line(void)
{
    accept_difference d;

    CHECK(!accept_first_difference("a\nb\n", "a\r\nb\r\n", &d));
    CHECK(!accept_first_difference("", "", &d));

    CHECK(accept_first_difference("a\nb\n", "a\nc\n", &d));
    CHECK_EQ_INT(d.line, 2);
    CHECK_EQ_INT(d.recording_ended, 0);
    CHECK_EQ_STR(d.description, "  expected: b\n  actual:   c");
    accept_difference_free(&d);

    CHECK(accept_first_difference("a\nb\nc\n", "a\nb\n", &d));
    CHECK_EQ_INT(d.line, 3);
    CHECK(contains(d.description, "1 more were expected"));
    accept_difference_free(&d);

    CHECK(accept_first_difference("a\nb\n", "a\nb\nc\n", &d));
    CHECK_EQ_INT(d.line, 3);
    CHECK_EQ_INT(d.recording_ended, 1);
    CHECK(contains(d.description, "goes on for 1 more lines"));
    accept_difference_free(&d);

    /* The recording of a short script ends with a bare prompt; the same
     * script grown has a command after that prompt. That is the
     * recording running out, not a change in the play, and it is told
     * apart from a real difference. */
    CHECK(accept_first_difference("Living Room\n>", "Living Room\n>x toys\nThey are everywhere.\n>", &d));
    CHECK_EQ_INT(d.recording_ended, 1);
    CHECK_EQ_INT(d.line, 2);
    CHECK(contains(d.description, "goes on for 2 more lines"));
    accept_difference_free(&d);

    CHECK(accept_first_difference("Living Room\n>", "Kitchen\n>x toys\n", &d));
    CHECK_EQ_INT(d.recording_ended, 0);
    CHECK_EQ_INT(d.line, 1);
    accept_difference_free(&d);
}

static void test_a_difference_is_placed_in_both_files(void)
{
    /* The play: a banner, then two commands and their replies. The
     * offsets are where the output stood when each command was read,
     * which is just after its prompt. */
    static char play[] = "West of House\n>look\nYou see a house.\n>north\nNorth of House\n";
    unsigned offsets[2];
    accept_script script;
    accept_result result;
    accept_difference d;
    char error[ACCEPT_ERROR_MAX];
    char *text;

    offsets[0] = (unsigned)(strstr(play, ">look") - play) + 1;
    offsets[1] = (unsigned)(strstr(play, ">north") - play) + 1;
    CHECK(accept_script_parse(&script, "! SEED=1\n! GAME=zork1.z3\n\nlook\n\nnorth\n", SCRIPT_PATH, NULL, error));
    memset(&result, 0, sizeof result);
    result.text = play;
    result.output_length = sizeof play - 1;
    result.ending = ACCEPT_SCRIPT_ENDED;
    result.offsets = offsets;
    result.offset_count = 2;
    memset(&d, 0, sizeof d);
    d.description = "x";

    d.line = 1;
    text = accept_describe(&d, &result, &script);
    CHECK(strncmp(text, "line 1 of test.expected differs, before the first command", 57) == 0);
    free(text);

    d.line = 3;
    text = accept_describe(&d, &result, &script);
    CHECK(strncmp(text, "line 3 of test.expected differs, while playing line 4 of the script, \"look\"", 74) == 0);
    free(text);

    d.line = 4;
    text = accept_describe(&d, &result, &script);
    CHECK(strncmp(text, "line 4 of test.expected differs, while playing line 6 of the script, \"north\"", 75) == 0);
    CHECK(ends_with(text, "\nx"));
    free(text);

    d.recording_ended = 1;
    text = accept_describe(&d, &result, &script);
    CHECK(strncmp(text, "line 4 of test.expected is its last, while playing line 6 of the script, \"north\"", 79) == 0);
    free(text);

    accept_script_free(&script);
}

static void test_a_play_is_recorded_as_text(void)
{
    accept_script script;
    accept_result result;
    char error[ACCEPT_ERROR_MAX];

    /* The simplest story there is prints a line and quits, so the play
     * ends with the game and holds that line alone. Any Version will do,
     * and the stream has to serve them all. */
    for (unsigned version = 1; version <= 8; version++) {
        char text[160];

        snprintf(text, sizeof text, "! SEED=7\n! GAME=simple-test-r%u-s260727.z%u\n", version <= 2 ? 0u : 1u, version);
        CHECK(accept_script_parse(&script, text, SCRIPT_PATH, NULL, error));
        CHECK_EQ_INT(accept_play(&script, 0, 0, &result), 1);
        CHECK_EQ_INT(result.ending, ACCEPT_QUIT);
        CHECK_EQ_STR(result.text, "hello from all z machine versions");
        CHECK_EQ_INT(result.output_length, strlen("hello from all z machine versions"));
        CHECK_EQ_INT(result.offset_count, 0);
        accept_result_free(&result);
        accept_script_free(&script);
    }

    /* A script whose game is not there is skipped, not failed, since
     * the games live outside the repository. */
    CHECK(accept_script_parse(&script, "! SEED=7\n! GAME=no-such-game.z5\n", SCRIPT_PATH, NULL, error));
    CHECK_EQ_INT(accept_play(&script, 0, 0, &result), ACCEPT_EXIT_SKIPPED);
    accept_script_free(&script);
}

int main(void)
{
    RUN_TEST(test_a_script_has_directives_comments_and_commands);
    RUN_TEST(test_a_seed_after_commands_is_a_change_of_seed);
    RUN_TEST(test_a_script_can_name_keys_and_touch_a_window);
    RUN_TEST(test_a_fenced_section_is_not_read);
    RUN_TEST(test_paths_are_relative_to_the_script_or_the_games_directory);
    RUN_TEST(test_a_script_that_cannot_be_used_says_why);
    RUN_TEST(test_the_first_difference_is_found_by_line);
    RUN_TEST(test_a_difference_is_placed_in_both_files);
    RUN_TEST(test_a_play_is_recorded_as_text);
    return CHECK_SUMMARY();
}
