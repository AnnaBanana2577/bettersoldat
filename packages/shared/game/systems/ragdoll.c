// Corpses: a dead soldier's gostek skeleton run as a Verlet particle system. Ported from
// the dead-soldier branches of OpenSoldat Sprites.pas (TSprite.Update, UpdatePose, Die,
// CheckSkeletonMapCollision) and Parts.pas by way of soldat-odin.
//
// A corpse starts from the soldier's pose where it died, moving as it moved, falls,
// meets the map and comes to rest; a bad death cuts constraints so the body comes
// apart. Nothing about a corpse crosses the wire: it follows from the soldier's state
// (where and how fast it died, how far below zero its health went, where it was last
// hit, whether a berserker did it), so the server and every client run the same body
// from the same word, and bullets meet it on the server as they do anywhere.
//
// One approximation: the original's body keeps moving from the last two poses of the
// living skeleton; here the pose before is the pose at death stepped back by the
// soldier's velocity, as the skeleton's state is not kept while it lives.

#include "game/systems/systems.h"

#define RAGDOLL_HEAD 11 // skeleton point 12: where the dead soldier is
#define RAGDOLL_DAMPING 0.9945f
#define RAGDOLL_GRAVITY 1.06f
#define PARA_CORPSE_LIFT (25.0f * -0.5f * 0.06f) // a parachute holds a body up by the head

// The constraints the deaths cut (0-based, gostek.po order): the neck, the legs at the
// hip, the upper arms.
#define CONSTRAINT_LEFT_LEG 1
#define CONSTRAINT_RIGHT_LEG 3
#define CONSTRAINT_NECK 19
#define CONSTRAINT_LEFT_ARM 20
#define CONSTRAINT_RIGHT_ARM 22

// The points that never meet the map: the arms and the hand's extras (the original's
// 7, 8 and 17 to 20).
static bool collides(int point) { return point != 6 && point != 7 && (point < 16 || point >= POSE_POINTS); }

// The body as it died: the pose at the death, the pose a tick before it.
static void ragdoll_start(const Context *ctx, World *w, uint8_t index)
{
    const Soldier *s = &w->soldiers[index];
    Ragdoll *r = &w->ragdolls[index];
    *r = (Ragdoll){.active = true};
    Pose now = soldier_pose(ctx->anims, s, s->death_pos);
    Pose before = soldier_pose(ctx->anims, s, vec2_sub(s->death_pos, s->death_vel));
    for (int i = 0; i < POSE_POINTS; i++) {
        r->pos[i] = now.p[i];
        r->old_pos[i] = before.p[i];
    }
    // the extra points hang off the neck and the head
    r->pos[20] = r->pos[21] = r->old_pos[20] = r->old_pos[21] = now.p[8];
    r->pos[22] = r->pos[23] = r->old_pos[22] = r->old_pos[23] = now.p[11];
}

// Die's cuts: by how far below zero the health went, where the body was last hit, and
// whether a berserker did it. The original cuts on every wound that leaves the body
// below 1, so this reads the state every tick: cuts only ever add.
static void ragdoll_tear(Ragdoll *r, const Soldier *s)
{
    uint32_t cuts = 0;
    uint32_t brutal = 1u << CONSTRAINT_LEFT_LEG | 1u << CONSTRAINT_RIGHT_LEG | 1u << CONSTRAINT_NECK |
                      1u << CONSTRAINT_LEFT_ARM | 1u << CONSTRAINT_RIGHT_ARM;
    if (s->health <= BRUTAL_DEATH_HEALTH || s->torn_apart) {
        cuts = brutal;
    } else if (s->health <= HEADCHOP_DEATH_HEALTH) {
        // the 1-based skeleton point hit: the head comes off, or a leg at the hip
        if (s->death_part == 12) cuts = 1u << CONSTRAINT_NECK;
        if (s->death_part == 3) cuts = 1u << CONSTRAINT_LEFT_LEG;
        if (s->death_part == 4) cuts = 1u << CONSTRAINT_RIGHT_LEG;
    }
    r->torn |= cuts;
}

// CheckSkeletonMapCollision: a point inside a poly goes back to where it was, less the
// push-out. Where it met one, a second look a little below, past the team and flagger
// polys, settles it.
static bool ragdoll_collide(const Context *ctx, World *w, uint8_t index, int i, Events *events)
{
    const Map *map = ctx->map;
    Soldier *s = &w->soldiers[index];
    Ragdoll *r = &w->ragdolls[index];
    Vec2 at = r->pos[i]; // what both looks are measured from, as the original passes it in
    bool hit = false;

    for (int pass = 0; pass < 2 && (pass == 0 || hit); pass++) {
        Vec2 probe = pass == 0 ? vec2(at.x - 1.0f, at.y + 4.0f) : vec2(at.x, at.y + 1.0f);
        int n = map->sectors_num;
        int sx = round_half_even(probe.x / (float)map->sectors_division);
        int sy = round_half_even(probe.y / (float)map->sectors_division);
        if (!(sx > -n && sx < n && sy > -n && sy < n)) continue;

        bg_test_big_poly_center(map, &s->bg, probe);
        PolySector sector = map_sector_at(map, sx, sy);
        for (int k = 0; k < sector.count; k++) {
            uint16_t idx = sector.polys[k];
            const Polygon *poly = &map->polys[idx];
            PolyType type = (PolyType)poly->type;
            bool solid = pass == 0 ? soldier_collides_with(s, type) : type != POLY_DOESNT && type != POLY_ONLY_BULLETS;
            if (!solid || !point_in_poly_edges(probe, poly)) continue;
            if (bg_test(map, &s->bg, idx)) continue;

            float dist;
            Vec2 normal = closest_perpendicular(poly, probe, &dist, NULL);
            r->pos[i] = vec2_sub(r->old_pos[i], vec2_scale(vec2_normalize(normal), dist));
            if (pass == 0) {
                // the thud of a body landing, for whoever is listening; the count quiets it
                float fall = fabsf(r->pos[i].y - r->old_pos[i].y);
                if (fall > 0.8f && r->hits < 13) {
                    event_emit(events, (Event){.type = EVENT_CORPSE_HIT, .corpse_hit = {.target = index, .pos = r->pos[i], .fall = fall, .count = r->hits}});
                }
                if (r->hits < 255) r->hits++;
            }
            hit = true;
        }
    }
    return hit;
}

// Verlet with the body's damping and gravity, then a pass over the constraints that
// still hold.
static void ragdoll_integrate(const Context *ctx, World *w, Ragdoll *r)
{
    for (int i = 0; i < RAGDOLL_POINTS; i++) {
        r->forces[i].y += RAGDOLL_GRAVITY * w->gravity;
        Vec2 p = r->pos[i];
        r->pos[i] = vec2_add(vec2_sub(vec2_scale(p, 1.0f + RAGDOLL_DAMPING), vec2_scale(r->old_pos[i], RAGDOLL_DAMPING)), r->forces[i]);
        r->old_pos[i] = p;
        r->forces[i] = (Vec2){0};
    }

    const ParticleObject *skeleton = &ctx->skeletons->gostek;
    for (int c = 0; c < skeleton->constraint_count; c++) {
        if (r->torn >> c & 1) continue;
        int a = skeleton->constraints[c][0], b = skeleton->constraints[c][1];
        float rest = vec2_length(vec2_sub(skeleton->points[b], skeleton->points[a]));
        Vec2 d = vec2_sub(r->pos[b], r->pos[a]);
        float length = sqrtf(vec2_dot(d, d));
        float diff = length != 0.0f ? (length - rest) / length : 0.0f;
        r->pos[a] = vec2_add(r->pos[a], vec2_scale(d, 0.5f * diff));
        r->pos[b] = vec2_sub(r->pos[b], vec2_scale(d, 0.5f * diff));
    }
}

// One tick of one corpse, in the dead branch's order.
static void ragdoll_step(const Context *ctx, World *w, uint8_t index, Events *events)
{
    Soldier *s = &w->soldiers[index];
    Ragdoll *r = &w->ragdolls[index];

    // The dead soldier's own particle still flies (its place is the head's, below, but
    // its speed is what a parachute on the body feels), and last tick's shove lands.
    soldier_integrate(s, w->gravity);
    s->vel = vec2_add(s->vel, s->next_push);
    s->next_push = (Vec2){0};

    // UpdatePose, dead: the extra points go back to the neck and the head
    r->old_pos[20] = r->pos[20];
    r->pos[20] = r->pos[8];
    r->old_pos[22] = r->pos[22];
    r->pos[22] = r->pos[11];

    bg_test_prepare(&s->bg);
    for (int i = 0; i < POSE_POINTS; i++) {
        if (collides(i)) r->on_ground = ragdoll_collide(ctx, w, index, i, events);
    }
    bg_test_reset(&s->bg);

    ragdoll_integrate(ctx, w, r);
    s->old_pos = s->pos;
    s->pos = r->pos[RAGDOLL_HEAD];

    // a body under a parachute is held up by it until it lands (the parachute lets go
    // of a landed body in the things pass)
    if (s->held && w->things[s->held - 1].style == THING_PARACHUTE) r->forces[RAGDOLL_HEAD].y = PARA_CORPSE_LIFT;
    r->dead_time++;

    s->vel.x = clampf(s->vel.x, -MAX_VELOCITY, MAX_VELOCITY);
    s->vel.y = clampf(s->vel.y, -MAX_VELOCITY, MAX_VELOCITY);
}

void ragdolls_update(const Context *ctx, World *w, Events *events)
{
    for (int i = 0; i < MAX_PLAYERS; i++) {
        const Soldier *s = &w->soldiers[i];
        Ragdoll *r = &w->ragdolls[i];
        if (!s->active || !s->dead || s->team == TEAM_SPECTATOR) {
            r->active = false;
            continue;
        }
        if (!r->active) ragdoll_start(ctx, w, (uint8_t)i);
        ragdoll_tear(r, s);
        ragdoll_step(ctx, w, (uint8_t)i, events);
    }
}

bool ragdoll_out_of_bounds(const Context *ctx, const Ragdoll *r)
{
    float bound = (float)(ctx->map->sectors_num * ctx->map->sectors_division - 50);
    for (int i = 0; i < POSE_POINTS; i++)
        if (fabsf(r->pos[i].x) > bound || fabsf(r->pos[i].y) > bound) return true;
    return false;
}

Pose ragdoll_pose(const Ragdoll *r)
{
    Pose pose;
    for (int k = 0; k < POSE_POINTS; k++) pose.p[k] = r->pos[k];
    return pose;
}
