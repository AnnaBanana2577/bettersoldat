#pragma once

// How big each image is in game units: <base>/mod.ini's [SCALE] section, the original's
// ScaleData. An image drawn at its pixel size divided by its scale; a scale by its path
// (interface-gfx/cursor.png=10), else by the nearest folder above it, else DefaultScale
// (4.5).

#include <stdbool.h>

#define SCALE_DATA_MAX 64

typedef struct ScaleEntry {
    char path[128]; // lowercase, forward slashes, as the original keys them
    float scale;
} ScaleEntry;

typedef struct ScaleData {
    float default_scale;
    ScaleEntry entries[SCALE_DATA_MAX];
    int count;
} ScaleData;

// Reads mod.ini; without one, everything is at DefaultScale 4.5.
void scale_data_load(ScaleData *sd, const char *base);

// The scale for an image at `path` relative to the base, such as "interface-gfx/nade.png".
float scale_data_get(const ScaleData *sd, const char *path);
