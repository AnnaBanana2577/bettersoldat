// A bullet's tick of collisions, in the original's order: the map, the colliders, the
// soldiers, the things. A bullet stopped by one is rewound so the later checks still
// see its path, and only a nearer hit wins. Ported from OpenSoldat Bullets.pas
// (CheckMapCollision, CheckColliderCollision, CheckSpriteCollision,
// CheckThingCollision) by way of soldat-odin.
//
// A soldier hit is a Hit event, never a wound: whoever applies the events decides (the
// server does; a client only shows the blood). A bullet that kills goes on through, as
// it does in the original, where the wound lands at once; here the kill is foreseen
// from the soldier's health.

#include <float.h>
#include <string.h>

#include "game/systems/systems.h"

#define PART_RADIUS 7.0f
#define FLAG_PART_RADIUS 10.0f // a thing's first two points
#define GRENADE_SURFACECOEF 0.88f
#define THING_PUSH_MULTIPLIER 9.0f
#define THING_COLLISION_COOLDOWN 60 // ticks before the same bullet can push the same thing again

// The ricochet's blend of the old path and the reflected one.
#define RICOCHET_KEEP ((float)(25.0 / 35.0))
#define RICOCHET_TURN ((float)(10.0 / 35.0))

const int HIT_PARTS[HIT_PART_COUNT] = {11, 10, 9, 5, 4, 3, 2};

float hitbox_modifier(const WeaponStats *stats, int part)
{
    int point = part + 1; // the original's 1-based skeleton numbering
    if (point <= 4) return stats->mod_legs;
    if (point <= 11) return stats->mod_chest;
    return stats->mod_head;
}

static float bullet_gravity(const World *w) { return w->gravity * BULLET_GRAVITY; }

// A thrown knife stopped where it is: the things pass lays it down there.
static void knife_land(const Bullet *b, Events *events)
{
    event_emit(events, (Event){.type = EVENT_KNIFE_LAND, .knife_land = {.owner = b->owner, .pos = b->pos}});
}

// A bullet made by this one carries its lag: it meets the soldiers as its parent did.
static void spawn_child(const Context *ctx, World *w, const Bullet *parent, Vec2 pos, Vec2 vel, WeaponId weapon, float damage,
                        Events *events)
{
    int k = bullet_spawn(ctx, w, pos, vel, weapon, parent->owner, damage, events);
    if (k >= 0) w->bullets[k].lag = parent->lag;
}

// The fragments scatter by the grenade's own numbers, not the world's dice, so every
// machine that flew it to the same spot rolls the same five. Random(50) / 10 and
// Random(25) / 10, as the original rolls them.
static void cluster_split(const Context *ctx, World *w, Bullet *b, uint16_t index, Events *events)
{
    Vec2 origin = vec2_sub(b->pos, b->vel);
    uint32_t x, y;
    memcpy(&x, &b->pos.x, sizeof x);
    memcpy(&y, &b->pos.y, sizeof y);
    uint64_t rng = (uint64_t)x << 32 | (uint64_t)y | 1;
    for (int i = 0; i < 5; i++) {
        Vec2 v = vec2_scale(b->vel, -0.75f);
        v.x = -v.x - 2.5f + (float)rand_int(&rng, 50) / 10.0f;
        v.y = v.y - 2.5f + (float)rand_int(&rng, 25) / 10.0f;
        spawn_child(ctx, w, b, origin, v, WEAPON_CLUSTER, ctx->weapons.info[WEAPON_FRAG].stats.damage / 2.0f, events);
    }
    event_emit(events, (Event){.type = EVENT_CLUSTER_SPLIT, .cluster_split = {.id = index, .owner = b->owner, .pos = b->pos}});
}

// --- the map -----------------------------------------------------------------------

static bool bullet_poly_collides(PolyType t, Team team)
{
    switch (t) {
    case POLY_ONLY_PLAYER:
    case POLY_DOESNT:
    case POLY_ONLY_FLAGGERS:
    case POLY_NOT_FLAGGERS:
    case POLY_BACKGROUND:
    case POLY_BACKGROUND_TRANSITION:
        return false;
    default:
        return bullet_team_collides(t, team);
    }
}

static bool solid_at(const Map *map, Vec2 pos, Team team, bool inner_sectors_only)
{
    int n = map->sectors_num;
    int sx = round_half_even(pos.x / (float)map->sectors_division);
    int sy = round_half_even(pos.y / (float)map->sectors_division);
    if (inner_sectors_only && !(sx > -n && sx < n && sy > -n && sy < n)) return false;
    PolySector sector = map_sector_at(map, sx, sy);
    for (int k = 0; k < sector.count; k++) {
        const Polygon *poly = &map->polys[sector.polys[k]];
        if (bullet_poly_collides((PolyType)poly->type, team) && point_in_poly_edges(pos, poly)) return true;
    }
    return false;
}

static void wall_hit(Bullet *b, uint16_t index, Vec2 pos, Vec2 vel, Events *events)
{
    event_emit(events, (Event){
        .type = EVENT_WALL_HIT,
        .wall_hit = {.id = index, .owner = b->owner, .weapon = b->weapon, .pos = pos, .vel = vel},
    });
}

// A glancing hit deflects the bullet; a second hit on the same spot, or a deflection
// straight into more wall, stops it. Where the next check looks from: the probe ahead
// if it deflected, else the contact.
static Vec2 ricochet(const Map *map, Bullet *b, uint16_t index, const Polygon *poly, Vec2 pos, Team team, bool reset_old_pos,
                     Events *events)
{
    if (vec2_length(vec2_sub(b->pos, b->hit_spot)) <= 50.0f) {
        bullet_end(b, index, events, &pos);
        return pos;
    }

    b->ricochet_count++;
    Vec2 normal = closest_perpendicular(poly, b->pos, NULL, NULL);
    float speed = vec2_length(b->vel);
    Vec2 reflect = vec2_scale(vec2_normalize(normal), -speed);
    b->vel = vec2_add(vec2_scale(b->vel, RICOCHET_KEEP), vec2_scale(reflect, RICOCHET_TURN));
    b->pos = pos;
    b->hit_spot = pos;
    if (reset_old_pos) b->old_pos = pos;

    Vec2 probe = vec2_add(pos, vec2_scale(vec2_normalize(b->vel), speed / 6.0f));
    if (solid_at(map, probe, team, true)) bullet_end(b, index, events, &pos);
    return probe;
}

// Steps along the velocity looking for a solid poly. Returns where it hit, or zero.
static Vec2 map_collide(const Context *ctx, World *w, Bullet *b, uint16_t index, Vec2 at, Events *events)
{
    const Map *map = ctx->map;
    Team team = w->soldiers[b->owner].team;
    int steps = (int)(maxf(fabsf(b->vel.x), fabsf(b->vel.y)) / 2.5f);
    if (steps == 0) steps = 1;
    Vec2 step = vec2_scale(b->vel, 1.0f / (float)steps);
    int n = map->sectors_num;

    for (int i = 0; i < steps; i++) {
        Vec2 pos = vec2(at.x + (float)i * step.x, at.y + (float)i * step.y);
        int sx = round_half_even(pos.x / (float)map->sectors_division);
        int sy = round_half_even(pos.y / (float)map->sectors_division);
        if (sx < -n || sx > n || sy < -n || sy > n) {
            bullet_end(b, index, events, NULL);
            return (Vec2){0};
        }

        PolySector sector = map_sector_at(map, sx, sy);
        for (int k = 0; k < sector.count; k++) {
            const Polygon *poly = &map->polys[sector.polys[k]];
            if (!bullet_poly_collides((PolyType)poly->type, team) || !point_in_poly_edges(pos, poly)) continue;

            Vec2 result = pos;
            switch (b->style) {
            case BULLET_PLAIN:
            case BULLET_SHOTGUN:
            case BULLET_PUNCH:
            case BULLET_KNIFE:
            case BULLET_M2: {
                Vec2 incoming = b->vel;
                b->old_pos = b->pos;
                b->pos = vec2_sub(pos, b->vel);
                result = ricochet(map, b, index, poly, pos, team, true, events);
                if (b->active) {
                    event_emit(events, (Event){
                        .type = EVENT_RICOCHET,
                        .ricochet = {.id = index, .owner = b->owner, .pos = b->pos, .vel = b->vel},
                    });
                } else {
                    wall_hit(b, index, pos, incoming, events);
                }
                break;
            }
            case BULLET_M79:
            case BULLET_FLAME_ARROW:
            case BULLET_LAW: {
                Vec2 before = vec2_sub(pos, b->vel), incoming = b->vel;
                b->old_pos = b->pos;
                b->pos = before;
                result = ricochet(map, b, index, poly, pos, team, false, events);
                if (b->active) {
                    event_emit(events, (Event){
                        .type = EVENT_RICOCHET,
                        .ricochet = {.id = index, .owner = b->owner, .pos = b->pos, .vel = b->vel},
                    });
                } else {
                    // it goes off short of the wall, as it came in
                    b->pos = before;
                    b->vel = incoming;
                    explode(ctx, w, b, index, EXPLOSION_M79, -1, -1, events);
                }
                break;
            }
            case BULLET_ARROW:
                // arrows stick into walls
                b->pos = vec2_sub(pos, b->vel);
                b->forces.y -= bullet_gravity(w);
                if (b->timeout > ARROW_RESIST) b->timeout = ARROW_RESIST;
                if (b->timeout < 20) b->forces.y += bullet_gravity(w);
                break;
            case BULLET_FRAG_GRENADE:
            case BULLET_FLAME: {
                if (b->style == BULLET_FRAG_GRENADE && vec2_length(b->vel) > 1.5f) {
                    event_emit(events, (Event){
                        .type = EVENT_GRENADE_BOUNCE,
                        .grenade_bounce = {.id = index, .owner = b->owner, .pos = pos},
                    });
                }
                float dist;
                Vec2 normal = closest_perpendicular(poly, b->pos, &dist, NULL);
                b->pos = pos;
                b->vel = vec2_scale(vec2_sub(b->vel, vec2_scale(vec2_normalize(normal), dist)), GRENADE_SURFACECOEF);
                if (b->style == BULLET_FLAME && b->timeout > 16) b->timeout = 16;
                break;
            }
            case BULLET_CLUSTER_NADE:
                cluster_split(ctx, w, b, index, events);
                bullet_end(b, index, events, NULL);
                break;
            case BULLET_CLUSTER:
                explode(ctx, w, b, index, EXPLOSION_CLUSTER, -1, -1, events);
                bullet_end(b, index, events, NULL);
                break;
            case BULLET_THROWN_KNIFE:
                b->pos = vec2_sub(pos, b->vel);
                knife_land(b, events);
                wall_hit(b, index, pos, b->vel, events);
                bullet_end(b, index, events, &pos);
                break;
            }
            return result;
        }
    }
    return (Vec2){0};
}

// --- the colliders -----------------------------------------------------------------

// The map's colliders: invisible circles, usually behind sandbags, that stop fire.
static bool collider_collide(const Context *ctx, World *w, Bullet *b, uint16_t index, float nearest, Events *events, Vec2 *hit)
{
    const Map *map = ctx->map;
    for (int i = 0; i < map->collider_count; i++) {
        const MapCollider *c = &map->colliders[i];
        if (!c->active) continue;
        Vec2 p;
        if (!line_circle_collision(b->pos, vec2_add(b->pos, b->vel), c->pos, c->radius / 1.7f, &p)) continue;
        if (nearest > -1.0f && vec2_length(vec2_sub(p, b->old_pos)) > nearest) return false; // something nearer stopped it

        switch (b->style) {
        case BULLET_PLAIN:
        case BULLET_SHOTGUN:
        case BULLET_PUNCH:
        case BULLET_KNIFE:
        case BULLET_THROWN_KNIFE:
        case BULLET_M2:
            b->pos = vec2_sub(p, b->vel);
            if (b->style == BULLET_THROWN_KNIFE) knife_land(b, events);
            event_emit(events, (Event){
                .type = EVENT_COLLIDER_HIT,
                .collider_hit = {.id = index, .owner = b->owner, .pos = p, .vel = b->vel},
            });
            bullet_end(b, index, events, &p);
            break;
        case BULLET_FRAG_GRENADE:
            // not stopped by cover it was thrown from right next to
            if (b->timeout < GRENADE_TIMEOUT - 2) {
                explode(ctx, w, b, index, EXPLOSION_FRAG, -1, -1, events);
                bullet_end(b, index, events, NULL);
            }
            break;
        case BULLET_FLAME:
            bullet_end(b, index, events, NULL);
            break;
        case BULLET_ARROW:
            if (b->timeout > ARROW_RESIST) {
                b->forces.y -= bullet_gravity(w);
                wall_hit(b, index, p, b->vel, events);
                bullet_end(b, index, events, &p);
            }
            break;
        case BULLET_M79:
        case BULLET_FLAME_ARROW:
        case BULLET_LAW:
            explode(ctx, w, b, index, EXPLOSION_M79, -1, -1, events);
            bullet_end(b, index, events, NULL);
            break;
        case BULLET_CLUSTER_NADE:
            cluster_split(ctx, w, b, index, events);
            bullet_end(b, index, events, NULL);
            break;
        case BULLET_CLUSTER:
            explode(ctx, w, b, index, EXPLOSION_CLUSTER, -1, -1, events);
            bullet_end(b, index, events, NULL);
            break;
        }
        *hit = p;
        return true;
    }
    return false;
}

// --- the soldiers ------------------------------------------------------------------

typedef struct Candidates {
    int index[MAX_PLAYERS];
    int count;
} Candidates;

// TargetableSprite and FilterSpritesByDistance: who the bullet may hit, nearest first.
// The corpses are among them, so long as the body has been started (a soldier the
// server has just killed has none for a tick).
static Candidates candidates(const World *w, const Soldier *frame, const Bullet *b)
{
    int owner_vulnerable_after;
    switch (b->style) {
    case BULLET_FRAG_GRENADE: owner_vulnerable_after = GRENADE_TIMEOUT - 50; break;
    case BULLET_M2: owner_vulnerable_after = M2BULLET_TIMEOUT - 20; break;
    case BULLET_FLAME: owner_vulnerable_after = FLAMER_TIMEOUT; break;
    default: owner_vulnerable_after = BULLET_TIMEOUT - 20; break;
    }

    Candidates c = {0};
    float dists[MAX_PLAYERS];
    for (int i = 0; i < MAX_PLAYERS; i++) {
        const Soldier *s = i == b->owner ? &w->soldiers[i] : &frame[i];
        if (!s->active || s->team == TEAM_SPECTATOR || i == b->hit_body) continue;
        if (i == b->owner && b->timeout >= owner_vulnerable_after) continue;
        if (w->soldiers[i].dead && !w->ragdolls[i].active) continue;

        Vec2 d = vec2_sub(b->pos, s->pos);
        float dist = d.x * d.x + d.y * d.y;
        int j = c.count;
        while (j > 0 && dist < dists[j - 1]) {
            dists[j] = dists[j - 1];
            c.index[j] = c.index[j - 1];
            j--;
        }
        dists[j] = dist;
        c.index[j] = i;
        c.count++;
    }
    return c;
}

// The wound of a hit on `part` of `pose` at `point`. Its impact, the blow the gun of a
// soldier it kills is thrown with, is the point away from the part, a little more, and
// upside down (the original's Norm).
static void wound(Events *events, const Bullet *b, int target, float amount, const Pose *pose, int part, Vec2 point, Vec2 push, bool spray)
{
    Vec2 impact = vec2_scale(vec2_sub(point, pose->p[part]), 1.3f);
    impact.y = -impact.y;
    event_emit(events, (Event){
        .type = EVENT_HIT,
        .hit = {.shooter = b->owner, .target = (uint8_t)target, .weapon = b->weapon, .amount = amount,
                .part = (uint8_t)(part + 1), .pos = point, .push = push, .impact = impact, .spray = spray},
    });
}

static void blood(Events *events, const Bullet *b, int target, Vec2 point)
{
    event_emit(events, (Event){.type = EVENT_BLOOD, .blood = {.shooter = b->owner, .target = (uint8_t)target, .pos = point, .vel = b->vel}});
}

// The bullet's path against each candidate's pose points. Returns whether it met one,
// and where (the last point met, as the original leaves it).
static bool body_collide(const Context *ctx, World *w, Bullet *b, uint16_t index, float nearest, Events *events, Vec2 *hit_point)
{
    if (b->style == BULLET_ARROW && b->timeout <= ARROW_RESIST) return false;
    if (b->style == BULLET_CLUSTER_NADE) return false;

    const WeaponStats *stats = &ctx->weapons.info[b->weapon].stats;
    const Soldier *owner = &w->soldiers[b->owner];
    Soldier *frame = history_targets(w, b->lag); // as the shooter saw them
    bool melee = b->style == BULLET_PUNCH || b->style == BULLET_KNIFE;
    float radius = b->style == BULLET_FRAG_GRENADE ? PART_RADIUS + 1.0f : PART_RADIUS;
    Candidates c = candidates(w, frame, b);
    bool hit = false;

    for (int n = 0; n < c.count; n++) {
        int ti = c.index[n];
        const Soldier *target = target_soldier(w, frame, b->owner, ti);
        Soldier *live = &w->soldiers[ti]; // the one the wound and the shove land on
        if (melee && ti == b->owner) continue;

        Vec2 start, end;
        if (melee) {
            Pose owner_pose = soldier_pose(ctx->anims, owner, owner->pos);
            start = vec2_add(owner_pose.p[14], vec2_scale(hands_aim_direction(&owner_pose), 4.0f));
        } else {
            start = b->pos;
        }
        end = vec2_add(b->pos, b->vel);

        // A corpse is met where its body lies this tick, not where it was `b->lag` ticks
        // ago: it moves slowly, and no history is kept of it.
        bool corpse = live->dead;
        Pose pose = corpse ? ragdoll_pose(&w->ragdolls[ti]) : soldier_pose(ctx->anims, target, target->pos);

        // The part is the one met nearest the start; the point is the last one met, in
        // priority order, which is what the original's variable holds when it is done.
        int part = -1;
        Vec2 point = {0};
        float best = FLT_MAX;
        for (int k = 0; k < HIT_PART_COUNT; k++) {
            Vec2 center = pose.p[HIT_PARTS[k]];
            if (!melee) center.x -= 2.0f; // the sprites sit two pixels off
            Vec2 q;
            if (!line_circle_collision(start, end, center, radius, &q)) continue;
            point = q;
            Vec2 d = vec2_sub(q, start);
            float dist = d.x * d.x + d.y * d.y;
            if (dist < best) {
                best = dist;
                part = HIT_PARTS[k];
            }
        }
        if (part < 0) continue;
        if (nearest > -1.0f && vec2_length(vec2_sub(point, b->old_pos)) > nearest) break; // a nearer hit wins
        *hit_point = point;
        hit = true;
        if (target->cease_fire_counter >= 0) continue; // spawn protection: it passes through

        Vec2 push = {0};
        if (!corpse && b->style != BULLET_FRAG_GRENADE && b->style != BULLET_FLAME && b->style != BULLET_ARROW) {
            push = vec2_scale(b->vel, stats->push);
        }
        float modifier = hitbox_modifier(stats, part);

        switch (b->style) {
        case BULLET_PLAIN:
        case BULLET_SHOTGUN:
        case BULLET_PUNCH:
        case BULLET_KNIFE:
        case BULLET_M2: {
            b->pos = point;
            blood(events, b, ti, point);
            float speed = vec2_length(b->vel);
            Hit h = {.shooter = b->owner, .target = (uint8_t)ti, .amount = speed * b->hit_multiply * modifier};
            bool kills = !corpse && live->health - hit_damage(w, h) < 1.0f;
            wound(events, b, ti, h.amount, &pose, part, point, push, true);

            // a punched enemy starts throwing its gun away
            if (b->style == BULLET_PUNCH && (live->team == TEAM_NONE || live->team != owner->team) &&
                live->weapon.id != WEAPON_BOW && live->weapon.id != WEAPON_BOW2) {
                anim_apply(ctx->anims, &live->body, ANIM_THROW_WEAPON, 11);
            }
            b->hit_body = (int8_t)ti;

            // through a corpse barely slowed; through the dead it made or anyone when
            // fast; through anyone when still near its full speed
            if (corpse) {
                b->vel = vec2_scale(b->vel, 0.9f);
                continue;
            }
            if (kills || speed > 23.0f) {
                b->vel = vec2_scale(b->vel, 0.75f);
                continue;
            }
            if (speed > 5.0f && speed / stats->speed >= 0.9f) {
                b->vel = vec2_scale(b->vel, 0.66f);
                continue;
            }
            bullet_end(b, index, events, &point);
            return true;
        }
        case BULLET_FRAG_GRENADE:
            if (corpse) return true; // grenades roll over corpses, and look no further
            explode(ctx, w, b, index, EXPLOSION_FRAG, ti, part, events);
            bullet_end(b, index, events, NULL);
            return true;
        case BULLET_ARROW: {
            b->pos = vec2_sub(point, b->vel);
            b->forces.y -= bullet_gravity(w);
            bool friendly = !w->rules.friendly_fire && owner->team != TEAM_NONE && owner->team == live->team;
            if (!friendly && live->bonus != BONUS_FLAME_GOD) blood(events, b, ti, point);
            wound(events, b, ti, vec2_length(b->vel) * b->hit_multiply * modifier, &pose, part, point, push, false);
            bullet_end(b, index, events, &point);
            return true;
        }
        case BULLET_M79:
        case BULLET_FLAME_ARROW:
        case BULLET_LAW:
            if (corpse) return true; // rockets fly over corpses, and look no further
            explode(ctx, w, b, index, EXPLOSION_M79, ti, part, events);
            b->pos = point;
            bullet_end(b, index, events, NULL);
            wound(events, b, ti, vec2_length(b->vel) * b->hit_multiply, &pose, part, point, push, false);
            return true;
        case BULLET_FLAME: {
            if (ti == b->owner) return true;
            // it clings to the one it burns, and spreads from it
            b->pos = pose.p[part];
            b->vel = corpse ? (Vec2){0} : target->vel;
            if (b->timeout < 3 && b->ricochet_count < 2) {
                if (b->hit_multiply >= ctx->weapons.info[WEAPON_FLAMER].stats.damage / 3.0f) {
                    b->timeout = FLAMER_TIMEOUT - 1;
                    b->ricochet_count++;
                    Vec2 away = vec2_scale(target->vel, -1.0f);
                    // a flame starts a step further out than it is aimed from
                    spawn_child(ctx, w, b, vec2_add(pose.p[part], away), away, WEAPON_FLAMER, 2.0f * b->hit_multiply / 3.0f, events);
                }
                if (live->health > -1.0f) wound(events, b, ti, b->hit_multiply, &pose, part, point, (Vec2){0}, false);
            }
            return true;
        }
        case BULLET_CLUSTER:
            explode(ctx, w, b, index, EXPLOSION_CLUSTER, ti, part, events);
            if (!vec2_is_zero(push)) wound(events, b, ti, 0.0f, &pose, part, point, push, false); // the shove of the hit itself
            bullet_end(b, index, events, NULL);
            return true;
        case BULLET_THROWN_KNIFE:
            wound(events, b, ti, vec2_length(b->vel) * b->hit_multiply * 0.01f, &pose, part, point, push, false);
            if (corpse) return true; // it goes through a corpse rather than sticking in it
            knife_land(b, events);
            bullet_end(b, index, events, &point);
            return true;
        case BULLET_CLUSTER_NADE:
            return true;
        }
    }
    return hit;
}

// --- the things --------------------------------------------------------------------

// Bullets knock flags (and guns and kits, as the match says) about; the bullet keeps
// flying.
static void thing_collide(const Context *ctx, World *w, Bullet *b, float nearest, Events *events)
{
    if (b->style == BULLET_FRAG_GRENADE) return;
    if (b->timeout >= BULLET_TIMEOUT - 1) return; // not on the tick it was fired (whatever its kind)

    for (int ti = 0; ti < MAX_THINGS; ti++) {
        Thing *t = &w->things[ti];
        if (t->style == THING_NONE || t->style == THING_STAT_GUN || !thing_collides_with_bullets(w, t)) continue;
        if (t->holder == b->owner + 1) continue; // not the flag you carry

        int part = -1;
        Vec2 point;
        for (int k = 0; k < 2; k++) {
            if (line_circle_collision(b->pos, vec2_add(b->pos, b->vel), t->pos[k], FLAG_PART_RADIUS, &point)) {
                part = k;
                break;
            }
        }
        if (part < 0) continue;
        if (nearest > -1.0f && vec2_length(vec2_sub(point, b->old_pos)) > nearest) return;

        // the original stops looking while this bullet is cooling down on this thing
        int slot = 0;
        for (int i = 0; i < 4; i++) {
            const ThingCooldown *cd = &b->thing_cooldowns[i];
            if (cd->thing == ti + 1 && w->tick < cd->until) return;
            if (cd->until < b->thing_cooldowns[slot].until) slot = i;
        }
        b->thing_cooldowns[slot] = (ThingCooldown){.thing = (uint8_t)(ti + 1), .until = w->tick + THING_COLLISION_COOLDOWN};

        Vec2 thing_vel = vec2_sub(t->pos[part], t->old_pos[part]);
        float push = ctx->weapons.info[b->weapon].stats.push * THING_PUSH_MULTIPLIER;
        t->pos[part] = vec2_add(t->pos[part], vec2_scale(vec2_sub(b->vel, thing_vel), push));
        t->is_static = false;
        if (b->style == BULLET_PLAIN || b->style == BULLET_SHOTGUN) {
            event_emit(events, (Event){.type = EVENT_THING_HIT, .thing_hit = {.thing = t->style, .pos = point, .vel = b->vel, .part = (uint8_t)part}});
        }
        return;
    }
}

// --- the tick ----------------------------------------------------------------------

void bullet_collide(const Context *ctx, World *w, Bullet *b, uint16_t index, Events *events)
{
    Vec2 saved_vel = b->vel, saved_pos = b->pos, saved_old = b->old_pos;
    float nearest = -1.0f; // the distance to what stopped it so far

    if (b->style == BULLET_FRAG_GRENADE) map_collide(ctx, w, b, index, vec2(b->pos.x, b->pos.y - 2.0f), events);
    Vec2 wall = map_collide(ctx, w, b, index, b->pos, events);
    if (!b->active) {
        nearest = vec2_length(vec2_sub(wall, saved_old));
        b->vel = saved_vel;
        b->pos = saved_pos;
        b->old_pos = saved_old;
        b->ricochet_count--;
    }

    Vec2 collider;
    bool hit_collider = collider_collide(ctx, w, b, index, nearest, events, &collider);
    if (!b->active) {
        nearest = vec2_length(vec2_sub(hit_collider ? collider : wall, saved_old));
        b->vel = saved_vel;
        b->pos = saved_pos;
        b->old_pos = saved_old;
    }

    Vec2 body;
    bool hit_body = body_collide(ctx, w, b, index, nearest, events, &body);
    if (!b->active) {
        Vec2 stop = hit_body ? body : hit_collider ? collider : wall;
        nearest = vec2_length(vec2_sub(stop, saved_old));
    }

    thing_collide(ctx, w, b, nearest, events);
}
