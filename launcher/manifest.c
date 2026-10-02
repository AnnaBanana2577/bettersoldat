#include "manifest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "files.h"
#include "sha256.h"

bool manifest_safe_path(const char *path)
{
    size_t n = strlen(path);
    if (n == 0 || n >= MANIFEST_PATH_SIZE || path[0] == '/' || path[n - 1] == '/') return false;
    for (const char *c = path; *c; c++)
        if ((unsigned char)*c < 0x20 || *c == 0x7f || *c == '\\' || *c == ':') return false;
    const char *part = path;
    for (;;) {
        const char *end = strchr(part, '/');
        size_t len = end ? (size_t)(end - part) : strlen(part);
        if (len == 0 || (len == 1 && part[0] == '.') || (len == 2 && part[0] == '.' && part[1] == '.')) return false;
        if (!end) return true;
        part = end + 1;
    }
}

bool manifest_top_level(const char *path) { return !strchr(path, '/'); }

// "<sha256> <bytes> <rest of the line>"
static bool parse_entry(const char *s, ManifestFile *f)
{
    char hex[65];
    if (strlen(s) < 66 || s[64] != ' ') return false;
    memcpy(hex, s, 64);
    hex[64] = '\0';
    if (!sha256_from_hex(hex, f->sha256)) return false;
    s += 65;
    char *end;
    if (*s < '0' || *s > '9') return false;
    f->size = strtoull(s, &end, 10);
    if (*end != ' ') return false;
    s = end + 1;
    if (!manifest_safe_path(s)) return false;
    snprintf(f->path, sizeof f->path, "%s", s);
    return true;
}

static bool add_file(Manifest *m, const ManifestFile *f)
{
    if (m->count == m->capacity) {
        int capacity = m->capacity ? m->capacity * 2 : 256;
        ManifestFile *files = realloc(m->files, (size_t)capacity * sizeof *files);
        if (!files) return false;
        m->files = files;
        m->capacity = capacity;
    }
    m->files[m->count++] = *f;
    return true;
}

bool manifest_parse(Manifest *m, const char *text, size_t size, char *error, size_t error_size)
{
    memset(m, 0, sizeof *m);
    const char *p = text, *stop = text + size;
    int number = 0;
    while (p < stop) {
        const char *eol = memchr(p, '\n', (size_t)(stop - p));
        if (!eol) eol = stop;
        char line[512];
        size_t n = (size_t)(eol - p);
        while (n > 0 && (p[n - 1] == '\r' || p[n - 1] == ' ' || p[n - 1] == '\t')) n--;
        number++;
        bool ok = n < sizeof line;
        if (ok) {
            memcpy(line, p, n);
            line[n] = '\0';
        }
        p = eol + 1;
        if (ok && (n == 0 || !strncmp(line, "//", 2))) continue;

        ManifestFile f;
        if (!ok)
            ;
        else if (!strncmp(line, "version ", 8))
            ok = n > 8 && n - 8 < sizeof m->version && snprintf(m->version, sizeof m->version, "%s", line + 8) > 0;
        else if (!strncmp(line, "file ", 5))
            ok = parse_entry(line + 5, &f) && add_file(m, &f);
        else if (!strncmp(line, "package update ", 15))
            ok = parse_entry(line + 15, &m->update) && manifest_top_level(m->update.path);
        else if (!strncmp(line, "package full ", 13))
            ok = parse_entry(line + 13, &m->full) && manifest_top_level(m->full.path);
        else
            ok = false;
        if (!ok) {
            snprintf(error, error_size, "line %d is not understood", number);
            manifest_free(m);
            return false;
        }
    }
    if (!m->version[0]) {
        snprintf(error, error_size, "it names no version");
        manifest_free(m);
        return false;
    }
    return true;
}

bool manifest_load(Manifest *m, const char *path, char *error, size_t error_size)
{
    uint64_t size;
    char *text = files_read(path, &size);
    if (!text) {
        memset(m, 0, sizeof *m);
        snprintf(error, error_size, "%s can't be read", path);
        return false;
    }
    bool ok = manifest_parse(m, text, (size_t)size, error, error_size);
    free(text);
    return ok;
}

void manifest_free(Manifest *m)
{
    free(m->files);
    memset(m, 0, sizeof *m);
}

bool manifest_write(const Manifest *m, const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    fprintf(f, "// What this install holds, which the launcher checks it against. Written by the\n"
               "// launcher; a file listed here with a different size or hash is replaced on the\n"
               "// next start.\n");
    fprintf(f, "version %s\n", m->version);
    for (int i = 0; i < m->count; i++) {
        char hex[65];
        sha256_to_hex(m->files[i].sha256, hex);
        fprintf(f, "file %s %llu %s\n", hex, (unsigned long long)m->files[i].size, m->files[i].path);
    }
    bool ok = !ferror(f);
    return fclose(f) == 0 && ok;
}

const ManifestFile *manifest_find(const Manifest *m, const char *path)
{
    for (int i = 0; i < m->count; i++)
        if (!strcmp(m->files[i].path, path)) return &m->files[i];
    return NULL;
}
