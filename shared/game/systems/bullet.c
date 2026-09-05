// The bullet pool: spawning, the tick (collisions, then movement, then the timeout and
// the damage falling off with distance), ending. Ported from OpenSoldat Bullets.pas by
// way of soldat-odin.
//
// Not ported yet: the flight and everything a bullet meets (bullet.odin,
// bullet_collision.odin, explosion.odin). Until then no bullet is made, so the pool
// stays empty and there is nothing to update.

#include "game/systems/systems.h"

int bullet_spawn(const Context *ctx, World *w, Vec2 pos, Vec2 vel, WeaponId weapon, uint8_t owner, float damage, Events *events)
{
    (void)ctx;
    (void)w;
    (void)pos;
    (void)vel;
    (void)weapon;
    (void)owner;
    (void)damage;
    (void)events;
    return -1;
}

void bullets_update(const Context *ctx, World *w, Events *events)
{
    (void)ctx;
    (void)w;
    (void)events;
}
