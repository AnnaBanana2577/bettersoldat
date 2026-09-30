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

static const char *const ART_FILES[SPARK_ART_COUNT] = {
    [SPARK_ART_SMOKE] = "smoke.png",         [SPARK_ART_LIL_SMOKE] = "lilsmoke.png", [SPARK_ART_MINI_SMOKE] = "minismoke.png",
    [SPARK_ART_BIG_SMOKE] = "bigsmoke.png",  [SPARK_ART_CHIP] = "odprysk.png",       [SPARK_ART_LIL_BLOOD] = "lilblood.png",
    [SPARK_ART_BLOOD] = "blood.png",         [SPARK_ART_SPAWN_SPARK] = "spawnspark.png", [SPARK_ART_JET_FIRE] = "jetfire.png",
};

// The casings, by the weapon that ejects one; the rest eject none.
static const char *const SHELL_STEMS[WEAPON_COUNT] = {
    [WEAPON_EAGLE] = "eagles-shell", [WEAPON_MP5] = "mp5-shell",   [WEAPON_AK74] = "ak74-shell",       [WEAPON_STEYR] = "steyraug-shell",
    [WEAPON_RUGER] = "ruger77-shell", [WEAPON_BARRETT] = "barretm82-shell", [WEAPON_M249] = "m249-shell", [WEAPON_MINIGUN] = "minigun-shell",
    [WEAPON_COLT] = "colt-shell",
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
    case SPARK_JET_FIRE: return true;
    default: return false;
    }
}

static bool collides(SparkStyle style)
{
    return style == SPARK_LIL_BLOOD || style == SPARK_BLOOD || style == SPARK_SHELL || style == SPARK_JET_FIRE;
}

void sparks_load(Sparks *s, const char *base)
{
    char path[512], dir[512];
    for (int k = 0; k < SPARK_ART_COUNT; k++) {
        snprintf(path, sizeof path, "%s/sparks-gfx/%s", base, ART_FILES[k]);
        if (!sprite_load(&s->art[k], path, &GREEN)) s->art[k] = (Sprite){0};
    }
    for (int i = 0; i < EXPLOSION_FRAMES; i++) {
        snprintf(path, sizeof path, "%s/sparks-gfx/explosion/explode%d.png", base, i + 1);
        if (!sprite_load(&s->explode[i], path, &GREEN)) s->explode[i] = (Sprite){0};
    }
    for (int i = 0; i < SMOKE_FRAMES; i++) {
        snprintf(path, sizeof path, "%s/sparks-gfx/explosion/smoke%d.png", base, i + 1);
        if (!sprite_load(&s->smoke[i], path, &GREEN)) s->smoke[i] = (Sprite){0};
    }
    snprintf(dir, sizeof dir, "%s/weapons-gfx", base);
    for (int id = 0; id < WEAPON_COUNT; id++) {
        s->shells[id] = (Sprite){0};
        if (!SHELL_STEMS[id]) continue;
        char name[128];
        snprintf(name, sizeof name, "%s.png", SHELL_STEMS[id]);
        if (find_image(dir, name, path, sizeof path)) sprite_load(&s->shells[id], path, NULL);
    }
    if (find_image(dir, "shell.png", path, sizeof path)) sprite_load(&s->shell, path, NULL);
    s->rng = 0x853C49E6748FEA9Bull;
    s->loaded = true;
}

void sparks_unload(Sparks *s)
{
    for (int k = 0; k < SPARK_ART_COUNT; k++) sprite_unload(&s->art[k]);
    for (int i = 0; i < EXPLOSION_FRAMES; i++) sprite_unload(&s->explode[i]);
    for (int i = 0; i < SMOKE_FRAMES; i++) sprite_unload(&s->smoke[i]);
    for (int id = 0; id < WEAPON_COUNT; id++) sprite_unload(&s->shells[id]);
    sprite_unload(&s->shell);
    *s = (Sparks){0};
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
        *spark = (Spark){.style = style, .life = life, .pos = pos, .vel = vel, .color = color};
        return spark;
    }
    return NULL;
}

static void add(Sparks *s, Vec2 pos, Vec2 vel, SparkStyle style, float life)
{
    spark_add(s, pos, vel, style, life, RGBA_WHITE);
}

// Point collision as TSpark.CheckMapCollision does it, with its probe offset of (-8, -1).
static void spark_collide(const Map *map, Spark *spark)
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
        return;
    }
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
    if (SHELL_STEMS[f->weapon]) {
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

static void sparks_event(Sparks *s, const Context *ctx, const World *w, const Event *e)
{
    switch (e->type) {
    case EVENT_WALL_HIT: wall_hit(s, e->wall_hit.pos, e->wall_hit.vel); break;
    case EVENT_RICOCHET: wall_hit(s, e->ricochet.pos, e->ricochet.vel); break;
    case EVENT_COLLIDER_HIT: wall_hit(s, e->collider_hit.pos, e->collider_hit.vel); break;
    case EVENT_THING_HIT:
        add(s, e->thing_hit.pos, vec2_scale(e->thing_hit.vel, -0.02f * (0.4f + rand01(s) * 0.4f)), SPARK_SMOKE, 70);
        break;
    case EVENT_BLOOD: blood(s, e->blood.pos, e->blood.vel); break;
    case EVENT_EXPLOSION: explosion(s, e->explosion.pos, e->explosion.weapon, e->explosion.radius); break;
    case EVENT_CLUSTER_SPLIT: add(s, e->cluster_split.pos, vec2(0, 0), SPARK_SPLIT_SMOKE, 55); break;
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
        if (!soldier->active || soldier->dead || !(soldier->controls & BUTTON_JET) || soldier->jets <= 0) continue;
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

// The corpses bleed from where they were cut (TSprite.Update's dead branch): every body
// point an end of a torn constraint hangs off drips, thrown along the way that point is
// moving. It thins after two seconds and stops after five, and thins again while the
// screen is already full of sparks, so a pile of bodies does not drown everything else.
static void corpses(Sparks *s, const Context *ctx, const World *w)
{
    int live = 0;
    for (int i = 0; i < MAX_SPARKS; i++) live += s->pool[i].style != SPARK_NONE;
    int base = live > 300 ? BLOOD_RANDOM_LOW : live > 50 ? BLOOD_RANDOM_NORMAL : BLOOD_RANDOM_HIGH;
    const ParticleObject *skeleton = &ctx->skeletons->gostek;

    for (int i = 0; i < MAX_PLAYERS; i++) {
        const Soldier *soldier = &w->soldiers[i];
        const Ragdoll *r = &w->ragdolls[i];
        if (!soldier->active || !soldier->dead || !r->active || r->torn == 0) continue;
        int odds = base;
        if (r->dead_time > LESSBLEED_TIME) odds *= 2;
        if (r->dead_time > NOBLEED_TIME) odds *= 100;
        for (int point = 0; point < POSE_POINTS; point++) {
            for (int ci = 0; ci < skeleton->constraint_count && ci < 32; ci++) {
                const int *c = skeleton->constraints[ci];
                if (!(r->torn >> ci & 1) || (c[0] != point && c[1] != point)) continue;
                if (ci == 9 || ci == 10) continue; // the two the original leaves dry
                Vec2 at = vec2_add(r->pos[point], vec2(0, 2));
                Vec2 vel = vec2_scale(vec2_sub(r->pos[point], r->old_pos[point]), 0.35f);
                if (rand_n(s, odds) == 0) add(s, at, vel, SPARK_BLOOD, 85.0f - (float)rand_n(s, 25));
                else if (rand_n(s, odds / 3 > 1 ? odds / 3 : 1) == 0) add(s, at, vel, SPARK_LIL_BLOOD, 85.0f - (float)rand_n(s, 25));
            }
        }
    }
}

void sparks_tick(Sparks *s, const Context *ctx, const World *w, const Events *events)
{
    if (!s->loaded) return;
    for (int i = 0; i < events->count; i++) sparks_event(s, ctx, w, &events->items[i]);
    jets(s, ctx, w);
    corpses(s, ctx, w);

    for (int i = 0; i < MAX_SPARKS; i++) {
        Spark *spark = &s->pool[i];
        if (spark->style == SPARK_NONE) continue;
        if (moves(spark->style)) {
            spark->vel.y += SPARK_GRAVITY;
            spark->pos = vec2_add(spark->pos, spark->vel);
            spark->vel = vec2_scale(spark->vel, SPARK_DAMPING);
        }
        if (collides(spark->style)) spark_collide(ctx->map, spark);
        spark->life -= 1.0f;
        if (spark->life <= 0.0f) spark->style = SPARK_NONE;
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

void sparks_draw(const Sparks *s)
{
    if (!s->loaded) return;
    const Rgba white = RGBA_WHITE;
    for (int i = 0; i < MAX_SPARKS; i++) {
        const Spark *spark = &s->pool[i];
        float l = spark->life;
        Vec2 p = spark->pos;
        switch (spark->style) {
        case SPARK_NONE: break;
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
            float spin = spark->weapon == WEAPON_BARRETT ? 3.5f : 4.0f;
            draw_spark(shell, p, 1, l * spin * RAD_PER_DEG, 255, white);
            break;
        }
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
