#pragma once

// A manifest: what an install of one version holds, file by file, which `xmake dist`
// writes and the launcher checks an install against. Text, a line to a fact:
//
//   version 0.5.0
//   package full <sha256> <bytes> soldatreloaded-0.5.0-windows-x64.zip
//   file <sha256> <bytes> data/maps/ctf_Ash.pms
//
// The path or name is the rest of the line, so it may hold spaces ("Soldat
// Reloaded.exe"); blank lines and // comments are skipped. The installed copy
// (manifest.txt, inside the packages) has the version and the files; the release's
// (latest-<platform>.txt, beside the package) adds where to get it ("package update", an
// older release's second, smaller package, is read and passed over). It lists every file a
// release ships; what the launcher does with each, by where it lies, is update.h's. The
// player's own files (config/client/, config/server/, their mods) are never listed.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MANIFEST_PATH_SIZE 256
#define MANIFEST_VERSION_SIZE 32

typedef struct ManifestFile {
    char path[MANIFEST_PATH_SIZE];
    uint8_t sha256[32];
    uint64_t size;
} ManifestFile;

typedef struct Manifest {
    char version[MANIFEST_VERSION_SIZE];
    ManifestFile full; // the package, by name; an empty path when not given
    ManifestFile *files;
    int count, capacity;
} Manifest;

// Reads a manifest's text. False, with the line that was wrong in `error`, on anything
// it doesn't understand, a path that would reach outside the install, or no version.
bool manifest_parse(Manifest *m, const char *text, size_t size, char *error, size_t error_size);
bool manifest_load(Manifest *m, const char *path, char *error, size_t error_size);
void manifest_free(Manifest *m);

// The version and the files, as the installed copy has them (no packages).
bool manifest_write(const Manifest *m, const char *path);

const ManifestFile *manifest_find(const Manifest *m, const char *path);

// Whether a path stays inside the directory it is relative to: not absolute, no drive,
// no "." or ".." among its parts, '/' between them and nothing that isn't printable.
bool manifest_safe_path(const char *path);

// Whether a path is at the top of the install, with no directory.
bool manifest_top_level(const char *path);
// Whether a path is always hashed, manifest.txt or no (update.h): the top-level files, the
// executables in bin/ and config/, which change with the code, where the art doesn't.
bool manifest_always_hashed(const char *path);

// Whether a path is the release's to keep as it has it (update.h): the top-level files (the
// launcher, version.txt), the executables in bin/, the game's data/ and mods/default/, its
// config/defaults/ and its scripts/examples/. Anything else a release ships is its start of
// a file that is then the player's.
bool manifest_protected(const char *path);
