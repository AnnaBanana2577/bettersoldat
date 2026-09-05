#pragma once

// Particle/constraint objects (.po): the gostek, flag, kit, parachute, stationary gun
// and rifle skeletons the Verlet bodies are built from. Ported from Anims.pas by way of
// soldat-odin.

#include "utils/utils.h"

// The scales Anims.pas loads each object at.
#define FLAG_SCALE 4.0f
#define KIT_SCALE 2.15f
#define PARA_SCALE 5.0f
#define STAT_SCALE 4.0f
#define GOSTEK_SKELETON_SCALE 3.0f // the animations' scale, so the rest lengths match the pose

// karabin.po at every gun length the original uses (RifleSkeleton10..55).
#define GUN_SCALE_COUNT 11
extern const float GUN_SCALES[GUN_SCALE_COUNT];

typedef struct ParticleObject {
    Vec2 *points;
    int point_count;
    int (*constraints)[2]; // 0-based point indices
    int constraint_count;
} ParticleObject;

typedef struct Skeletons {
    ParticleObject flag, kit, para, stat;
    ParticleObject gostek; // the corpses' constraints and rest lengths
    ParticleObject rifles[GUN_SCALE_COUNT];
} Skeletons;

// Parses a .po file (text is modified) at a scale. False if out of memory.
bool particle_object_parse(ParticleObject *obj, char *text, float scale);

// <base_dir>/objects/<file> at a scale. Reports failures on stderr.
bool particle_object_load(ParticleObject *obj, const char *base_dir, const char *file, float scale);

void particle_object_destroy(ParticleObject *obj);

// Every skeleton the game uses. NULL on failure. Free with skeletons_destroy.
Skeletons *skeletons_load(const char *base_dir);
void skeletons_destroy(Skeletons *sk);
