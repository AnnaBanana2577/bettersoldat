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

typedef struct BulletArt {
    Sprite weapons[WEAPON_COUNT]; // each weapon's round; a weapon without one uses the Colt's
    Sprite shared[BULLET_ART_SHARED_COUNT];
    Sprite flames[FLAME_FRAMES]; // sparks-gfx/flames/explode1..16: a flamer shot burning out
    bool loaded;
} BulletArt;

void bullet_art_load(BulletArt *b, const char *base);
void bullet_art_unload(BulletArt *b);

// Every live bullet, `alpha` of the way from its last tick to this one. `seconds` drives
// the M2's wobble, `grenade_color` is cl_grenade_color (gostek.h), and `trails`
// (r_trails) draws the streaks behind the rounds, as the original's Trails does. Under
// the camera's transform.
void bullets_draw(const BulletArt *b, const Bullet *bullets, float alpha, Rgba grenade_color, bool trails, double seconds);
