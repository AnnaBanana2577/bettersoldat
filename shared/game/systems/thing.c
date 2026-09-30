// The thing pool and what every thing shares: a small Verlet skeleton (two points for a
// gun, four for a flag, kit, parachute or stationary gun), its physics against the map,
// a holder, an owner, a timeout, respawning, the pickups. What each kind of thing means
// lives in its own file: flag, kit, dropped_gun, parachute, stat_gun. Ported from
// OpenSoldat Things.pas (CreateThing, TThing.Update, CheckMapCollision, Respawn,
// CheckSpriteCollision, SpawnBoxes, RandomizeStart) and Server.pas's round start.
//
// The things are decided where the world has authority: that world makes them, takes
// them up and puts them back. Every world moves them.

#include "game/systems/systems.h"

#define MIN_MOVE_DELTA 0.63f // below this average movement a grounded thing goes static
#define FLAG_STAND_FORCEUP (-16.0f)
#define FLAG_HOLDING_FORCEUP (-14.0f)
#define PARACHUTE_TIMEOUT 3600
#define STAT_GUN_WARMUP 60 // ticks a new stationary gun settles before it can be manned

#define FLAG_RADIUS 19.0f
#define GUN_RADIUS 10.0f
#define BOW_RADIUS 20.0f
#define KIT_RADIUS 12.0f

bool thing_is_flag(ThingStyle style) { return style == THING_ALPHA_FLAG || style == THING_BRAVO_FLAG; }

bool thing_is_kit(ThingStyle style)
{
    switch (style) {
    case THING_MEDICAL_KIT:
    case THING_GRENADE_KIT:
    case THING_FLAMER_KIT:
    case THING_PREDATOR_KIT:
    case THING_VEST_KIT:
    case THING_BERSERK_KIT:
    case THING_CLUSTER_KIT:
        return true;
    default:
        return false;
    }
}

static bool is_bow(const Thing *t) { return t->style == THING_WEAPON && (t->weapon == WEAPON_BOW || t->weapon == WEAPON_BOW2); }

// --- what each kind is made of (CreateThing's cases) --------------------------------

typedef struct GunBody {
    float scale; // which karabin.po
    float damping, gravity;
} GunBody;

static const GunBody GUN_BODIES[WEAPON_COUNT] = {
    [WEAPON_COLT] = {1.0f, 0.994f, 1.05f},    [WEAPON_EAGLE] = {1.1f, 0.996f, 1.09f},   [WEAPON_MP5] = {2.2f, 0.995f, 1.11f},
    [WEAPON_AK74] = {3.7f, 0.994f, 1.16f},    [WEAPON_STEYR] = {3.7f, 0.994f, 1.16f},   [WEAPON_SPAS] = {3.6f, 0.993f, 1.15f},
    [WEAPON_RUGER] = {3.6f, 0.993f, 1.13f},   [WEAPON_M79] = {2.8f, 0.994f, 1.15f},     [WEAPON_BARRETT] = {4.3f, 0.993f, 1.18f},
    [WEAPON_M249] = {3.9f, 0.993f, 1.2f},     [WEAPON_MINIGUN] = {5.5f, 0.991f, 1.4f},  [WEAPON_KNIFE] = {1.8f, 0.994f, 1.15f},
    [WEAPON_CHAINSAW] = {2.8f, 0.994f, 1.15f}, [WEAPON_LAW] = {2.8f, 0.994f, 1.15f},    [WEAPON_BOW] = {5.0f, 0.996f, 0.65f},
    [WEAPON_BOW2] = {5.0f, 0.996f, 0.65f},
};

static const ParticleObject *thing_skeleton(const Context *ctx, const Thing *t)
{
    const Skeletons *sk = ctx->skeletons;
    switch (t->style) {
    case THING_ALPHA_FLAG:
    case THING_BRAVO_FLAG: return &sk->flag;
    case THING_PARACHUTE: return &sk->para;
    case THING_STAT_GUN: return &sk->stat;
    case THING_WEAPON:
        for (int i = 0; i < GUN_SCALE_COUNT; i++)
            if (GUN_SCALES[i] == GUN_BODIES[t->weapon].scale) return &sk->rifles[i];
        return &sk->rifles[0];
    default: return &sk->kit;
    }
}

static void thing_physics_of(const Thing *t, float *damping, float *gravity)
{
    switch (t->style) {
    case THING_ALPHA_FLAG:
    case THING_BRAVO_FLAG: *damping = 0.991f, *gravity = 1.0f; return;
    case THING_WEAPON: *damping = GUN_BODIES[t->weapon].damping, *gravity = GUN_BODIES[t->weapon].gravity; return;
    case THING_MEDICAL_KIT: *damping = 0.989f, *gravity = 1.05f; return;
    case THING_GRENADE_KIT:
    case THING_CLUSTER_KIT: *damping = 0.989f, *gravity = 1.07f; return;
    case THING_PARACHUTE: *damping = 0.993f, *gravity = 1.15f; return;
    case THING_STAT_GUN: *damping = 0.99f, *gravity = 0.2f; return;
    default: *damping = 0.989f, *gravity = 1.17f; return; // the bonus kits
    }
}

// How near a soldier must be to take it.
static float thing_radius(const Thing *t)
{
    switch (t->style) {
    case THING_ALPHA_FLAG:
    case THING_BRAVO_FLAG: return FLAG_RADIUS;
    case THING_WEAPON: return is_bow(t) ? BOW_RADIUS : t->weapon == WEAPON_KNIFE ? GUN_RADIUS * 1.5f : GUN_RADIUS;
    case THING_PARACHUTE: return 0.0f;
    case THING_STAT_GUN: return 0.0f; // stat_gun.c has its own
    default: return KIT_RADIUS;
    }
}

// --- the pool ----------------------------------------------------------------------

void thing_kill(Thing *t)
{
    uint8_t last_spawn = t->last_spawn;
    *t = (Thing){0};
    t->last_spawn = last_spawn;
}

int thing_create(const Context *ctx, World *w, ThingStyle style, Vec2 pos, WeaponId weapon, uint8_t owner, int slot)
{
    // a new flag replaces the old
    if (thing_is_flag(style)) {
        for (int i = 0; i < MAX_THINGS; i++)
            if (w->things[i].style == style) thing_kill(&w->things[i]);
    }
    if (slot < 0) {
        for (int i = 0; i < MAX_THINGS && slot < 0; i++)
            if (w->things[i].style == THING_NONE) slot = i;
        if (slot < 0) return -1;
    }

    Thing *t = &w->things[slot];
    thing_kill(t);
    t->style = style;
    t->weapon = weapon;
    t->owner = owner;
    t->in_base = thing_is_flag(style);
    t->bg = (BackgroundState){.status = BACKGROUND_TRANSITION, .poly = BACKGROUND_POLY_UNKNOWN};
    if (style == THING_WEAPON) t->ammo = ctx->weapons.info[weapon].stats.ammo;

    switch (style) {
    case THING_ALPHA_FLAG:
    case THING_BRAVO_FLAG:
        t->timeout = FLAG_TIMEOUT;
        t->interest = FLAG_INTEREST_TIME;
        break;
    case THING_WEAPON:
        t->timeout = is_bow(t) ? FLAG_TIMEOUT : GUN_RESIST_TIME;
        t->interest = is_bow(t) ? BOW_INTEREST_TIME : 0;
        break;
    case THING_MEDICAL_KIT:
        t->timeout = w->rules.respawn_time * GUN_RESIST_TIME; // never runs out: it is not among those that go
        t->interest = DEFAULT_INTEREST_TIME;
        break;
    case THING_PARACHUTE: t->timeout = PARACHUTE_TIMEOUT; break;
    case THING_STAT_GUN: t->timeout = STAT_GUN_WARMUP; break;
    default: // the grenade kit and the bonus kits
        t->timeout = FLAG_TIMEOUT;
        t->interest = DEFAULT_INTEREST_TIME;
        break;
    }

    const ParticleObject *skel = thing_skeleton(ctx, t);
    t->points = skel->point_count < 4 ? skel->point_count : 4;
    for (int k = 0; k < t->points; k++) t->pos[k] = skel->points[k];
    // the two flags face each other: alpha's cloth is on the other side of the pole
    if (style == THING_ALPHA_FLAG) t->pos[2].x = t->pos[3].x = 12.0f;
    // a knife lies blade first, never quite level
    if (style == THING_WEAPON && weapon == WEAPON_KNIFE) {
        Vec2 grip = t->pos[1];
        t->pos[1] = t->pos[0];
        t->pos[0] = grip;
        t->pos[0].x += (float)rand_int(&w->rng, 100) / 100.0f;
        t->pos[1].x -= (float)rand_int(&w->rng, 100) / 100.0f;
    }
    for (int k = 0; k < t->points; k++) {
        t->pos[k] = vec2_add(t->pos[k], pos);
        t->old_pos[k] = t->pos[k];
    }

    // A gun leaving a hand flies off with it: the hand's speed, the grip barely and the
    // muzzle along the aim, less so from the dead. (Not the knife: it lands, or is
    // thrown as a bullet.)
    bool thrown = style == THING_WEAPON && weapon != WEAPON_KNIFE;
    if (thrown && owner > 0) {
        const Soldier *s = &w->soldiers[owner - 1];
        Pose pose = soldier_pose(ctx->anims, s, s->pos);
        Vec2 aim = vec2_normalize(vec2_sub(s->aim, pose.p[14]));
        float grip = s->dead ? 0.02f : 0.01f, muzzle = s->dead ? 0.64f : 3.0f;
        t->pos[0] = vec2_add(vec2_add(t->pos[0], s->vel), vec2_scale(aim, grip));
        t->pos[1] = vec2_add(vec2_add(t->pos[1], s->vel), vec2_scale(aim, muzzle));
    }
    if (owner > 0) t->flip = w->soldiers[owner - 1].direction != 1;
    return slot;
}

// RandomizeStart: a random active spawn point of the kind, a few pixels off.
bool thing_spawn_point(const Map *m, int32_t kind, uint64_t *rng, Vec2 *pos)
{
    bool found = true;
    int count = 0;
    for (int i = 0; i < m->spawnpoint_count; i++)
        if (m->spawnpoints[i].active && m->spawnpoints[i].team == kind) count++;
    if (count == 0) {
        found = false;
        for (int i = 0; i < m->spawnpoint_count; i++)
            if (m->spawnpoints[i].active) count++;
    }
    *pos = (Vec2){0};
    if (count == 0) return found;

    int pick = rand_int(rng, count);
    for (int i = 0; i < m->spawnpoint_count; i++) {
        const Spawnpoint *s = &m->spawnpoints[i];
        if (!s->active || (found && s->team != kind)) continue;
        if (pick-- == 0) {
            *pos = vec2(s->pos.x - 4.0f + (float)rand_int(rng, 8), s->pos.y - 4.0f + (float)rand_int(rng, 4));
            break;
        }
    }
    return found;
}

// SpawnBoxes: RandomizeStart, but not the spawn point the slot last came up at, unless
// it is the only one of the kind.
bool thing_spawn_boxes(const Map *m, int32_t kind, Thing *t, uint64_t *rng, Vec2 *pos)
{
    int spawns[MAX_SPAWNPOINTS], count = 0, previous = 0;
    bool found = true;
    for (int i = 0; i < m->spawnpoint_count && i < MAX_SPAWNPOINTS; i++) {
        if (!m->spawnpoints[i].active || m->spawnpoints[i].team != kind) continue;
        if (t->last_spawn != i + 1) spawns[count++] = i;
        else previous = i + 1;
    }
    if (count == 0) {
        if (previous) {
            spawns[count++] = previous - 1;
        } else {
            found = false;
            for (int i = 0; i < m->spawnpoint_count && i < MAX_SPAWNPOINTS; i++)
                if (m->spawnpoints[i].active) spawns[count++] = i;
        }
    }
    *pos = (Vec2){0};
    if (count == 0) return found;

    int i = spawns[rand_int(rng, count)];
    *pos = vec2(m->spawnpoints[i].pos.x - 4.0f + (float)rand_int(rng, 8), m->spawnpoints[i].pos.y - 4.0f + (float)rand_int(rng, 4));
    t->last_spawn = (uint8_t)(i + 1);
    return found;
}

static int32_t spawn_kind(ThingStyle style)
{
    switch (style) {
    case THING_ALPHA_FLAG: return SPAWN_ALPHA_FLAG;
    case THING_BRAVO_FLAG: return SPAWN_BRAVO_FLAG;
    case THING_MEDICAL_KIT: return SPAWN_MEDICAL_KIT;
    case THING_GRENADE_KIT: return SPAWN_GRENADE_KIT;
    case THING_FLAMER_KIT: return SPAWN_FLAMER_KIT;
    case THING_PREDATOR_KIT: return SPAWN_PREDATOR_KIT;
    case THING_VEST_KIT: return SPAWN_VEST_KIT;
    case THING_BERSERK_KIT: return SPAWN_BERSERK_KIT;
    case THING_CLUSTER_KIT: return SPAWN_CLUSTER_KIT;
    case THING_WEAPON: return SPAWN_BOW;
    default: return 0;
    }
}

void thing_respawn(const Context *ctx, World *w, int index)
{
    Thing *t = &w->things[index];
    if (t->holder) w->soldiers[t->holder - 1].held = 0;
    ThingStyle style = t->style;
    WeaponId weapon = t->weapon;
    thing_kill(t);

    Vec2 pos;
    if (style == THING_MEDICAL_KIT || style == THING_GRENADE_KIT) thing_spawn_boxes(ctx->map, spawn_kind(style), t, &w->rng, &pos);
    else thing_spawn_point(ctx->map, spawn_kind(style), &w->rng, &pos);

    thing_create(ctx, w, style, pos, weapon, 0, index);
    t->timeout = FLAG_TIMEOUT;
    t->interest = thing_is_flag(style) ? FLAG_INTEREST_TIME : is_bow(t) ? BOW_INTEREST_TIME : DEFAULT_INTEREST_TIME;
}

void things_spawn(const Context *ctx, World *w)
{
    for (int i = 0; i < MAX_THINGS; i++) thing_kill(&w->things[i]);
    Vec2 pos;
    if (thing_spawn_point(ctx->map, SPAWN_ALPHA_FLAG, &w->rng, &pos)) thing_create(ctx, w, THING_ALPHA_FLAG, pos, WEAPON_NONE, 0, -1);
    if (thing_spawn_point(ctx->map, SPAWN_BRAVO_FLAG, &w->rng, &pos)) thing_create(ctx, w, THING_BRAVO_FLAG, pos, WEAPON_NONE, 0, -1);
    kits_spawn(ctx, w, THING_MEDICAL_KIT, ctx->map->medikits);
    if (w->rules.max_grenades > 0) kits_spawn(ctx, w, THING_GRENADE_KIT, ctx->map->grenade_packs);
    if (w->rules.stationary_guns) {
        for (int i = 0; i < ctx->map->spawnpoint_count; i++) {
            const Spawnpoint *s = &ctx->map->spawnpoints[i];
            if (s->active && s->team == SPAWN_STAT_GUN) thing_create(ctx, w, THING_STAT_GUN, s->pos, WEAPON_NONE, 0, -1);
        }
    }
}

void things_let_go(World *w, uint8_t index)
{
    Soldier *s = &w->soldiers[index];
    for (int i = 0; i < MAX_THINGS; i++) {
        Thing *t = &w->things[i];
        if (t->holder == index + 1 && thing_is_flag(t->style)) {
            t->holder = 0;
            s->held = 0;
        }
        if (t->owner == index + 1) t->owner = 0;
    }
}

bool thing_collides_with_bullets(const World *w, const Thing *t)
{
    switch (t->style) {
    case THING_ALPHA_FLAG:
    case THING_BRAVO_FLAG: return true;
    case THING_WEAPON: return is_bow(t) || w->rules.guns_collide;
    case THING_PARACHUTE:
    case THING_STAT_GUN:
    case THING_NONE: return false;
    default: return w->rules.kits_collide;
    }
}

// --- the physics -------------------------------------------------------------------

// One point against the map (CheckMapCollision): a flag's pole base stops dead and its
// other points bounce along the push-out; everything else is put back where it was and
// pushed out, so it slides to a stop.
static bool map_collide(const Context *ctx, const World *w, Thing *t, int k, Vec2 at, Events *events)
{
    const Map *map = ctx->map;
    Vec2 pos = vec2(at.x, at.y - 0.5f);
    int n = map->sectors_num;
    int sx = round_half_even(pos.x / (float)map->sectors_division);
    int sy = round_half_even(pos.y / (float)map->sectors_division);
    if (!(sx > -n && sx < n && sy > -n && sy < n)) return false;

    bg_test_big_poly_center(map, &t->bg, pos);
    bool flag = thing_is_flag(t->style);
    const Soldier *owner = t->owner ? &w->soldiers[t->owner - 1] : NULL;
    bool hit = false;

    PolySector sector = map_sector_at(map, sx, sy);
    for (int i = 0; i < sector.count; i++) {
        uint16_t idx = sector.polys[i];
        const Polygon *poly = &map->polys[idx];
        PolyType type = (PolyType)poly->type;
        bool team = owner ? team_collides(type, owner->team) : true;
        if (flag && type > POLY_LAVA && type < POLY_BOUNCY) team = false; // the flags pass every team's polys
        if (!team || type == POLY_ONLY_BULLETS || type == POLY_ONLY_PLAYER || type == POLY_DOESNT ||
            type == POLY_ONLY_FLAGGERS || type == POLY_NOT_FLAGGERS) {
            continue;
        }
        if (!point_in_poly_edges(pos, poly)) continue;
        if (bg_test(map, &t->bg, idx)) continue;

        float dist;
        Vec2 normal = closest_perpendicular(poly, pos, &dist, NULL);
        Vec2 push = vec2_scale(vec2_normalize(normal), dist);
        Vec2 *p = &t->pos[k], *o = &t->old_pos[k];
        if (flag && k == 0) {
            *p = *o;
        } else if (flag) {
            float travel = vec2_length(vec2_sub(*p, *o));
            *p = vec2_sub(*p, push);
            *o = vec2_add(*p, vec2_scale(vec2_normalize(push), travel));
            if (k == 1 && t->holder == 0) t->forces[1].y -= 1.0f;
        } else {
            *p = vec2_sub(*o, push);
            // the landing sounds: the first touch, then any hard enough bounce
            int limit = t->style == THING_WEAPON && !is_bow(t) ? 30 : 3;
            bool heard = t->style != THING_STAT_GUN &&
                         (t->collide_count[k] == 0 || (vec2_length(vec2_sub(*p, *o)) > 1.5f && t->collide_count[k] < limit));
            if (heard) {
                event_emit(events, (Event){
                    .type = EVENT_THING_HIT,
                    .thing_hit = {.thing = t->style, .pos = *p, .vel = vec2_sub(*p, *o), .part = (uint8_t)k},
                });
            }
        }
        t->collide_count[k]++;
        hit = true;
    }
    return hit;
}

// Verlet with the thing's damping and gravity, then one pass of its constraints.
static void verlet(const Context *ctx, const World *w, Thing *t)
{
    float damping, gravity;
    thing_physics_of(t, &damping, &gravity);
    for (int k = 0; k < t->points; k++) {
        t->forces[k].y += gravity * w->gravity;
        Vec2 p = t->pos[k];
        t->pos[k] = vec2_add(vec2_sub(vec2_scale(p, 1.0f + damping), vec2_scale(t->old_pos[k], damping)), t->forces[k]);
        t->old_pos[k] = p;
        t->forces[k] = (Vec2){0};
    }

    const ParticleObject *skel = thing_skeleton(ctx, t);
    for (int c = 0; c < skel->constraint_count - t->cut; c++) {
        int a = skel->constraints[c][0], b = skel->constraints[c][1];
        if (a >= t->points || b >= t->points) continue;
        float rest = vec2_length(vec2_sub(skel->points[b], skel->points[a]));
        Vec2 d = vec2_sub(t->pos[b], t->pos[a]);
        float length = sqrtf(vec2_dot(d, d));
        float diff = length != 0.0f ? (length - rest) / length : 0.0f;
        t->pos[a] = vec2_add(t->pos[a], vec2_scale(d, 0.5f * diff));
        t->pos[b] = vec2_sub(t->pos[b], vec2_scale(d, 0.5f * diff));
    }
}

// One tick of the skeleton: each point against the map, the step, rest once settled; a
// carried flag hangs from its carrier's hand.
static void thing_physics(const Context *ctx, World *w, int index, Events *events)
{
    Thing *t = &w->things[index];
    bool flag = thing_is_flag(t->style);
    bool collided = false, collided2 = false;

    bg_test_prepare(&t->bg);
    for (int k = 0; k < t->points; k++) {
        if (t->holder && k != 1) continue; // carried, only the cloth's tip meets the map
        bool hit;
        if (flag && k == 0) {
            // the pole base feels around itself, so a flag can stand on a ledge
            Vec2 p = t->pos[0];
            hit = map_collide(ctx, w, t, 0, vec2(p.x - 10.0f, p.y - 8.0f), events) ||
                  map_collide(ctx, w, t, 0, vec2(t->pos[0].x + 10.0f, t->pos[0].y - 8.0f), events) ||
                  map_collide(ctx, w, t, 0, vec2(t->pos[0].x - 10.0f, t->pos[0].y), events) ||
                  map_collide(ctx, w, t, 0, vec2(t->pos[0].x + 10.0f, t->pos[0].y), events);
            if (hit) t->forces[1].y += FLAG_STAND_FORCEUP * w->gravity;
        } else {
            hit = map_collide(ctx, w, t, k, t->pos[k], events);
        }
        if (!hit) continue;
        if (collided) collided2 = true;
        collided = true;
    }
    bg_test_reset(&t->bg);

    verlet(ctx, w, t);

    // a stationary gun's legs stand once it has settled
    if (t->style == THING_STAT_GUN && t->timeout < 0) {
        t->pos[1] = t->old_pos[1];
        t->pos[2] = t->old_pos[2];
    }

    if (t->style != THING_STAT_GUN && collided && collided2) {
        float movement = (vec2_length(vec2_sub(t->pos[0], t->old_pos[0])) + vec2_length(vec2_sub(t->pos[1], t->old_pos[1]))) / 2.0f;
        if (movement < MIN_MOVE_DELTA) t->is_static = true;
    }

    if (flag && t->holder) {
        Soldier *holder = &w->soldiers[t->holder - 1];
        Pose pose = soldier_pose(ctx->anims, holder, holder->pos);
        t->pos[0] = pose.p[7]; // the hand
        t->forces[1].y += FLAG_HOLDING_FORCEUP * w->gravity;
        t->interest = FLAG_INTEREST_TIME;
        holder->held = (uint8_t)(index + 1);
        t->timeout = FLAG_TIMEOUT;
        if (t->bg.status != BACKGROUND_TRANSITION) {
            t->bg.status = holder->bg.status;
            t->bg.poly = holder->bg.poly;
        }
    }
}

// --- the pickups -------------------------------------------------------------------

// Who takes it (CheckSpriteCollision): the nearest living soldier within reach that
// would want it. Reach is measured from the middle of its first two points, else from
// either; the point a far soldier left the measure at is where the next one is
// measured from first, as in the original.
static void thing_pickup(const Context *ctx, World *w, int index, Events *events)
{
    Thing *t = &w->things[index];
    float radius = thing_radius(t);
    Vec2 pos = vec2_scale(vec2_add(t->pos[0], t->pos[1]), 0.5f);
    float closest = 9999999.0f;
    int taker = -1;

    for (int j = 0; j < MAX_PLAYERS; j++) {
        const Soldier *s = &w->soldiers[j];
        if (!s->active || s->dead || s->team == TEAM_SPECTATOR) continue;
        if (vec2_length(vec2_sub(pos, s->pos)) >= radius) {
            pos = t->pos[0];
            if (vec2_length(vec2_sub(pos, s->pos)) >= radius) pos = t->pos[1];
        }
        float dist = vec2_length(vec2_sub(pos, s->pos));
        if (dist >= radius || dist >= closest) continue;
        if (t->style == THING_MEDICAL_KIT && s->health == DEFAULT_HEALTH) continue;
        if (t->style == THING_GRENADE_KIT && s->grenades == w->rules.max_grenades && s->grenade_type == WEAPON_FRAG) continue;
        if (thing_is_flag(t->style) && s->cease_fire_counter > 0) continue;
        closest = dist;
        taker = j;
    }
    if (taker < 0) return;

    if (thing_is_flag(t->style)) flag_touch(ctx, w, index, (uint8_t)taker, events);
    else if (t->style == THING_WEAPON) dropped_gun_take(ctx, w, index, (uint8_t)taker, events);
    else if (thing_is_kit(t->style)) kit_take(ctx, w, index, (uint8_t)taker, events);
}

// --- the tick ----------------------------------------------------------------------

static bool out_of_bounds(const Map *m, const Thing *t)
{
    float bound = (float)(m->sectors_num * m->sectors_division - 10);
    for (int k = 0; k < t->points; k++)
        if (fabsf(t->pos[k].x) > bound || fabsf(t->pos[k].y) > bound) return true;
    return false;
}

static void thing_update(const Context *ctx, World *w, int index, Events *events)
{
    Thing *t = &w->things[index];
    bool was_static = t->is_static;

    if (!t->is_static) thing_physics(ctx, w, index, events);
    if (thing_is_flag(t->style)) flag_update(ctx, w, index, events);
    if (t->style == THING_STAT_GUN) stat_gun_update(ctx, w, index, events);
    if (w->authority && t->style != THING_STAT_GUN && t->style != THING_NONE) thing_pickup(ctx, w, index, events);
    if (t->style == THING_NONE) return; // taken, and gone

    // Rambo: a bow on the ground goes while someone holds one
    if (is_bow(t)) {
        for (int i = 0; i < MAX_PLAYERS; i++) {
            const Soldier *s = &w->soldiers[i];
            if (s->active && (s->weapon.id == WEAPON_BOW || s->weapon.id == WEAPON_BOW2)) {
                thing_kill(t);
                return;
            }
        }
    }
    if (t->style == THING_PARACHUTE) parachute_update(ctx, w, index);

    t->timeout--;
    if (t->timeout < -1000) t->timeout = -1000;
    if (t->timeout == 0) {
        if (thing_is_flag(t->style) || is_bow(t)) {
            if (w->authority) {
                if (t->holder) t->timeout = FLAG_TIMEOUT;
                else {
                    Vec2 at = t->pos[0];
                    ThingStyle style = t->style;
                    thing_respawn(ctx, w, index);
                    if (style != THING_WEAPON) {
                        event_emit(events, (Event){.type = EVENT_FLAG_RETURN, .flag_return = {.player = 255, .flag = style, .pos = at}});
                    }
                }
            }
        } else if (t->style == THING_WEAPON || t->style == THING_PARACHUTE ||
                   (thing_is_kit(t->style) && t->style != THING_MEDICAL_KIT && t->style != THING_GRENADE_KIT)) {
            thing_kill(t);
            return;
        }
    }

    if (out_of_bounds(ctx->map, t)) {
        if (thing_is_flag(t->style) || thing_is_kit(t->style) || is_bow(t)) {
            if (w->authority) thing_respawn(ctx, w, index);
        } else if (t->style == THING_WEAPON || t->style == THING_STAT_GUN) {
            thing_kill(t);
            return;
        }
        // a parachute off the map is left to its timeout
    }

    if (!was_static && t->is_static) {
        for (int k = 0; k < 4; k++) t->old_pos[k] = t->pos[k];
    }
}

// A bullet struck the thing: the point takes the bullet's velocity over its own, by the
// weapon's push, and the thing is moving again.
static void thing_knock(World *w, const EventThingKnock *e)
{
    Thing *t = &w->things[e->thing];
    if (t->style == THING_NONE || e->part >= t->points) return;
    Vec2 thing_vel = vec2_sub(t->pos[e->part], t->old_pos[e->part]);
    t->pos[e->part] = vec2_add(t->pos[e->part], vec2_scale(vec2_sub(e->vel, thing_vel), e->push));
    t->is_static = false;
}

// A soldier placed anew: what it held goes back, a flag to its base and a parachute
// away, and a high spawn gets a parachute.
static void things_on_respawn(const Context *ctx, World *w, uint8_t soldier)
{
    for (int i = 0; i < MAX_THINGS; i++) {
        Thing *t = &w->things[i];
        if (t->holder != soldier + 1) continue;
        if (t->style == THING_PARACHUTE) thing_kill(t);
        else thing_respawn(ctx, w, i);
    }
    parachute_deploy(ctx, w, soldier);
}

// The things' counters on the soldiers: a flag just thrown, a medikit just taken.
static void things_cooldowns(World *w)
{
    for (int i = 0; i < MAX_PLAYERS; i++) {
        Soldier *s = &w->soldiers[i];
        if (!s->active) continue;
        if (s->flag_grab_cooldown > 0) s->flag_grab_cooldown--;
        if (s->medikit_cooldown > 0) s->medikit_cooldown--;
    }
}

void things_update(const Context *ctx, World *w, const Events *last, Events *events)
{
    things_cooldowns(w);
    EventCursor pending = events_pending(last, events, PASS_THINGS);
    for (const Event *e = events_next(&pending); e; e = events_next(&pending)) {
        switch (e->type) {
        case EVENT_WEAPON_DROP: dropped_gun_drop(ctx, w, &e->weapon_drop); break;
        case EVENT_KNIFE_LAND: thrown_knife_land(ctx, w, &e->knife_land); break;
        case EVENT_KILL: things_let_go(w, e->kill.target); break;
        case EVENT_FLAG_THROW: flag_throw(ctx, w, e->flag_throw.player); break;
        case EVENT_RESPAWN: things_on_respawn(ctx, w, e->respawn.target); break;
        case EVENT_THING_KNOCK: thing_knock(w, &e->thing_knock); break;
        default: break;
        }
    }
    stat_guns_cool(w);

    for (int i = 0; i < MAX_THINGS; i++) {
        if (w->things[i].style != THING_NONE) thing_update(ctx, w, i, events);
    }
}
