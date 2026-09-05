// The weapon in hand: firing with the spread and bink, the reloads, changing, throwing
// grenades and the gun, the punch and the rifle butt. Ported from OpenSoldat
// Sprites.pas / Control.pas by way of soldat-odin.

#include "game/systems/systems.h"

Weapon weapon_state(const Context *ctx, WeaponId id)
{
    const WeaponStats *stats = &ctx->weapons.info[id].stats;
    return (Weapon){
        .id = id,
        .ammo = id == WEAPON_M79 ? 0 : stats->ammo, // the M79 spawns empty and reloads
        .fire_count = stats->fire_interval,
        .reload_count = stats->reload_time,
        .startup_count = stats->startup,
    };
}

void combat_control(const Context *ctx, World *w, uint8_t index, Events *events)
{
    // Not ported yet: firing, the melee, grenades, reloads by hand, changing and
    // dropping the gun (soldat-odin's combat.odin). They need the bullet pool and the
    // dropped guns, which come with it. With no buttons pressed and a full gun the
    // original does nothing here either, so movement is unaffected.
    (void)ctx;
    (void)w;
    (void)index;
    (void)events;
}

void weapon_timers(const Context *ctx, Soldier *s)
{
    const Anims *anims = ctx->anims;
    Weapon *weapon = &s->weapon;
    const WeaponInfo *info = &ctx->weapons.info[weapon->id];
    Anim *body = &s->body;

    if (s->auto_reload_when_can_fire && (weapon->id != WEAPON_SPAS || weapon->fire_count == 0)) {
        s->auto_reload_when_can_fire = false;
        if (weapon->id == WEAPON_SPAS && body->id != ANIM_ROLL && body->id != ANIM_ROLL_BACK &&
            body->id != ANIM_CHANGE && weapon->ammo != info->stats.ammo) {
            anim_apply(anims, body, ANIM_RELOAD, 1);
        }
    }
    if (weapon->fire_count > 0 && (weapon->ammo > 0 || weapon->id == WEAPON_SPAS)) weapon->fire_count--;
    if (!(s->controls & BUTTON_FIRE)) s->can_auto_reload_spas = true;

    bool busy = body->id == ANIM_ROLL || body->id == ANIM_ROLL_BACK || body->id == ANIM_MELEE ||
                body->id == ANIM_CHANGE || body->id == ANIM_THROW || body->id == ANIM_THROW_WEAPON;
    if (weapon->ammo != 0 || !(weapon->id == WEAPON_CHAINSAW || !busy)) return;

    if (body->id != ANIM_GET_UP) {
        if (weapon->id == WEAPON_SPAS) {
            if (weapon->fire_count == 0 && s->can_auto_reload_spas) anim_apply(anims, body, ANIM_RELOAD, 1);
        } else if (weapon->id == WEAPON_BOW || weapon->id == WEAPON_BOW2) {
            anim_apply(anims, body, ANIM_RELOAD_BOW, 1);
        } else if (body->id != ANIM_CLIP_IN && body->id != ANIM_SLIDE_BACK && (weapon->id != WEAPON_CHAINSAW || !busy)) {
            anim_apply(anims, body, ANIM_CLIP_OUT, 1);
        }
        s->burst_count = 0;
    }

    if (weapon->id != WEAPON_SPAS) {
        if (weapon->reload_count > 0) weapon->reload_count--;
        weapon->fire_count = info->stats.fire_interval;
        if (weapon->reload_count < 1) {
            weapon->reload_count = info->stats.reload_time;
            weapon->fire_count = info->stats.fire_interval;
            weapon->startup_count = info->stats.startup;
            weapon->ammo = info->stats.ammo;
        }
    }
}

uint16_t calculate_bink(uint16_t accumulated, int bink)
{
    if (bink <= 0) return accumulated;
    float acc = (float)accumulated;
    int result = (int)accumulated + bink - round_half_even(acc * (acc / (10.0f * (float)bink + acc)));
    return (uint16_t)clampi(result, 0, 65535);
}

void hit_spray(const Context *ctx, World *w, uint8_t victim, uint8_t attacker)
{
    Soldier *v = &w->soldiers[victim];
    const Soldier *a = &w->soldiers[attacker];
    if (victim != attacker && !w->rules.friendly_fire && v->team != TEAM_NONE && v->team == a->team) return;

    int32_t bink = ctx->weapons.info[v->weapon.id].stats.bink;
    if (bink > 0) v->hit_spray = calculate_bink(v->hit_spray, bink);
}

float movement_inaccuracy(const Context *ctx, const Soldier *s)
{
    float acc = ctx->weapons.info[s->weapon.id].stats.movement_acc;
    if (acc <= 0.0f) return 0.0f;

    switch (s->legs.id) {
    case ANIM_JUMP:
    case ANIM_JUMP_SIDE:
    case ANIM_RUN:
    case ANIM_RUN_BACK:
    case ANIM_ROLL:
    case ANIM_ROLL_BACK:
        return acc * 7.0f;
    default:
        break;
    }
    if ((s->controls & BUTTON_JET) && s->jets > 0) return acc * 7.0f;

    AnimId legs = s->legs.id;
    bool lying_or_crouched = legs == ANIM_PRONE || legs == ANIM_PRONE_MOVE || legs == ANIM_CROUCH ||
                             legs == ANIM_CROUCH_RUN || legs == ANIM_CROUCH_RUN_BACK;
    if ((!s->on_ground_permanent && !lying_or_crouched) || legs == ANIM_GET_UP ||
        (legs == ANIM_PRONE && s->legs.frame < anim_frames(ctx->anims, ANIM_PRONE))) {
        return acc * 3.0f;
    }
    return 0.0f;
}

Vec2 hands_aim_direction(const Pose *pose)
{
    return vec2_normalize(vec2_sub(pose->p[14], pose->p[15]));
}

Vec2 aim_direction(const Soldier *s)
{
    Vec2 d = vec2_normalize(vec2_sub(s->aim, s->pos));
    if (vec2_is_zero(d)) return vec2((float)s->direction, 0.0f);
    return d;
}
