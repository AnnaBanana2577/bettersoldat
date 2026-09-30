#pragma once

// The server's console reads its standard input: a thread blocks on it line by line,
// and the main loop takes what has come (nextmap, quit, a cvar). A thread rather than
// polling, because a console and a pipe are polled differently on each platform and
// a blocking read is the same everywhere.

#include <stdbool.h>
#include <stddef.h>

#define STDIN_LINE_SIZE 256

// Starts the thread; false if it couldn't be. Without one the server runs deaf.
bool stdin_reader_start(void);

// The oldest line waiting, without its newline; false when there is none.
bool stdin_reader_take(char *line, size_t size);
