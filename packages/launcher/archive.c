#include "archive.h"

#include <miniz.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "files.h"
#include "manifest.h"

// The path an entry lands on: `into`, then the entry's path without its first
// directory. False if the entry isn't one to write (a path that would reach outside).
static bool landing(const char *into, const char *entry, char *out, size_t out_size)
{
    char name[1024];
    snprintf(name, sizeof name, "%s", entry);
    for (char *c = name; *c; c++)
        if (*c == '\\') *c = '/';
    const char *inside = strchr(name, '/');
    inside = inside ? inside + 1 : name;
    if (!manifest_safe_path(inside)) return false;
    return snprintf(out, out_size, "%s/%s", into, inside) < (int)out_size;
}

// --- zip -------------------------------------------------------------------------------

// An entry inflated whole into memory and written out in one go: miniz's own
// extract_to_file writes in pieces small enough to make unpacking the full package five
// times slower on Windows.
static bool extract_entry(mz_zip_archive *zip, mz_uint i, const char *path)
{
    size_t size;
    void *data = mz_zip_reader_extract_to_heap(zip, i, &size, 0);
    if (!data) return false;
    bool ok = files_write(path, data, size);
    mz_free(data);
    return ok;
}

static bool extract_zip(const char *archive, const char *into, ArchiveProgress progress, void *user, char *error,
                        size_t error_size)
{
    mz_zip_archive zip;
    memset(&zip, 0, sizeof zip);
    if (!mz_zip_reader_init_file(&zip, archive, 0)) {
        snprintf(error, error_size, "the zip can't be read");
        return false;
    }
    mz_uint count = mz_zip_reader_get_num_files(&zip);
    uint64_t total = 0, done = 0;
    for (mz_uint i = 0; i < count; i++) {
        mz_zip_archive_file_stat st;
        if (mz_zip_reader_file_stat(&zip, i, &st) && !st.m_is_directory) total += st.m_uncomp_size;
    }
    bool ok = true;
    for (mz_uint i = 0; i < count && ok; i++) {
        mz_zip_archive_file_stat st;
        char path[1024];
        if (!mz_zip_reader_file_stat(&zip, i, &st)) {
            snprintf(error, error_size, "entry %u of the zip can't be read", i);
            ok = false;
        } else if (st.m_is_directory) {
            continue;
        } else if (!landing(into, st.m_filename, path, sizeof path)) {
            snprintf(error, error_size, "the zip holds a path outside the game: %s", st.m_filename);
            ok = false;
        } else if (!extract_entry(&zip, i, path)) {
            snprintf(error, error_size, "%s can't be written", path);
            ok = false;
        } else {
            if ((st.m_external_attr >> 16) & 0111) files_set_executable(path); // made on a Unix
            done += st.m_uncomp_size;
            if (progress) progress(user, done, total);
        }
    }
    mz_zip_reader_end(&zip);
    return ok;
}

// --- tar.gz ----------------------------------------------------------------------------

// The tar as it is inflated, a 512-byte block at a time: a header, then the entry's
// blocks. GNU tar names a path longer than a header holds in an 'L' entry before it,
// and POSIX tar in an 'x' entry's "path=" record; both are kept for the entry after.
typedef enum TarState { TAR_HEADER, TAR_FILE, TAR_LONG_NAME, TAR_PAX, TAR_SKIP } TarState;

typedef struct Tar {
    const char *into;
    TarState state;
    uint8_t block[512];
    size_t fill;
    uint64_t remaining; // bytes of the current entry still to come
    FILE *out;
    char path[1024];
    bool executable;
    char *meta;         // an 'L' or 'x' entry's contents
    size_t meta_fill, meta_size;
    char next_name[1024]; // what the last of those named the next entry
    uint32_t crc;
    uint64_t done, total;
    ArchiveProgress progress;
    void *user;
    char *error;
    size_t error_size;
    bool ended;
} Tar;

static uint64_t octal(const uint8_t *field, size_t size)
{
    uint64_t v = 0;
    size_t i = 0;
    while (i < size && field[i] == ' ') i++;
    for (; i < size && field[i] >= '0' && field[i] <= '7'; i++) v = v * 8 + (uint64_t)(field[i] - '0');
    return v;
}

static void field(char *out, size_t out_size, const uint8_t *src, size_t size)
{
    size_t n = 0;
    while (n < size && src[n]) n++;
    if (n >= out_size) n = out_size - 1;
    memcpy(out, src, n);
    out[n] = '\0';
}

static bool tar_fail(Tar *t, const char *fmt, const char *what)
{
    snprintf(t->error, t->error_size, fmt, what);
    return false;
}

// The record "path=" of a pax header: "<length> path=<value>\n".
static void pax_path(Tar *t)
{
    size_t at = 0;
    while (at < t->meta_fill) {
        size_t length = 0, i = at;
        while (i < t->meta_fill && t->meta[i] >= '0' && t->meta[i] <= '9') length = length * 10 + (size_t)(t->meta[i++] - '0');
        if (length == 0 || at + length > t->meta_fill || i >= t->meta_fill || t->meta[i] != ' ') return;
        const char *record = t->meta + i + 1;
        size_t record_size = at + length - (i + 1);
        if (record_size > 5 && !strncmp(record, "path=", 5)) {
            size_t n = record_size - 5 - 1; // without the newline
            if (n >= sizeof t->next_name) n = sizeof t->next_name - 1;
            memcpy(t->next_name, record + 5, n);
            t->next_name[n] = '\0';
        }
        at += length;
    }
}

static bool tar_header(Tar *t)
{
    const uint8_t *h = t->block;
    bool empty = true;
    for (int i = 0; i < 512 && empty; i++) empty = h[i] == 0;
    if (empty) {
        t->ended = true; // the end-of-archive blocks; anything after is padding
        return true;
    }
    unsigned sum = 0;
    for (int i = 0; i < 512; i++) sum += (i >= 148 && i < 156) ? ' ' : h[i];
    if (sum != octal(h + 148, 8)) return tar_fail(t, "the tar is damaged%s", "");

    uint64_t size = octal(h + 124, 12);
    char type = (char)h[156];
    t->remaining = size;
    if (type == 'L' || type == 'x') {
        if (size > 1 << 20) return tar_fail(t, "the tar has a header too large%s", "");
        free(t->meta);
        t->meta = malloc((size_t)size + 1);
        if (!t->meta) return tar_fail(t, "out of memory%s", "");
        t->meta_fill = 0;
        t->meta_size = (size_t)size;
        t->state = type == 'L' ? TAR_LONG_NAME : TAR_PAX;
    } else if (type == '0' || type == '\0' || type == '7') {
        char name[1024];
        if (t->next_name[0]) {
            snprintf(name, sizeof name, "%s", t->next_name);
        } else {
            char base[101], prefix[156];
            field(base, sizeof base, h, 100);
            field(prefix, sizeof prefix, h + 345, 155);
            if (!memcmp(h + 257, "ustar", 5) && prefix[0]) snprintf(name, sizeof name, "%s/%s", prefix, base);
            else snprintf(name, sizeof name, "%s", base);
        }
        t->next_name[0] = '\0';
        if (!landing(t->into, name, t->path, sizeof t->path))
            return tar_fail(t, "the tar holds a path outside the game: %s", name);
        if (!files_make_parents(t->path) || !(t->out = fopen(t->path, "wb")))
            return tar_fail(t, "%s can't be written", t->path);
        setvbuf(t->out, NULL, _IOFBF, 1 << 16); // the blocks come 512 bytes at a time
        t->executable = (octal(h + 100, 8) & 0111) != 0;
        t->state = TAR_FILE;
    } else {
        t->next_name[0] = '\0'; // a directory, a link, a global pax header: nothing to write
        t->state = TAR_SKIP;
    }
    return true;
}

static bool tar_entry_done(Tar *t)
{
    switch (t->state) {
    case TAR_FILE: {
        bool ok = fclose(t->out) == 0;
        t->out = NULL;
        if (!ok) return tar_fail(t, "%s can't be written", t->path);
        if (t->executable) files_set_executable(t->path);
        break;
    }
    case TAR_LONG_NAME:
        t->meta[t->meta_fill] = '\0';
        snprintf(t->next_name, sizeof t->next_name, "%s", t->meta);
        break;
    case TAR_PAX:
        pax_path(t);
        break;
    default:
        break;
    }
    t->state = TAR_HEADER;
    return true;
}

static bool tar_block(Tar *t)
{
    if (t->ended) return true;
    if (t->state == TAR_HEADER) {
        if (!tar_header(t)) return false;
        // an empty entry has no blocks after its header: done at once
        return t->state == TAR_HEADER || t->remaining > 0 || tar_entry_done(t);
    }
    size_t n = t->remaining < 512 ? (size_t)t->remaining : 512;
    if (t->state == TAR_FILE) {
        if (fwrite(t->block, 1, n, t->out) != n) return tar_fail(t, "%s can't be written", t->path);
    } else if (t->state == TAR_LONG_NAME || t->state == TAR_PAX) {
        memcpy(t->meta + t->meta_fill, t->block, n);
        t->meta_fill += n;
    }
    t->remaining -= n;
    return t->remaining > 0 || tar_entry_done(t);
}

static int tar_take(const void *data, int size, void *user)
{
    Tar *t = user;
    const uint8_t *p = data;
    t->crc = (uint32_t)mz_crc32(t->crc, p, (size_t)size);
    t->done += (uint64_t)size;
    while (size > 0) {
        size_t take = 512 - t->fill < (size_t)size ? 512 - t->fill : (size_t)size;
        memcpy(t->block + t->fill, p, take);
        t->fill += take;
        p += take;
        size -= (int)take;
        if (t->fill == 512) {
            t->fill = 0;
            if (!tar_block(t)) return 0;
        }
    }
    if (t->progress) t->progress(t->user, t->done, t->total);
    return 1;
}

static bool extract_tar_gz(const char *archive, const char *into, ArchiveProgress progress, void *user,
                           char *error, size_t error_size)
{
    uint64_t size;
    uint8_t *data = (uint8_t *)files_read(archive, &size);
    if (!data) {
        snprintf(error, error_size, "the archive can't be read");
        return false;
    }
    // A gzip is one member or several, one after another (RFC 1952), each a header with
    // its optional fields, a deflate stream, and its own CRC-32 and size; gzip reads
    // them in turn as one stream, and so does this: the tar runs on across them. The
    // tar.gz xmake writes (xmake dist) is an empty member, then the archive's own.
    // The last member's size is the progress's total, near enough.
    Tar t = {.into = into, .state = TAR_HEADER, .progress = progress, .user = user, .error = error, .error_size = error_size};
    error[0] = '\0';
    if (size >= 8) {
        const uint8_t *last = data + size - 8;
        t.total = (uint32_t)last[4] | (uint32_t)last[5] << 8 | (uint32_t)last[6] << 16 | (uint32_t)last[7] << 24;
    }
    size_t at = 0;
    int members = 0;
    bool ok = true;
    while (ok && at + 18 <= size && data[at] == 0x1f && data[at + 1] == 0x8b) {
        uint8_t flags = data[at + 3];
        size_t p = at + 10;
        if (flags & 4) {
            if (p + 2 > size) break;
            p += 2 + (size_t)(data[p] | data[p + 1] << 8);
        }
        if (flags & 8) while (p < size && data[p++]);
        if (flags & 16) while (p < size && data[p++]);
        if (flags & 2) p += 2;
        if (data[at + 2] != 8 || p + 8 > size) break;

        t.crc = MZ_CRC32_INIT;
        uint64_t start = t.done;
        size_t in_size = (size_t)size - 8 - p;
        ok = tinfl_decompress_mem_to_callback(data + p, &in_size, tar_take, &t, 0) != 0; // in_size: what it took
        if (!ok) {
            if (!error[0]) snprintf(error, error_size, "the archive is damaged");
            break;
        }
        p += in_size;
        if (p + 8 > size) {
            ok = false;
            break;
        }
        const uint8_t *trailer = data + p;
        uint32_t crc = (uint32_t)trailer[0] | (uint32_t)trailer[1] << 8 | (uint32_t)trailer[2] << 16 | (uint32_t)trailer[3] << 24;
        uint32_t isize = (uint32_t)trailer[4] | (uint32_t)trailer[5] << 8 | (uint32_t)trailer[6] << 16 | (uint32_t)trailer[7] << 24;
        if (t.crc != crc || (uint32_t)(t.done - start) != isize) {
            snprintf(error, error_size, "the archive is damaged (its checksum is wrong)");
            ok = false;
            break;
        }
        at = p + 8;
        members++;
    }
    if (t.out) fclose(t.out);
    free(t.meta);
    free(data);
    if (!ok) {
        if (!error[0]) snprintf(error, error_size, "the archive is damaged");
        return false;
    }
    if (members == 0 || at != size) {
        snprintf(error, error_size, "the archive is damaged");
        return false;
    }
    if (t.state != TAR_HEADER || t.fill != 0) {
        snprintf(error, error_size, "the archive ends part way through");
        return false;
    }
    return true;
}

bool archive_extract(const char *archive, const char *into, ArchiveProgress progress, void *user, char *error,
                     size_t error_size)
{
    uint8_t magic[4] = {0};
    FILE *f = fopen(archive, "rb");
    if (!f) {
        snprintf(error, error_size, "%s can't be read", archive);
        return false;
    }
    size_t n = fread(magic, 1, sizeof magic, f);
    fclose(f);
    if (!files_make_directory(into)) {
        snprintf(error, error_size, "%s can't be made", into);
        return false;
    }
    if (n == 4 && magic[0] == 'P' && magic[1] == 'K' && magic[2] == 3 && magic[3] == 4)
        return extract_zip(archive, into, progress, user, error, error_size);
    if (n >= 3 && magic[0] == 0x1f && magic[1] == 0x8b)
        return extract_tar_gz(archive, into, progress, user, error, error_size);
    snprintf(error, error_size, "%s is neither a zip nor a tar.gz", archive);
    return false;
}
