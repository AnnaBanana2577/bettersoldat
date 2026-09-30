// Guns on the ground: thrown from a hand, let go of by a death, a thrown knife that
// landed. A two-point thing (karabin.po at the gun's length) carrying the weapon and
// its ammo; it resists pickup for half a second, settles, and goes after a while if
// nobody takes it. Ported from OpenSoldat Sprites.pas (DropWeapon, Die) and Things.pas
// (the weapon cases of CheckSpriteCollision).
//
// Only the world with authority makes them: elsewhere the gun leaves the hand and is
// heard of from there.

#include "game/systems/systems.h"

#define PICKUP_RESIST (GUN_RESIST_TIME - 30) // no taking it for its first half second

// The guns that lie on the ground once let go of: not the hands, the flamer, the M2,
// the grenades, nor the bows outside Rambo.
static bool droppable(WeaponId id)
{
    return weapon_is_primary(id) || id == WEAPON_COLT || id == WEAPON_KNIFE || id == WEAPON_CHAINSAW || id == WEAPON_LAW;
}

// The gun from the hand (Skeleton.Pos[16]), flying as thing_create throws it; a death
// gives its muzzle the killing blow.
static void drop(const Context *ctx, World *w, uint8_t index, Soldier *s, WeaponId weapon, int32_t ammo, const Vec2 *impact, Events *events)
{
    if (!droppable(weapon)) return;
    event_emit(events, (Event){.type = EVENT_WEAPON_DROP, .weapon_drop = {.player = index, .weapon = weapon, .ammo = ammo, .thrown = impact == NULL}});
    if (!w->authority) return;

    Pose pose = soldier_pose(ctx->anims, s, s->pos);
    int k = thing_create(ctx, w, THING_WEAPON, pose.p[15], weapon, (uint8_t)(index + 1), -1);
    if (k < 0) return;
    w->things[k].ammo = ammo;
    if (impact) w->things[k].forces[1] = *impact;
}

void dropped_gun_throw(const Context *ctx, World *w, uint8_t index, Soldier *s, WeaponId weapon, int32_t ammo, Events *events)
{
    drop(ctx, w, index, s, weapon, ammo, NULL, events);
}

void dropped_gun_from_death(const Context *ctx, World *w, uint8_t index, Soldier *s, Vec2 impact, Events *events)
{
    drop(ctx, w, index, s, s->weapon.id, s->weapon.ammo, &impact, events);
}

void thrown_knife_land(const Context *ctx, World *w, const Bullet *b, Events *events)
{
    (void)events;
    if (w->authority) thing_create(ctx, w, THING_WEAPON, b->pos, WEAPON_KNIFE, (uint8_t)(b->owner + 1), -1);
}

bool dropped_gun_wanted(const Thing *t, const Soldier *s)
{
    if (s->weapon.id != WEAPON_NONE || s->body.id == ANIM_CHANGE) return false;
    if (t->weapon == WEAPON_BOW || t->weapon == WEAPON_BOW2) return t->timeout < FLAG_TIMEOUT - 100;
    return t->timeout < PICKUP_RESIST;
}

void dropped_gun_take(const Context *ctx, World *w, int index, uint8_t soldier, Events *events)
{
    Thing *t = &w->things[index];
    Soldier *s = &w->soldiers[soldier];
    if (!dropped_gun_wanted(t, s)) return;

    event_emit(events, (Event){
        .type = EVENT_WEAPON_PICKUP,
        .weapon_pickup = {.player = soldier, .thing = (uint8_t)index, .weapon = t->weapon, .ammo = t->ammo, .pos = t->pos[0]},
    });
    if (t->weapon == WEAPON_BOW || t->weapon == WEAPON_BOW2) {
        // the bow, and its flaming twin behind it; the thing goes once it is held
        s->weapon = weapon_state(ctx, WEAPON_BOW);
        s->secondary = weapon_state(ctx, WEAPON_BOW2);
        s->weapon.ammo = 1;
        return;
    }
    s->weapon = weapon_state(ctx, t->weapon);
    s->weapon.ammo = t->ammo;
    thing_kill(t);
}
