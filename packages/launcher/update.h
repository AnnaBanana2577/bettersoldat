#pragma once

// Bringing an install up to the latest release, or back to it when its files are
// damaged. What a release publishes beside its packages, for each platform:
//
//   latest-<platform>.txt   its manifest (manifest.h): the version, the package and every
//                           file of an install, with their hashes
//   <stem>.zip              the package: everything a player unpacks, under one directory
//                           named after it
//
// The launcher fetches <releases>/latest/download/latest-<platform>.txt, which GitHub
// redirects to the newest release's copy. Its own file comes first, and alone: where the
// release's differs, it is brought, the running launcher moved aside to UPDATE_TMP (a
// running executable may be renamed, though not written over) and the new one put in its
// name, and the player is asked to start the game again (UPDATE_RESTART), so the rest of
// the release is always brought by its own launcher, by its rules. The new one deletes
// UPDATE_TMP as it starts.
//
// Then it compares the install with the release: a file missing or of the wrong size, or
// whose hash isn't the manifest's, has to come down. Only those come down. A zip says at
// its end where each file in it lies, so the launcher reads that, by an HTTP range, and
// then the files it wants, each by its own range (a read-ahead making neighbours one
// request): a new executable or a fixed sound costs what it weighs, not the package. When the files wanted are most of the package,
// or the server won't send parts, the package is downloaded whole instead, and checked
// against its hash. Either way the files land in .update/, are each checked against the
// manifest, and only then moved into place, version.txt last, so an update cut off part
// way is finished on the next start. A file already the release's is left where it is,
// so the launcher is replaced only by a release that changes it. What the old
// manifest.txt listed and the release doesn't is removed.
//
// So an update only ever touches what a release lists, each by where it lies, the file on
// disk weighed against the manifest it came with (manifest.txt) and the release's:
//
//   the release's own (manifest_protected: the top-level files, bin/, data/,
//   mods/default/, scripts/examples/): brought where it is missing or
//   otherwise, damage repaired; taken away once a release no longer lists it
//
//   anything else a release ships (scripts/main.lua, config/): its start of a file that is
//   then the player's. Made where it is missing and never was, so one taken out stays out; the
//   release's new one where it is still as the last release made it, but left as it is
//   once the player has changed it; taken away once no release lists it only if it is
//   still as it came
//
// The player's own files (their mods beside mods/default/, demos/, a config.cfg from
// before config/) are in no manifest, and never touched.
//
// Hashing every asset on every start would take a second, so the install's
// manifest.txt is trusted for what it vouches for: a file it lists with the release's
// hash, and of that size, is taken as it is. The files a release changes most (the
// top-level files and bin/) are always hashed, and `thorough` hashes the rest too.

#include <stdbool.h>
#include <stdint.h>

#include "manifest.h"

#define UPDATE_STAGING ".update"
#define UPDATE_MANIFEST "manifest.txt"
#define UPDATE_VERSION "version.txt"
#ifdef _WIN32
#define UPDATE_TMP "tmp.exe" // the launcher a new one replaced, which the new one deletes
#else
#define UPDATE_TMP "tmp"
#endif

// What an update says while it works: a line for the window, and how far along.
typedef struct UpdateReport {
    void *user;
    void (*phase)(void *user, const char *text);
    void (*progress)(void *user, uint64_t done, uint64_t total);
} UpdateReport;

// The files the install needs to become `latest`, given what `installed` (manifest.txt;
// empty if there was none) vouches for: each of `latest`'s it lacks or holds otherwise,
// marked in `wanted` (latest->count of them). How many; `changed`, if not NULL, gets the
// first.
int update_plan(const Manifest *installed, const Manifest *latest, bool thorough, const UpdateReport *report,
                bool *wanted, char *changed, size_t changed_size);

// Brings the files `wanted` from the package at `url` (a release's is
// <releases>/download/v<version>/<name>), each by itself or the package whole, checks them
// and moves them into place. False, with the reason in `error`, if any step failed; the
// install is then as it was, or part way to `latest` and the next run takes it the rest of
// the way.
bool update_apply(const Manifest *latest, const bool *wanted, const char *url, const UpdateReport *report,
                  char *error, size_t error_size);

typedef enum UpdateOutcome {
    UPDATE_CURRENT,   // the install is the latest release, intact
    UPDATE_UPDATED,   // it is now
    UPDATE_REPAIRED,  // it was the latest, with files missing or damaged; now intact
    UPDATE_UNCHECKED, // the releases couldn't be asked (offline, or no list for this platform)
    UPDATE_FAILED,    // `error` says why
    UPDATE_RESTART,   // the launcher is the release's now, and only it: start it again for the rest
} UpdateOutcome;

typedef struct UpdateOptions {
    const char *releases; // https://github.com/<owner>/<repo>/releases
    const char *platform; // windows-x64, linux-x86_64
    bool thorough;        // hash every file, not only those manifest.txt doesn't vouch for
    const char *self;     // the launcher's own file in the install, brought first and alone; NULL for none
} UpdateOptions;

// The whole of it: the release asked for its manifest, the install compared, and
// whatever is needed brought down. `version` gets the version installed at the end.
UpdateOutcome update_run(const UpdateOptions *options, const UpdateReport *report, char *version, size_t version_size,
                         char *error, size_t error_size);
