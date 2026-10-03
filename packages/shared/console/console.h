#pragma once

// The console, Quake 3's in miniature: cvars, commands, binds, and the text that runs
// them. Shared so the client and the server read the same config files and the same
// command line.
//
//   cvar     a named value, kept as text and read as a number: `name` shows it,
//            `name value` or `set name value` sets it
//   command  a named function: `name arg arg...`
//   bind     a key's name and the text it runs when pressed. A bind to "+cmd" runs
//            "+cmd" on the press and "-cmd" on the release
//
// Text is commands separated by ';' or newlines. A command is words separated by
// spaces; "quotes" make one word of several, and // comments out the rest of the line.
//
// Everything runs at once, when it is executed: there is no buffer and no `wait`. The
// console knows key names only as text: the client names its keys (see console_key).
//
// A cvar set before it is registered (from the command line, or a config read before
// the code that owns it started) is kept, and the registration adopts its value. So the
// order at startup is free: exec the config, run the command line, then open the
// subsystems that register their cvars.
//
// The defaults and the player's own are kept apart (config/): the default files run
// first, console_mark_defaults notes where they left every cvar and bind, the player's
// files run over them, and console_save_changes writes the player's files back as only
// what differs from the defaults. So a new default reaches every player who hasn't set
// that thing otherwise. console_save keeps the older way, one file written back into in
// place: a line setting a saved cvar or binding a key is updated where it stands when its
// value changed, a bind's line goes when the key was unbound, and what the file didn't
// have goes after it, comments and all else kept.
//
// The built-in commands:
//   set / seta name value    set (seta also marks it to be saved)
//   reset name               back to its default
//   toggle name              0 <-> 1
//   vstr name                run the text a cvar holds
//   echo text...             print
//   exec file                run a config file (".cfg" added if it has no extension)
//   bind key [text]          bind a key, or show its bind
//   unbind key / unbindall
//   cvarlist / cmdlist / bindlist [prefix]

#include "utils/utils.h"

#define CONSOLE_MAX_CVARS 512
#define CONSOLE_MAX_COMMANDS 256
#define CONSOLE_MAX_BINDS 128
#define CONSOLE_NAME_SIZE 32
#define CONSOLE_VALUE_SIZE 256 // a cvar's value, a bind's text
#define CONSOLE_TEXT_SIZE 1024 // one command, once split from the others
#define CONSOLE_MAX_ARGS 64
#define CONSOLE_LOG_LINES 256 // the scrollback
#define CONSOLE_LOG_WIDTH 160
#define CONSOLE_MAX_EXEC_DEPTH 16 // config files exec'ing config files

typedef struct Console Console;

typedef enum CvarFlags {
    CVAR_ARCHIVE = 1 << 0,  // saved by console_save
    CVAR_READONLY = 1 << 1, // the console shows it but only code sets it
    CVAR_USER = 1 << 2,     // made by `set` rather than registered by code
} CvarFlags;

typedef struct Cvar {
    char name[CONSOLE_NAME_SIZE];
    char value[CONSOLE_VALUE_SIZE];
    char default_value[CONSOLE_VALUE_SIZE];
    float number; // the value as a number, 0 if it isn't one
    int integer;
    uint32_t flags;
    const char *help;

    // Set whenever the value is set; the owner clears it once it has acted on it.
    bool modified;
} Cvar;

// A command's function. argv[0] is the command's name. `user` is what was registered
// with it.
typedef void (*ConsoleCommandFn)(Console *con, int argc, char **argv, void *user);

// Everything the console prints also goes here, if it is set (to stdout, a log file).
typedef void (*ConsolePrintFn)(const char *text, void *user);

// --- the console ---------------------------------------------------------------------

// A console with the built-in commands. Large; on the heap. Free with console_destroy.
Console *console_create(ConsolePrintFn print, void *print_user);
void console_destroy(Console *con);

// Prints to the scrollback (and the print hook). Lines end at '\n'.
#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 2, 3)))
#endif
void console_print(Console *con, const char *fmt, ...);

// The same, in a colour the HUD shows the line in; console_print's lines have none.
#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 3, 4)))
#endif
void console_print_color(Console *con, Rgba color, const char *fmt, ...);

// The scrollback: line 0 is the newest finished line, 1 the one before... NULL past the
// oldest kept.
const char *console_log_line(const Console *con, int back);
// The colour a scrollback line was printed in; false if it had none.
bool console_log_color(const Console *con, int back, Rgba *color);
// How many lines have been finished, ever: a reader keeps its own count to take the new ones.
uint32_t console_log_total(const Console *con);
// Whether `name` is a command or a cvar here.
bool console_knows(const Console *con, const char *name);

// --- running text --------------------------------------------------------------------

// Runs text: commands separated by ';' or newlines.
void console_execute(Console *con, const char *text);

// Runs a config file. False (and says so) if it can't be read.
bool console_execute_file(Console *con, const char *path);

// Runs the command line, Quake style: each argument starting with '+' begins a command
// and the arguments after it are its words, up to the next '+'. What comes before the
// first '+' is left alone, argv[0] included. So
//   client +set name "Major Pain" +map Arena
// runs `set name "Major Pain"`, then `map Arena`. A '+' word that names no command and
// has arguments sets a cvar, so `+name Pain` works before the cvar is registered.
void console_execute_args(Console *con, int argc, char **argv);

// Writes the binds and the archived cvars into a config file that exec reads back. An
// existing file is updated in place (see above); a new one is written from scratch,
// with `unbindall` first so it holds the whole of the binds. False if it can't be
// written.
bool console_save(const Console *con, const char *path);

// The defaults are in: every cvar's value and every bind as they stand now are what a
// player's own files are told apart from (console_save_changes). Run after the default
// config files, before the player's.
void console_mark_defaults(Console *con);

// The player's own files, written whole: into `settings_path` a `seta` for each saved
// cvar whose value differs from the defaults (or, unmarked, from its registered
// default); into `binds_path` a `bind` for each key bound otherwise than the defaults
// bind it, or not at all by them, and an `unbind` for each the defaults bind and the
// player has let go. Each after its header. A file whose text is already that isn't
// touched. False if one can't be written.
bool console_save_changes(const Console *con, const char *settings_path, const char *settings_header, const char *binds_path,
                          const char *binds_header);

// --- cvars ---------------------------------------------------------------------------

// Registers a cvar and returns it; the pointer holds for the console's life, so the
// owner keeps it and reads ->number, ->integer or ->value. If it was already set (see
// above) it keeps that value and takes this default and these flags. NULL if the name is
// taken by a command or there is no room. `help` is kept, not copied: a string literal.
Cvar *cvar_register(Console *con, const char *name, const char *default_value, uint32_t flags, const char *help);

Cvar *cvar_find(const Console *con, const char *name);

// Sets a cvar from code (read-only ones too), making it if it doesn't exist. NULL if it
// can't be made.
Cvar *cvar_set(Console *con, const char *name, const char *value);

// --- commands ------------------------------------------------------------------------

// False if the name is taken or there is no room. `help` is kept, not copied.
bool console_add_command(Console *con, const char *name, ConsoleCommandFn fn, void *user, const char *help);

// --- binds ---------------------------------------------------------------------------

// Key names are the client's to choose ("a", "space", "mouse1", "f1"...); they are
// matched without regard to case. An empty or NULL text unbinds.
bool console_bind(Console *con, const char *key, const char *text);
const char *console_bind_get(const Console *con, const char *key); // NULL if unbound
// The binds, for listing: how many, and the i-th's key and text (false past the end).
int console_bind_count(const Console *con);
bool console_bind_at(const Console *con, int i, const char **key, const char **text);

// A key went down or up: runs its bind. Down runs the bind; up runs "-cmd" if the bind
// is "+cmd", and nothing otherwise. The client ignores key repeats.
void console_key(Console *con, const char *key, bool down);
