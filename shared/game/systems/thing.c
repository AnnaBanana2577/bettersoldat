// The thing pool and what every thing shares: a small Verlet skeleton (two points for a
// gun, four for a flag, kit, parachute or stationary gun), its physics against the map,
// a holder, a timeout. Ported from TThing in Things.pas by way of soldat-odin.
//
// Not ported yet: the pool, its physics and every kind of thing (thing.odin, flag.odin,
// kit.odin, dropped_gun.odin, parachute.odin, stat_gun.odin). Until then the pool
// stays empty: nothing is placed, and a gun let go of is gone.

#include "game/systems/systems.h"

void things_spawn(const Context *ctx, World *w)
{
    (void)ctx;
    (void)w;
}

void things_update(const Context *ctx, World *w, Events *events)
{
    (void)ctx;
    (void)w;
    (void)events;
}

void dropped_gun_throw(const Context *ctx, World *w, uint8_t index, Soldier *s, WeaponId weapon, int32_t ammo, Events *events)
{
    (void)ctx;
    (void)w;
    (void)index;
    (void)s;
    (void)weapon;
    (void)ammo;
    (void)events;
}

void dropped_gun_from_death(const Context *ctx, World *w, uint8_t index, Soldier *s, Vec2 impact, Events *events)
{
    (void)ctx;
    (void)w;
    (void)index;
    (void)s;
    (void)impact;
    (void)events;
}
