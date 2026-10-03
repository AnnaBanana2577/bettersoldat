#pragma once

// Bringing an install up to the latest release, or back to it when its files are
// damaged. What a release publishes beside its packages, for each platform:
//
//   latest-<platform>.txt   its manifest (manifest.h): the version, the two packages
//                           and every file of an install, with their hashes
//   <stem>-patch.<ext>      the update package: the files at the top of an install
//                           (the executables, version.txt, manifest.txt, the licence)
//   <stem>.<ext>            the full package: everything a player unpacks
//
// The launcher fetches <releases>/latest/download/latest-<platform>.txt, which GitHub
// redirects to the newest release's copy, and compares the install with it: a file
// missing or of the wrong size, or whose hash isn't the manifest's, has to come down.
// When all of those are at the top of the install or in config/ the update package will
// do; when any is in data/, mods/default/ or scripts/ it takes the full one. The package is downloaded into
// .update/, checked against its hash, unpacked, each file checked again, and only then
// moved into place, version.txt last, so an update cut off part way is finished on the
// next start. A file already the release's is left where it is, so the launcher is
// replaced only by a release that changes it. What the old manifest.txt listed and the
// release doesn't is removed. The player's own files (config/client/, config/server/,
// their mods beside mods/default/, demos/, a config.cfg from before config/) are in no
// package and no manifest, so they are never touched; config/defaults/ and mods/default/
// are the release's, and replaced with it.
//
// Hashing every asset on every start would take a second, so the install's
// manifest.txt is trusted for what it vouches for: a file it lists with the release's
// hash, and of that size, is taken as it is. The top-level files are always hashed, and
// `thorough` hashes the rest too.

#include <stdbool.h>
#include <stdint.h>

#include "manifest.h"

#define UPDATE_STAGING ".update"
#define UPDATE_MANIFEST "manifest.txt"
#define UPDATE_VERSION "version.txt"

typedef enum UpdateNeed {
    UPDATE_NOTHING, // the install is the release's
    UPDATE_BINARIES, // only top-level files differ: the update package
    UPDATE_FULL,     // something in a directory differs: the full package
} UpdateNeed;

// What an update says while it works: a line for the window, and how far along.
typedef struct UpdateReport {
    void *user;
    void (*phase)(void *user, const char *text);
    void (*progress)(void *user, uint64_t done, uint64_t total);
} UpdateReport;

// What the install needs to become `latest`, given what `installed` (manifest.txt;
// empty if there was none) vouches for. `changed`, if not NULL, gets the first file
// found wanting.
UpdateNeed update_plan(const Manifest *installed, const Manifest *latest, bool thorough, const UpdateReport *report,
                       char *changed, size_t changed_size);

// Downloads the package `need` names from <releases>/download/v<version>/, checks it,
// unpacks it into .update/ and moves its files into place. False, with the reason in
// `error`, if any step failed; the install is then as it was, or part way to `latest`
// and the next run takes it the rest of the way.
bool update_apply(const Manifest *latest, UpdateNeed need, const char *releases, const UpdateReport *report,
                  char *error, size_t error_size);

typedef enum UpdateOutcome {
    UPDATE_CURRENT,   // the install is the latest release, intact
    UPDATE_UPDATED,   // it is now
    UPDATE_REPAIRED,  // it was the latest, with files missing or damaged; now intact
    UPDATE_UNCHECKED, // the releases couldn't be asked (offline, or no list for this platform)
    UPDATE_FAILED,    // `error` says why
} UpdateOutcome;

typedef struct UpdateOptions {
    const char *releases; // https://github.com/<owner>/<repo>/releases
    const char *platform; // windows-x64, linux-x86_64
    bool thorough;        // hash every file, not only those manifest.txt doesn't vouch for
} UpdateOptions;

// The whole of it: the release asked for its manifest, the install compared, and
// whatever is needed brought down. `version` gets the version installed at the end.
UpdateOutcome update_run(const UpdateOptions *options, const UpdateReport *report, char *version, size_t version_size,
                         char *error, size_t error_size);
