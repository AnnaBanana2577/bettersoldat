#pragma once

// The projectiles as drawn, from TBullet.Render in Bullets.pas by way of soldat-odin's
// r_bullet_art.odin. Most styles are the bullet's own image at pos + vel, stretched
// along its length by the speed and turned to face the way it goes, with a fainter
// stretched streak behind it as a trail. Grenades and rockets spin on their timeout.

#include "game/game.h"
#include "render/sprite.h"

#define FLAME_FRAMES 16

typedef enum BulletShared {
    BULLET_ART_STREAK,
    BULLET_ART_MISSILE,
    BULLET_ART_FRAG_GRENADE,
    BULLET_ART_CLUSTER_GRENADE,
    BULLET_ART_CLUSTER,
    BULLET_ART_ARROW,
    BULLET_ART_KNIFE,
    BULLET_ART_KNIFE_LEFT,
    BULLET_ART_SMUDGE,
    BULLET_ART_SHARED_COUNT
} BulletShared;

// The flight of a bullet heard of from another machine that was run forward on being
// made (EVENT_BULLET_TRACE): nobody here saw it fly from the muzzle to where it turned
// up, or to where it ended, so its streak is drawn along the way and fades, over about
// as many ticks as the flight took.
#define TRACERS 64
#define TRACER_LIFE_MIN 4.0f // ticks a tracer shows at least
#define TRACER_LIFE_MAX 10.0f
#define TRACER_SHORTEST 12.0f // a flight shorter than this, in world units, is not worth a streak

typedef struct Tracer {
    Vec2 from, to;
    WeaponId weapon;
    float age, life; // ticks
} Tracer;

typedef struct BulletArt {
    Sprite weapons[WEAPON_COUNT]; // each weapon's round; a weapon without one uses the Colt's
    Sprite shared[BULLET_ART_SHARED_COUNT];
    Sprite flames[FLAME_FRAMES]; // sparks-gfx/flames/explode1..16: a flamer shot burning out
    bool loaded;
    Tracer tracers[TRACERS];
    int tracer_count;
} BulletArt;

void bullet_art_load(BulletArt *b, const char *base);
void bullet_art_unload(BulletArt *b);

// Once per tick, after the game's: the tick's tracers begin and the old ones age.
void bullet_art_tick(BulletArt *b, const Events *events);

// Every live bullet, `alpha` of the way from its last tick to this one. `seconds` drives
// the M2's wobble. Under the camera's transform.
void bullets_draw(const BulletArt *b, const Bullet *bullets, float alpha, double seconds);
