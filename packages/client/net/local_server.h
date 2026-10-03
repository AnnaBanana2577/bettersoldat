#pragma once

// Local Play: the dedicated server, started by the game beside it (bin/server, the
// executable next to the client's own), so a game hosted from the main menu is the one a
// server from this install hosts: the same program on the same files (config/server.cfg,
// weapons.ini, maplist.txt, the lists, scripts/main.lua). Its console's output comes back
// line by line for the game's console, and lines go to its console as typed there
// (addbot). It is ended with the game: told to quit, and made to if it doesn't; and tied to
// the game's process besides (a job object on Windows, the parent's death signal on
// Linux), so a crash doesn't leave it holding the port.

#include <stdbool.h>
#include <stddef.h>

#define LOCAL_SERVER_LINE 512

typedef struct LocalServer {
    bool running;
#ifdef _WIN32
    void *process, *job, *input, *output;
#else
    int pid, input, output;
#endif
    char partial[LOCAL_SERVER_LINE]; // a line of its output not yet ended
    size_t partial_len;
} LocalServer;

// The server started, with `args` (NULL-ended, may be NULL) on its command line, in the
// working directory, which is the install. False, with why in `error`, if it couldn't be.
bool local_server_start(LocalServer *s, const char *const *args, char *error, size_t error_size);

// A line of its output, without its newline, into `line`; false when none has come.
bool local_server_line(LocalServer *s, char *line, size_t size);

// Whether it is still running.
bool local_server_alive(LocalServer *s);

// `text` to its console, as typed there.
void local_server_send(LocalServer *s, const char *text);

// Told to quit, given a moment, then ended if it hasn't.
void local_server_stop(LocalServer *s);
