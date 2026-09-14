#include "render/textures.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOGDI // its Polygon would collide with the map's
#include <windows.h>
#else
#include <dirent.h>
#endif

#define MAP_MIPMAP_BIAS -0.5f // the original's r_mipmapbias default: the texture a touch sharper

static void lowercase(char *dst, const char *src, size_t size)
{
    size_t i = 0;
    for (; src[i] && i + 1 < size; i++) dst[i] = (char)tolower((unsigned char)src[i]);
    dst[i] = '\0';
}

// Each file name in `dir`, to `fn`, until it returns false. False when the directory
// can't be read.
typedef bool (*FileVisitor)(const char *name, void *user);

static bool for_each_file(const char *dir, FileVisitor fn, void *user)
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

typedef struct ImageSearch {
    char as_png[256], lower[256]; // what is wanted, lowercased: the .png first, then as written
    char found[256];              // the .png's actual name, when seen
    char fallback[256];           // the name as written, when seen
} ImageSearch;

static bool image_visit(const char *name, void *user)
{
    ImageSearch *s = user;
    char actual[256];
    lowercase(actual, name, sizeof(actual));
    if (strcmp(actual, s->as_png) == 0) {
        snprintf(s->found, sizeof(s->found), "%s", name);
        return false;
    }
    if (strcmp(actual, s->lower) == 0) snprintf(s->fallback, sizeof(s->fallback), "%s", name);
    return true;
}

bool find_image(const char *dir, const char *name, char *path, int path_size)
{
    if (!name || !name[0]) return false;

    ImageSearch s = {0};
    lowercase(s.lower, name, sizeof(s.lower));
    snprintf(s.as_png, sizeof(s.as_png), "%s", s.lower);
    char *dot = strrchr(s.as_png, '.');
    if (dot && dot != s.as_png) *dot = '\0';
    strncat(s.as_png, ".png", sizeof(s.as_png) - strlen(s.as_png) - 1);

    if (!for_each_file(dir, image_visit, &s)) return false;
    const char *best = s.found[0] ? s.found : s.fallback[0] ? s.fallback : NULL;
    if (!best) return false;
    snprintf(path, (size_t)path_size, "%s/%s", dir, best);
    return true;
}

GfxTexture map_texture_load(const char *base, const char *name)
{
    char dir[512], path[512];
    snprintf(dir, sizeof(dir), "%s/textures", base);
    if (!find_image(dir, name, path, sizeof(path))) {
        fprintf(stderr, "map texture '%s' not found; drawing the polygons untextured\n", name);
        return (GfxTexture){0};
    }

    GfxTexture tex;
    if (!gfx_texture_load(&tex, path, NULL)) return tex;
    gfx_texture_wrap(tex, true);
    gfx_texture_mipmap(tex);
    gfx_texture_lod_bias(tex, MAP_MIPMAP_BIAS);
    return tex;
}

GfxTexture *scenery_load(const char *base, const Map *map)
{
    char dir[512];
    snprintf(dir, sizeof(dir), "%s/scenery-gfx", base);
    GfxTexture *out = calloc((size_t)(map->scenery_count ? map->scenery_count : 1), sizeof(GfxTexture));
    if (!out) return NULL;

    // Keyed on pure green rather than an alpha channel, as the original's ApplyColorKey.
    const Rgba green = {0, 255, 0, 255};
    int missing = 0;
    for (int i = 0; i < map->scenery_count; i++) {
        char path[512];
        if (!find_image(dir, map->scenery[i], path, sizeof(path))) {
            missing++; // map authors ship custom scenery that is not in the base assets
            continue;
        }
        gfx_texture_load(&out[i], path, &green);
    }
    if (missing > 0) fprintf(stderr, "%d of %d scenery images not found in %s\n", missing, map->scenery_count, dir);
    return out;
}

void scenery_unload(GfxTexture *scenery, int count)
{
    if (!scenery) return;
    for (int i = 0; i < count; i++) gfx_texture_delete(&scenery[i]);
    free(scenery);
}
