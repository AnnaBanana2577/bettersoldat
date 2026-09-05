#include "render/textures.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void lowercase(char *dst, const char *src, size_t size)
{
    size_t i = 0;
    for (; src[i] && i + 1 < size; i++) dst[i] = (char)tolower((unsigned char)src[i]);
    dst[i] = '\0';
}

bool find_image(const char *dir, const char *name, char *path, int path_size)
{
    if (!name || !name[0] || !DirectoryExists(dir)) return false;

    char lower[256], as_png[256];
    lowercase(lower, name, sizeof(lower));
    snprintf(as_png, sizeof(as_png), "%s", lower);
    char *dot = strrchr(as_png, '.');
    if (dot && dot != as_png) *dot = '\0';
    strncat(as_png, ".png", sizeof(as_png) - strlen(as_png) - 1);

    FilePathList files = LoadDirectoryFiles(dir);
    const char *best = NULL;
    bool found = false;
    for (unsigned int i = 0; i < files.count && !found; i++) {
        if (!IsPathFile(files.paths[i])) continue;
        char actual[256];
        lowercase(actual, GetFileName(files.paths[i]), sizeof(actual));
        if (strcmp(actual, as_png) == 0) {
            snprintf(path, (size_t)path_size, "%s", files.paths[i]);
            found = true;
        } else if (strcmp(actual, lower) == 0) {
            best = files.paths[i];
        }
    }
    if (!found && best) {
        snprintf(path, (size_t)path_size, "%s", best);
        found = true;
    }
    UnloadDirectoryFiles(files);
    return found;
}

Texture2D map_texture_load(const char *base, const char *name)
{
    char dir[512], path[512];
    snprintf(dir, sizeof(dir), "%s/textures", base);
    if (!find_image(dir, name, path, sizeof(path))) {
        fprintf(stderr, "map texture '%s' not found; drawing the polygons untextured\n", name);
        return (Texture2D){0};
    }

    Texture2D tex = LoadTexture(path);
    if (tex.id == 0) return tex;
    SetTextureWrap(tex, TEXTURE_WRAP_REPEAT);
    GenTextureMipmaps(&tex);
    SetTextureFilter(tex, TEXTURE_FILTER_TRILINEAR);
    return tex;
}

Texture2D *scenery_load(const char *base, const Map *map)
{
    char dir[512];
    snprintf(dir, sizeof(dir), "%s/scenery-gfx", base);
    Texture2D *out = calloc((size_t)(map->scenery_count ? map->scenery_count : 1), sizeof(Texture2D));
    if (!out) return NULL;

    int missing = 0;
    for (int i = 0; i < map->scenery_count; i++) {
        char path[512];
        if (!find_image(dir, map->scenery[i], path, sizeof(path))) {
            missing++; // map authors ship custom scenery that is not in the base assets
            continue;
        }
        Image img = LoadImage(path);
        if (!img.data) continue;

        // Keyed on pure green rather than an alpha channel, as the original's ApplyColorKey.
        ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
        unsigned char *px = img.data;
        for (int p = 0; p < img.width * img.height; p++, px += 4) {
            if (px[0] == 0 && px[1] == 255 && px[2] == 0 && px[3] == 255) px[3] = px[1] = 0;
        }
        out[i] = LoadTextureFromImage(img);
        UnloadImage(img);
    }
    if (missing > 0) fprintf(stderr, "%d of %d scenery images not found in %s\n", missing, map->scenery_count, dir);
    return out;
}

void scenery_unload(Texture2D *scenery, int count)
{
    if (!scenery) return;
    for (int i = 0; i < count; i++) {
        if (scenery[i].id != 0) UnloadTexture(scenery[i]);
    }
    free(scenery);
}
