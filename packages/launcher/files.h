#pragma once

// The file system as the launcher needs it, the same on Windows and Linux. Paths are
// relative to the install directory, which the launcher makes its working directory
// first thing, and use '/'; the files the game ships are named in ASCII, so the C
// library's narrow calls serve.

#include <stdbool.h>
#include <stdint.h>

#include "sha256.h"

// The directory the running executable is in becomes the working directory. False if
// it couldn't be found or entered.
bool files_enter_own_directory(void);

bool files_exists(const char *path);
// The size of a regular file; false if there is none.
bool files_size(const char *path, uint64_t *size);
// Every directory above `path`, made where missing. False if one couldn't be.
bool files_make_parents(const char *path);
bool files_make_directory(const char *path);
// A directory and everything in it, gone; true if nothing is left.
bool files_remove_tree(const char *path);

// `from` takes `to`'s place, replacing it. A file in use (the launcher itself, on
// Windows) can't be replaced but can be moved, so it is moved to "<to>.old" first, and
// files_remove_old clears that away on a later run.
bool files_replace(const char *from, const char *to);
void files_remove_old(const char *path);

// rwxr-xr-x, so an executable runs (nothing on Windows).
void files_set_executable(const char *path);

// The whole of a file, NUL-terminated beyond `size`; NULL if it can't be read. Free it.
char *files_read(const char *path, uint64_t *size);
bool files_write(const char *path, const void *data, size_t size);

// A file's SHA-256, `progress` (if not NULL) told the bytes read as they go.
typedef void (*FilesProgress)(void *user, uint64_t bytes);
bool files_sha256(const char *path, uint8_t digest[32], FilesProgress progress, void *user);

// Waits a moment: a file just written can be held briefly by a virus scanner.
void files_pause(int milliseconds);
