#include "update.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "archive.h"
#include "files.h"
#include "http.h"
#include "sha256.h"

#define UPDATE_FILES UPDATE_STAGING "/files"

// What releases before 0.5.1 named the client and the server. A launcher of theirs that
// brings in a newer release leaves them behind, not knowing they are gone.
static const char *const RETIRED[] = {"soldatreloaded.exe", "soldatreloaded-server.exe", "soldatreloaded",
                                      "soldatreloaded-server"};

static void say(const UpdateReport *r, const char *fmt, ...)
{
    if (!r || !r->phase) return;
    char text[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(text, sizeof text, fmt, args);
    va_end(args);
    r->phase(r->user, text);
}

static void advance(const UpdateReport *r, uint64_t done, uint64_t total)
{
    if (r && r->progress) r->progress(r->user, done, total);
}

static void advance_bytes(void *user, uint64_t done, uint64_t total) { advance(user, done, total); }

static bool same_hash(const uint8_t a[32], const uint8_t b[32]) { return !memcmp(a, b, 32); }

// Whether the file at `path` is `f`: its size, and its hash if `hash`.
static bool matches(const char *path, const ManifestFile *f, bool hash)
{
    uint64_t size;
    if (!files_size(path, &size) || size != f->size) return false;
    if (!hash) return true;
    uint8_t digest[32];
    return files_sha256(path, digest, NULL, NULL) && same_hash(digest, f->sha256);
}

// The package holds a file: the update package the top-level ones, the full one all.
static bool in_package(UpdateNeed need, const ManifestFile *f)
{
    return need == UPDATE_FULL || manifest_top_level(f->path);
}

UpdateNeed update_plan(const Manifest *installed, const Manifest *latest, bool thorough, const UpdateReport *report,
                       char *changed, size_t changed_size)
{
    UpdateNeed need = UPDATE_NOTHING;
    if (changed && changed_size) changed[0] = '\0';
    for (int i = 0; i < latest->count; i++) {
        const ManifestFile *f = &latest->files[i];
        bool top = manifest_top_level(f->path);
        // What manifest.txt vouches for, as the release has it, is taken on its word.
        // Anything else is hashed: a file it lists with another hash may already be the
        // new one, moved into place by an update cut off before manifest.txt was written.
        const ManifestFile *vouched = installed ? manifest_find(installed, f->path) : NULL;
        bool hash = top || thorough || !vouched || vouched->size != f->size || !same_hash(vouched->sha256, f->sha256);
        if (!matches(f->path, f, hash)) {
            if (changed && changed_size && !changed[0]) snprintf(changed, changed_size, "%s", f->path);
            if (!top) return UPDATE_FULL; // nothing needs more
            need = UPDATE_BINARIES;
        }
        advance(report, (uint64_t)i + 1, (uint64_t)latest->count);
    }
    return need;
}

static bool fail(char *error, size_t error_size, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vsnprintf(error, error_size, fmt, args);
    va_end(args);
    return false;
}

// version.txt's first line, or "" if there is none.
static void read_version(char *version, size_t size)
{
    char *text = files_read(UPDATE_VERSION, NULL);
    version[0] = '\0';
    if (!text) return;
    text[strcspn(text, "\r\n")] = '\0';
    snprintf(version, size, "%s", text);
    free(text);
}

bool update_apply(const Manifest *latest, UpdateNeed need, const char *releases, const UpdateReport *report,
                  char *error, size_t error_size)
{
    if (need == UPDATE_NOTHING) return true;
    const ManifestFile *package = need == UPDATE_FULL ? &latest->full : &latest->update;
    if (!package->path[0])
        return fail(error, error_size, "the release names no %s package", need == UPDATE_FULL ? "full" : "update");
    if (!files_make_directory(UPDATE_STAGING)) return fail(error, error_size, "%s can't be made", UPDATE_STAGING);

    // The download, unless an earlier try left it whole.
    char archive[MANIFEST_PATH_SIZE + 32];
    snprintf(archive, sizeof archive, "%s/%s", UPDATE_STAGING, package->path);
    if (!matches(archive, package, true)) {
        char url[1024], why[256];
        snprintf(url, sizeof url, "%s/download/v%s/%s", releases, latest->version, package->path);
        char installed[MANIFEST_VERSION_SIZE];
        read_version(installed, sizeof installed);
        if (!strcmp(installed, latest->version)) say(report, "Downloading the missing files");
        else say(report, "Downloading version %s", latest->version);
        uint8_t digest[32];
        uint64_t size = 0;
        HttpResult got = http_download(url, archive, digest, &size, advance_bytes, (void *)report, why, sizeof why);
        if (got == HTTP_NOT_FOUND) return fail(error, error_size, "%s isn't in the release", package->path);
        if (got != HTTP_OK) return fail(error, error_size, "the download failed: %s", why);
        if (size != package->size || !same_hash(digest, package->sha256)) {
            remove(archive);
            return fail(error, error_size, "the download is damaged: it isn't what the release lists");
        }
    }

    say(report, "Unpacking");
    files_remove_tree(UPDATE_FILES);
    char why[256];
    if (!archive_extract(archive, UPDATE_FILES, advance_bytes, (void *)report, why, sizeof why))
        return fail(error, error_size, "the package can't be unpacked: %s", why);

    // Every file it should hold, checked before any is moved.
    say(report, "Checking the files");
    for (int i = 0; i < latest->count; i++) {
        const ManifestFile *f = &latest->files[i];
        if (!in_package(need, f)) continue;
        char staged[MANIFEST_PATH_SIZE + 32];
        snprintf(staged, sizeof staged, "%s/%s", UPDATE_FILES, f->path);
        if (!matches(staged, f, true)) return fail(error, error_size, "%s in the package isn't the release's", f->path);
        advance(report, (uint64_t)i + 1, (uint64_t)latest->count);
    }

    // Into place, version.txt last: until it is, the install still says it is the old one.
    // A file that is already the release's is left as it is: the launcher, running, among them.
    say(report, "Installing");
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < latest->count; i++) {
            const ManifestFile *f = &latest->files[i];
            if (!in_package(need, f) || (pass == 1) != !strcmp(f->path, UPDATE_VERSION)) continue;
            if (matches(f->path, f, true)) continue;
            char staged[MANIFEST_PATH_SIZE + 32];
            snprintf(staged, sizeof staged, "%s/%s", UPDATE_FILES, f->path);
            if (!files_replace(staged, f->path))
                return fail(error, error_size, "%s can't be replaced: is the game or a server still running?", f->path);
        }
    }
    // the player's config, only where there is none
    if (need == UPDATE_FULL && !files_exists("config.cfg") && files_exists(UPDATE_FILES "/config.cfg"))
        files_replace(UPDATE_FILES "/config.cfg", "config.cfg");

    files_remove_tree(UPDATE_STAGING);
    return true;
}

// What the install held that the release no longer does: what manifest.txt lists and
// `latest` doesn't, and the old names. A player's own files were never listed.
static void remove_retired(const Manifest *installed, const Manifest *latest)
{
    for (int i = 0; i < installed->count; i++)
        if (!manifest_find(latest, installed->files[i].path)) remove(installed->files[i].path);
    for (size_t i = 0; i < sizeof RETIRED / sizeof RETIRED[0]; i++)
        if (!manifest_find(latest, RETIRED[i])) remove(RETIRED[i]);
}

UpdateOutcome update_run(const UpdateOptions *options, const UpdateReport *report, char *version, size_t version_size,
                         char *error, size_t error_size)
{
    char before[MANIFEST_VERSION_SIZE];
    read_version(before, sizeof before);
    snprintf(version, version_size, "%s", before);
    error[0] = '\0';

    char why[256];
    Manifest installed;
    bool have_installed = manifest_load(&installed, UPDATE_MANIFEST, why, sizeof why);
    // what a replaced executable left behind on Windows (files_replace)
    for (int i = 0; i < installed.count; i++)
        if (manifest_top_level(installed.files[i].path)) files_remove_old(installed.files[i].path);

    say(report, "Checking for updates");
    char url[1024];
    snprintf(url, sizeof url, "%s/latest/download/latest-%s.txt", options->releases, options->platform);
    char *text = NULL;
    size_t size = 0;
    HttpResult got = http_get(url, 16u << 20, &text, &size, why, sizeof why);
    if (got != HTTP_OK) {
        if (got == HTTP_NOT_FOUND) snprintf(why, sizeof why, "the newest release has nothing for %s", options->platform);
        // Nothing to compare with but what the install says of itself.
        char damaged[MANIFEST_PATH_SIZE] = "";
        if (have_installed) update_plan(&installed, &installed, false, NULL, damaged, sizeof damaged);
        manifest_free(&installed);
        if (damaged[0]) {
            snprintf(error, error_size, "%s is missing or damaged, and it can't be repaired now: %s", damaged, why);
            return UPDATE_FAILED;
        }
        snprintf(error, error_size, "Couldn't check for updates: %s", why);
        return UPDATE_UNCHECKED;
    }

    Manifest latest;
    bool parsed = manifest_parse(&latest, text, size, why, sizeof why);
    free(text);
    if (!parsed) {
        manifest_free(&installed);
        snprintf(error, error_size, "the release's manifest can't be read: %s", why);
        return UPDATE_FAILED;
    }

    say(report, "Checking the game's files");
    char changed[MANIFEST_PATH_SIZE];
    UpdateNeed need = update_plan(&installed, &latest, options->thorough, report, changed, sizeof changed);
    UpdateOutcome outcome = need == UPDATE_NOTHING           ? UPDATE_CURRENT
                            : !strcmp(before, latest.version) ? UPDATE_REPAIRED
                                                              : UPDATE_UPDATED;
    if (!update_apply(&latest, need, options->releases, report, error, error_size)) {
        outcome = UPDATE_FAILED;
    } else {
        // what the install now is, for the next start to trust
        if (need != UPDATE_NOTHING || !have_installed || strcmp(installed.version, latest.version) ||
            installed.count != latest.count)
            manifest_write(&latest, UPDATE_MANIFEST);
        if (need == UPDATE_NOTHING) files_remove_tree(UPDATE_STAGING);
        remove_retired(&installed, &latest);
        snprintf(version, version_size, "%s", latest.version);
    }
    manifest_free(&installed);
    manifest_free(&latest);
    return outcome;
}
