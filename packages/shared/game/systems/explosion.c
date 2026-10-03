// A grenade, rocket or cluster going off: a Hit on every living soldier in the radius
// (the caller wounds), the corpses and things thrown about, nearby grenades and rockets
// set off. Ported from OpenSoldat Bullets.pas (TBullet.ExplosionHit) by way of
// soldat-odin.
//
// Every explosion sets off its neighbours, the cluster's and the M2's flak included.
// The original means to spare those two, but writes the test as `not Typ in [...]`,
// which Pascal reads as `(not Typ) in [...]` and which is never true; this keeps what
// the game does, not what the line meant.

#include <float.h>

#include "game/systems/systems.h"

#define M79_EXPLOSION_RADIUS 64.0f
#define FRAG_EXPLOSION_RADIUS 85.0f
#define CLUSTER_EXPLOSION_RADIUS 35.0f
#define AFTER_EXPLOSION_RADIUS 50.0f // grenades and rockets this near go off too
#define EXPLOSION_IMPACT_MULTIPLY 3.75f
#define EXPLOSION_DEADIMPACT_MULTIPLY 4.5f
#define CORPSE_BLAST_POINTS 16 // the skeleton points a blast throws about

// The living: the pose point nearest the blast (or the one struck directly) takes a
// wound falling off with distance, and the soldier is thrown from it. Spawn protection
// spares the wound and not the throw.
static void blast_soldier(const Context *ctx, World *w, const Bullet *b, int i, const Soldier *s, ExplosionKind kind,
                          int hit_soldier, int hit_part, Events *events)
{
    const WeaponStats *stats = &ctx->weapons.info[kind == EXPLOSION_M79 ? WEAPON_M79 : WEAPON_FRAG].stats;
    float radius = kind == EXPLOSION_FRAG ? FRAG_EXPLOSION_RADIUS : kind == EXPLOSION_M79 ? M79_EXPLOSION_RADIUS : CLUSTER_EXPLOSION_RADIUS;
    Pose pose = soldier_pose(ctx->anims, s, s->pos);

    int part = hit_part;
    if (i != hit_soldier || hit_part < 0) {
        float best = FLT_MAX;
        for (int k = 0; k < HIT_PART_COUNT; k++) {
            Vec2 d = vec2_sub(b->pos, pose.p[HIT_PARTS[k]]);
            float dist = vec2_dot(d, d);
            if (dist < best) {
                best = dist;
                part = HIT_PARTS[k];
            }
        }
    }
    float modifier = hitbox_modifier(stats, part);

    Vec2 a = vec2_sub(b->pos, pose.p[part]);
    float dist2 = vec2_dot(a, a);
    if (dist2 >= radius * radius) return;

    float dist = sqrtf(dist2);
    a = vec2_scale(vec2_scale(a, 1.0f / (dist + 1.0f)), EXPLOSION_IMPACT_MULTIPLY);
    if (kind == EXPLOSION_CLUSTER) modifier *= 0.5f;
    else a.y *= 2.0f;

    float amount = s->cease_fire_counter < 0 ? (1.0f / (dist + 1.0f)) * stats->damage * modifier : 0.0f;
    event_emit(events, (Event){
        .type = EVENT_HIT,
        .hit = {.shooter = b->owner, .target = (uint8_t)i, .weapon = b->weapon, .amount = amount, .part = 0,
                .pos = pose.p[part], .push = vec2_scale(a, -1.0f), .impact = a, .spray = true},
    });
}

// The dead: every point in reach is thrown, and the body takes a wound by the last of
// them, which is what tears it apart.
static void blast_corpse(const Context *ctx, World *w, const Bullet *b, int i, ExplosionKind kind, Events *events)
{
    Ragdoll *r = &w->ragdolls[i];
    if (!r->active) return;
    const WeaponStats *stats = &ctx->weapons.info[kind == EXPLOSION_M79 ? WEAPON_M79 : WEAPON_FRAG].stats;
    float radius = kind == EXPLOSION_FRAG ? FRAG_EXPLOSION_RADIUS : kind == EXPLOSION_M79 ? M79_EXPLOSION_RADIUS : CLUSTER_EXPLOSION_RADIUS;

    bool reached = false;
    float last = 0.0f;
    for (int k = 0; k < CORPSE_BLAST_POINTS; k++) {
        Vec2 a = vec2_sub(b->pos, r->pos[k]);
        float dist2 = vec2_dot(a, a);
        if (dist2 >= radius * radius) continue;
        float dist = sqrtf(dist2);
        r->old_pos[k] = vec2_add(r->old_pos[k], vec2_scale(a, (1.0f / (dist + 1.0f)) * EXPLOSION_DEADIMPACT_MULTIPLY));
        reached = true;
        last = dist;
    }
    if (!reached) return;

    float modifier = 1.0f;
    if (kind == EXPLOSION_M79) last = maxf(last, 20.0000001f);
    else if (kind == EXPLOSION_CLUSTER) modifier = 0.5f;
    event_emit(events, (Event){
        .type = EVENT_HIT,
        .hit = {.shooter = b->owner, .target = (uint8_t)i, .weapon = b->weapon, .amount = (1.0f / (last + 1.0f)) * stats->damage * modifier,
                .part = 0, .pos = b->pos},
    });
}

void explode(const Context *ctx, World *w, Bullet *b, uint16_t index, ExplosionKind kind, int hit_soldier, int hit_part, Events *events)
{
    float radius = kind == EXPLOSION_FRAG ? FRAG_EXPLOSION_RADIUS : kind == EXPLOSION_M79 ? M79_EXPLOSION_RADIUS : CLUSTER_EXPLOSION_RADIUS;
    shot_end_tell(w, b, b->pos, (uint8_t)(kind + 1), events); // where it went off, for the clients' own flights of it
    event_emit(events, (Event){
        .type = EVENT_EXPLOSION,
        .explosion = {.id = index, .player = b->owner, .weapon = kind == EXPLOSION_M79 ? WEAPON_M79 : WEAPON_FRAG, .pos = b->pos, .radius = radius},
    });

    for (int i = 0; i < MAX_PLAYERS; i++) {
        const Soldier *s = &w->soldiers[i];
        if (!s->active || s->team == TEAM_SPECTATOR) continue;
        if (s->dead) blast_corpse(ctx, w, b, i, kind, events);
        else blast_soldier(ctx, w, b, i, bullet_target(w, b, i), kind, hit_soldier, hit_part, events); // as the thrower saw it
    }

    // the blast shoves the things it may: every point in range gets its previous
    // position pulled back, which the Verlet step turns into a kick away from it
    for (int i = 0; i < MAX_THINGS; i++) {
        Thing *t = &w->things[i];
        if (t->style == THING_NONE || !thing_collides_with_bullets(w, t)) continue;
        for (int k = 0; k < t->points; k++) {
            Vec2 a = vec2_sub(b->pos, t->pos[k]);
            float dist2 = vec2_dot(a, a);
            if (dist2 >= radius * radius) continue;
            t->old_pos[k] = vec2_add(t->old_pos[k], vec2_scale(a, 0.5f * (1.0f / (sqrtf(dist2) + 1.0f)) * EXPLOSION_IMPACT_MULTIPLY));
            t->is_static = false;
        }
    }

    // and sets off the grenades and rockets near it
    bullet_end(b, index, events, NULL);
    for (int i = 0; i < MAX_BULLETS; i++) {
        Bullet *other = &w->bullets[i];
        if (!other->active) continue;
        if (other->style != BULLET_FRAG_GRENADE && other->style != BULLET_M79 && other->style != BULLET_LAW) continue;
        Vec2 a = vec2_sub(b->pos, other->pos);
        if (vec2_dot(a, a) >= AFTER_EXPLOSION_RADIUS * AFTER_EXPLOSION_RADIUS) continue;
        explode(ctx, w, other, (uint16_t)i, other->style == BULLET_FRAG_GRENADE ? EXPLOSION_FRAG : EXPLOSION_M79, -1, -1, events);
    }
}
