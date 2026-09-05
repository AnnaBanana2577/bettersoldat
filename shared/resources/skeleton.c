#include "resources/skeleton.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const float GUN_SCALES[GUN_SCALE_COUNT] = {1.0f, 1.1f, 1.8f, 2.2f, 2.8f, 3.6f, 3.7f, 3.9f, 4.3f, 5.0f, 5.5f};

static bool grow(void **items, int *capacity, int count, size_t item_size)
{
    if (count < *capacity) return true;
    int next = *capacity ? *capacity * 2 : 16;
    void *grown = realloc(*items, (size_t)next * item_size);
    if (!grown) return false;
    *items = grown;
    *capacity = next;
    return true;
}

bool particle_object_parse(ParticleObject *obj, char *text, float scale)
{
    memset(obj, 0, sizeof(*obj));
    int point_capacity = 0, constraint_capacity = 0;
    char *cursor = text;

    for (;;) {
        char *name = text_next_line(&cursor);
        if (!name || strcmp(name, "CONSTRAINTS") == 0) break;
        char *xs = text_next_line(&cursor);
        text_next_line(&cursor); // y: depth, unused in 2D
        char *zs = text_next_line(&cursor);

        // Through double and then narrowed, as the Odin port's strconv does.
        float x = xs ? (float)strtod(xs, NULL) : 0.0f;
        float z = zs ? (float)strtod(zs, NULL) : 0.0f;

        if (!grow((void **)&obj->points, &point_capacity, obj->point_count, sizeof(Vec2))) goto oom;
        obj->points[obj->point_count++] = (Vec2){-x * scale / 1.2f, -z * scale};
    }

    for (;;) {
        char *a = text_next_line(&cursor);
        if (!a || strcmp(a, "ENDFILE") == 0) break;
        char *b = text_next_line(&cursor);
        if (!b || strlen(a) < 2 || strlen(b) < 2) break;

        if (!grow((void **)&obj->constraints, &constraint_capacity, obj->constraint_count, sizeof(int[2]))) goto oom;
        // Written "P1" / "P2": the point numbers are 1-based.
        obj->constraints[obj->constraint_count][0] = (int)strtol(a + 1, NULL, 10) - 1;
        obj->constraints[obj->constraint_count][1] = (int)strtol(b + 1, NULL, 10) - 1;
        obj->constraint_count++;
    }
    return true;

oom:
    particle_object_destroy(obj);
    return false;
}

bool particle_object_load(ParticleObject *obj, const char *base_dir, const char *file, float scale)
{
    char path[512];
    path_join(path, sizeof(path), base_dir, "objects", file);

    char *text = (char *)file_read_all(path, NULL);
    if (!text) {
        fprintf(stderr, "failed to read %s\n", path);
        memset(obj, 0, sizeof(*obj));
        return false;
    }
    bool ok = particle_object_parse(obj, text, scale);
    free(text);
    return ok;
}

void particle_object_destroy(ParticleObject *obj)
{
    free(obj->points);
    free(obj->constraints);
    memset(obj, 0, sizeof(*obj));
}

Skeletons *skeletons_load(const char *base_dir)
{
    Skeletons *sk = calloc(1, sizeof(Skeletons));
    if (!sk) return NULL;

    bool ok = particle_object_load(&sk->flag, base_dir, "flag.po", FLAG_SCALE) &&
              particle_object_load(&sk->kit, base_dir, "kit.po", KIT_SCALE) &&
              particle_object_load(&sk->para, base_dir, "para.po", PARA_SCALE) &&
              particle_object_load(&sk->stat, base_dir, "stat.po", STAT_SCALE) &&
              particle_object_load(&sk->gostek, base_dir, "gostek.po", GOSTEK_SKELETON_SCALE);
    for (int i = 0; ok && i < GUN_SCALE_COUNT; i++) {
        ok = particle_object_load(&sk->rifles[i], base_dir, "karabin.po", GUN_SCALES[i]);
    }

    if (!ok) {
        skeletons_destroy(sk);
        return NULL;
    }
    return sk;
}

void skeletons_destroy(Skeletons *sk)
{
    if (!sk) return;
    particle_object_destroy(&sk->flag);
    particle_object_destroy(&sk->kit);
    particle_object_destroy(&sk->para);
    particle_object_destroy(&sk->stat);
    particle_object_destroy(&sk->gostek);
    for (int i = 0; i < GUN_SCALE_COUNT; i++) particle_object_destroy(&sk->rifles[i]);
    free(sk);
}
