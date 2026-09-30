// The console: cvars, commands, the parser, binds, the command line, saving.

#include <stdio.h>
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

    CHECK(console_save(con, SAVED), "the binds and archived cvars save");
    console_execute(con, "unbindall; set fov 50");
    CHECK(console_bind_get(con, "space") == NULL, "unbindall unbinds");
    CHECK(console_execute_file(con, SAVED), "and exec reads the save back");
    CHECK(console_bind_get(con, "space") && fov->integer == 100, "with the binds and the cvars as they were");
    remove(SAVED);
    console_destroy(con);
}

void console_tests(void)
{
    cvars_and_command_line();
    parser();
    binds_and_saving();
}
