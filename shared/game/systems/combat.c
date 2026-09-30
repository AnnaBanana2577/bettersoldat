// The weapon in hand: firing with the spread and bink, the reloads, changing, throwing
// grenades, the gun and the knife, the punch and the rifle butt. Ported from OpenSoldat
// Sprites.pas (Fire, ThrowGrenade) and Control.pas (ControlSprite) by way of soldat-odin.
//
// The spread is OpenSoldat's own: a hash of the shooter and its count of bullets
// (ShotRandom), so every machine rolls the same shot. The original rolls the Eagle's
// second bullet and the shotgun's pellets from Pascal's global Random, which no two
// machines share; here they come from the same hash, one pair of numbers per bullet.

#include "game/systems/systems.h"

#define MAX_INACCURACY 0.5f
#define MELEE_DIST 12.0f
#define PI_F 3.14159265f

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

// --- firing ------------------------------------------------------------------------

// ShotRandom: the lowbias32 hash of a shot's key and an index, in [-1, 1).
static float shot_random(uint32_t key, uint32_t index)
{
    uint32_t x = key * 2654435761u + index * 40503u;
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return (float)(x >> 8) / (float)(1u << 24) * 2.0f - 1.0f;
}

// A shot's key: the shooter (numbered from 1, as the original's are) and its count of
// bullets, which wraps as a Word does.
static uint32_t shot_key(uint8_t index, uint32_t count)
{
    return (uint32_t)(index + 1) << 16 | (count & 0xFFFF);
}

// One bullet's share of a spread: `vel` thrown off by up to `amount` each way.
static Vec2 spread(uint32_t key, int bullet, Vec2 vel, float amount)
{
    return vec2(vel.x + shot_random(key, 2 + 2 * (uint32_t)bullet) * amount,
                vel.y + shot_random(key, 3 + 2 * (uint32_t)bullet) * amount);
}

static float signf(float x) { return x > 0.0f ? 1.0f : x < 0.0f ? -1.0f : 0.0f; }

// Kneeling or lying: the LAW fires only so, and its wind-up only counts down so.
static bool law_stance(const Soldier *s)
{
    switch (s->legs.id) {
    case ANIM_CROUCH: return s->legs.frame > 13;
    case ANIM_CROUCH_RUN:
    case ANIM_CROUCH_RUN_BACK: return true;
    case ANIM_PRONE: return s->legs.frame > 23;
    default: return false;
    }
}

static void crouch_recoil(const Anims *anims, Soldier *s)
{
    if (s->stance != STANCE_CROUCH) return;
    anim_apply(anims, &s->body, s->body.id == ANIM_HANDS_UP_AIM ? ANIM_HANDS_UP_RECOIL : ANIM_AIM_RECOIL, 1);
}

static void recoil_animation(const Anims *anims, Soldier *s)
{
    Anim *body = &s->body;
    bool free = body->id != ANIM_THROW && body->id != ANIM_GET_UP && body->id != ANIM_MELEE;
    switch (s->weapon.id) {
    case WEAPON_AK74:
    case WEAPON_M249:
    case WEAPON_MP5:
    case WEAPON_EAGLE:
    case WEAPON_STEYR:
    case WEAPON_COLT:
    case WEAPON_BOW:
    case WEAPON_BOW2:
        if (free && s->stance == STANCE_STAND) anim_apply(anims, body, ANIM_SMALL_RECOIL, 1);
        crouch_recoil(anims, s);
        break;
    case WEAPON_RUGER:
        if (free && s->stance == STANCE_STAND) anim_apply(anims, body, ANIM_RECOIL, 1);
        crouch_recoil(anims, s);
        break;
    case WEAPON_SPAS:
        if (free && s->stance != STANCE_PRONE) anim_apply(anims, body, ANIM_SHOTGUN, 1);
        if (s->stance == STANCE_PRONE && body->id == ANIM_RELOAD) body->frame = anim_frames(anims, ANIM_RELOAD);
        break;
    case WEAPON_M79:
        if (free && s->stance != STANCE_PRONE) anim_apply(anims, body, ANIM_SMALL_RECOIL, 1);
        break;
    case WEAPON_BARRETT:
        if (free) anim_apply(anims, body, ANIM_BARRET, 1);
        break;
    case WEAPON_MINIGUN:
        if (free && s->stance == STANCE_STAND) anim_apply(anims, body, ANIM_SMALL_RECOIL, 2);
        break;
    default:
        break;
    }
}

int soldier_shoot(World *w, uint8_t index, WeaponId weapon, Vec2 pos, Vec2 vel, float damage, Events *events)
{
    Soldier *s = &w->soldiers[index];
    if (events->count >= MAX_EVENTS) return -1;
    event_emit(events, (Event){
        .type = EVENT_SHOT,
        .shot = {.player = index, .weapon = weapon, .pos = pos, .vel = vel, .damage = damage, .shot = ++s->shot_count},
    });
    return events->count - 1;
}

// One pull of the trigger: the bullets asked for, the self-push, the ammo, the recoil,
// the bink.
static void fire(const Context *ctx, World *w, uint8_t index, Events *events)
{
    Soldier *s = &w->soldiers[index];
    Weapon *weapon = &s->weapon;
    const WeaponStats *stats = &ctx->weapons.info[weapon->id].stats;
    Pose pose = soldier_pose(ctx->anims, s, s->pos);
    bool mercy = s->body.id == ANIM_MERCY || s->body.id == ANIM_MERCY2;

    Vec2 aim = stats->style == BULLET_KNIFE || mercy ? hands_aim_direction(&pose)
                                                     : vec2_normalize(vec2_sub(s->aim, pose.p[14]));
    Vec2 origin = vec2(pose.p[14].x - aim.x * 4.0f, pose.p[14].y - aim.y * 4.0f - 2.0f);

    float inaccuracy = (float)s->hit_spray * 0.01f + movement_inaccuracy(ctx, s);
    if (weapon->id != WEAPON_EAGLE && weapon->id != WEAPON_SPAS && stats->style != BULLET_SHOTGUN && stats->spread > 0.0f) {
        AnimId legs = s->legs.id;
        if (legs == ANIM_PRONE_MOVE || (legs == ANIM_PRONE && s->legs.frame > 23)) {
            inaccuracy += stats->spread / 1.625f;
        } else if (legs == ANIM_CROUCH_RUN || legs == ANIM_CROUCH_RUN_BACK || (legs == ANIM_CROUCH && s->legs.frame > 13)) {
            inaccuracy += stats->spread / 1.3f;
        } else {
            inaccuracy += stats->spread;
        }
    }
    inaccuracy = minf(inaccuracy * 0.25f, MAX_INACCURACY);
    float max_dev = MAX_INACCURACY * sinf(inaccuracy / MAX_INACCURACY * (PI_F / 2.0f));
    uint32_t key = shot_key(index, s->shot_count);
    Vec2 dev = vec2(shot_random(key, 0) * max_dev, shot_random(key, 1) * max_dev);
    Vec2 vel = vec2_add(vec2_scale(vec2_normalize(vec2_add(aim, dev)), stats->speed), vec2_scale(s->vel, stats->inherit));

    // a muzzle inside a wall (the head in a ceiling) is lowered a bit
    if (map_collision_test(ctx->map, origin, false, NULL)) origin.y += 2.5f;

    int bullet = -1; // the shot the mercy antic marks as its own: the event's index
    WeaponId id = weapon->id;
    bool plain = id != WEAPON_EAGLE && id != WEAPON_SPAS && id != WEAPON_FLAMER && id != WEAPON_NONE &&
                 id != WEAPON_KNIFE && id != WEAPON_CHAINSAW && id != WEAPON_LAW;
    if (plain || mercy) bullet = soldier_shoot(w, index, id, origin, vel, stats->damage, events);

    // The Eagles' and the shotgun's spread is keyed on the count after their first
    // bullet, as the original's is.
    uint32_t spread_key = shot_key(index, s->shot_count + 1);
    if (id == WEAPON_EAGLE) {
        bullet = soldier_shoot(w, index, id, origin, spread(spread_key, 0, vel, stats->spread), stats->damage, events);
        Vec2 n = vec2_normalize(vel);
        Vec2 beside = vec2(origin.x - signf(vel.x) * fabsf(n.y) * 3.0f, origin.y + signf(vel.y) * fabsf(n.x) * 3.0f);
        soldier_shoot(w, index, id, beside, spread(spread_key, 1, vel, stats->spread), stats->damage, events);
    }
    if (stats->style == BULLET_SHOTGUN) {
        for (int i = 0; i < 6; i++) {
            int b = soldier_shoot(w, index, id, origin, spread(spread_key, i, vel, stats->spread), stats->damage, events);
            if (i == 0) bullet = b;
        }
        s->vel = vec2_sub(s->vel, vec2_mul(vel, vec2(0.0412f, 0.041f)));
    }
    if (id == WEAPON_MINIGUN) {
        Vec2 push = (s->controls & BUTTON_JET) && s->jets > 0 ? vec2_mul(vel, vec2(0.0012f, 0.0009f))
                                                              : vec2_mul(vel, vec2(0.0082f, 0.0078f));
        if (s->held) push = vec2_mul(push, vec2(0.5f, 0.7f)); // anything held: the flag, even the parachute
        push.x *= 0.6f;
        s->vel = vec2_sub(s->vel, push);
    }
    // a flame starts a step further out than it is aimed from (CreateBullet's offset)
    if (id == WEAPON_FLAMER) bullet = soldier_shoot(w, index, id, vec2_add(origin, vec2_scale(vel, 3.0f)), vel, stats->damage, events);
    if (id == WEAPON_CHAINSAW) bullet = soldier_shoot(w, index, id, vec2_add(origin, vec2_scale(vel, 2.0f)), vel, stats->damage, events);
    if (id == WEAPON_LAW) {
        if (!((s->on_ground || s->on_ground_permanent || s->on_ground_for_law) && law_stance(s))) return;
        bullet = soldier_shoot(w, index, id, origin, vel, stats->damage, events);
    }

    // the mercy antic shoots the shooter's own head: the bullet leaves it alone
    if (mercy && bullet >= 0) events->items[bullet].shot.self = true;

    if (weapon->ammo > 0) weapon->ammo--;
    if (id == WEAPON_SPAS) s->can_auto_reload_spas = false;
    weapon->fire_count = stats->fire_interval;
    s->fired = true;
    recoil_animation(ctx->anims, s);
    if (s->burst_count < 255) s->burst_count++;

    // self-bink for the next shot; halved when crouched or prone
    if (stats->bink < 0) {
        AnimId legs = s->legs.id;
        bool steady = legs == ANIM_CROUCH || legs == ANIM_CROUCH_RUN || legs == ANIM_CROUCH_RUN_BACK ||
                      legs == ANIM_PRONE || legs == ANIM_PRONE_MOVE;
        int bink = steady ? round_half_even((float)-stats->bink / 2.0f) : -stats->bink;
        s->hit_spray = calculate_bink(s->hit_spray, bink);
    }

    event_emit(events, (Event){.type = EVENT_FIRE, .fire = {.player = index, .weapon = id, .pos = origin, .vel = vel}});
}

// --- throwing ----------------------------------------------------------------------

// The grenade: hold to wind up (longer is further), release to throw.
static void throw_grenade(const Context *ctx, World *w, uint8_t index, Events *events)
{
    Soldier *s = &w->soldiers[index];
    const Anims *anims = ctx->anims;
    Anim *body = &s->body;
    bool throw = (s->controls & BUTTON_THROW) != 0;

    if (!throw) s->grenade_can_throw = true;
    if (s->grenade_can_throw && throw && body->id != ANIM_ROLL && body->id != ANIM_ROLL_BACK) {
        anim_apply(anims, body, ANIM_THROW, 1);
    }
    if (body->id != ANIM_THROW || (throw && body->frame != 36)) return;

    if (body->frame > 14 && body->frame < 37 && s->grenades > 0 && s->cease_fire_counter < 0) {
        const WeaponStats *frag = &ctx->weapons.info[WEAPON_FRAG].stats;
        Pose pose = soldier_pose(anims, s, s->pos);
        Vec2 dir = vec2_normalize(vec2_sub(s->aim, pose.p[14]));

        // a few degrees of arc, which disappear aiming straight up or down
        float arc = signf(dir.x) / 8.0f * (1.0f - fabsf(dir.y));
        float arc_x = sinf(dir.y * PI_F / 2.0f) * arc;
        float arc_y = sinf(dir.x * PI_F / 2.0f) * arc;
        dir = vec2_normalize(vec2(dir.x + arc_x, dir.y - arc_y));

        Vec2 vel = vec2_scale(dir, (float)body->frame / frag->speed);
        if (body->frame < 24) vel = vec2_scale(vel, 0.65f);
        vel = vec2_add(vel, vec2_scale(s->vel, frag->inherit));

        Vec2 origin = vec2(pose.p[14].x + vel.x * 3.0f, pose.p[14].y - 2.0f + vel.y * 3.0f);
        Vec2 head = vec2(s->pos.x, s->pos.y - 12.0f);
        RayFilter filter = {.bullet = true, .team = s->team};
        if (!map_collision_test(ctx->map, origin, false, NULL) && !map_ray_cast(ctx->map, head, origin, 50.0f, filter, NULL)) {
            soldier_shoot(w, index, s->grenade_type, origin, vel, frag->damage, events);
            s->grenades--;
            if (frag->bink < 0) s->hit_spray = calculate_bink(s->hit_spray, -frag->bink);
            event_emit(events, (Event){.type = EVENT_FIRE, .fire = {.player = index, .weapon = s->grenade_type, .pos = origin, .vel = vel}});
        }
    }
    if (throw) s->grenade_can_throw = false;

    Weapon *weapon = &s->weapon;
    const WeaponInfo *info = &ctx->weapons.info[weapon->id];
    if (weapon->ammo == 0) {
        if (weapon->reload_count > info->clip_out_time) anim_apply(anims, body, ANIM_CLIP_OUT, 1);
        if (weapon->reload_count < info->clip_out_time) anim_apply(anims, body, ANIM_CLIP_IN, 1);
        if (weapon->reload_count < info->clip_in_time && weapon->reload_count > 0) anim_apply(anims, body, ANIM_SLIDE_BACK, 1);
    }
}

// The knife leaves the hand spinning: sooner is weaker. A held drop key throws nothing
// more until it is let go.
static void throw_knife(const Context *ctx, World *w, uint8_t index, Events *events)
{
    Soldier *s = &w->soldiers[index];
    const WeaponStats *knife = &ctx->weapons.info[WEAPON_THROWN_KNIFE].stats;
    Pose pose = soldier_pose(ctx->anims, s, s->pos);

    s->dont_drop = true;
    float strength = clampf((float)s->body.frame, 8.0f, 16.0f) / 16.0f;
    Vec2 vel = vec2_scale(vec2_normalize(vec2_sub(s->aim, pose.p[14])), knife->speed * 1.5f * strength);
    vel = vec2_add(vel, vec2_scale(s->vel, knife->inherit));
    soldier_shoot(w, index, WEAPON_THROWN_KNIFE, pose.p[15], vel, knife->damage, events);

    s->weapon = weapon_state(ctx, WEAPON_NONE);
    anim_apply(ctx->anims, &s->body, ANIM_STAND, 1);
}

// --- the control step --------------------------------------------------------------

static bool rolling(const Anim *body) { return body->id == ANIM_ROLL || body->id == ANIM_ROLL_BACK; }

// The trigger: the punch with bare hands or a knife, the wind-up, the shot.
static void trigger(const Context *ctx, World *w, uint8_t index, Events *events)
{
    Soldier *s = &w->soldiers[index];
    Anim *body = &s->body;
    Weapon *weapon = &s->weapon;
    const WeaponStats *stats = &ctx->weapons.info[weapon->id].stats;

    if (weapon->id != WEAPON_CHAINSAW && (rolling(body) || body->id == ANIM_MELEE || body->id == ANIM_CHANGE)) {
        weapon->startup_count = stats->startup;
        s->burst_count = 0;
        return;
    }
    // crouched behind cover, the gun comes up before it fires
    if (body->id == ANIM_HANDS_UP_AIM && body->frame != 11) return;

    if (!(s->controls & BUTTON_FIRE) || s->cease_fire_counter >= 0) {
        weapon->startup_count = stats->startup;
        return;
    }
    if (weapon->id == WEAPON_NONE || weapon->id == WEAPON_KNIFE) {
        anim_apply(ctx->anims, body, ANIM_PUNCH, 1);
        return;
    }
    if (weapon->fire_count != 0 || weapon->ammo <= 0) return;

    if (stats->startup > 0 && weapon->startup_count > 0) {
        if (weapon->id != WEAPON_LAW || ((s->on_ground || s->on_ground_permanent) && law_stance(s))) weapon->startup_count--;
    } else {
        fire(ctx, w, index, events);
    }
}

void combat_control(const Context *ctx, World *w, uint8_t index, Events *events)
{
    Soldier *s = &w->soldiers[index];
    const Anims *anims = ctx->anims;
    Anim *body = &s->body;
    Weapon *weapon = &s->weapon;
    const WeaponInfo *info = &ctx->weapons.info[weapon->id];
    bool fire_held = (s->controls & BUTTON_FIRE) != 0;
    bool drop_held = (s->controls & BUTTON_DROP) != 0;
    bool flame_god = s->bonus == BONUS_FLAME_GOD;

    // safety: a weapon whose counters are past its stats is taken fresh, and empty
    if (weapon->ammo > info->stats.ammo || weapon->fire_count > info->stats.fire_interval ||
        weapon->reload_count > info->stats.reload_time) {
        *weapon = weapon_state(ctx, weapon->id);
        weapon->ammo = 0;
    }

    // the rifle butt, standing next to someone standing
    if (s->stat == 0 && s->stance == STANCE_STAND && fire_held && s->cease_fire_counter < 0 &&
        weapon->id != WEAPON_NONE && weapon->id != WEAPON_KNIFE && weapon->id != WEAPON_CHAINSAW) {
        for (int i = 0; i < MAX_PLAYERS; i++) {
            const Soldier *other = &w->soldiers[i];
            if (i == index || !other->active || other->dead || other->stance != STANCE_STAND || other->team == TEAM_SPECTATOR) continue;
            if (vec2_length(vec2_sub(s->pos, other->pos)) < MELEE_DIST) anim_apply(anims, body, ANIM_MELEE, 1);
        }
    }

    if (s->stat == 0) trigger(ctx, w, index, events);
    if (!fire_held) s->burst_count = 0;

    // a semi-automatic needs the trigger released between shots
    if (info->semi_auto && fire_held && (s->burst_count > 0 || (s->controls & BUTTON_RELOAD)) && weapon->fire_count < 2) {
        weapon->fire_count++;
    }

    // the flag's throw is asked of the things pass
    if (!rolling(body) && (s->controls & BUTTON_FLAG_THROW) && s->held) {
        event_emit(events, (Event){.type = EVENT_FLAG_THROW, .flag_throw = {.player = index}});
    }
    throw_grenade(ctx, w, index, events);

    if (!rolling(body) && !flame_god && (s->controls & BUTTON_CHANGE)) anim_apply(anims, body, ANIM_CHANGE, 1);

    // throwing the gun away
    if (s->dont_drop && (!drop_held || weapon->id == WEAPON_KNIFE)) s->dont_drop = false;
    if (drop_held && !(s->controls & BUTTON_THROW) && !s->dont_drop && !rolling(body) &&
        (body->id != ANIM_CHANGE || body->frame > 25) && !flame_god && weapon->id != WEAPON_BOW &&
        weapon->id != WEAPON_BOW2 && weapon->id != WEAPON_NONE) {
        anim_apply(anims, body, ANIM_THROW_WEAPON, 1);
        if (weapon->id == WEAPON_KNIFE) body->speed = 2;
    }

    // reloading by hand
    if ((weapon->id == WEAPON_CHAINSAW || (!rolling(body) && body->id != ANIM_CHANGE)) && (s->controls & BUTTON_RELOAD) &&
        weapon->ammo != info->stats.ammo) {
        if (weapon->id == WEAPON_SPAS) {
            if (weapon->ammo < info->stats.ammo) {
                if (weapon->fire_count == 0) anim_apply(anims, body, ANIM_RELOAD, 1);
                else s->auto_reload_when_can_fire = true;
            }
        } else {
            weapon->ammo = 0;
            weapon->fire_count = info->stats.fire_interval;
        }
        s->burst_count = 0;
    }

    // the shotgun reloads shell by shell
    if (body->id == ANIM_RELOAD && body->frame == 7) body->frame++;
    if ((!fire_held || weapon->ammo == 0) && body->id == ANIM_RELOAD && body->frame == 14) {
        weapon->ammo++;
        if (weapon->ammo < info->stats.ammo) body->frame = 1;
    }

    // the change swaps the guns at frame 25
    if (body->id == ANIM_CHANGE && body->frame == 2) body->frame++;
    if (body->id == ANIM_CHANGE && body->frame == 25 && !flame_god) {
        Weapon held = s->weapon;
        s->weapon = s->secondary;
        s->secondary = held;
        s->weapon.startup_count = ctx->weapons.info[s->weapon.id].stats.startup;
        s->burst_count = 0;
        info = &ctx->weapons.info[weapon->id];
    }
    if (body->id == ANIM_CHANGE && body->frame == anim_frames(anims, ANIM_CHANGE) && !flame_god && weapon->ammo == 0) {
        anim_apply(anims, body, ANIM_STAND, 1);
    }

    // the gun leaves the hand at frame 19 of the throw; the knife flies from frame 16,
    // or as soon as the key is let go
    if (weapon->id != WEAPON_KNIFE && body->id == ANIM_THROW_WEAPON && body->frame == 19 && weapon->id != WEAPON_NONE) {
        if (weapon_droppable(weapon->id)) {
            Pose pose = soldier_pose(anims, s, s->pos);
            event_emit(events, (Event){
                .type = EVENT_WEAPON_DROP,
                .weapon_drop = {.player = index, .weapon = weapon->id, .ammo = weapon->ammo, .thrown = true, .pos = pose.p[15]},
            });
        }
        s->weapon = weapon_state(ctx, WEAPON_NONE);
        info = &ctx->weapons.info[WEAPON_NONE];
        anim_apply(anims, body, ANIM_STAND, 1);
    }
    if (weapon->id == WEAPON_KNIFE && body->id == ANIM_THROW_WEAPON && (!drop_held || body->frame == 16)) {
        throw_knife(ctx, w, index, events);
        info = &ctx->weapons.info[WEAPON_NONE];
    }

    // the punch or the stab
    if (body->id == ANIM_PUNCH && body->frame == 11 && weapon->id != WEAPON_LAW && weapon->id != WEAPON_M79) {
        Pose pose = soldier_pose(anims, s, s->pos);
        float dir = (float)s->direction;
        soldier_shoot(w, index, weapon->id, vec2(pose.p[15].x + 2.0f * dir, pose.p[15].y + 3.0f), vec2(dir * 0.1f, 0.0f),
                      info->stats.damage, events);
        body->frame++;
    }

    // the rifle butt
    if (body->id == ANIM_MELEE && body->frame == 12) {
        Pose pose = soldier_pose(anims, s, s->pos);
        float dir = (float)s->direction;
        soldier_shoot(w, index, WEAPON_NONE, vec2(pose.p[15].x + 2.0f * dir, pose.p[15].y + 3.0f), vec2(dir * 0.1f, 0.0f),
                      ctx->weapons.info[WEAPON_NONE].stats.damage, events);
    }
    if (body->id == ANIM_MELEE && body->frame > 20) anim_apply(anims, body, ANIM_STAND, 1);

    // the shotgun's shell is ejected at frame 24, which it skips
    if (body->id == ANIM_SHOTGUN && body->frame == 24) body->frame++;

    // the M79's spent casing holds the reload a tick
    if (weapon->id == WEAPON_M79 && weapon->reload_count == info->clip_out_time && weapon->reload_count > 0) {
        weapon->reload_count--;
    }
}

void combat_after_prone(const Context *ctx, World *w, Soldier *s)
{
    (void)w;
    // working the Barrett's bolt between shots
    Anim *body = &s->body;
    if (s->weapon.id == WEAPON_BARRETT && s->weapon.fire_count > 0 &&
        (body->id == ANIM_STAND || body->id == ANIM_CROUCH || body->id == ANIM_PRONE)) {
        anim_apply(ctx->anims, body, ANIM_BARRET, 1);
    }
}

void combat_reload_animation(const Context *ctx, Soldier *s)
{
    // the reload animation follows the reload timer
    const WeaponInfo *info = &ctx->weapons.info[s->weapon.id];
    Anim *body = &s->body;
    if (s->weapon.reload_count == info->clip_out_time && body->id != ANIM_RELOAD && body->id != ANIM_RELOAD_BOW && !rolling(body)) {
        anim_apply(ctx->anims, body, ANIM_CLIP_IN, 1);
    }
    if (s->weapon.reload_count == info->clip_in_time) anim_apply(ctx->anims, body, ANIM_SLIDE_BACK, 1);
}

// --- the timers --------------------------------------------------------------------

void weapon_timers(const Context *ctx, Soldier *s)
{
    const Anims *anims = ctx->anims;
    Weapon *weapon = &s->weapon;
    const WeaponInfo *info = &ctx->weapons.info[weapon->id];
    Anim *body = &s->body;

    if (s->auto_reload_when_can_fire && (weapon->id != WEAPON_SPAS || weapon->fire_count == 0)) {
        s->auto_reload_when_can_fire = false;
        if (weapon->id == WEAPON_SPAS && !rolling(body) && body->id != ANIM_CHANGE && weapon->ammo != info->stats.ammo) {
            anim_apply(anims, body, ANIM_RELOAD, 1);
        }
    }
    if (weapon->fire_count > 0 && (weapon->ammo > 0 || weapon->id == WEAPON_SPAS)) weapon->fire_count--;
    if (!(s->controls & BUTTON_FIRE)) s->can_auto_reload_spas = true;

    bool busy = rolling(body) || body->id == ANIM_MELEE || body->id == ANIM_CHANGE || body->id == ANIM_THROW ||
                body->id == ANIM_THROW_WEAPON;
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

// --- bink and aim ------------------------------------------------------------------

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
