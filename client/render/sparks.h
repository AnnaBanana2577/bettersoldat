#pragma once

// The particle effects: chips off walls, blood, smoke, explosions, shell casings, the
// jets' flames. From Sparks.pas and the bursts in Bullets.pas, SpriteEffects.pas and
// Sprites.pas, by way of soldat-odin's r_sparks.odin; only the styles our events
// produce. Purely cosmetic, so they use their own rng, not the sim's, and are fed
// from the tick's events rather than reached into by the simulation.

#include "game/game.h"
#include "render/sprite.h"

#define MAX_SPARKS 558
#define EXPLOSION_FRAMES 16
#define SMOKE_FRAMES 10

typedef enum SparkStyle {
    SPARK_NONE, // a free slot
    SPARK_SMOKE,
    SPARK_CHIP,
    SPARK_LIL_BLOOD,
    SPARK_BLOOD,
    SPARK_EXPLODE_M79,
    SPARK_EXPLODE_FRAG,
    SPARK_SPAWN_SPARK,
    SPARK_CHIP_FIRE,
    SPARK_EXPLODE_CLUSTER,
    SPARK_SPLIT_SMOKE,
    SPARK_EXPLODE_SMOKE,
    SPARK_MINI_SMOKE,
    SPARK_BIG_SMOKE,
    SPARK_LIL_SMOKE,
    SPARK_SHELL,    // a spent casing, the weapon's own (`weapon`), tumbling and bouncing
    SPARK_JET_FIRE, // a flame from a jet, in the player's jet colour
    SPARK_STYLE_COUNT
} SparkStyle;

typedef struct Spark {
    SparkStyle style;
    float life;
    Vec2 pos, vel;
    Rgba color;      // the spawn spark carries the team colour, the jet fire the jet's
    WeaponId weapon; // a shell's
} Spark;

typedef enum SparkArt {
    SPARK_ART_SMOKE,
    SPARK_ART_LIL_SMOKE,
    SPARK_ART_MINI_SMOKE,
    SPARK_ART_BIG_SMOKE,
    SPARK_ART_CHIP,
    SPARK_ART_LIL_BLOOD,
    SPARK_ART_BLOOD,
    SPARK_ART_SPAWN_SPARK,
    SPARK_ART_JET_FIRE,
    SPARK_ART_COUNT
} SparkArt;

typedef struct Sparks {
    Spark pool[MAX_SPARKS];
    Sprite art[SPARK_ART_COUNT];
    Sprite explode[EXPLOSION_FRAMES]; // explosion/explode1..16
    Sprite smoke[SMOKE_FRAMES];       // explosion/smoke1..10
    Sprite shells[WEAPON_COUNT];      // weapons-gfx/<weapon>-shell.png; the plain shell for the rest
    Sprite shell;
    uint64_t rng;
    bool loaded;
} Sparks;

void sparks_load(Sparks *s, const char *base);
void sparks_unload(Sparks *s);
void sparks_clear(Sparks *s); // a new map: the old one's sparks go with it

// Once per tick, after the game's: this tick's bursts from `events`, the jets' flames
// and the corpses' bleeding from the world, then every spark on by one step.
void sparks_tick(Sparks *s, const Context *ctx, const World *w, const Events *events);

// Under the camera's transform, after everything they land on.
void sparks_draw(const Sparks *s);
