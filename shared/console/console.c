#include "console/console.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Command {
    char name[CONSOLE_NAME_SIZE];
    ConsoleCommandFn fn;
    void *user;
    const char *help;
} Command;

typedef struct Bind {
    char key[CONSOLE_NAME_SIZE];
    char text[CONSOLE_VALUE_SIZE];
} Bind;

struct Console {
    Cvar cvars[CONSOLE_MAX_CVARS]; // never removed, so a Cvar * holds
    int cvar_count;
    Command commands[CONSOLE_MAX_COMMANDS];
    int command_count;
    Bind binds[CONSOLE_MAX_BINDS];
    int bind_count;

    // The scrollback, a ring: line `log_total % CONSOLE_LOG_LINES` is the one being
    // written, the ones before it are finished.
    char log[CONSOLE_LOG_LINES][CONSOLE_LOG_WIDTH];
    uint32_t log_total;
    int log_column;

    ConsolePrintFn print;
    void *print_user;

    int depth; // text running text: exec, vstr, binds
};

// --- names ---------------------------------------------------------------------------

static void copy(char *out, size_t size, const char *s) { snprintf(out, size, "%s", s ? s : ""); }

static bool name_eq(const char *a, const char *b)
{
    while (*a && tolower((unsigned char)*a) == tolower((unsigned char)*b)) a++, b++;
    return tolower((unsigned char)*a) == tolower((unsigned char)*b);
}

static bool has_prefix(const char *s, const char *prefix)
{
    for (; *prefix; s++, prefix++)
        if (tolower((unsigned char)*s) != tolower((unsigned char)*prefix)) return false;
    return true;
}

// A name is one word: printable, no quotes, no separators, and it fits.
static bool name_valid(const char *name)
{
    size_t len = strlen(name);
    if (len == 0 || len >= CONSOLE_NAME_SIZE) return false;
    for (const char *p = name; *p; p++)
        if (!isgraph((unsigned char)*p) || *p == '"' || *p == ';') return false;
    return true;
}

static Command *command_find(const Console *con, const char *name)
{
    for (int i = 0; i < con->command_count; i++)
        if (name_eq(con->commands[i].name, name)) return (Command *)&con->commands[i];
    return NULL;
}

static Bind *bind_find(const Console *con, const char *key)
{
    for (int i = 0; i < con->bind_count; i++)
        if (name_eq(con->binds[i].key, key)) return (Bind *)&con->binds[i];
    return NULL;
}

// The words from argv[first] on, joined by spaces.
static char *join_args(char *out, size_t size, int argc, char **argv, int first)
{
    size_t n = 0;
    out[0] = '\0';
    for (int i = first; i < argc && n < size - 1; i++) {
        int w = snprintf(out + n, size - n, i > first ? " %s" : "%s", argv[i]);
        if (w < 0) break;
        n += (size_t)w;
    }
    return out;
}

// --- the console ---------------------------------------------------------------------

static char *log_current(Console *con) { return con->log[con->log_total % CONSOLE_LOG_LINES]; }

static void log_newline(Console *con)
{
    con->log_total++;
    con->log_column = 0;
    log_current(con)[0] = '\0';
}

void console_print(Console *con, const char *fmt, ...)
{
    char text[CONSOLE_TEXT_SIZE];
    va_list args;
    va_start(args, fmt);
    vsnprintf(text, sizeof text, fmt, args);
    va_end(args);

    if (con->print) con->print(text, con->print_user);

    for (const char *p = text; *p; p++) {
        if (*p == '\n') {
            log_newline(con);
            continue;
        }
        if (con->log_column == CONSOLE_LOG_WIDTH - 1) log_newline(con); // wrap
        char *line = log_current(con);
        line[con->log_column++] = *p;
        line[con->log_column] = '\0';
    }
}

const char *console_log_line(const Console *con, int back)
{
    if (back < 0 || (uint32_t)back >= con->log_total || back >= CONSOLE_LOG_LINES - 1) return NULL;
    return con->log[(con->log_total - 1 - (uint32_t)back) % CONSOLE_LOG_LINES];
}

// --- cvars ---------------------------------------------------------------------------

static void cvar_assign(Cvar *cv, const char *value)
{
    copy(cv->value, sizeof cv->value, value);
    cv->number = strtof(cv->value, NULL);
    cv->integer = atoi(cv->value);
    cv->modified = true;
}

// A new cvar holding `value`, which is also its default.
static Cvar *cvar_new(Console *con, const char *name, const char *value)
{
    if (!name_valid(name)) {
        console_print(con, "bad cvar name: \"%s\"\n", name);
        return NULL;
    }
    if (command_find(con, name)) {
        console_print(con, "%s is a command, not a cvar\n", name);
        return NULL;
    }
    if (con->cvar_count == CONSOLE_MAX_CVARS) {
        console_print(con, "no room for cvar %s\n", name);
        return NULL;
    }
    Cvar *cv = &con->cvars[con->cvar_count++];
    copy(cv->name, sizeof cv->name, name);
    copy(cv->default_value, sizeof cv->default_value, value);
    cvar_assign(cv, value);
    return cv;
}

Cvar *cvar_find(const Console *con, const char *name)
{
    for (int i = 0; i < con->cvar_count; i++)
        if (name_eq(con->cvars[i].name, name)) return (Cvar *)&con->cvars[i];
    return NULL;
}

Cvar *cvar_register(Console *con, const char *name, const char *default_value, uint32_t flags, const char *help)
{
    Cvar *cv = cvar_find(con, name);
    if (cv) {
        // Set before it was registered: it keeps that value, unless nobody may set it.
        copy(cv->default_value, sizeof cv->default_value, default_value);
        if (flags & CVAR_READONLY) cvar_assign(cv, default_value);
    } else {
        cv = cvar_new(con, name, default_value);
        if (!cv) return NULL;
    }
    cv->flags = flags;
    cv->help = help;
    return cv;
}

Cvar *cvar_set(Console *con, const char *name, const char *value)
{
    Cvar *cv = cvar_find(con, name);
    if (!cv) {
        cv = cvar_new(con, name, value);
        if (cv) cv->flags = CVAR_USER;
        return cv;
    }
    cvar_assign(cv, value);
    return cv;
}

// What the console may do to a cvar: not set a read-only one. `flags` are added.
static void cvar_set_from_console(Console *con, const char *name, const char *value, uint32_t flags)
{
    Cvar *cv = cvar_find(con, name);
    if (cv && (cv->flags & CVAR_READONLY)) {
        console_print(con, "%s is read only\n", cv->name);
        return;
    }
    cv = cvar_set(con, name, value);
    if (cv) cv->flags |= flags;
}

static void cvar_show(Console *con, const Cvar *cv)
{
    console_print(con, "\"%s\" is \"%s\" (default \"%s\")\n", cv->name, cv->value, cv->default_value);
    if (cv->help) console_print(con, "  %s\n", cv->help);
}

// --- commands ------------------------------------------------------------------------

bool console_add_command(Console *con, const char *name, ConsoleCommandFn fn, void *user, const char *help)
{
    if (!name_valid(name) || !fn) {
        console_print(con, "bad command name: \"%s\"\n", name);
        return false;
    }
    if (command_find(con, name) || cvar_find(con, name)) {
        console_print(con, "%s is already defined\n", name);
        return false;
    }
    if (con->command_count == CONSOLE_MAX_COMMANDS) {
        console_print(con, "no room for command %s\n", name);
        return false;
    }
    Command *c = &con->commands[con->command_count++];
    copy(c->name, sizeof c->name, name);
    c->fn = fn;
    c->user = user;
    c->help = help;
    return true;
}

// --- binds ---------------------------------------------------------------------------

bool console_bind(Console *con, const char *key, const char *text)
{
    Bind *b = bind_find(con, key);
    if (!text || !*text) {
        if (b) *b = con->binds[--con->bind_count];
        return true;
    }
    if (!b) {
        if (!name_valid(key)) {
            console_print(con, "bad key name: \"%s\"\n", key);
            return false;
        }
        if (con->bind_count == CONSOLE_MAX_BINDS) {
            console_print(con, "no room to bind %s\n", key);
            return false;
        }
        b = &con->binds[con->bind_count++];
        copy(b->key, sizeof b->key, key);
    }
    copy(b->text, sizeof b->text, text);
    return true;
}

const char *console_bind_get(const Console *con, const char *key)
{
    const Bind *b = bind_find(con, key);
    return b ? b->text : NULL;
}

void console_key(Console *con, const char *key, bool down)
{
    const char *bound = console_bind_get(con, key);
    if (!bound) return;

    // A copy: what it runs may rebind the key.
    char text[CONSOLE_VALUE_SIZE];
    if (bound[0] == '+') {
        if (down) copy(text, sizeof text, bound);
        else snprintf(text, sizeof text, "-%s", bound + 1);
    } else if (down) {
        copy(text, sizeof text, bound);
    } else {
        return;
    }
    console_execute(con, text);
}

// --- running text --------------------------------------------------------------------

typedef struct Args {
    int argc;
    char *argv[CONSOLE_MAX_ARGS];
    char words[CONSOLE_TEXT_SIZE + CONSOLE_MAX_ARGS];
} Args;

static bool comment(const char *p) { return p[0] == '/' && p[1] == '/'; }

// Reads one command's words from p, up to a ';', a newline or a // comment. Words are
// split at spaces; "quotes" keep spaces (and ';') in, up to the end of the line.
// Returns where the next command starts.
static const char *next_command(const char *p, Args *a)
{
    char *out = a->words, *end = a->words + sizeof a->words - 1;
    a->argc = 0;
    for (;;) {
        while (*p != '\n' && isspace((unsigned char)*p)) p++;
        if (comment(p))
            while (*p && *p != '\n') p++;
        if (!*p) return p;
        if (*p == ';' || *p == '\n') return p + 1;

        bool quoted = *p == '"';
        bool keep = a->argc < CONSOLE_MAX_ARGS; // words past the last are dropped
        if (quoted) p++;
        if (keep) a->argv[a->argc++] = out;
        for (; *p && *p != '\n'; p++) {
            if (quoted ? *p == '"' : isspace((unsigned char)*p) || *p == ';' || *p == '"' || comment(p)) break;
            if (keep && out < end) *out++ = *p;
        }
        if (quoted && *p == '"') p++;
        if (keep) {
            *out = '\0';
            if (out < end) out++;
        }
    }
}

// One command: a command's name, or a cvar's (to show or set it).
static void run(Console *con, Args *a)
{
    if (a->argc == 0) return;

    char *name = a->argv[0];
    if (*name == '/' || *name == '\\') a->argv[0] = ++name; // typed as "/cmd", as in chat
    if (!*name) return;

    const Command *c = command_find(con, name);
    if (c) {
        c->fn(con, a->argc, a->argv, c->user);
        return;
    }
    Cvar *cv = cvar_find(con, name);
    if (cv) {
        if (a->argc == 1) cvar_show(con, cv);
        else cvar_set_from_console(con, cv->name, a->argv[1], 0);
        return;
    }
    console_print(con, "unknown command: %s\n", name);
}

void console_execute(Console *con, const char *text)
{
    if (con->depth >= CONSOLE_MAX_EXEC_DEPTH) {
        console_print(con, "commands nested too deep (an exec or vstr loop?)\n");
        return;
    }
    con->depth++;
    Args a;
    for (const char *p = text; *p;) {
        p = next_command(p, &a);
        run(con, &a);
    }
    con->depth--;
}

bool console_execute_file(Console *con, const char *path)
{
    char *text = (char *)file_read_all(path, NULL);
    if (!text) {
        console_print(con, "couldn't exec %s\n", path);
        return false;
    }
    console_print(con, "execing %s\n", path);
    console_execute(con, text);
    free(text);
    return true;
}

void console_execute_args(Console *con, int argc, char **argv)
{
    int i = 1;
    while (i < argc && argv[i][0] != '+') i++;
    while (i < argc) {
        const char *name = argv[i] + 1;
        int end = i + 1;
        while (end < argc && argv[end][0] != '+') end++;

        // "+name value" where name is no command sets a cvar, registered yet or not.
        bool set = end > i + 1 && !command_find(con, name);
        char text[CONSOLE_TEXT_SIZE];
        int n = snprintf(text, sizeof text, set ? "set %s" : "%s", name);
        for (int k = i + 1; k < end && n >= 0 && n < (int)sizeof text; k++)
            n += snprintf(text + n, sizeof text - (size_t)n, " \"%s\"", argv[k]); // one argument, one word
        console_execute(con, text);
        i = end;
    }
}

// --- saving --------------------------------------------------------------------------

// Text being composed, grown as it goes.
typedef struct Text {
    char *data;
    size_t len, cap;
    bool failed; // out of memory somewhere along the way
} Text;

static void text_append(Text *t, const char *s, size_t n)
{
    if (t->failed) return;
    if (t->len + n + 1 > t->cap) {
        size_t cap = t->cap ? t->cap * 2 : 4096;
        while (cap < t->len + n + 1) cap *= 2;
        char *data = realloc(t->data, cap);
        if (!data) {
            t->failed = true;
            return;
        }
        t->data = data;
        t->cap = cap;
    }
    memcpy(t->data + t->len, s, n);
    t->len += n;
    t->data[t->len] = '\0';
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 2, 3)))
#endif
static void text_printf(Text *t, const char *fmt, ...)
{
    char line[CONSOLE_TEXT_SIZE];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(line, sizeof line, fmt, args);
    va_end(args);
    if (n < 0) return;
    text_append(t, line, n < (int)sizeof line ? (size_t)n : sizeof line - 1);
}

static bool text_ends_with(const Text *t, const char *s)
{
    size_t n = strlen(s);
    return t->len >= n && memcmp(t->data + t->len - n, s, n) == 0;
}

// Where a line's trailing comment begins, the spaces before the // included; the line's
// end if it has none. A // inside quotes is part of a word.
static size_t comment_start(const char *line, size_t len)
{
    bool quoted = false;
    for (size_t i = 0; i < len; i++) {
        if (line[i] == '"') quoted = !quoted;
        else if (!quoted && i + 1 < len && line[i] == '/' && line[i + 1] == '/') {
            while (i > 0 && isspace((unsigned char)line[i - 1])) i--;
            return i;
        }
    }
    return len;
}

// A cvar is saved if it was marked to be and the console may set it.
static bool cvar_saved(const Cvar *cv) { return (cv->flags & CVAR_ARCHIVE) && !(cv->flags & CVAR_READONLY); }

// One command of a config file on its way back out: the text from `start` to `end`,
// its leading spaces and its ';' included, as next_command cut it. A command setting a
// saved cvar ("set name value", "seta name value" or "name value") or binding a key is
// the console's: kept as it is when it says what the console holds, rewritten with the
// current value when it doesn't, and dropped if it binds a key since unbound. Any other
// command is the file's own and passes through untouched. `seen` marks what the file
// already holds; `trim` drops the leading spaces, after a dropped command.
typedef enum Saved { SAVED_KEPT, SAVED_REWRITTEN, SAVED_DROPPED } Saved;

static Saved save_command(const Console *con, const char *start, const char *end, Args *a, bool trim, Text *out,
                          bool *seen_cvar, bool *seen_bind)
{
    size_t indent = 0;
    while (isspace((unsigned char)start[indent])) indent++;
    if (trim) start += indent, indent = 0;

    // what it sets, and to what
    const Cvar *cv = NULL;
    const Bind *b = NULL;
    bool unbound = false;
    const char *verb = NULL; // "set" or "seta" as written; NULL for the bare "name value"
    char value[CONSOLE_VALUE_SIZE] = "";
    if (a->argc >= 3 && (name_eq(a->argv[0], "set") || name_eq(a->argv[0], "seta"))) {
        cv = cvar_find(con, a->argv[1]);
        verb = a->argv[0];
        join_args(value, sizeof value, a->argc, a->argv, 2);
    } else if (a->argc >= 2 && (cv = cvar_find(con, a->argv[0])) != NULL) {
        copy(value, sizeof value, a->argv[1]);
    } else if (a->argc >= 3 && name_eq(a->argv[0], "bind")) {
        b = bind_find(con, a->argv[1]);
        unbound = !b;
        join_args(value, sizeof value, a->argc, a->argv, 2);
    }
    if (cv && !cvar_saved(cv)) cv = NULL; // the file's to set, not the console's to save

    if (cv) seen_cvar[cv - con->cvars] = true;
    if (b) seen_bind[b - con->binds] = true;
    bool changed = unbound || (cv && strcmp(value, cv->value) != 0) || (b && strcmp(value, b->text) != 0);
    if (!changed) {
        text_append(out, start, (size_t)(end - start));
        return SAVED_KEPT;
    }
    if (unbound) {
        if (out->len == 0) text_append(out, start, indent); // the line keeps its indentation
        return SAVED_DROPPED;
    }
    text_append(out, start, indent);
    if (b) text_printf(out, "bind %s \"%s\"", b->key, b->text);
    else if (verb) text_printf(out, "%s %s \"%s\"", verb, cv->name, cv->value);
    else text_printf(out, "%s \"%s\"", cv->name, cv->value);
    if (end > start && end[-1] == ';') text_append(out, ";", 1);
    return SAVED_REWRITTEN;
}

// One line of a config file on its way back out: its commands, each as it was or as
// the console now has it (save_command), then its comment. A line too long to parse
// passes through as it is, and a line whose commands all went goes, its comment too.
static void save_line(const Console *con, const char *line, size_t len, const char *newline, Text *out,
                      bool *seen_cvar, bool *seen_bind)
{
    size_t body = comment_start(line, len); // the commands, before any comment
    Text rebuilt = {0};
    bool changed = false;
    if (body < CONSOLE_TEXT_SIZE) {
        char text[CONSOLE_TEXT_SIZE];
        memcpy(text, line, body);
        text[body] = '\0';
        bool dropped = false;
        for (const char *p = text; *p;) {
            const char *start = p;
            Args a;
            p = next_command(start, &a);
            Saved saved = save_command(con, start, p, &a, dropped, &rebuilt, seen_cvar, seen_bind);
            if (saved != SAVED_KEPT) changed = true;
            dropped = saved == SAVED_DROPPED;
        }
    }
    if (rebuilt.failed) out->failed = true;

    if (!changed) {
        text_append(out, line, len);
        text_append(out, newline, strlen(newline));
    } else {
        size_t i = 0;
        while (i < rebuilt.len && isspace((unsigned char)rebuilt.data[i])) i++;
        if (i < rebuilt.len) {
            text_append(out, rebuilt.data ? rebuilt.data : "", rebuilt.len);
            text_append(out, line + body, len - body); // the comment
            text_append(out, newline, strlen(newline));
        }
    }
    free(rebuilt.data);
}

bool console_save(const Console *con, const char *path)
{
    size_t old_size = 0;
    char *old = (char *)file_read_all(path, &old_size);
    Text out = {0}, add = {0};
    bool seen_cvar[CONSOLE_MAX_CVARS] = {0}, seen_bind[CONSOLE_MAX_BINDS] = {0};

    if (old) {
        // the file's lines, each as it was or as the console now has it
        for (const char *p = old, *end = old + old_size; p < end;) {
            const char *nl = memchr(p, '\n', (size_t)(end - p));
            const char *next = nl ? nl + 1 : end;
            size_t len = (size_t)((nl ? nl : end) - p);
            const char *newline = !nl ? "" : len > 0 && p[len - 1] == '\r' ? "\r\n" : "\n";
            if (newline[0] == '\r') len--;
            save_line(con, p, len, newline, &out, seen_cvar, seen_bind);
            p = next;
        }
    } else {
        const char *header = "// saved by the game\nunbindall\n";
        text_append(&out, header, strlen(header));
    }

    // what the file didn't have goes after it
    for (int i = 0; i < con->cvar_count; i++) {
        const Cvar *cv = &con->cvars[i];
        if (cvar_saved(cv) && !seen_cvar[i]) text_printf(&add, "seta %s \"%s\"\n", cv->name, cv->value);
    }
    for (int i = 0; i < con->bind_count; i++)
        if (!seen_bind[i]) text_printf(&add, "bind %s \"%s\"\n", con->binds[i].key, con->binds[i].text);
    if (add.len) {
        if (out.len && !text_ends_with(&out, "\n")) text_append(&out, "\n", 1);
        if (old && !text_ends_with(&out, "\n\n")) text_append(&out, "\n", 1);
        text_append(&out, add.data, add.len);
    }

    // written only if it changed, so a file the game has nothing to say to stays as it is
    bool ok = !out.failed && !add.failed;
    bool same = old && old_size == out.len && memcmp(old, out.data, out.len) == 0;
    if (ok && !same) {
        FILE *f = fopen(path, "wb");
        ok = f && fwrite(out.data, 1, out.len, f) == out.len;
        if (f && fclose(f) != 0) ok = false;
    }
    free(old);
    free(out.data);
    free(add.data);
    return ok;
}

// --- the built-in commands -----------------------------------------------------------

// The cvar a "<command> <name>" names, or NULL (and says why).
static const Cvar *cvar_arg(Console *con, int argc, char **argv)
{
    if (argc != 2) {
        console_print(con, "usage: %s <name>\n", argv[0]);
        return NULL;
    }
    const Cvar *cv = cvar_find(con, argv[1]);
    if (!cv) console_print(con, "no cvar %s\n", argv[1]);
    return cv;
}

static void cmd_set(Console *con, int argc, char **argv, void *user)
{
    (void)user;
    if (argc < 3) {
        console_print(con, "usage: %s <name> <value>\n", argv[0]);
        return;
    }
    char value[CONSOLE_VALUE_SIZE];
    uint32_t flags = name_eq(argv[0], "seta") ? CVAR_ARCHIVE : 0;
    cvar_set_from_console(con, argv[1], join_args(value, sizeof value, argc, argv, 2), flags);
}

static void cmd_reset(Console *con, int argc, char **argv, void *user)
{
    (void)user;
    const Cvar *cv = cvar_arg(con, argc, argv);
    if (!cv) return;
    cvar_set_from_console(con, cv->name, cv->default_value, 0);
}

static void cmd_toggle(Console *con, int argc, char **argv, void *user)
{
    (void)user;
    const Cvar *cv = cvar_arg(con, argc, argv);
    if (!cv) return;
    cvar_set_from_console(con, cv->name, cv->integer ? "0" : "1", 0);
}

static void cmd_vstr(Console *con, int argc, char **argv, void *user)
{
    (void)user;
    const Cvar *cv = cvar_arg(con, argc, argv);
    if (!cv) return;
    char text[CONSOLE_VALUE_SIZE]; // a copy: what it runs may set the cvar
    copy(text, sizeof text, cv->value);
    console_execute(con, text);
}

static void cmd_echo(Console *con, int argc, char **argv, void *user)
{
    (void)user;
    char text[CONSOLE_TEXT_SIZE];
    console_print(con, "%s\n", join_args(text, sizeof text, argc, argv, 1));
}

static void cmd_exec(Console *con, int argc, char **argv, void *user)
{
    (void)user;
    if (argc != 2) {
        console_print(con, "usage: exec <file>\n");
        return;
    }
    char path[512];
    const char *base = argv[1]; // the file's name, past its directories
    for (const char *p = argv[1]; *p; p++)
        if (*p == '/' || *p == '\\') base = p + 1;
    snprintf(path, sizeof path, strchr(base, '.') ? "%s" : "%s.cfg", argv[1]);
    console_execute_file(con, path);
}

static void cmd_bind(Console *con, int argc, char **argv, void *user)
{
    (void)user;
    if (argc < 2) {
        console_print(con, "usage: bind <key> [command]\n");
        return;
    }
    if (argc == 2) {
        const char *text = console_bind_get(con, argv[1]);
        if (text) console_print(con, "\"%s\" = \"%s\"\n", argv[1], text);
        else console_print(con, "\"%s\" is not bound\n", argv[1]);
        return;
    }
    char text[CONSOLE_VALUE_SIZE];
    console_bind(con, argv[1], join_args(text, sizeof text, argc, argv, 2));
}

static void cmd_unbind(Console *con, int argc, char **argv, void *user)
{
    (void)user;
    if (argc != 2) console_print(con, "usage: unbind <key>\n");
    else console_bind(con, argv[1], NULL);
}

static void cmd_unbindall(Console *con, int argc, char **argv, void *user)
{
    (void)argc, (void)argv, (void)user;
    con->bind_count = 0;
}

static void cmd_cvarlist(Console *con, int argc, char **argv, void *user)
{
    (void)user;
    const char *prefix = argc > 1 ? argv[1] : "";
    int shown = 0;
    for (int i = 0; i < con->cvar_count; i++) {
        const Cvar *cv = &con->cvars[i];
        if (!has_prefix(cv->name, prefix)) continue;
        console_print(con, "%c%c%c %s \"%s\"\n", cv->flags & CVAR_ARCHIVE ? 'A' : ' ',
                      cv->flags & CVAR_READONLY ? 'R' : ' ', cv->flags & CVAR_USER ? 'U' : ' ', cv->name, cv->value);
        shown++;
    }
    console_print(con, "%d cvars\n", shown);
}

static void cmd_cmdlist(Console *con, int argc, char **argv, void *user)
{
    (void)user;
    const char *prefix = argc > 1 ? argv[1] : "";
    int shown = 0;
    for (int i = 0; i < con->command_count; i++) {
        const Command *c = &con->commands[i];
        if (!has_prefix(c->name, prefix)) continue;
        if (c->help) console_print(con, "%s - %s\n", c->name, c->help);
        else console_print(con, "%s\n", c->name);
        shown++;
    }
    console_print(con, "%d commands\n", shown);
}

static void cmd_bindlist(Console *con, int argc, char **argv, void *user)
{
    (void)argc, (void)argv, (void)user;
    for (int i = 0; i < con->bind_count; i++) console_print(con, "%s \"%s\"\n", con->binds[i].key, con->binds[i].text);
    console_print(con, "%d binds\n", con->bind_count);
}

Console *console_create(ConsolePrintFn print, void *print_user)
{
    Console *con = calloc(1, sizeof *con);
    if (!con) return NULL;
    con->print = print;
    con->print_user = print_user;

    console_add_command(con, "set", cmd_set, NULL, "set a cvar");
    console_add_command(con, "seta", cmd_set, NULL, "set a cvar and save it");
    console_add_command(con, "reset", cmd_reset, NULL, "set a cvar back to its default");
    console_add_command(con, "toggle", cmd_toggle, NULL, "flip a cvar between 0 and 1");
    console_add_command(con, "vstr", cmd_vstr, NULL, "run the text a cvar holds");
    console_add_command(con, "echo", cmd_echo, NULL, "print text");
    console_add_command(con, "exec", cmd_exec, NULL, "run a config file");
    console_add_command(con, "bind", cmd_bind, NULL, "bind a key to commands, or show its bind");
    console_add_command(con, "unbind", cmd_unbind, NULL, "unbind a key");
    console_add_command(con, "unbindall", cmd_unbindall, NULL, "unbind every key");
    console_add_command(con, "cvarlist", cmd_cvarlist, NULL, "list the cvars [starting with a prefix]");
    console_add_command(con, "cmdlist", cmd_cmdlist, NULL, "list the commands [starting with a prefix]");
    console_add_command(con, "bindlist", cmd_bindlist, NULL, "list the binds");
    return con;
}

void console_destroy(Console *con) { free(con); }
