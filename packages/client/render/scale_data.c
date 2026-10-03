#include "render/scale_data.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "utils/utils.h"

#define DEFAULT_SCALE 4.5f

static void normalize(char *dst, const char *src, size_t size)
{
    size_t i = 0;
    for (; src[i] && i + 1 < size; i++) dst[i] = src[i] == '\\' ? '/' : (char)tolower((unsigned char)src[i]);
    dst[i] = '\0';
}

void scale_data_load(ScaleData *sd, const Mod *mod)
{
    *sd = (ScaleData){.default_scale = DEFAULT_SCALE};

    char path[512];
    mod_file(mod, path, sizeof(path), "mod.ini");
    size_t size;
    char *text = (char *)file_read_all(path, &size);
    if (!text) return;

    bool in_scale = false;
    char *cursor = text, *line;
    while ((line = text_next_line(&cursor))) {
        if (line[0] == '[') {
            in_scale = strncmp(line, "[SCALE]", 7) == 0;
            continue;
        }
        if (!in_scale || line[0] == '\0' || line[0] == ';') continue;
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        float value = strtof(eq + 1, NULL);
        if (value <= 0.0f) continue;
        if (strcmp(line, "DefaultScale") == 0) {
            sd->default_scale = value;
        } else if (sd->count < SCALE_DATA_MAX) {
            ScaleEntry *e = &sd->entries[sd->count++];
            normalize(e->path, line, sizeof(e->path));
            e->scale = value;
        }
    }
    free(text);
}

static const ScaleEntry *find(const ScaleData *sd, const char *key)
{
    for (int i = 0; i < sd->count; i++) {
        if (strcmp(sd->entries[i].path, key) == 0) return &sd->entries[i];
    }
    return NULL;
}

float scale_data_get(const ScaleData *sd, const char *path)
{
    char key[128];
    normalize(key, path, sizeof(key));
    const ScaleEntry *e = find(sd, key);
    if (!e) {
        char *slash = strrchr(key, '/'); // then the folder
        if (slash) {
            *slash = '\0';
            e = find(sd, key);
        }
    }
    return e ? e->scale : sd->default_scale;
}
