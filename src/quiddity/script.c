/*
 * script.c: see script.h.
 */

#include "script.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The keys a script may name instead of numbering, as their ZSCII input
 * codes (Spec §3.8.2): the cursor keys, escape, and the space bar. */
static const struct {
    const char *name;
    unsigned code;
} key_names[] = {
    { "<up>", 129 },
    { "<down>", 130 },
    { "<left>", 131 },
    { "<right>", 132 },
    { "<escape>", 27 },
    { "<space>", 32 },
};

static char *copy_string(const char *s, size_t length)
{
    char *copy = malloc(length + 1);

    if (copy != NULL) {
        memcpy(copy, s, length);
        copy[length] = '\0';
    }
    return copy;
}

static int is_absolute(const char *path)
{
    return path[0] == '/' || path[0] == '\\' || (isalpha((unsigned char)path[0]) && path[1] == ':');
}

/* Where the file name begins in a path. */
static size_t base_name_at(const char *path)
{
    size_t at = 0;

    for (size_t i = 0; path[i] != '\0'; i++) {
        if (path[i] == '/' || path[i] == '\\') {
            at = i + 1;
        }
    }
    return at;
}

static int file_exists(const char *path)
{
    FILE *file = fopen(path, "rb");

    if (file == NULL) {
        return 0;
    }
    fclose(file);
    return 1;
}

/* directory/value, or value alone when it is absolute. */
static char *join(const char *directory, size_t directory_length, const char *value)
{
    size_t length = strlen(value);
    char *path;

    if (is_absolute(value) || directory_length == 0) {
        return copy_string(value, length);
    }
    path = malloc(directory_length + 1 + length + 1);
    if (path == NULL) {
        return NULL;
    }
    memcpy(path, directory, directory_length);
    path[directory_length] = '/';
    memcpy(path + directory_length + 1, value, length + 1);
    return path;
}

/* A file the script names: beside the script, as written, or, when it
 * is not there and the runner has a games directory, under that. The
 * scripts in the repository name their games under the games directory,
 * which is wherever the person running them keeps the corpus. */
static char *resolve(const char *script_dir, size_t script_dir_length, const char *games_dir, const char *value)
{
    char *beside = join(script_dir, script_dir_length, value);
    char *under;

    if (beside == NULL || games_dir == NULL || games_dir[0] == '\0' || is_absolute(value) || file_exists(beside)) {
        return beside;
    }
    under = join(games_dir, strlen(games_dir), value);
    if (under == NULL) {
        return beside;
    }
    free(beside);
    return under;
}

static int yes_or_no(const char *value, int *flag)
{
    static const char *const yes[] = { "1", "YES", "ON", "TRUE" };
    static const char *const no[] = { "0", "NO", "OFF", "FALSE" };
    char upper[8];
    size_t i;

    for (i = 0; value[i] != '\0' && i < sizeof upper - 1; i++) {
        upper[i] = (char)toupper((unsigned char)value[i]);
    }
    upper[i] = '\0';
    if (value[i] != '\0') {
        return 0;
    }
    for (i = 0; i < 4; i++) {
        if (strcmp(upper, yes[i]) == 0) {
            *flag = 1;
            return 1;
        }
        if (strcmp(upper, no[i]) == 0) {
            *flag = 0;
            return 1;
        }
    }
    return 0;
}

static int add_command(accept_script *s, const char *text, size_t length, unsigned line)
{
    char **commands = realloc(s->commands, (s->command_count + 1) * sizeof *commands);
    unsigned *lines;

    if (commands == NULL) {
        return 0;
    }
    s->commands = commands;
    lines = realloc(s->command_lines, (s->command_count + 1) * sizeof *lines);
    if (lines == NULL) {
        return 0;
    }
    s->command_lines = lines;
    commands[s->command_count] = copy_string(text, length);
    if (commands[s->command_count] == NULL) {
        return 0;
    }
    lines[s->command_count] = line;
    s->command_count++;
    return 1;
}

static int add_seed_change(accept_script *s, unsigned index, uint32_t seed)
{
    accept_seed_change *changes;

    /* A second change at the same place replaces the first, as a later
     * line should. */
    for (unsigned i = 0; i < s->seed_change_count; i++) {
        if (s->seed_changes[i].index == index) {
            s->seed_changes[i].seed = seed;
            return 1;
        }
    }
    changes = realloc(s->seed_changes, (s->seed_change_count + 1) * sizeof *changes);
    if (changes == NULL) {
        return 0;
    }
    s->seed_changes = changes;
    changes[s->seed_change_count].index = index;
    changes[s->seed_change_count].seed = seed;
    s->seed_change_count++;
    return 1;
}

/* A whole number of 1 or more, as SEED wants. */
static int parse_seed(const char *value, uint32_t *seed)
{
    char *end = NULL;
    unsigned long parsed;

    if (!isdigit((unsigned char)value[0])) {
        return 0;
    }
    parsed = strtoul(value, &end, 10);
    if (*end != '\0' || parsed < 1 || parsed > 0xffffffffUL) {
        return 0;
    }
    *seed = (uint32_t)parsed;
    return 1;
}

/* A whole number of 1 to 255, as HEIGHT wants. */
static int parse_height(const char *value, unsigned *height)
{
    uint32_t parsed;

    if (!parse_seed(value, &parsed) || parsed > 255) {
        return 0;
    }
    *height = (unsigned)parsed;
    return 1;
}

/* A touch of a window or a link, as "<click column,row>" or "<link
 * value>", which belong to Glulx games: turned into the command form a
 * display understands, "[click column,row]" or "[link value]". Returns
 * 1 for one of the two, 0 for anything else, and -1 for one written
 * wrongly, with the explanation in error. */
static int pointer_command(const char *inside, size_t length, char *out, size_t out_size, char *error, const char *name,
                           unsigned line)
{
    char text[64];
    unsigned long a, b;
    char *end;

    if (length >= sizeof text) {
        return 0;
    }
    memcpy(text, inside, length);
    text[length] = '\0';

    if (strncmp(text, "click ", 6) == 0 || strncmp(text, "CLICK ", 6) == 0 || strncmp(text, "Click ", 6) == 0) {
        const char *at = text + 6;
        const char *comma = strchr(at, ',');

        while (*at == ' ') {
            at++;
        }
        if (comma == NULL || !isdigit((unsigned char)*at)) {
            goto bad_click;
        }
        a = strtoul(at, &end, 10);
        while (*end == ' ') {
            end++;
        }
        if (end != comma) {
            goto bad_click;
        }
        at = comma + 1;
        while (*at == ' ') {
            at++;
        }
        if (!isdigit((unsigned char)*at)) {
            goto bad_click;
        }
        b = strtoul(at, &end, 10);
        while (*end == ' ') {
            end++;
        }
        if (*end != '\0') {
            goto bad_click;
        }
        snprintf(out, out_size, "[click %lu,%lu]", a, b);
        return 1;
    bad_click:
        snprintf(error, ACCEPT_ERROR_MAX,
                 "%s, line %u: <%s> is not a click; a click is written <click column,row>, counting from zero at "
                 "the window's top left corner",
                 name, line, text);
        return -1;
    }

    if (strncmp(text, "link ", 5) == 0 || strncmp(text, "LINK ", 5) == 0 || strncmp(text, "Link ", 5) == 0) {
        const char *at = text + 5;

        while (*at == ' ') {
            at++;
        }
        if (isdigit((unsigned char)*at)) {
            a = strtoul(at, &end, 10);
            while (*end == ' ') {
                end++;
            }
            if (*end == '\0' && a != 0) {
                snprintf(out, out_size, "[link %lu]", a);
                return 1;
            }
        }
        snprintf(error, ACCEPT_ERROR_MAX, "%s, line %u: <%s> is not a link; a link is written <link value>, and a link value is never zero",
                 name, line, text);
        return -1;
    }

    return 0;
}

/* The parse itself, which leaves whatever it allocated for
 * accept_script_parse() to free when it fails. */
static int parse(accept_script *s, const char *text, const char *path, const char *games_dir, char *error)
{
    size_t base = base_name_at(path);
    size_t directory_length = base > 0 ? base - 1 : 0;
    const char *directory = path;
    const char *name = path + base;
    int have_seed = 0;
    unsigned fenced_from = 0;
    unsigned line_number = 0;
    const char *at = text;

    memset(s, 0, sizeof *s);
    error[0] = '\0';

    s->path = copy_string(path, strlen(path));
    s->name = copy_string(path + base, strlen(path + base));
    if (s->path == NULL || s->name == NULL) {
        goto out_of_memory;
    }

    /* The expected file: the script's name with its extension changed. */
    {
        size_t length = strlen(path);
        size_t stem = length;

        for (size_t i = length; i > base; i--) {
            if (path[i - 1] == '.') {
                stem = i - 1;
                break;
            }
        }
        s->expected_path = malloc(stem + sizeof ".expected");
        if (s->expected_path == NULL) {
            goto out_of_memory;
        }
        memcpy(s->expected_path, path, stem);
        strcpy(s->expected_path + stem, ".expected");
    }

    while (*at != '\0') {
        const char *end = strchr(at, '\n');
        size_t length = end != NULL ? (size_t)(end - at) : strlen(at);
        const char *line = at;
        const char *trimmed;
        size_t trimmed_length;

        line_number++;
        at = end != NULL ? end + 1 : at + length;
        if (length > 0 && line[length - 1] == '\r') {
            length--;
        }

        trimmed = line;
        trimmed_length = length;
        while (trimmed_length > 0 && isspace((unsigned char)trimmed[0])) {
            trimmed++;
            trimmed_length--;
        }
        while (trimmed_length > 0 && isspace((unsigned char)trimmed[trimmed_length - 1])) {
            trimmed_length--;
        }

        if (trimmed_length >= 3 && strncmp(trimmed, "```", 3) == 0) {
            size_t label = 3;

            while (label < trimmed_length && isspace((unsigned char)trimmed[label])) {
                label++;
            }
            if (fenced_from == 0) {
                fenced_from = line_number;
            } else if (label == trimmed_length) {
                fenced_from = 0;
            } else {
                snprintf(error, ACCEPT_ERROR_MAX,
                         "%s, line %u: a fenced section is still open from line %u; close it with a bare ``` first",
                         name, line_number, fenced_from);
                return 0;
            }
            continue;
        }

        if (fenced_from != 0 || trimmed_length == 0 || trimmed[0] == '#') {
            continue;
        }

        if (trimmed[0] == '>') {
            const char *command = trimmed + 1;
            size_t command_length = trimmed_length - 1;

            while (command_length > 0 && isspace((unsigned char)command[0])) {
                command++;
                command_length--;
            }
            if (!add_command(s, command, command_length, line_number)) {
                goto out_of_memory;
            }
            continue;
        }

        /* A named key is written as its code, which is what the command
         * file format takes; a name that is not a key is more likely a
         * typo than a command, and is refused so that its letters are
         * not typed into the game. */
        if (trimmed[0] == '<' && trimmed[trimmed_length - 1] == '>') {
            char command[64];
            char lowered[16];
            int pointer = pointer_command(trimmed + 1, trimmed_length - 2, command, sizeof command, error, name,
                                          line_number);
            size_t i;

            if (pointer < 0) {
                return 0;
            }
            if (pointer > 0) {
                if (!add_command(s, command, strlen(command), line_number)) {
                    goto out_of_memory;
                }
                continue;
            }

            for (i = 0; i < trimmed_length && i < sizeof lowered - 1; i++) {
                lowered[i] = (char)tolower((unsigned char)trimmed[i]);
            }
            lowered[i] = '\0';
            for (i = 0; i < sizeof key_names / sizeof key_names[0]; i++) {
                if (trimmed_length < sizeof lowered && strcmp(lowered, key_names[i].name) == 0) {
                    snprintf(command, sizeof command, "[%u]", key_names[i].code);
                    break;
                }
            }
            if (i == sizeof key_names / sizeof key_names[0]) {
                snprintf(error, ACCEPT_ERROR_MAX,
                         "%s, line %u: %.*s is not a key; the keys are <up>, <down>, <left>, <right>, <escape>, "
                         "<space>, or <click column,row> and <link value>, and a command that begins with < is "
                         "written with the prompt in front",
                         name, line_number, (int)trimmed_length, trimmed);
                return 0;
            }
            if (!add_command(s, command, strlen(command), line_number)) {
                goto out_of_memory;
            }
            continue;
        }

        if (trimmed[0] != '!') {
            if (!add_command(s, line, length, line_number)) {
                goto out_of_memory;
            }
            continue;
        }

        /* A directive: ! NAME=value. */
        {
            const char *directive = trimmed + 1;
            size_t directive_length = trimmed_length - 1;
            const char *equals;
            char key[32];
            char value[1024];
            size_t key_length;
            size_t value_length;
            size_t i;

            while (directive_length > 0 && isspace((unsigned char)directive[0])) {
                directive++;
                directive_length--;
            }
            equals = memchr(directive, '=', directive_length);
            if (equals == NULL) {
                snprintf(error, ACCEPT_ERROR_MAX, "%s, line %u: a directive is written ! NAME=value", name, line_number);
                return 0;
            }
            key_length = (size_t)(equals - directive);
            while (key_length > 0 && isspace((unsigned char)directive[key_length - 1])) {
                key_length--;
            }
            value_length = directive_length - (size_t)(equals + 1 - directive);
            {
                const char *v = equals + 1;

                while (value_length > 0 && isspace((unsigned char)v[0])) {
                    v++;
                    value_length--;
                }
                while (value_length > 0 && isspace((unsigned char)v[value_length - 1])) {
                    value_length--;
                }
                if (value_length >= sizeof value) {
                    value_length = sizeof value - 1;
                }
                memcpy(value, v, value_length);
                value[value_length] = '\0';
            }
            for (i = 0; i < key_length && i < sizeof key - 1; i++) {
                key[i] = (char)toupper((unsigned char)directive[i]);
            }
            key[i] = '\0';

            if (strcmp(key, "GAME") == 0) {
                free(s->game_path);
                s->game_path = resolve(directory, directory_length, games_dir, value);
                if (s->game_path == NULL) {
                    goto out_of_memory;
                }
            } else if (strcmp(key, "BLORB") == 0) {
                free(s->blorb_path);
                s->blorb_path = resolve(directory, directory_length, games_dir, value);
                if (s->blorb_path == NULL) {
                    goto out_of_memory;
                }
            } else if (strcmp(key, "INTERPRETER") == 0) {
                free(s->interpreter);
                s->interpreter = copy_string(value, value_length);
                if (s->interpreter == NULL) {
                    goto out_of_memory;
                }
            } else if (strcmp(key, "TANDY") == 0 || strcmp(key, "UPPER") == 0 || strcmp(key, "PICTURES") == 0
                       || strcmp(key, "GRAPHICS") == 0) {
                int *flag = key[0] == 'T' ? &s->tandy : key[0] == 'U' ? &s->upper : key[0] == 'P' ? &s->pictures
                                                                                                    : &s->graphics;

                if (!yes_or_no(value, flag)) {
                    snprintf(error, ACCEPT_ERROR_MAX, "%s, line %u: %s must be yes or no", name, line_number, key);
                    return 0;
                }
            } else if (strcmp(key, "HEIGHT") == 0) {
                if (!parse_height(value, &s->height)) {
                    snprintf(error, ACCEPT_ERROR_MAX, "%s, line %u: HEIGHT must be a whole number of 1 to 255", name,
                             line_number);
                    return 0;
                }
            } else if (strcmp(key, "SEED") == 0) {
                uint32_t seed;

                if (!parse_seed(value, &seed)) {
                    snprintf(error, ACCEPT_ERROR_MAX, "%s, line %u: SEED must be a whole number of 1 or more", name,
                             line_number);
                    return 0;
                }
                if (s->command_count == 0) {
                    s->seed = seed;
                    have_seed = 1;
                } else if (!have_seed) {
                    snprintf(error, ACCEPT_ERROR_MAX,
                             "%s, line %u: the first SEED directive must come before the first command", name,
                             line_number);
                    return 0;
                } else if (!add_seed_change(s, s->command_count, seed)) {
                    goto out_of_memory;
                }
            } else {
                snprintf(error, ACCEPT_ERROR_MAX,
                         "%s, line %u: there is no %.*s directive; the directives are GAME, SEED, BLORB, INTERPRETER, "
                         "TANDY, UPPER, HEIGHT, PICTURES, and GRAPHICS",
                         name, line_number, (int)key_length, directive);
                return 0;
            }
        }
    }

    s->ends_fenced = fenced_from != 0;

    if (s->game_path == NULL) {
        snprintf(error, ACCEPT_ERROR_MAX, "%s: no GAME directive names the story file", name);
        return 0;
    }
    if (!have_seed) {
        snprintf(error, ACCEPT_ERROR_MAX, "%s: no SEED directive gives the random number seed", name);
        return 0;
    }
    return 1;

out_of_memory:
    snprintf(error, ACCEPT_ERROR_MAX, "%s: out of memory", name);
    return 0;
}

int accept_script_parse(accept_script *s, const char *text, const char *path, const char *games_dir, char *error)
{
    if (parse(s, text, path, games_dir, error)) {
        return 1;
    }
    accept_script_free(s);
    return 0;
}

int accept_script_load(accept_script *s, const char *path, const char *games_dir, char *error)
{
    FILE *file = fopen(path, "rb");
    char *text;
    size_t size;
    size_t read;
    int ok;

    memset(s, 0, sizeof *s);
    if (file == NULL) {
        snprintf(error, ACCEPT_ERROR_MAX, "no such file: %s", path);
        return 0;
    }
    if (fseek(file, 0, SEEK_END) != 0 || (size = (size_t)ftell(file)) == (size_t)-1 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        snprintf(error, ACCEPT_ERROR_MAX, "%s cannot be read", path);
        return 0;
    }
    text = malloc(size + 1);
    if (text == NULL) {
        fclose(file);
        snprintf(error, ACCEPT_ERROR_MAX, "out of memory");
        return 0;
    }
    read = fread(text, 1, size, file);
    fclose(file);
    text[read] = '\0';

    ok = accept_script_parse(s, text, path, games_dir, error);
    free(text);
    return ok;
}

void accept_script_free(accept_script *s)
{
    free(s->path);
    free(s->name);
    free(s->expected_path);
    free(s->game_path);
    free(s->blorb_path);
    free(s->interpreter);
    for (unsigned i = 0; i < s->command_count; i++) {
        free(s->commands[i]);
    }
    free(s->commands);
    free(s->command_lines);
    free(s->seed_changes);
    memset(s, 0, sizeof *s);
}

int accept_script_seed_change(const accept_script *s, unsigned index, uint32_t *seed)
{
    for (unsigned i = 0; i < s->seed_change_count; i++) {
        if (s->seed_changes[i].index == index) {
            *seed = s->seed_changes[i].seed;
            return 1;
        }
    }
    return 0;
}
