#pragma once

// The map's own images, its texture and its scenery, found the way the original finds
// them. Ported from soldat-odin's client/render/textures.odin. The context must be up.

#include "gfx/gfx.h"
#include "resources/map.h"

// <base>/textures/<name>, tiling (Soldat's polygon UVs run past 0..1) and mipmapped.
// A texture with handle 0 when it can't be found: the polygons then draw untextured.
GfxTexture map_texture_load(const char *base, const char *name);

// One texture per scenery name, from <base>/scenery-gfx, handle 0 where one failed to
// load. Pure green is the transparent colour. Free with scenery_unload.
GfxTexture *scenery_load(const char *base, const Map *map);
void scenery_unload(GfxTexture *scenery, int count);

// Resolves an image name the way the original's FindImagePath does: case-insensitively,
// preferring .png whatever extension the map asked for, then the name as written.
bool find_image(const char *dir, const char *name, char *path, int path_size);

// The names of the files in `dir` ending in `ext` (case-insensitively), without it, at most
// `max` of them, sorted; how many there were.
int list_files(const char *dir, const char *ext, char (*names)[64], int max);
