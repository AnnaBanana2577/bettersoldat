#pragma once

// The particle effects: chips off walls, blood, smoke, explosions, shell casings and
// clips, the jets' flames, the flames off a burning body. From Sparks.pas and the
// bursts in Bullets.pas, SpriteEffects.pas and Sprites.pas, by way of soldat-odin's
// r_sparks.odin; only the styles our events produce. Purely cosmetic, so they use their
// own rng, not the sim's, and are fed from the tick's events rather than reached into
// by the simulation.
//
// Some sparks make a sound: a casing or a clip landing, the crackle of a burning body.
// Those are rolled here, with the spark, and left in `sounds` for the audio to play
// after the tick (the original plays them from TSpark.CheckMapCollision and the
// sprite's fire loop, in the same breath as it makes the spark).

#include "game/game.h"
#include "render/sprite.h"

#define MAX_SPARKS 558
#define EXPLOSION_FRAMES 16
#define SMOKE_FRAMES 10
#define MAX_SPARK_SOUNDS 32

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
    SPARK_SHELL,       // a spent casing, the weapon's own (`weapon`), tumbling and bouncing
    SPARK_CLIP,        // an empty clip out of a reload, the weapon's own, falling and landing
    SPARK_JET_FIRE,    // a flame from a jet, in the player's jet colour
    SPARK_FLAME,       // a tongue of fire off a burning body (the original's style 36)
    SPARK_BLACK_SMOKE, // the smoke off one (style 37)
    SPARK_SPIT,        // the antics': the tobacco spat out (32), the spent match (33),
    SPARK_MATCH,       // the cigar's stub (34), a drop of piss (57)
    SPARK_CIGAR,
    SPARK_PISS,
    SPARK_STYLE_COUNT
} SparkStyle;

typedef struct Spark {
    SparkStyle style;
    float life;
    Vec2 pos, vel;
    Rgba color;            // the spawn spark carries the team colour, the jet fire the jet's
    WeaponId weapon;       // a shell's or a clip's
    uint8_t collide_count; // landings so far: a casing sounds on some and is gone after five
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
    SPARK_ART_FLAME,
    SPARK_ART_BLACK_SMOKE,
    SPARK_ART_STUFF,
    SPARK_ART_CIGAR,
    SPARK_ART_COUNT
} SparkArt;

// What a spark sounded like this tick, for the audio: which noise, and where.
typedef enum SparkNoise {
    SPARK_NOISE_SHELL,       // a casing landing (shell.wav, shell2.wav)
    SPARK_NOISE_GAUGE_SHELL, // a shotgun's (gaugeshell.wav)
    SPARK_NOISE_CLIP,        // a clip landing (clipfall.wav)
    SPARK_NOISE_ONFIRE,      // a burning body (onfire.wav)
    SPARK_NOISE_FIRECRACK,   // and its crackle (firecrack.wav)
} SparkNoise;

typedef struct SparkSound {
    SparkNoise noise;
    Vec2 pos;
} SparkSound;

typedef struct Sparks {
    Spark pool[MAX_SPARKS];
    Sprite art[SPARK_ART_COUNT];
    Sprite explode[EXPLOSION_FRAMES]; // explosion/explode1..16
    Sprite smoke[SMOKE_FRAMES];       // explosion/smoke1..10
    Sprite shells[WEAPON_COUNT];      // weapons-gfx/<weapon>-shell.png; the plain shell for the rest
    Sprite shell;
    Sprite clips[WEAPON_COUNT]; // weapons-gfx/<weapon>-clip.png, for the weapons that drop one
    int32_t last_reload[MAX_PLAYERS]; // each soldier's reload count last tick: the clip drops as it passes the clip-out time
    int32_t last_pump[MAX_PLAYERS];   // each soldier's frame of the shotgun's pump last tick, 0 when not pumping: the shell flies as it passes 24
    SparkSound sounds[MAX_SPARK_SOUNDS]; // this tick's, in order; a full list drops the rest
    int sound_count;
    uint64_t rng;
    bool loaded;
} Sparks;

void sparks_load(Sparks *s, const char *base);
void sparks_unload(Sparks *s);
void sparks_clear(Sparks *s); // a new map: the old one's sparks go with it

// Once per tick, after the game's: this tick's bursts from `events`, the jets' flames,
// the clips out of the reloads, the corpses' bleeding and burning from the world, then
// every spark on by one step. Leaves this tick's `sounds`.
void sparks_tick(Sparks *s, const Context *ctx, const World *w, const Events *events);

// Under the camera's transform, after everything they land on.
void sparks_draw(const Sparks *s);
