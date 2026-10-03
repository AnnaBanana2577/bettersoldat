// Guns on the ground: thrown from a hand, let go of by a death, a thrown knife that
// landed. A two-point thing (karabin.po at the gun's length) carrying the weapon and
// its ammo; it resists pickup for half a second, settles, and goes after a while if
// nobody takes it. Ported from OpenSoldat Sprites.pas (DropWeapon, Die) and Things.pas
// (the weapon cases of CheckSpriteCollision).
//
// The gun leaves the hand as an event (EVENT_WEAPON_DROP, EVENT_KNIFE_LAND) and the
// things pass lays it down from that, where the world has authority; elsewhere it is
// heard of.

#include "game/systems/systems.h"

#define PICKUP_RESIST (GUN_RESIST_TIME - 30) // no taking it for its first half second

void dropped_gun_drop(const Context *ctx, World *w, const EventWeaponDrop *e)
{
    if (!w->authority) return;
    int k = thing_create(ctx, w, THING_WEAPON, e->pos, e->weapon, (uint8_t)(e->player + 1), -1);
    if (k < 0) return;
    w->things[k].ammo = e->ammo;
    if (!e->thrown) w->things[k].forces[1] = e->impact; // a death gives its muzzle the killing blow
}

void thrown_knife_land(const Context *ctx, World *w, const EventKnifeLand *e)
{
    if (w->authority) thing_create(ctx, w, THING_WEAPON, e->pos, WEAPON_KNIFE, (uint8_t)(e->owner + 1), -1);
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
    // the bow's thing goes once the bow is held; a gun goes now
    if (t->weapon != WEAPON_BOW && t->weapon != WEAPON_BOW2) thing_kill(t);
}

void dropped_gun_give(const Context *ctx, Soldier *s, WeaponId weapon, int32_t ammo)
{
    if (weapon == WEAPON_BOW || weapon == WEAPON_BOW2) {
        // the bow, and its flaming twin behind it
        s->weapon = weapon_state(ctx, WEAPON_BOW);
        s->secondary = weapon_state(ctx, WEAPON_BOW2);
        s->weapon.ammo = 1;
        return;
    }
    s->weapon = weapon_state(ctx, weapon);
    s->weapon.ammo = ammo;
}
