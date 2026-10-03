#include "update.h"

#include <miniz.h>
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

int update_plan(const Manifest *installed, const Manifest *latest, bool thorough, const UpdateReport *report,
                bool *wanted, char *changed, size_t changed_size)
{
    int count = 0;
    if (changed && changed_size) changed[0] = '\0';
    for (int i = 0; i < latest->count; i++) {
        const ManifestFile *f = &latest->files[i];
        // What manifest.txt vouches for, as the release has it, is taken on its word.
        // Anything else is hashed: a file it lists with another hash may already be the
        // new one, moved into place by an update cut off before manifest.txt was written.
        const ManifestFile *vouched = installed ? manifest_find(installed, f->path) : NULL;
        if (!manifest_protected(f->path)) {
            // Not the release's to keep (manifest_protected), so the player's once it is made:
            // made where it is missing and never was, so one they took out stays out; the new
            // one where it is still as the last release made it, so one they changed is theirs.
            uint8_t digest[32];
            if (!files_exists(f->path)) wanted[i] = !vouched;
            else wanted[i] = vouched && files_sha256(f->path, digest, NULL, NULL) && !same_hash(digest, f->sha256) &&
                             same_hash(digest, vouched->sha256);
        } else {
            bool hash = manifest_always_hashed(f->path) || thorough || !vouched || vouched->size != f->size ||
                        !same_hash(vouched->sha256, f->sha256);
            wanted[i] = !matches(f->path, f, hash);
        }
        if (wanted[i] && !count++ && changed && changed_size) snprintf(changed, changed_size, "%s", f->path);
        advance(report, (uint64_t)i + 1, (uint64_t)latest->count);
    }
    return count;
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

// An entry's path in the install: the package's own directory dropped (archive.h).
static const char *in_install(const char *entry)
{
    const char *slash = strchr(entry, '/');
    return slash ? slash + 1 : entry;
}

// --- the files alone, by range ---------------------------------------------------------

// The package where it lies, read through ranges of it. miniz reads the directory at the
// zip's end, then each file's header and data; each wanted file is brought before it asks,
// in one range with the wanted files close after it, so a file costs about what it weighs
// and a run of them one request. What it reads besides (the directory) comes 64 KB at a
// time.
#define GAP (64u << 10)  // what a miss brings, and the unwanted bytes a range may carry to join two files
#define SPAN (8u << 20)  // the most one range brings for files joined
#define SLACK 1024u      // a file's header past what the directory says of it: its name, an extra field

typedef struct Remote {
    const char *url;
    uint64_t size;
    uint8_t *cache;
    size_t capacity, fill;
    uint64_t start; // where in the package `cache` begins
    uint64_t done, total;
    const UpdateReport *report;
    char why[256];
} Remote;

// [at, at + n) of the package, as far as its end, into the cache by one range.
static bool remote_fetch(Remote *r, uint64_t at, uint64_t n)
{
    if (n > r->size - at) n = r->size - at;
    if (n > r->capacity) {
        uint8_t *grown = realloc(r->cache, (size_t)n);
        if (!grown) return fail(r->why, sizeof r->why, "no memory");
        r->cache = grown;
        r->capacity = (size_t)n;
    }
    r->fill = 0;
    if (http_get_range(r->url, at, (size_t)n, r->cache, r->why, sizeof r->why) != HTTP_OK) return false;
    r->start = at;
    r->fill = (size_t)n;
    r->done += n;
    advance(r->report, r->done, r->total);
    return true;
}

static size_t remote_read(void *opaque, mz_uint64 at, void *out, size_t n)
{
    Remote *r = opaque;
    if (r->why[0] || at > r->size || n > r->size - at) return 0;
    bool cached = at >= r->start && at + n <= r->start + r->fill;
    if (!cached && !remote_fetch(r, at, n > GAP ? n : GAP)) return 0;
    memcpy(out, r->cache + (at - r->start), n);
    return n;
}

// Where a file's data ends in the package, its header's name and extra field allowed for.
static uint64_t entry_end(const Remote *r, const mz_zip_archive_file_stat *st)
{
    uint64_t end = st->m_local_header_ofs + 30 + strlen(st->m_filename) + SLACK + st->m_comp_size;
    return end < r->size ? end : r->size;
}

// A file of the zip as the install has it: its entry, and whether it is wanted.
static bool wanted_entry(mz_zip_archive *zip, mz_uint i, const Manifest *latest, const bool *wanted,
                         mz_zip_archive_file_stat *st, const ManifestFile **f)
{
    if (!mz_zip_reader_file_stat(zip, i, st) || st->m_is_directory) return false;
    *f = manifest_find(latest, in_install(st->m_filename));
    return *f && wanted[*f - latest->files];
}

// The files `wanted` from the package at `url`, each into .update/files/, unless they are
// most of it (it is then quicker whole) or the server won't send parts: false, with why.
static bool fetch_files(const char *url, const ManifestFile *package, const Manifest *latest, const bool *wanted,
                        const UpdateReport *report, char *why, size_t why_size)
{
    Remote r = {.url = url, .size = package->size, .report = report};
    mz_zip_archive zip;
    memset(&zip, 0, sizeof zip);
    zip.m_pRead = remote_read;
    zip.m_pIO_opaque = &r;
    if (!mz_zip_reader_init(&zip, package->size, 0)) {
        fail(why, why_size, "its list of files can't be read (%s)", r.why[0] ? r.why : "not a zip");
        free(r.cache);
        return false;
    }

    mz_uint count = mz_zip_reader_get_num_files(&zip);
    mz_zip_archive_file_stat st;
    const ManifestFile *f;
    uint64_t bytes = 0;
    for (mz_uint i = 0; i < count; i++)
        if (wanted_entry(&zip, i, latest, wanted, &st, &f)) bytes += st.m_comp_size;
    bool ok = bytes * 2 <= package->size || fail(why, why_size, "most of it is wanted");
    r.total = r.done + bytes;

    for (mz_uint i = 0; i < count && ok; i++) {
        if (!wanted_entry(&zip, i, latest, wanted, &st, &f)) continue;
        if (st.m_local_header_ofs < r.start || entry_end(&r, &st) > r.start + r.fill) {
            // this file, and the wanted ones close after it, in one range
            uint64_t from = st.m_local_header_ofs, to = entry_end(&r, &st);
            for (mz_uint j = i + 1; j < count; j++) {
                mz_zip_archive_file_stat next;
                const ManifestFile *g;
                if (!mz_zip_reader_file_stat(&zip, j, &next) || next.m_local_header_ofs < from ||
                    next.m_local_header_ofs > to + GAP)
                    break;
                if (wanted_entry(&zip, j, latest, wanted, &next, &g) && entry_end(&r, &next) - from <= SPAN &&
                    entry_end(&r, &next) > to)
                    to = entry_end(&r, &next);
            }
            if (!remote_fetch(&r, from, to - from)) {
                ok = fail(why, why_size, "%s", r.why);
                break;
            }
        }
        char staged[MANIFEST_PATH_SIZE + 32];
        snprintf(staged, sizeof staged, "%s/%s", UPDATE_FILES, f->path);
        size_t size;
        void *data = mz_zip_reader_extract_to_heap(&zip, i, &size, 0);
        if (!data) {
            ok = fail(why, why_size, "%s can't be read from it (%s)", f->path, r.why[0] ? r.why : "damaged");
        } else {
            ok = files_write(staged, data, size) || fail(why, why_size, "%s can't be written", staged);
            mz_free(data);
            if (ok && (st.m_external_attr >> 16) & 0111) files_set_executable(staged); // made on a Unix
        }
    }
    mz_zip_reader_end(&zip);
    free(r.cache);
    return ok;
}

// --- or the package whole ----------------------------------------------------------------

static bool fetch_package(const char *url, const ManifestFile *package, const UpdateReport *report, char *error,
                          size_t error_size)
{
    // the download, unless an earlier try left it whole
    char archive[MANIFEST_PATH_SIZE + 32];
    snprintf(archive, sizeof archive, "%s/%s", UPDATE_STAGING, package->path);
    if (!matches(archive, package, true)) {
        char why[256];
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
    char why[256];
    if (!archive_extract(archive, UPDATE_FILES, advance_bytes, (void *)report, why, sizeof why))
        return fail(error, error_size, "the package can't be unpacked: %s", why);
    return true;
}

// The files `wanted` into .update/files/, each by itself or the package whole, and each
// checked against the release: nothing is moved yet.
static bool bring(const Manifest *latest, const bool *wanted, const char *url, const char *what, const UpdateReport *report,
                  char *error, size_t error_size)
{
    const ManifestFile *package = &latest->full;
    if (!package->path[0]) return fail(error, error_size, "the release names no package");
    if (!files_make_directory(UPDATE_STAGING)) return fail(error, error_size, "%s can't be made", UPDATE_STAGING);
    files_remove_tree(UPDATE_FILES);

    char installed[MANIFEST_VERSION_SIZE], why[256];
    read_version(installed, sizeof installed);
    if (what) say(report, "Downloading %s", what);
    else if (!strcmp(installed, latest->version)) say(report, "Downloading the missing files");
    else say(report, "Downloading version %s", latest->version);
    if (!fetch_files(url, package, latest, wanted, report, why, sizeof why)) {
        fprintf(stderr, "launcher: the package whole, not its files alone: %s\n", why);
        files_remove_tree(UPDATE_FILES);
        if (!fetch_package(url, package, report, error, error_size)) return false;
    }

    // Every file wanted, checked before any is moved.
    say(report, "Checking the files");
    for (int i = 0; i < latest->count; i++) {
        const ManifestFile *f = &latest->files[i];
        if (!wanted[i]) continue;
        char staged[MANIFEST_PATH_SIZE + 32];
        snprintf(staged, sizeof staged, "%s/%s", UPDATE_FILES, f->path);
        if (!matches(staged, f, true)) return fail(error, error_size, "%s in the package isn't the release's", f->path);
        advance(report, (uint64_t)i + 1, (uint64_t)latest->count);
    }
    return true;
}

bool update_apply(const Manifest *latest, const bool *wanted, const char *url, const UpdateReport *report,
                  char *error, size_t error_size)
{
    int count = 0;
    for (int i = 0; i < latest->count; i++) count += wanted[i];
    if (!count) return true;
    if (!bring(latest, wanted, url, NULL, report, error, error_size)) return false;

    // Into place, version.txt last: until it is, the install still says it is the old one.
    say(report, "Installing");
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < latest->count; i++) {
            const ManifestFile *f = &latest->files[i];
            if (!wanted[i] || (pass == 1) != !strcmp(f->path, UPDATE_VERSION)) continue;
            char staged[MANIFEST_PATH_SIZE + 32];
            snprintf(staged, sizeof staged, "%s/%s", UPDATE_FILES, f->path);
            if (!files_replace(staged, f->path))
                return fail(error, error_size, "%s can't be replaced: is the game or a server still running?", f->path);
        }
    }
    files_remove_tree(UPDATE_STAGING);
    return true;
}

// The launcher, the release's `self`, brought alone and put in the running one's place,
// which moves aside to UPDATE_TMP (a running executable may be renamed, though not written
// over). The new one deletes it as it starts.
static bool update_self(const Manifest *latest, const ManifestFile *self, const char *url, const UpdateReport *report,
                        char *error, size_t error_size)
{
    bool *wanted = calloc((size_t)latest->count, sizeof *wanted);
    if (!wanted) return fail(error, error_size, "no memory");
    wanted[self - latest->files] = true;
    char what[64];
    snprintf(what, sizeof what, "the launcher of version %s", latest->version);
    bool ok = bring(latest, wanted, url, what, report, error, error_size);
    free(wanted);
    if (!ok) return false;

    say(report, "Installing the launcher");
    char staged[MANIFEST_PATH_SIZE + 32];
    snprintf(staged, sizeof staged, "%s/%s", UPDATE_FILES, self->path);
    remove(UPDATE_TMP);
    if (!files_move(self->path, UPDATE_TMP)) return fail(error, error_size, "the launcher can't be moved aside to " UPDATE_TMP);
    if (!files_move(staged, self->path)) {
        files_move(UPDATE_TMP, self->path); // the old one back, to try again on the next start
        return fail(error, error_size, "the new launcher can't be put in place");
    }
    files_set_executable(self->path);
    files_remove_tree(UPDATE_STAGING);
    return true;
}

// What the install held that the release no longer does: what manifest.txt lists and
// `latest` doesn't, and the old names, with the "<name>.old" a replace may have moved
// them to (files_replace). A player's own files were never listed; one outside the
// release's own (manifest_protected) goes only as it came, as one changed is the player's.
static void remove_retired(const Manifest *installed, const Manifest *latest)
{
    for (int i = 0; i < installed->count; i++) {
        const ManifestFile *f = &installed->files[i];
        if (manifest_find(latest, f->path) || (!manifest_protected(f->path) && !matches(f->path, f, true))) continue;
        remove(f->path);
        files_remove_old(f->path);
    }
    for (size_t i = 0; i < sizeof RETIRED / sizeof RETIRED[0]; i++)
        if (!manifest_find(latest, RETIRED[i])) {
            remove(RETIRED[i]);
            files_remove_old(RETIRED[i]);
        }
}

UpdateOutcome update_run(const UpdateOptions *options, const UpdateReport *report, char *version, size_t version_size,
                         char *error, size_t error_size)
{
    char before[MANIFEST_VERSION_SIZE];
    read_version(before, sizeof before);
    snprintf(version, version_size, "%s", before);
    error[0] = '\0';
    remove(UPDATE_TMP); // the launcher this one replaced, if it did

    char why[256];
    Manifest installed;
    bool have_installed = manifest_load(&installed, UPDATE_MANIFEST, why, sizeof why);
    // what a replaced executable left behind on Windows (files_replace)
    for (int i = 0; i < installed.count; i++)
        if (manifest_always_hashed(installed.files[i].path)) files_remove_old(installed.files[i].path);

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
        bool *wanted = have_installed ? calloc((size_t)installed.count + 1, sizeof *wanted) : NULL;
        if (wanted) update_plan(&installed, &installed, false, NULL, wanted, damaged, sizeof damaged);
        free(wanted);
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
    bool *wanted = parsed ? calloc((size_t)latest.count + 1, sizeof *wanted) : NULL;
    if (!wanted) {
        if (parsed) manifest_free(&latest);
        manifest_free(&installed);
        snprintf(error, error_size, parsed ? "no memory" : "the release's manifest can't be read: %s", why);
        return UPDATE_FAILED;
    }

    // The launcher first, and alone: the rest is brought by the new one, by its own rules.
    const ManifestFile *self = options->self && options->self[0] ? manifest_find(&latest, options->self) : NULL;
    if (self && !matches(self->path, self, true)) {
        snprintf(url, sizeof url, "%s/download/v%s/%s", options->releases, latest.version, latest.full.path);
        bool ok = update_self(&latest, self, url, report, error, error_size);
        if (ok) snprintf(error, error_size, "The launcher has been updated to version %s. Please start the game again.", latest.version);
        free(wanted);
        manifest_free(&installed);
        manifest_free(&latest);
        return ok ? UPDATE_RESTART : UPDATE_FAILED;
    }

    say(report, "Checking the game's files");
    char changed[MANIFEST_PATH_SIZE];
    int count = update_plan(&installed, &latest, options->thorough, report, wanted, changed, sizeof changed);
    UpdateOutcome outcome = !count                            ? UPDATE_CURRENT
                            : !strcmp(before, latest.version) ? UPDATE_REPAIRED
                                                              : UPDATE_UPDATED;
    snprintf(url, sizeof url, "%s/download/v%s/%s", options->releases, latest.version, latest.full.path);
    if (!update_apply(&latest, wanted, url, report, error, error_size)) {
        outcome = UPDATE_FAILED;
    } else {
        // what the install now is, for the next start to trust
        if (count || !have_installed || strcmp(installed.version, latest.version) || installed.count != latest.count)
            manifest_write(&latest, UPDATE_MANIFEST);
        if (!count) files_remove_tree(UPDATE_STAGING);
        remove_retired(&installed, &latest);
        snprintf(version, version_size, "%s", latest.version);
    }
    free(wanted);
    manifest_free(&installed);
    manifest_free(&latest);
    return outcome;
}
