#include "render/sparks.h"

#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <stdio.h>

#include "game/systems/systems.h"
#include "render/textures.h"

#define SPARK_GRAVITY (DEFAULT_GRAVITY / 1.4f)
#define SPARK_DAMPING 0.998f
#define SPARK_SURFACECOEF 0.7f
#define RAD_PER_DEG ((float)M_PI / 180.0f)

// One in this many ticks a cut joint drips, by how busy the screen already is
// (BLOOD_RANDOM_LOW, _NORMAL, _HIGH in the original's constants).
#define BLOOD_RANDOM_LOW 22
#define BLOOD_RANDOM_NORMAL 10
#define BLOOD_RANDOM_HIGH 6
#define LESSBLEED_TIME 120 // ticks dead after which a body bleeds less, then not at all
#define NOBLEED_TIME 300
#define CLUSTER_EXPLOSION_RADIUS 35.0f
// A burning body flames for this long, one in this many ticks per burning point, by how
// busy the screen is (ONFIRE_TIME, FIRE_RANDOM_LOW, _NORMAL, _HIGH).
#define ONFIRE_TIME 240
#define FIRE_RANDOM_LOW 70
#define FIRE_RANDOM_NORMAL 50
#define FIRE_RANDOM_HIGH 30
#define SHELL_LANDINGS 5 // a casing or a clip is gone after this many touches of the map

static const char *const ART_FILES[SPARK_ART_COUNT] = {
    [SPARK_ART_SMOKE] = "smoke.png",         [SPARK_ART_LIL_SMOKE] = "lilsmoke.png", [SPARK_ART_MINI_SMOKE] = "minismoke.png",
    [SPARK_ART_BIG_SMOKE] = "bigsmoke.png",  [SPARK_ART_CHIP] = "odprysk.png",       [SPARK_ART_LIL_BLOOD] = "lilblood.png",
    [SPARK_ART_BLOOD] = "blood.png",         [SPARK_ART_SPAWN_SPARK] = "spawnspark.png", [SPARK_ART_JET_FIRE] = "jetfire.png",
    [SPARK_ART_FLAME] = "plomyk.png",        [SPARK_ART_BLACK_SMOKE] = "blacksmoke.png", [SPARK_ART_STUFF] = "stuff.png",
    [SPARK_ART_CIGAR] = "cygaro.png",        [SPARK_ART_RAIN] = "rain.png",      [SPARK_ART_SAND] = "sand.png",
    [SPARK_ART_SNOW] = "snow.png",
};

// The casings, by the weapon that ejects one; the rest eject none. The shotgun's and
// the M79's come out on the reload (the pump, the breech), not the shot.
static const char *const SHELL_STEMS[WEAPON_COUNT] = {
    [WEAPON_EAGLE] = "eagles-shell", [WEAPON_MP5] = "mp5-shell",   [WEAPON_AK74] = "ak74-shell",       [WEAPON_STEYR] = "steyraug-shell",
    [WEAPON_RUGER] = "ruger77-shell", [WEAPON_BARRETT] = "barretm82-shell", [WEAPON_M249] = "m249-shell", [WEAPON_MINIGUN] = "minigun-shell",
    [WEAPON_COLT] = "colt-shell",     [WEAPON_SPAS] = "spas12-shell", [WEAPON_M79] = "m79-shell",
};

// The clips, by the weapon that drops one on its reload (SpriteEffects.pas PlayClipOut).
static const char *const CLIP_STEMS[WEAPON_COUNT] = {
    [WEAPON_EAGLE] = "deserteagle-clip", [WEAPON_MP5] = "mp5-clip",   [WEAPON_AK74] = "ak74-clip", [WEAPON_STEYR] = "steyraug-clip",
    [WEAPON_BARRETT] = "barretm82-clip", [WEAPON_M249] = "m249-clip", [WEAPON_COLT] = "colt1911-clip",
};

static const Rgba GREEN = {0, 255, 0, 255};

static bool moves(SparkStyle style)
{
    switch (style) {
    case SPARK_SMOKE:
    case SPARK_CHIP:
    case SPARK_LIL_BLOOD:
    case SPARK_BLOOD:
    case SPARK_CHIP_FIRE:
    case SPARK_MINI_SMOKE:
    case SPARK_LIL_SMOKE:
    case SPARK_SHELL:
    case SPARK_CLIP:
    case SPARK_JET_FIRE:
    case SPARK_SPIT:
    case SPARK_MATCH:
    case SPARK_CIGAR:
    case SPARK_PISS:
    case SPARK_RAIN: // the weather falls by its own weight and the air, through everything
    case SPARK_SAND:
    case SPARK_SNOW: return true;
    default: return false; // the flames and their smoke hang where they were lit
    }
}

static bool collides(SparkStyle style)
{
    switch (style) {
    case SPARK_LIL_BLOOD:
    case SPARK_BLOOD:
    case SPARK_SHELL:
    case SPARK_CLIP:
    case SPARK_JET_FIRE:
    case SPARK_SPIT:
    case SPARK_MATCH:
    case SPARK_CIGAR:
    case SPARK_PISS: return true;
    default: return false;
    }
}

// The sparks that count their landings: for a sound, and to be gone after a few.
static bool lands(SparkStyle style)
{
    return style == SPARK_SHELL || style == SPARK_CLIP || style == SPARK_SPIT || style == SPARK_MATCH || style == SPARK_CIGAR;
}

void sparks_load(Sparks *s, const Mod *mod)
{
    char path[512];
    for (int k = 0; k < SPARK_ART_COUNT; k++) {
        mod_file(mod, path, sizeof path, "sparks-gfx/%s", ART_FILES[k]);
        if (!sprite_load(&s->art[k], path, &GREEN)) s->art[k] = (Sprite){0};
    }
    for (int i = 0; i < EXPLOSION_FRAMES; i++) {
        mod_file(mod, path, sizeof path, "sparks-gfx/explosion/explode%d.png", i + 1);
        if (!sprite_load(&s->explode[i], path, &GREEN)) s->explode[i] = (Sprite){0};
    }
    for (int i = 0; i < SMOKE_FRAMES; i++) {
        mod_file(mod, path, sizeof path, "sparks-gfx/explosion/smoke%d.png", i + 1);
        if (!sprite_load(&s->smoke[i], path, &GREEN)) s->smoke[i] = (Sprite){0};
    }
    for (int id = 0; id < WEAPON_COUNT; id++) {
        s->shells[id] = (Sprite){0};
        if (!SHELL_STEMS[id]) continue;
        char name[128];
        snprintf(name, sizeof name, "%s.png", SHELL_STEMS[id]);
        if (mod_image(mod, "weapons-gfx", name, path, sizeof path)) sprite_load(&s->shells[id], path, NULL);
    }
    if (mod_image(mod, "weapons-gfx", "shell.png", path, sizeof path)) sprite_load(&s->shell, path, NULL);
    for (int id = 0; id < WEAPON_COUNT; id++) {
        s->clips[id] = (Sprite){0};
        if (!CLIP_STEMS[id]) continue;
        char name[128];
        snprintf(name, sizeof name, "%s.png", CLIP_STEMS[id]);
        if (mod_image(mod, "weapons-gfx", name, path, sizeof path)) sprite_load(&s->clips[id], path, NULL);
    }
    s->rng = 0x853C49E6748FEA9Bull;
    s->loaded = true;
}

void sparks_unload(Sparks *s)
{
    for (int k = 0; k < SPARK_ART_COUNT; k++) sprite_unload(&s->art[k]);
    for (int i = 0; i < EXPLOSION_FRAMES; i++) sprite_unload(&s->explode[i]);
    for (int i = 0; i < SMOKE_FRAMES; i++) sprite_unload(&s->smoke[i]);
    for (int id = 0; id < WEAPON_COUNT; id++) sprite_unload(&s->shells[id]);
    for (int id = 0; id < WEAPON_COUNT; id++) sprite_unload(&s->clips[id]);
    sprite_unload(&s->shell);
    *s = (Sparks){0};
}

// A spark's noise, for the audio to play after the tick.
static void noise(Sparks *s, SparkNoise n, Vec2 pos)
{
    if (s->sound_count == MAX_SPARK_SOUNDS) return;
    s->sounds[s->sound_count++] = (SparkSound){.noise = n, .pos = pos};
}

void sparks_clear(Sparks *s)
{
    for (int i = 0; i < MAX_SPARKS; i++) s->pool[i].style = SPARK_NONE;
}

static float rand01(Sparks *s) { return rand_f32(&s->rng); }
static int rand_n(Sparks *s, int n) { return rand_int(&s->rng, n); }
static float rand_spread(Sparks *s, float amount) { return (rand01(s) * 2.0f - 1.0f) * amount; }

static Spark *spark_add(Sparks *s, Vec2 pos, Vec2 vel, SparkStyle style, float life, Rgba color)
{
    for (int i = 0; i < MAX_SPARKS; i++) {
        Spark *spark = &s->pool[i];
        if (spark->style != SPARK_NONE) continue;
        *spark = (Spark){.style = style, .life = life, .pos = pos, .vel = vel, .color = color, .old_pos = pos, .prev_life = life};
        return spark;
    }
    return NULL;
}

static void add(Sparks *s, Vec2 pos, Vec2 vel, SparkStyle style, float life)
{
    spark_add(s, pos, vel, style, life, RGBA_WHITE);
}

// Point collision as TSpark.CheckMapCollision does it, with its probe offset of (-8, -1).
// True when the spark touched the map this tick.
static bool spark_collide(const Map *map, Spark *spark)
{
    Vec2 probe = vec2_add(spark->pos, vec2(-8, -1));
    PolySector sector = map_sector_polys(map, probe);
    for (int k = 0; k < sector.count; k++) {
        const Polygon *poly = &map->polys[sector.polys[k]];
        switch ((PolyType)poly->type) {
        case POLY_ONLY_BULLETS:
        case POLY_ONLY_PLAYER:
        case POLY_DOESNT:
        case POLY_BACKGROUND:
        case POLY_BACKGROUND_TRANSITION: continue;
        default: break;
        }
        if (!point_in_poly_edges(probe, poly)) continue;
        float dist;
        int edge;
        Vec2 normal = closest_perpendicular(poly, probe, &dist, &edge);
        spark->vel = vec2_sub(spark->vel, vec2_scale(vec2_normalize(normal), dist));
        spark->vel = vec2_scale(spark->vel, SPARK_SURFACECOEF);
        return true;
    }
    return false;
}

// A casing or a clip touched the map: its sound on the landings the original counts
// (CheckMapCollision: a casing on its first, third and fifth, a shotgun's on every one,
// a clip on its first and fifth), and it is gone after the fifth.
static void landing(Sparks *s, Spark *spark)
{
    int n = spark->collide_count;
    int gone = SHELL_LANDINGS;
    switch (spark->style) {
    case SPARK_SHELL:
        if (spark->weapon == WEAPON_SPAS) noise(s, SPARK_NOISE_GAUGE_SHELL, spark->pos);
        else if (n == 0 || n == 2 || n == 4) noise(s, SPARK_NOISE_SHELL, spark->pos);
        break;
    case SPARK_CLIP:
        if (n == 0 || n == 4) noise(s, SPARK_NOISE_CLIP, spark->pos);
        break;
    case SPARK_SPIT: gone = 3; break; // the original's style 32 goes after its third touch
    default: break;                   // the match and the stub after their fifth, as a casing
    }
    if (spark->collide_count < 255) spark->collide_count++;
    if (spark->collide_count > gone) spark->style = SPARK_NONE;
}

// ---- the bursts each event makes ----

static void wall_hit(Sparks *s, Vec2 at, Vec2 vel)
{
    Vec2 b = vec2_scale(vel, -0.06f);
    b.y -= 1.0f;
    b.x *= 0.6f + rand01(s) * 0.8f;
    b.y *= 0.8f + rand01(s) * 0.4f;
    add(s, at, b, SPARK_CHIP, 60);
    b.x *= 0.8f + rand01(s) * 0.4f;
    b.y *= 0.6f + rand01(s) * 0.8f;
    add(s, at, b, SPARK_CHIP, 65);
    add(s, at, vec2_scale(b, 0.4f + rand01(s) * 0.4f), SPARK_SMOKE, 60);
    b.x *= 0.5f + rand01(s) * 0.4f;
    b.y *= 0.7f + rand01(s) * 0.8f;
    add(s, at, b, SPARK_CHIP, 50);
    add(s, at, vec2(0, 0), SPARK_MINI_SMOKE, 22);
}

static void blood(Sparks *s, Vec2 pos, Vec2 vel)
{
    Vec2 b = vec2_scale(vel, 0.025f);
    b.x *= 1.2f;
    b.y *= 0.85f;
    add(s, pos, b, SPARK_LIL_BLOOD, 70);
    b.x *= 0.745f;
    b.y *= 1.1f;
    add(s, pos, b, SPARK_LIL_BLOOD, 75);
    b.x *= 0.9f;
    b.y *= 0.85f;
    if (rand_n(s, 2) == 0) add(s, pos, b, SPARK_LIL_BLOOD, 75);
    b.x *= 1.2f;
    b.y *= 0.85f;
    add(s, pos, b, SPARK_BLOOD, 80);
    add(s, pos, b, SPARK_BLOOD, 85);
    b.x *= 0.5f;
    b.y *= 1.05f;
    if (rand_n(s, 2) == 0) add(s, pos, b, SPARK_BLOOD, 75);
    for (int i = 0; i < 7; i++) {
        if (rand_n(s, 6) != 0) continue;
        Vec2 spray = vec2(sinf(rand01(s) * 100.0f) * 1.6f, cosf(rand01(s) * 100.0f) * 1.6f);
        add(s, pos, spray, SPARK_LIL_BLOOD, 55);
    }
}

static void explosion(Sparks *s, Vec2 pos, WeaponId weapon, float radius)
{
    if (radius <= CLUSTER_EXPLOSION_RADIUS) {
        add(s, pos, vec2(0, 0), SPARK_EXPLODE_CLUSTER, (float)(EXPLOSION_FRAMES * 3));
        return;
    }
    bool m79 = weapon == WEAPON_M79;
    add(s, pos, vec2(0, 0), SPARK_BIG_SMOKE, m79 ? 255.0f : 190.0f);
    add(s, pos, vec2(0, 0), SPARK_EXPLODE_SMOKE, (float)(SMOKE_FRAMES * 4 + 10));
    add(s, pos, vec2(0, 0), m79 ? SPARK_EXPLODE_M79 : SPARK_EXPLODE_FRAG, (float)(EXPLOSION_FRAMES * 3));
}

// A shot: the casing out of the breech, sideways to the aim and tumbling, and a puff of
// smoke off the muzzle (SpriteEffects.pas PlayFire). The bullet's own art is the sim's.
static void fire(Sparks *s, const Context *ctx, const World *w, const EventFire *f)
{
    const Soldier *shooter = &w->soldiers[f->player];
    if (!shooter->active) return;
    if (SHELL_STEMS[f->weapon] && f->weapon != WEAPON_SPAS && f->weapon != WEAPON_M79) {
        Pose pose = soldier_pose(ctx->anims, shooter, shooter->pos);
        float dir = (float)shooter->direction;
        Vec2 aim = vec2_normalize(f->vel);
        Vec2 c = vec2(shooter->vel.x + dir * aim.y * (rand01(s) * 0.5f + 0.8f), shooter->vel.y - dir * aim.x * (rand01(s) * 0.5f + 0.8f));
        Vec2 a = vec2(pose.p[15 - 1].x + 2 - dir * 0.015f * f->vel.x, pose.p[15 - 1].y - 2 - dir * 0.015f * f->vel.y);
        Spark *shell = spark_add(s, a, c, SPARK_SHELL, 255, RGBA_WHITE);
        if (shell) shell->weapon = f->weapon;
    }
    if (f->weapon != WEAPON_KNIFE && f->weapon != WEAPON_CHAINSAW && f->weapon != WEAPON_FRAG && f->weapon != WEAPON_CLUSTER_NADE) {
        add(s, f->pos, vec2_scale(vec2_normalize(f->vel), 0.35f), SPARK_LIL_SMOKE, 30);
    }
}

// An antic's sparks (SpriteEffects.pas SE_SPIT, SE_CIGARLIGHT, SE_CIGARPUFF,
// SE_CIGARTHROW and PissSpark): the simulation says where and how fast; the piss's
// odds are rolled here, with the spark.
static void antic(Sparks *s, const EventAntic *e)
{
    switch (e->kind) {
    case ANTIC_SPIT: add(s, e->pos, e->vel, SPARK_SPIT, 245); break;
    case ANTIC_CIGAR_PUFF: add(s, e->pos, e->vel, SPARK_LIL_SMOKE, 65); break;
    case ANTIC_MATCH: add(s, e->pos, e->vel, SPARK_MATCH, 245); break;
    case ANTIC_CIGAR_THROW: add(s, e->pos, e->vel, SPARK_CIGAR, 245); break;
    case ANTIC_PISS:
        if (e->odds > 0 && rand_n(s, e->odds) == 0) add(s, e->pos, e->vel, SPARK_PISS, (float)e->life);
        break;
    }
}

static void sparks_event(Sparks *s, const Context *ctx, const World *w, const Event *e)
{
    switch (e->type) {
    case EVENT_ANTIC: antic(s, &e->antic); break;
    case EVENT_WALL_HIT: wall_hit(s, e->wall_hit.pos, e->wall_hit.vel); break;
    case EVENT_RICOCHET: wall_hit(s, e->ricochet.pos, e->ricochet.vel); break;
    case EVENT_COLLIDER_HIT: wall_hit(s, e->collider_hit.pos, e->collider_hit.vel); break;
    case EVENT_THING_HIT:
        add(s, e->thing_hit.pos, vec2_scale(e->thing_hit.vel, -0.02f * (0.4f + rand01(s) * 0.4f)), SPARK_SMOKE, 70);
        break;
    case EVENT_BLOOD: blood(s, e->blood.pos, e->blood.vel); break;
    case EVENT_EXPLOSION: explosion(s, e->explosion.pos, e->explosion.weapon, e->explosion.radius); break;
    case EVENT_CLUSTER_SPLIT: add(s, e->cluster_split.pos, vec2(0, 0), SPARK_SPLIT_SMOKE, 55); break;
    case EVENT_ROPE_CUT: add(s, e->rope_cut.pos, vec2(0, 0), SPARK_LIL_SMOKE, 25); break; // the rope's snap, wherever it was cut
    case EVENT_RESPAWN: {
        Rgba shirt = w->soldiers[e->respawn.target].look.shirt;
        shirt.a = 255;
        spark_add(s, e->respawn.pos, vec2(0, 0), SPARK_SPAWN_SPARK, 33, shirt);
        break;
    }
    case EVENT_FIRE: fire(s, ctx, w, &e->fire); break;
    case EVENT_POLY_EFFECT:
        switch (e->poly_effect.type) {
        case POLY_LAVA:
        case POLY_EXPLODES:
            for (int i = 0; i < 3; i++) add(s, e->poly_effect.pos, vec2(rand_spread(s, 0.8f), -0.5f - rand01(s)), SPARK_CHIP_FIRE, 35);
            break;
        case POLY_REGENERATES: add(s, e->poly_effect.pos, vec2(0, -0.4f), SPARK_SMOKE, 50); break;
        case POLY_BOUNCY: break; // its thud is the audio's
        default: add(s, e->poly_effect.pos, vec2(0, -0.2f), SPARK_LIL_BLOOD, 45); break;
        }
        break;
    default: break;
    }
}

// The jets' flames (SpriteEffects.pas JetEffects): from each foot, back along the
// shin, a flame in the jet's colour now and then, and smoke now and then.
static void jets(Sparks *s, const Context *ctx, const World *w)
{
    for (int i = 0; i < MAX_PLAYERS; i++) {
        const Soldier *soldier = &w->soldiers[i];
        if (!soldier->active || soldier->dead || soldier->gear != GEAR_JETS || !(soldier->controls & BUTTON_JET) ||
            soldier->jets <= 0)
            continue;
        Pose pose = soldier_pose(ctx->anims, soldier, soldier->pos);
        Rgba jet = soldier->look.jet;
        jet.a = 255;
        const int feet[2] = {1, 2}, shin_from[2] = {4, 3}, shin_to[2] = {5, 6};
        for (int k = 0; k < 2; k++) {
            Vec2 a = vec2_add(pose.p[feet[k] - 1], vec2(-1, 3));
            Vec2 b = vec2_scale(vec2_normalize(vec2_sub(pose.p[shin_to[k] - 1], pose.p[shin_from[k] - 1])), -0.5f);
            if (rand_n(s, 8) == 0) add(s, a, soldier->vel, SPARK_SMOKE, 75);
            if (rand_n(s, 7) == 0) spark_add(s, a, b, SPARK_JET_FIRE, 40, jet);
        }
    }
}

// A casing out of a reload (SpriteEffects.pas PlayShell): the shotgun's off the pump,
// the M79's out of the breech, spun away from the hand along the aim.
static void reload_shell(Sparks *s, const Context *ctx, const Soldier *soldier, WeaponId weapon, float spin)
{
    Pose pose = soldier_pose(ctx->anims, soldier, soldier->pos);
    float dir = (float)soldier->direction;
    Vec2 b = vec2_scale(vec2_normalize(vec2_sub(soldier->aim, pose.p[15 - 1])), ctx->weapons.info[weapon].stats.speed);
    b.x = dir * spin * b.y + soldier->vel.x;
    b.y = -dir * spin * b.x + soldier->vel.y; // from the x just set, as the original has it
    Vec2 a = vec2(pose.p[15 - 1].x + 2 - dir * 0.015f * b.x, pose.p[15 - 1].y - 2 - dir * 0.015f * b.y);
    Spark *shell = spark_add(s, a, b, SPARK_SHELL, 255, RGBA_WHITE);
    if (shell) shell->weapon = weapon;
}

// What a reload drops (Sprites.pas, Control.pas): the empty clip as the reload passes
// its clip-out time (the Desert Eagles drop two), the M79's casing then too, and the
// shotgun's as the pump passes its 24th frame, which the sim steps over in one tick.
static void reloads(Sparks *s, const Context *ctx, const World *w)
{
    for (int i = 0; i < MAX_PLAYERS; i++) {
        const Soldier *soldier = &w->soldiers[i];
        const WeaponInfo *info = &ctx->weapons.info[soldier->weapon.id];
        int32_t count = soldier->weapon.reload_count, last = s->last_reload[i];
        int32_t pump = soldier->body.id == ANIM_SHOTGUN ? soldier->body.frame : 0, last_pump = s->last_pump[i];
        s->last_reload[i] = count;
        s->last_pump[i] = pump;
        if (!soldier->active || soldier->dead) continue;
        bool clip_out = count == info->clip_out_time && count > 0 && last != count && soldier->weapon.ammo == 0;
        if (clip_out && CLIP_STEMS[soldier->weapon.id]) {
            Pose pose = soldier_pose(ctx->anims, soldier, soldier->pos);
            Vec2 hand = pose.p[15 - 1];
            Spark *clip = spark_add(s, vec2_add(hand, vec2(0, 6)), vec2_add(soldier->vel, vec2(0, -0.001f)), SPARK_CLIP, 255, RGBA_WHITE);
            if (clip) clip->weapon = soldier->weapon.id;
            if (soldier->weapon.id == WEAPON_EAGLE) {
                clip = spark_add(s, vec2_add(hand, vec2(-2, 7)), vec2_add(soldier->vel, vec2(0.3f, -0.003f)), SPARK_CLIP, 255, RGBA_WHITE);
                if (clip) clip->weapon = WEAPON_EAGLE;
            }
        }
        if (clip_out && soldier->weapon.id == WEAPON_M79) reload_shell(s, ctx, soldier, WEAPON_M79, 0.08f);
        if (soldier->weapon.id == WEAPON_SPAS && pump >= 25 && last_pump > 0 && last_pump < 25) reload_shell(s, ctx, soldier, WEAPON_SPAS, 0.025f);
    }
}

// The corpses bleed from where they were cut (TSprite.Update's dead branch): every body
// point an end of a torn constraint hangs off drips, thrown along the way that point is
// moving. It thins after two seconds and stops after five, and thins again while the
// screen is already full of sparks, so a pile of bodies does not drown everything else.
// A body that died burning (death_fire) flames and smokes off every death_fire-th point
// for its first four seconds, and crackles as it does.
static void corpses(Sparks *s, const Context *ctx, const World *w)
{
    int live = 0;
    for (int i = 0; i < MAX_SPARKS; i++) live += s->pool[i].style != SPARK_NONE;
    int base = live > 300 ? BLOOD_RANDOM_LOW : live > 50 ? BLOOD_RANDOM_NORMAL : BLOOD_RANDOM_HIGH;
    int fire_odds = live > 170 ? FIRE_RANDOM_LOW : live < 17 ? FIRE_RANDOM_HIGH : FIRE_RANDOM_NORMAL;
    const ParticleObject *skeleton = &ctx->skeletons->gostek;

    for (int i = 0; i < MAX_PLAYERS; i++) {
        const Soldier *soldier = &w->soldiers[i];
        const Ragdoll *r = &w->ragdolls[i];
        if (!soldier->active || !soldier->dead || !r->active) continue;
        int odds = base;
        if (r->dead_time > LESSBLEED_TIME) odds *= 2;
        if (r->dead_time > NOBLEED_TIME) odds *= 100;
        bool burning = soldier->death_fire > 0 && r->dead_time < ONFIRE_TIME;
        for (int point = 0; point < POSE_POINTS; point++) {
            Vec2 moved = vec2_sub(r->pos[point], r->old_pos[point]);
            for (int ci = 0; r->torn != 0 && ci < skeleton->constraint_count && ci < 32; ci++) {
                const int *c = skeleton->constraints[ci];
                if (!(r->torn >> ci & 1) || (c[0] != point && c[1] != point)) continue;
                if (ci == 9 || ci == 10) continue; // the two the original leaves dry
                Vec2 at = vec2_add(r->pos[point], vec2(0, 2));
                Vec2 vel = vec2_scale(moved, 0.35f);
                if (rand_n(s, odds) == 0) add(s, at, vel, SPARK_BLOOD, 85.0f - (float)rand_n(s, 25));
                else if (rand_n(s, odds / 3 > 1 ? odds / 3 : 1) == 0) add(s, at, vel, SPARK_LIL_BLOOD, 85.0f - (float)rand_n(s, 25));
            }
            if (burning && (point + 1) % soldier->death_fire == 0) {
                Vec2 at = vec2_add(r->pos[point], vec2(0, 3));
                Vec2 vel = vec2_scale(moved, 0.3f);
                if (rand_n(s, fire_odds) == 0) {
                    add(s, at, vel, SPARK_FLAME, 35);
                    if (rand_n(s, 8) == 0) noise(s, SPARK_NOISE_ONFIRE, soldier->pos);
                    if (rand_n(s, 2) == 0) noise(s, SPARK_NOISE_FIRECRACK, soldier->pos);
                } else if (rand_n(s, fire_odds / 3) == 0) {
                    add(s, at, vel, SPARK_BLACK_SMOKE, 75);
                }
            }
        }
    }
}

void sparks_tick(Sparks *s, const Context *ctx, const World *w, const Events *events)
{
    if (!s->loaded) return;
    s->sound_count = 0;
    for (int i = 0; i < events->count; i++) sparks_event(s, ctx, w, &events->items[i]);
    jets(s, ctx, w);
    reloads(s, ctx, w);
    corpses(s, ctx, w);

    for (int i = 0; i < MAX_SPARKS; i++) {
        Spark *spark = &s->pool[i];
        if (spark->style == SPARK_NONE) continue;
        spark->old_pos = spark->pos; // the frames between this tick and the next blend from here
        spark->prev_life = spark->life;
        if (moves(spark->style)) {
            spark->vel.y += SPARK_GRAVITY;
            spark->pos = vec2_add(spark->pos, spark->vel);
            spark->vel = vec2_scale(spark->vel, SPARK_DAMPING);
        }
        if (collides(spark->style) && spark_collide(ctx->map, spark) && lands(spark->style)) {
            landing(s, spark);
            if (spark->style == SPARK_NONE) continue;
        }
        spark->life -= 1.0f;
        if (spark->life <= 0.0f) spark->style = SPARK_NONE;
    }
}

// The weather, as WeatherEffects.pas makes it. The original's r_maxsparks at its default
// (557) is past MAX_SPARKS - 10, so every seventeenth tick it makes eight, each its
// spacing on from the last along the view's top, a little up or down.
void sparks_weather(Sparks *s, uint8_t weather, Vec2 camera, Vec2 view, uint32_t tick)
{
    if (!s->loaded || weather < 1 || weather > 3 || tick % 17 != 0) return;
    Vec2 half = vec2_scale(view, 0.5f);
    SparkStyle style;
    Vec2 vel;
    float x, top, life;
    switch (weather) {
    case 1: // MakeRain
        style = SPARK_RAIN, vel = vec2(0, 12), x = camera.x - half.x - 128, top = camera.y - half.y - 128 - 60, life = 60;
        break;
    case 2: // MakeSandStorm: blown in from the left
        style = SPARK_SAND, vel = vec2(10, 7), x = camera.x - half.x - 1.5f * 512, top = camera.y - half.y - 256 - 60, life = 80;
        break;
    default: // MakeSnow
        style = SPARK_SNOW, vel = vec2(1, 2), x = camera.x - half.x - 256, top = camera.y - half.y - 60, life = 80;
        break;
    }
    for (int i = 0; i < 8; i++) {
        x += 128 - 50 + (float)rand_n(s, 90);
        float y = top + (float)rand_n(s, 150);
        // CreateSpark makes none outside a view of whom the camera follows, but rain
        bool seen = fabsf(x - camera.x) < view.x && fabsf(y - camera.y) < view.y;
        if (style == SPARK_RAIN || seen) add(s, vec2(x, y), vel, style, life);
    }
}

// ---- drawing ----

static void draw_spark(Sprite sprite, Vec2 at, float scale, float angle, float alpha, Rgba tint)
{
    if (alpha <= 0.0f || sprite.tex.handle == 0) return;
    tint.a = alpha8(alpha);
    draw_sprite(sprite, at, vec2(0, 0), vec2(scale, scale), angle, tint);
}

// The frame for a countdown life: the animation runs forward as the life falls.
static int explosion_frame(float l, float step)
{
    return clampi(EXPLOSION_FRAMES - 1 - (int)roundf(l / step), 0, EXPLOSION_FRAMES - 1);
}

void sparks_draw(const Sparks *s, float between)
{
    if (!s->loaded) return;
    const Rgba white = RGBA_WHITE;
    for (int i = 0; i < MAX_SPARKS; i++) {
        const Spark *spark = &s->pool[i];
        // between the last tick and the latest, as the original's frame lerps them
        float l = spark->prev_life + (spark->life - spark->prev_life) * between;
        Vec2 p = vec2_add(spark->old_pos, vec2_scale(vec2_sub(spark->pos, spark->old_pos), between));
        switch (spark->style) {
        case SPARK_NONE: break;
        // the weather: as it is, at a steady 105 (Sparks.pas, styles 38, 39 and 53)
        case SPARK_RAIN: draw_spark(s->art[SPARK_ART_RAIN], p, 1, 0, 105, white); break;
        case SPARK_SAND: draw_spark(s->art[SPARK_ART_SAND], p, 1, 0, 105, white); break;
        case SPARK_SNOW: draw_spark(s->art[SPARK_ART_SNOW], p, 1, 0, 105, white); break;
        case SPARK_SMOKE: draw_spark(s->art[SPARK_ART_SMOKE], p, 1, 0, l + 10, white); break;
        case SPARK_LIL_SMOKE: draw_spark(s->art[SPARK_ART_LIL_SMOKE], p, 1, 0, l * 3, white); break;
        case SPARK_CHIP: draw_spark(s->art[SPARK_ART_CHIP], p, 1, 0, l * 3 + 10, white); break;
        case SPARK_CHIP_FIRE: draw_spark(s->art[SPARK_ART_CHIP], p, 1, 0, l * 3 + 154, (Rgba){255, 254, 53, 255}); break;
        case SPARK_LIL_BLOOD: draw_spark(s->art[SPARK_ART_LIL_BLOOD], p, 0.75f, l * 10 * RAD_PER_DEG, l * 2 + 65, white); break;
        case SPARK_BLOOD:
            draw_spark(s->art[SPARK_ART_BLOOD], p, l > 10 ? 0.33f + 10 / l : 1, l * 2 * RAD_PER_DEG, l * 2 + 85, white);
            break;
        case SPARK_MINI_SMOKE: draw_spark(s->art[SPARK_ART_MINI_SMOKE], vec2_sub(p, vec2(3, 3)), 1, 0, l * 2.5f, white); break;
        case SPARK_SPAWN_SPARK:
            draw_spark(s->art[SPARK_ART_SPAWN_SPARK], vec2_sub(p, vec2(20, 20)), 1, l * RAD_PER_DEG, l * 6, spark->color);
            break;
        case SPARK_JET_FIRE: draw_spark(s->art[SPARK_ART_JET_FIRE], p, 1, l * RAD_PER_DEG, l * 5, spark->color); break;
        case SPARK_SHELL: {
            Sprite shell = s->shells[spark->weapon].tex.handle ? s->shells[spark->weapon] : s->shell;
            float spin = spark->weapon == WEAPON_BARRETT ? 3.5f : spark->weapon == WEAPON_SPAS || spark->weapon == WEAPON_M79 ? 3.77f : 4.0f;
            draw_spark(shell, p, 1, l * spin * RAD_PER_DEG, 255, white);
            break;
        }
        case SPARK_CLIP: draw_spark(s->clips[spark->weapon], vec2_add(p, vec2(8, 0)), 1, (float)M_PI, 255, white); break; // upside down, as it fell
        case SPARK_FLAME: {
            float sc = l / 35.0f;
            draw_spark(s->art[SPARK_ART_FLAME], vec2_sub(p, vec2(0, 1 / sc)), sc, 0, fminf(l * 2 + 185, 255.0f), white);
            break;
        }
        case SPARK_BLACK_SMOKE: {
            float sc = l / 75.0f;
            draw_spark(s->art[SPARK_ART_BLACK_SMOKE], vec2_sub(p, vec2(0, 1 / sc)), sc, 0, l * 3, white);
            break;
        }
        case SPARK_SPIT: draw_spark(s->art[SPARK_ART_STUFF], p, 1, 0, l + 10, white); break;
        case SPARK_MATCH: draw_spark(s->shell, p, 1, l * 4 * RAD_PER_DEG, 255, (Rgba){187, 170, 169, 255}); break; // a casing, greyed
        case SPARK_CIGAR: draw_spark(s->art[SPARK_ART_CIGAR], p, 1, l * 4 * RAD_PER_DEG, 255, white); break;
        case SPARK_PISS: draw_spark(s->art[SPARK_ART_CHIP], p, 1, 0, l * 2 + 10, (Rgba){255, 255, 0, 255}); break;
        case SPARK_EXPLODE_M79: {
            int frame = explosion_frame(l, 4);
            Vec2 at = vec2_sub(p, vec2(19, 38));
            if (frame > 0) draw_spark(s->explode[frame - 1], at, 0.75f, 0, 100, (Rgba){173, 173, 173, 255});
            draw_spark(s->explode[frame], at, 0.75f, 0, 255 - EXPLOSION_FRAMES * 5 + l, white);
            break;
        }
        case SPARK_EXPLODE_FRAG: {
            int frame = explosion_frame(l, 4);
            Vec2 at = vec2_sub(p, vec2(25, 50));
            if (frame > 0) draw_spark(s->explode[frame - 1], at, 1, 0, 100, (Rgba){171, 171, 171, 255});
            draw_spark(s->explode[frame], at, 1, 0, 255 - EXPLOSION_FRAMES * 5 + l, white);
            break;
        }
        case SPARK_EXPLODE_CLUSTER:
            draw_spark(s->explode[explosion_frame(l, 3)], vec2_sub(p, vec2(15, 37)), 0.5f, 0, 255 - 2 * l, white);
            break;
        case SPARK_EXPLODE_SMOKE:
            if (l <= SMOKE_FRAMES * 4) {
                int frame = clampi(SMOKE_FRAMES - 1 - (int)roundf(l / 4), 0, SMOKE_FRAMES - 1);
                Vec2 at = vec2_sub(p, vec2(26, 48));
                if (frame > 0) draw_spark(s->smoke[frame - 1], at, 1, 0, l * 2 + 10, (Rgba){204, 204, 204, 255});
                draw_spark(s->smoke[frame], at, 1, 0, l * 3 + 10, (Rgba){222, 222, 222, 255});
            }
            break;
        case SPARK_BIG_SMOKE: {
            float sc = 0.5f + 16 / (l + 50);
            draw_spark(s->art[SPARK_ART_BIG_SMOKE], vec2_sub(p, vec2(14 * sc, 30)), sc, 0, l / 3.3f, white);
            break;
        }
        case SPARK_SPLIT_SMOKE: {
            float sc = 0.5f * (0.6f + (75 / l) / 96);
            draw_spark(s->art[SPARK_ART_BIG_SMOKE], vec2_sub(p, vec2(22 * sc, 48 - l / 1.5f)), sc, 0, l * 2.5f, white);
            break;
        }
        default: break;
        }
    }
}
