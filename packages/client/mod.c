#include "mod.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "render/textures.h"

void mod_init(Mod *m, const char *root, const char *name)
{
    *m = (Mod){0};
    snprintf(m->fallback, sizeof m->fallback, "%s/%s", root, MOD_DEFAULT);
    if (name && name[0] && strcmp(name, MOD_DEFAULT) != 0) snprintf(m->dir, sizeof m->dir, "%s/%s", root, name);
}

static bool exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f) fclose(f);
    return f != NULL;
}

void mod_file(const Mod *m, char *path, size_t size, const char *fmt, ...)
{
    char rel[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(rel, sizeof rel, fmt, args);
    va_end(args);
    if (m->dir[0]) {
        snprintf(path, size, "%s/%s", m->dir, rel);
        if (exists(path)) return;
    }
    snprintf(path, size, "%s/%s", m->fallback, rel);
}

bool mod_image(const Mod *m, const char *dir, const char *name, char *path, int path_size)
{
    char where[512];
    if (m->dir[0]) {
        snprintf(where, sizeof where, "%s/%s", m->dir, dir);
        if (find_image(where, name, path, path_size)) return true;
    }
    snprintf(where, sizeof where, "%s/%s", m->fallback, dir);
    return find_image(where, name, path, path_size);
}
