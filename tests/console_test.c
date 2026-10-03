// The console: cvars, commands, the parser, binds, the command line, saving.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "console/console.h"
#include "test.h"

#define SAVED "build/console_test.cfg"

// What the +attack / -attack commands saw.
static int presses, releases;

static void attack(Console *con, int argc, char **argv, void *user)
{
    (void)con, (void)argc, (void)user;
    if (argv[0][0] == '+') presses++;
    else releases++;
}

// The newest scrollback line.
static const char *last(const Console *con) { return console_log_line(con, 0); }

static void cvars_and_command_line(void)
{
    Console *con = console_create(NULL, NULL);
    char *argv[] = {"client", "-assets", "x", "+set", "name", "Major Pain", "+sensitivity", "2.5", "+echo", "a;b"};
    console_execute_args(con, 10, argv);
    CHECK(strcmp(last(con), "a;b") == 0, "a '+' word runs a command, its arguments whole (\"%s\")", last(con));

    Cvar *sens = cvar_register(con, "sensitivity", "1", CVAR_ARCHIVE, "mouse speed");
    Cvar *name = cvar_register(con, "name", "Player", CVAR_ARCHIVE, NULL);
    CHECK(sens->number == 2.5f && sens->integer == 2, "a cvar set on the command line before it exists keeps its value");
    CHECK(strcmp(name->value, "Major Pain") == 0, "and so does one set with +set");

    Cvar *version = cvar_register(con, "version", "0.1", CVAR_READONLY, NULL);
    console_execute(con, "version 2");
    CHECK(strcmp(version->value, "0.1") == 0, "a read-only cvar can't be set from the console");

    console_execute(con, "toggle sensitivity");
    CHECK(sens->integer == 0, "toggle flips a cvar");
    console_execute(con, "reset sensitivity");
    CHECK(sens->number == 1.0f, "reset puts back the default");
    console_execute(con, "set loop \"vstr loop\"; vstr loop");
    CHECK(strstr(last(con), "too deep") != NULL, "a vstr loop is stopped");
    console_execute(con, "nope");
    CHECK(strcmp(last(con), "unknown command: nope") == 0, "an unknown command says so");
    console_destroy(con);
}

static void parser(void)
{
    Console *con = console_create(NULL, NULL);
    console_execute(con, "echo \"a;b\" c; echo x//y z");
    CHECK(strcmp(console_log_line(con, 1), "a;b c") == 0, "quotes keep a ';' in a word");
    CHECK(strcmp(last(con), "x") == 0, "// comments out the rest of the line");
    console_execute(con, "echo \"open quote; still\necho next line");
    CHECK(strcmp(console_log_line(con, 1), "open quote; still") == 0 && strcmp(last(con), "next line") == 0,
          "an unclosed quote ends with its line");
    console_execute(con, "echo   spaced    out  ;;; echo a\"b\"c");
    CHECK(strcmp(console_log_line(con, 1), "spaced out") == 0 && strcmp(last(con), "a b c") == 0, "words split at spaces and quotes");
    console_destroy(con);
}

static void binds_and_saving(void)
{
    Console *con = console_create(NULL, NULL);
    console_add_command(con, "+attack", attack, NULL, NULL);
    console_add_command(con, "-attack", attack, NULL, NULL);
    Cvar *fov = cvar_register(con, "fov", "90", CVAR_ARCHIVE, NULL);
    console_execute(con, "/bind mouse1 +attack; bind space \"echo hi; echo there\"; set fov 100");

    presses = releases = 0;
    console_key(con, "MOUSE1", true);
    console_key(con, "mouse1", false);
    CHECK(presses == 1 && releases == 1, "a +bind runs + on the press and - on the release, any case");
    console_key(con, "space", true);
    CHECK(strcmp(last(con), "there") == 0, "a bind runs all its commands");

    remove(SAVED);
    CHECK(console_save(con, SAVED), "the binds and archived cvars save");
    console_execute(con, "unbindall; set fov 50");
    CHECK(console_bind_get(con, "space") == NULL, "unbindall unbinds");
    CHECK(console_execute_file(con, SAVED), "and exec reads the save back");
    CHECK(console_bind_get(con, "space") && fov->integer == 100, "with the binds and the cvars as they were");
    remove(SAVED);
    console_destroy(con);
}

// The file's text, or NULL.
static char *read_saved(void) { return (char *)file_read_all(SAVED, NULL); }

static bool write_saved(const char *text)
{
    FILE *f = fopen(SAVED, "wb");
    if (!f) return false;
    fputs(text, f);
    return fclose(f) == 0;
}

// Saving into a config somebody wrote: what changed is rewritten where it was, the
// rest is left alone, and a file with nothing to change isn't rewritten.
static void saving_in_place(void)
{
    const char *written = "// my settings\r\n"
                          "set assets ./stuff // where the files are\n"
                          "seta fov 100            // the field of view\n"
                          "  sensitivity 2\n"
                          "seta fov 100; seta name \"Major Pain\"\n"
                          "unbindall\n"
                          "bind mouse1 +attack\n"
                          "bind space \"echo hi\"   // a greeting\n"
                          "bind x jump; bind y +attack\n"
                          "bind v jump // gone with its comment\n";
    CHECK(write_saved(written), "a config file can be written for the test");

    Console *con = console_create(NULL, NULL);
    console_add_command(con, "+attack", attack, NULL, NULL);
    console_add_command(con, "-attack", attack, NULL, NULL);
    Cvar *fov = cvar_register(con, "fov", "90", CVAR_ARCHIVE, NULL);
    cvar_register(con, "sensitivity", "1", CVAR_ARCHIVE, NULL);
    Cvar *assets = cvar_register(con, "assets", "assets", 0, NULL);
    console_execute_file(con, SAVED);
    CHECK(fov->integer == 100 && strcmp(assets->value, "./stuff") == 0, "the file sets what it says");

    CHECK(console_save(con, SAVED), "saving into it works");
    char *text = read_saved();
    CHECK(text && strcmp(text, written) == 0, "and, with nothing changed, leaves it as it was:\n%s", text ? text : "");
    free(text);

    cvar_register(con, "volume", "0.5", CVAR_ARCHIVE, NULL); // a saved cvar the file doesn't have
    console_execute(con, "set fov 120; sensitivity 3; set assets other; bind space \"echo bye\";"
                         "unbind x; unbind v; bind z +attack; set volume 1");
    CHECK(console_save(con, SAVED), "saving with changes works");
    text = read_saved();
    const char *expected = "// my settings\r\n"
                           "set assets ./stuff // where the files are\n"
                           "seta fov \"120\"            // the field of view\n"
                           "  sensitivity \"3\"\n"
                           "seta fov \"120\"; seta name \"Major Pain\"\n"
                           "unbindall\n"
                           "bind mouse1 +attack\n"
                           "bind space \"echo bye\"   // a greeting\n"
                           "bind y +attack\n"
                           "\n"
                           "seta volume \"1\"\n"
                           "bind z \"+attack\"\n";
    CHECK(text && strcmp(text, expected) == 0,
          "a changed value is rewritten where it was, with its comment and line ending, on a line of several "
          "commands too; an unsaved cvar's line is left alone; an unbound key's bind goes; the rest is added "
          "after:\n%s",
          text ? text : "");
    free(text);

    console_execute(con, "unbindall; set fov 1; set volume 0; set name x");
    CHECK(console_execute_file(con, SAVED), "the file execs");
    CHECK(fov->integer == 120 && console_bind_get(con, "z") && !console_bind_get(con, "x"), "and holds what was saved");

    remove(SAVED);
    console_execute(con, "unbindall; bind a +attack");
    CHECK(console_save(con, SAVED), "with no file there, one is written");
    text = read_saved();
    CHECK(text && strstr(text, "unbindall\n") && strstr(text, "bind a \"+attack\"\n") && strstr(text, "seta fov \"120\"\n") &&
              !strstr(text, "assets"),
          "with unbindall, the binds and the saved cvars only:\n%s", text ? text : "");
    free(text);
    remove(SAVED);
    console_destroy(con);
}

// A line printed in a colour keeps it, for the HUD; the rest have none.
static void colours(void)
{
    Console *con = console_create(NULL, NULL);
    Cvar *fov = cvar_register(con, "fov", "90", 0, NULL);
    console_execute(con, "/fov 110"); // as typed at the game's prompt
    CHECK(fov->integer == 110 && strstr(console_log_line(con, 0), "is now set to: \"110\"") != NULL,
          "a cvar typed with a slash and a value, as at the prompt, is set and says so (%d: %s)", fov->integer, console_log_line(con, 0));
    console_execute(con, "fov");
    CHECK(strstr(console_log_line(con, 0), "110") != NULL, "and typed alone shows its value (%s)", console_log_line(con, 0));
    console_print(con, "plain\n");
    console_print_color(con, (Rgba){1, 2, 3, 255}, "said\nand said again\n");
    Rgba c = {0};
    CHECK(console_log_color(con, 0, &c) && c.r == 1 && c.g == 2 && c.b == 3, "a coloured line keeps its colour");
    CHECK(console_log_color(con, 1, &c) && c.b == 3, "every line of the print does");
    CHECK(!console_log_color(con, 2, &c), "and a plain line has none");
    CHECK(console_log_total(con) == 5, "five lines have been printed, by the count (%u)", console_log_total(con));
    CHECK(console_knows(con, "echo") && !console_knows(con, "nosuchthing"), "the console knows its commands by name");
    console_destroy(con);
}

// The player's own files, apart from the defaults: only what differs, and the defaults'
// keys let go of as unbinds; read back over the defaults, the same console again.
static void saving_changes(void)
{
    const char *settings = "build/console_test_settings.cfg", *binds = "build/console_test_binds.cfg";
    Console *con = console_create(NULL, NULL);
    Cvar *fov = cvar_register(con, "fov", "90", CVAR_ARCHIVE, NULL);
    Cvar *volume = cvar_register(con, "volume", "50", CVAR_ARCHIVE, NULL);
    cvar_register(con, "name", "x", 0, NULL);
    console_execute(con, "seta fov 100; bind a +attack; bind b jump; bind c crouch"); // the defaults
    console_mark_defaults(con);
    console_execute(con, "seta volume 20; set name y; bind b fire; unbind c; bind d drop"); // the player's
    CHECK(console_save_changes(con, settings, "// mine\n", binds, "// keys\n"), "the player's files are written");
    size_t size = 0;
    char *s = (char *)file_read_all(settings, &size);
    CHECK(s && strcmp(s, "// mine\nseta volume \"20\"\n") == 0, "the settings hold only what differs from the defaults:\n%s", s ? s : "");
    free(s);
    char *k = (char *)file_read_all(binds, &size);
    CHECK(k && strstr(k, "bind b \"fire\"\n") && strstr(k, "bind d \"drop\"\n") && strstr(k, "unbind c\n") && !strstr(k, "bind a"),
          "the binds hold a key bound otherwise, a new one, and the default let go, not the default kept:\n%s", k ? k : "");
    free(k);

    Console *again = console_create(NULL, NULL);
    cvar_register(again, "fov", "90", CVAR_ARCHIVE, NULL);
    Cvar *volume2 = cvar_register(again, "volume", "50", CVAR_ARCHIVE, NULL);
    console_execute(again, "seta fov 100; bind a +attack; bind b jump; bind c crouch");
    console_mark_defaults(again);
    console_execute_file(again, settings);
    console_execute_file(again, binds);
    CHECK(volume2->integer == 20 && fov->integer == 100 && strcmp(console_bind_get(again, "b"), "fire") == 0 &&
              !console_bind_get(again, "c") && strcmp(console_bind_get(again, "a"), "+attack") == 0,
          "read back over the defaults, they make the same console");
    (void)volume;
    remove(settings);
    remove(binds);
    console_destroy(again);
    console_destroy(con);
}

void console_tests(void)
{
    colours();
    cvars_and_command_line();
    parser();
    binds_and_saving();
    saving_in_place();
    saving_changes();
}
