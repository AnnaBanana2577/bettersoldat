#include "desktop.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "files.h"

#ifndef _WIN32
#include <limits.h>
#include <unistd.h>
#endif

// Appends `text` to the entry, false once it no longer fits.
typedef struct Text {
    char *out;
    size_t size, used;
} Text;

static bool put(Text *t, const char *text)
{
    size_t n = strlen(text);
    if (t->used + n >= t->size) return false;
    memcpy(t->out + t->used, text, n + 1);
    t->used += n;
    return true;
}

// A path as the Desktop Entry Specification has it. In Exec it is quoted, with ", `, $
// and \ escaped by a backslash and % doubled, and then, as for any string value, each
// backslash escaped again; in Icon only that second escaping applies.
static bool put_path(Text *t, const char *path, bool exec)
{
    for (const char *c = path; *c; c++) {
        char one[2] = {*c, '\0'};
        if ((unsigned char)*c < 0x20 || *c == 0x7f) return false;
        bool ok;
        if (exec && (*c == '"' || *c == '`' || *c == '$')) ok = put(t, "\\\\") && put(t, one);
        else if (*c == '\\') ok = put(t, exec ? "\\\\\\\\" : "\\\\");
        else if (exec && *c == '%') ok = put(t, "%%");
        else ok = put(t, one);
        if (!ok) return false;
    }
    return true;
}

bool desktop_entry_text(const char *launcher, char *out, size_t size)
{
    const char *slash = strrchr(launcher, '/');
    if (!slash || size == 0) return false;
    char *dir = malloc((size_t)(slash - launcher) + 1);
    if (!dir) return false;
    memcpy(dir, launcher, (size_t)(slash - launcher));
    dir[slash - launcher] = '\0';

    Text t = {out, size, 0};
    out[0] = '\0';
    bool ok = put(&t, "[Desktop Entry]\nType=Application\nName=Soldat Reloaded\nExec=\"") && put_path(&t, launcher, true) &&
              put(&t, "\"\nIcon=") && put_path(&t, dir, false) && put(&t, "/assets/icon.png\n") &&
              put(&t, "Terminal=false\nCategories=Game;ActionGame;\nStartupWMClass=" DESKTOP_APP_ID "\n");
    free(dir);
    return ok;
}

bool desktop_entry_install(void)
{
#ifdef _WIN32
    return true;
#else
    // an install, not a build directory: the icon the entry names is there
    if (!files_exists("assets/icon.png")) return true;
    char launcher[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", launcher, sizeof launcher - 1);
    if (n <= 0) return false;
    launcher[n] = '\0';

    // the path twice, escaped: up to four times its length in Exec and twice in Icon
    size_t size = 6 * (size_t)n + 256;
    char *text = malloc(size);
    if (!text) return false;
    char path[PATH_MAX];
    const char *data = getenv("XDG_DATA_HOME"), *home = getenv("HOME");
    int written;
    if (data && data[0] == '/') written = snprintf(path, sizeof path, "%s/applications/" DESKTOP_APP_ID ".desktop", data);
    else if (home && home[0] == '/')
        written = snprintf(path, sizeof path, "%s/.local/share/applications/" DESKTOP_APP_ID ".desktop", home);
    else written = -1;
    bool ok = written > 0 && (size_t)written < sizeof path && desktop_entry_text(launcher, text, size);
    if (ok) {
        uint64_t old_size = 0;
        char *old = files_read(path, &old_size);
        bool same = old && old_size == strlen(text) && !memcmp(old, text, old_size);
        free(old);
        if (!same) ok = files_make_parents(path) && files_write(path, text, strlen(text));
    }
    free(text);
    return ok;
#endif
}
