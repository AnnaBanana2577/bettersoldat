#include "files.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#define NOGDI
#define WIN32_LEAN_AND_MEAN
#include <direct.h>
#include <windows.h>
#else
#include <dirent.h>
#include <limits.h>
#include <time.h>
#include <unistd.h>
#endif

bool files_enter_own_directory(void)
{
#ifdef _WIN32
    // wide, so an install under a user name outside the code page is still found
    wchar_t exe[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(NULL, exe, (DWORD)(sizeof exe / sizeof exe[0]));
    if (n == 0 || n >= sizeof exe / sizeof exe[0]) return false;
    wchar_t *slash = wcsrchr(exe, L'\\');
    if (!slash) return false;
    *slash = L'\0';
    return SetCurrentDirectoryW(exe) != 0;
#else
    char exe[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n <= 0) return false;
    exe[n] = '\0';
    char *slash = strrchr(exe, '/');
    if (!slash) return false;
    *slash = '\0';
    return chdir(exe[0] ? exe : "/") == 0;
#endif
}

bool files_exists(const char *path)
{
#ifdef _WIN32
    struct _stat64 st;
    return _stat64(path, &st) == 0;
#else
    struct stat st;
    return stat(path, &st) == 0;
#endif
}

bool files_size(const char *path, uint64_t *size)
{
#ifdef _WIN32
    struct _stat64 st;
    if (_stat64(path, &st) != 0 || !(st.st_mode & _S_IFREG)) return false;
#else
    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) return false;
#endif
    *size = (uint64_t)st.st_size;
    return true;
}

bool files_make_directory(const char *path)
{
#ifdef _WIN32
    if (_mkdir(path) == 0) return true;
#else
    if (mkdir(path, 0755) == 0) return true;
#endif
    return files_exists(path);
}

bool files_make_parents(const char *path)
{
    char dir[1024];
    snprintf(dir, sizeof dir, "%s", path);
    // most often the directory is there already, made for the file before
    char *last = strrchr(dir, '/');
    if (!last) return true;
    *last = '\0';
    bool there = files_exists(dir);
    *last = '/';
    if (there) return true;
    for (char *p = dir + 1; *p; p++) {
        if (*p != '/') continue;
        *p = '\0';
        bool made = files_make_directory(dir);
        *p = '/';
        if (!made) return false;
    }
    return true;
}

bool files_remove_tree(const char *path)
{
#ifdef _WIN32
    char pattern[1024];
    snprintf(pattern, sizeof pattern, "%s/*", path);
    WIN32_FIND_DATAA found;
    HANDLE h = FindFirstFileA(pattern, &found);
    if (h == INVALID_HANDLE_VALUE) return remove(path) == 0 || !files_exists(path);
    do {
        if (!strcmp(found.cFileName, ".") || !strcmp(found.cFileName, "..")) continue;
        char child[1024];
        snprintf(child, sizeof child, "%s/%s", path, found.cFileName);
        if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) files_remove_tree(child);
        else remove(child);
    } while (FindNextFileA(h, &found));
    FindClose(h);
    _rmdir(path);
#else
    DIR *d = opendir(path);
    if (!d) return remove(path) == 0 || !files_exists(path);
    struct dirent *e;
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char child[1024];
        snprintf(child, sizeof child, "%s/%s", path, e->d_name);
        struct stat st;
        if (lstat(child, &st) == 0 && S_ISDIR(st.st_mode)) files_remove_tree(child);
        else remove(child);
    }
    closedir(d);
    rmdir(path);
#endif
    return !files_exists(path);
}

void files_pause(int milliseconds)
{
#ifdef _WIN32
    Sleep((DWORD)milliseconds);
#else
    struct timespec t = {milliseconds / 1000, (long)(milliseconds % 1000) * 1000000L};
    nanosleep(&t, NULL);
#endif
}

bool files_replace(const char *from, const char *to)
{
    if (!files_make_parents(to)) return false;
#ifdef _WIN32
    // A few tries: a scanner may be reading what was just extracted.
    for (int attempt = 0; attempt < 5; attempt++) {
        if (MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) return true;
        // In use: a running executable can't be overwritten, but it can be moved aside.
        char old[1024];
        snprintf(old, sizeof old, "%s.old", to);
        DeleteFileA(old);
        if (MoveFileExA(to, old, MOVEFILE_REPLACE_EXISTING) &&
            MoveFileExA(from, to, MOVEFILE_COPY_ALLOWED))
            return true;
        files_pause(200);
    }
    return false;
#else
    return rename(from, to) == 0;
#endif
}

void files_remove_old(const char *path)
{
    char old[1024];
    snprintf(old, sizeof old, "%s.old", path);
    remove(old);
}

void files_set_executable(const char *path)
{
#ifdef _WIN32
    (void)path;
#else
    chmod(path, 0755);
#endif
}

char *files_read(const char *path, uint64_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    char *data = NULL;
    uint64_t n = 0;
    if (fseek(f, 0, SEEK_END) == 0) {
        long end = ftell(f);
        if (end >= 0 && fseek(f, 0, SEEK_SET) == 0) {
            n = (uint64_t)end;
            data = malloc((size_t)n + 1);
            if (data && fread(data, 1, (size_t)n, f) != n) {
                free(data);
                data = NULL;
            }
        }
    }
    fclose(f);
    if (!data) return NULL;
    data[n] = '\0';
    if (size) *size = n;
    return data;
}

bool files_write(const char *path, const void *data, size_t size)
{
    if (!files_make_parents(path)) return false;
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    bool ok = fwrite(data, 1, size, f) == size;
    return fclose(f) == 0 && ok;
}

bool files_sha256(const char *path, uint8_t digest[32], FilesProgress progress, void *user)
{
    enum { CHUNK = 1 << 16 };
    FILE *f = fopen(path, "rb");
    uint8_t *buffer = f ? malloc(CHUNK) : NULL;
    if (!buffer) {
        if (f) fclose(f);
        return false;
    }
    Sha256 s;
    sha256_init(&s);
    size_t n;
    while ((n = fread(buffer, 1, CHUNK, f)) > 0) {
        sha256_feed(&s, buffer, n);
        if (progress) progress(user, n);
    }
    bool ok = !ferror(f);
    fclose(f);
    free(buffer);
    if (ok) sha256_finish(&s, digest);
    return ok;
}
