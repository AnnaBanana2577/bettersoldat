#include "utils/utils.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint8_t *file_read_all(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    uint8_t *data = NULL;
    long length = -1;
    if (fseek(f, 0, SEEK_END) == 0) length = ftell(f);
    if (length >= 0 && fseek(f, 0, SEEK_SET) == 0) data = malloc((size_t)length + 1);

    if (data) {
        size_t read = fread(data, 1, (size_t)length, f);
        if (read != (size_t)length) {
            free(data);
            data = NULL;
        } else {
            data[read] = '\0';
            if (size) *size = read;
        }
    }
    fclose(f);
    return data;
}

char *path_join(char *out, size_t out_size, const char *a, const char *b, const char *c)
{
    if (c) snprintf(out, out_size, "%s/%s/%s", a, b, c);
    else snprintf(out, out_size, "%s/%s", a, b);
    return out;
}

char *text_next_line(char **cursor)
{
    char *line = *cursor;
    if (!line || !*line) return NULL;

    char *end = strchr(line, '\n');
    if (end) {
        *end = '\0';
        *cursor = end + 1;
    } else {
        *cursor = line + strlen(line);
    }

    while (isspace((unsigned char)*line)) line++;
    size_t len = strlen(line);
    while (len > 0 && isspace((unsigned char)line[len - 1])) line[--len] = '\0';
    return line;
}

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#include <windows.h>
#else
#include <dirent.h>
#endif

bool for_each_file(const char *dir, FileVisitor fn, void *user)
{
#ifdef _WIN32
    char pattern[512];
    snprintf(pattern, sizeof(pattern), "%s\\*", dir);
    WIN32_FIND_DATAA data;
    HANDLE h = FindFirstFileA(pattern, &data);
    if (h == INVALID_HANDLE_VALUE) return false;
    do {
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (!fn(data.cFileName, user)) break;
    } while (FindNextFileA(h, &data));
    FindClose(h);
    return true;
#else
    DIR *d = opendir(dir);
    if (!d) return false;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_type == DT_DIR) continue;
        if (!fn(e->d_name, user)) break;
    }
    closedir(d);
    return true;
#endif
}

typedef struct FileList {
    const char *ext;
    char (*names)[64];
    int max, count;
} FileList;

static bool list_visit(const char *name, void *user)
{
    FileList *l = user;
    size_t n = strlen(name), e = strlen(l->ext);
    if (n <= e || l->count >= l->max) return true;
    for (size_t i = 0; i < e; i++)
        if (tolower((unsigned char)name[n - e + i]) != tolower((unsigned char)l->ext[i])) return true;
    snprintf(l->names[l->count], 64, "%.*s", (int)(n - e), name);
    l->count++;
    return true;
}

static int name_compare(const void *a, const void *b) { return strcmp(a, b); }

int list_files(const char *dir, const char *ext, char (*names)[64], int max)
{
    FileList l = {.ext = ext, .names = names, .max = max};
    for_each_file(dir, list_visit, &l);
    qsort(names, (size_t)l.count, 64, name_compare);
    return l.count;
}
