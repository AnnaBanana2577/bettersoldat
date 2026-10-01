#include "render/bullet_art.h"

#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <stdio.h>
#include <string.h>

#include "game/systems/systems.h"
#include "render/textures.h"

#define BULLET_TRAIL 13.0f
#define BULLET_ALPHA 110

// Plain rounds of weapons not listed use the USSOCOM's image, as the original does.
static const char *const BULLET_STEMS[WEAPON_COUNT] = {
    [WEAPON_EAGLE] = "eagles-bullet", [WEAPON_MP5] = "mp5-bullet",         [WEAPON_AK74] = "ak74-bullet",
    [WEAPON_STEYR] = "steyraug-bullet", [WEAPON_SPAS] = "spas12-bullet",   [WEAPON_RUGER] = "ruger77-bullet",
    [WEAPON_M79] = "m79-bullet",     [WEAPON_BARRETT] = "barretm82-bullet", [WEAPON_M249] = "m249-bullet",
    [WEAPON_MINIGUN] = "minigun-bullet", [WEAPON_COLT] = "colt-bullet",    [WEAPON_LAW] = "missile",
    [WEAPON_BOW] = "arrow",          [WEAPON_BOW2] = "arrow",
};

static const char *const SHARED_STEMS[BULLET_ART_SHARED_COUNT] = {
    [BULLET_ART_STREAK] = "bullet",          [BULLET_ART_MISSILE] = "missile", [BULLET_ART_FRAG_GRENADE] = "frag-grenade",
    [BULLET_ART_CLUSTER_GRENADE] = "cluster-grenade", [BULLET_ART_CLUSTER] = "cluster", [BULLET_ART_ARROW] = "arrow",
    [BULLET_ART_KNIFE] = "knife",            [BULLET_ART_KNIFE_LEFT] = "knife2", [BULLET_ART_SMUDGE] = "smudge",
};

static bool load_stem(Sprite *s, const char *dir, const char *stem, const Rgba *key)
{
    char name[128], path[512];
    *s = (Sprite){0};
    if (!stem) return true;
    snprintf(name, sizeof name, "%s.png", stem);
    if (!find_image(dir, name, path, sizeof path)) return false;
    return sprite_load(s, path, key);
}

void bullet_art_load(BulletArt *b, const char *base)
{
    char dir[512];
    snprintf(dir, sizeof dir, "%s/weapons-gfx", base);
    int missing = 0;
    for (int id = 0; id < WEAPON_COUNT; id++) missing += !load_stem(&b->weapons[id], dir, BULLET_STEMS[id], NULL);
    for (int k = 0; k < BULLET_ART_SHARED_COUNT; k++) missing += !load_stem(&b->shared[k], dir, SHARED_STEMS[k], NULL);
    const Rgba green = {0, 255, 0, 255};
    for (int i = 0; i < FLAME_FRAMES; i++) {
        char path[512];
        snprintf(path, sizeof path, "%s/sparks-gfx/flames/explode%d.png", base, i + 1);
        missing += !sprite_load(&b->flames[i], path, &green);
    }
    if (missing > 0) fprintf(stderr, "%d bullet sprites not found under %s\n", missing, base);
    b->loaded = true;
}

void bullet_art_unload(BulletArt *b)
{
    for (int id = 0; id < WEAPON_COUNT; id++) sprite_unload(&b->weapons[id]);
    for (int k = 0; k < BULLET_ART_SHARED_COUNT; k++) sprite_unload(&b->shared[k]);
    for (int i = 0; i < FLAME_FRAMES; i++) sprite_unload(&b->flames[i]);
    *b = (BulletArt){0};
}

// A streak: `at` is its leading point and the art extends back along the heading, so
// it trails the bullet. The image's left edge sits at the head, turned to face back.
static void draw_streak(Sprite sprite, Vec2 at, Vec2 scale, float angle, Rgba color)
{
    if (sprite.tex.handle == 0) return;
    draw_sprite(sprite, at, vec2(0, 0), scale, angle + (float)M_PI, color);
}

// A discrete object (a grenade, a cluster), anchored at its own origin.
static void draw_one(Sprite sprite, Vec2 at, Vec2 scale, float angle, Rgba color)
{
    if (sprite.tex.handle == 0) return;
    draw_sprite(sprite, at, vec2(0, 0), scale, angle, color);
}

static void bullet_draw(const BulletArt *b, const Bullet *bullet, float alpha, double seconds)
{
    Vec2 pos = vec2_add(bullet->old_pos, vec2_scale(vec2_sub(bullet->pos, bullet->old_pos), alpha));
    float timeout = (float)bullet->timeout + 1.0f - alpha; // TimeOutReal
    Vec2 vel = bullet->vel;
    float speed = vec2_length(vel);
    float heading = atan2f(vel.y, vel.x);
    float spin = timeout * -6.0f * (float)M_PI / 180.0f; // the timeout counts down, so it stands in for age
    float sinus = sinf(timeout + 5.1f * (float)seconds); // the M2's wobbling smudge
    Vec2 off = vec2(vel.y > 0 ? -1.0f : 1.0f, vel.x > 0 ? 1.0f : -1.0f);

    Sprite streak = b->shared[BULLET_ART_STREAK];
    Sprite own = b->weapons[bullet->weapon];
    if (own.tex.handle == 0) own = b->weapons[WEAPON_COLT];
    if (own.tex.handle == 0) own = streak;
    uint8_t half = BULLET_ALPHA / 2;

    switch (bullet->style) {
    case BULLET_FRAG_GRENADE:
        if (timeout < GRENADE_TIMEOUT - 3) {
            draw_streak(streak, vec2_sub(vec2_add(pos, off), vec2(0, 3)), vec2(speed / 3, 1), heading, (Rgba){100, 255, 100, 82});
        }
        draw_one(b->shared[BULLET_ART_FRAG_GRENADE], vec2_sub(pos, vec2(1, 4)), vec2(1, 1), 0, RGBA_WHITE);
        break;
    case BULLET_CLUSTER_NADE: {
        float turn = timeout * -5.0f * (float)M_PI / 180.0f * (vel.x < 0 ? -1.0f : 1.0f);
        draw_one(b->shared[BULLET_ART_CLUSTER_GRENADE], vec2_sub(pos, vec2(0, 3)), vec2(1, 1), turn, RGBA_WHITE);
        break;
    }
    case BULLET_CLUSTER: draw_one(b->shared[BULLET_ART_CLUSTER], vec2_sub(pos, vec2(0, 2)), vec2(1, 1), 0, RGBA_WHITE); break;
    case BULLET_M79:
        if (timeout >= BULLET_TIMEOUT - 2) break;
        draw_one(own, vec2_add(pos, vec2(0, 1)), vec2(1, 1), spin, (Rgba){255, 255, 255, 252}); // only the M79 round tumbles
        if (timeout < BULLET_TIMEOUT - 4) {
            draw_streak(streak, vec2_add(pos, off), vec2(speed / 4, 1.3f), heading, (Rgba){255, 255, 85, BULLET_ALPHA});
        }
        break;
    case BULLET_LAW:
        if (timeout >= BULLET_TIMEOUT - 2) break;
        draw_streak(b->shared[BULLET_ART_MISSILE], vec2_add(pos, vel), vec2(1, 1), heading, RGBA_WHITE);
        if (timeout < BULLET_TIMEOUT - 7) draw_streak(streak, pos, vec2(speed / 3, 1), heading, (Rgba){255, 255, 255, BULLET_ALPHA / 5});
        break;
    case BULLET_ARROW:
    case BULLET_FLAME_ARROW:
        if (timeout >= BULLET_TIMEOUT - 2) break;
        draw_streak(b->shared[BULLET_ART_ARROW], vec2_add(pos, vel), vec2(1, 1), heading, RGBA_WHITE);
        if (bullet->style == BULLET_ARROW && timeout > ARROW_RESIST) {
            draw_streak(streak, pos, vec2(speed / 3, 1), heading, (Rgba){255, 255, 255, BULLET_ALPHA / 7});
        }
        break;
    case BULLET_SHOTGUN:
        if (timeout >= BULLET_TIMEOUT - 2) break;
        draw_streak(own, vec2_add(pos, vel), vec2(1, 1), heading, (Rgba){255, 255, 255, 150});
        if (timeout < BULLET_TIMEOUT - 3) draw_streak(streak, pos, vec2(speed / 9, 1), heading, (Rgba){255, 255, 255, BULLET_ALPHA / 5});
        break;
    case BULLET_M2:
        if (timeout >= M2BULLET_TIMEOUT - 2) break;
        draw_streak(streak, vec2_add(pos, vel), vec2(speed / BULLET_TRAIL, 1.2f), heading, (Rgba){255, 191, 120, BULLET_ALPHA * 2});
        if (timeout < M2BULLET_TIMEOUT - 13) {
            draw_streak(streak, pos, vec2(speed / 3, 1), heading, (Rgba){255, 255, 255, BULLET_ALPHA / 5});
            draw_streak(b->shared[BULLET_ART_SMUDGE], pos, vec2(speed / (sinus + 2.5f), sinus), heading,
                        (Rgba){255, 255, 255, BULLET_ALPHA / 6});
        }
        break;
    case BULLET_FLAME: {
        if (timeout <= 0 || timeout > FLAMER_TIMEOUT) break;
        int frame = clampi(FLAME_FRAMES - 1 - (int)(timeout / 2), 0, FLAME_FRAMES - 1);
        draw_one(b->flames[frame], vec2_sub(pos, vec2(8, 17)), vec2(1, 1), 0, RGBA_WHITE);
        break;
    }
    case BULLET_THROWN_KNIFE: {
        float turn = timeout / (float)M_PI;
        Vec2 at = vec2_add(vec2_add(pos, vel), vec2(4, 1));
        if (vel.x >= 0) draw_sprite(b->shared[BULLET_ART_KNIFE], at, vec2(4, 1), vec2(1, 1), -turn, RGBA_WHITE);
        else draw_sprite(b->shared[BULLET_ART_KNIFE_LEFT], at, vec2(4, 1), vec2(1, 1), turn, RGBA_WHITE);
        break;
    }
    case BULLET_PUNCH:
    case BULLET_KNIFE: break; // melee has no projectile art
    default: {
        if (timeout >= BULLET_TIMEOUT - 2) break;
        float stretch = speed / BULLET_TRAIL;
        float a = clampf(bullet->hit_multiply * stretch * stretch / 4.63f * 255.0f, 50.0f, 230.0f);
        draw_streak(own, vec2_add(pos, vel), vec2(stretch, 1), heading, (Rgba){255, 255, 255, (uint8_t)a});
        // the trail is the weapon's own art at half alpha; a round that hit someone trails pink
        if (timeout < BULLET_TIMEOUT - 7) {
            if (bullet->hit_body >= 0) draw_streak(own, pos, vec2(speed / 4, 1), heading, (Rgba){255, 222, 222, half});
            else draw_streak(own, pos, vec2(speed / 3.5f, 1), heading, (Rgba){255, 255, 255, half});
        }
        break;
    }
    }
}

// A grenade, a knife or a blade is not drawn as a streak when it flies, so no tracer
// stands in for one either; nor do a shotgun's six pellets, which would fan out as
// six long lines.
static bool tracer_worthy(WeaponId weapon)
{
    switch (weapon) {
    case WEAPON_SPAS:
    case WEAPON_FRAG:
    case WEAPON_CLUSTER_NADE:
    case WEAPON_KNIFE:
    case WEAPON_CHAINSAW:
    case WEAPON_FLAMER:
    case WEAPON_NONE: return false;
    default: return true;
    }
}

void bullet_art_tick(BulletArt *b, const Events *events)
{
    int kept = 0;
    for (int i = 0; i < b->tracer_count; i++) {
        Tracer *t = &b->tracers[i];
        t->age += 1.0f;
        if (t->age < t->life) b->tracers[kept++] = *t;
    }
    b->tracer_count = kept;
    for (int i = 0; i < events->count; i++) {
        const Event *e = &events->items[i];
        if (e->type != EVENT_BULLET_TRACE || !tracer_worthy(e->bullet_trace.weapon)) continue;
        if (vec2_length(vec2_sub(e->bullet_trace.to, e->bullet_trace.from)) < TRACER_SHORTEST) continue;
        if (b->tracer_count == TRACERS) { // full: the oldest goes
            memmove(b->tracers, b->tracers + 1, (TRACERS - 1) * sizeof b->tracers[0]);
            b->tracer_count--;
        }
        b->tracers[b->tracer_count++] = (Tracer){
            .from = e->bullet_trace.from, .to = e->bullet_trace.to, .weapon = e->bullet_trace.weapon,
            .life = clampf((float)e->bullet_trace.ticks, TRACER_LIFE_MIN, TRACER_LIFE_MAX),
        };
    }
}

// The weapon's own streak stretched the length of the flight, fading as it ages.
static void tracer_draw(const BulletArt *b, const Tracer *t, float alpha)
{
    Sprite own = b->weapons[t->weapon];
    if (own.tex.handle == 0) own = b->weapons[WEAPON_COLT];
    if (own.tex.handle == 0) own = b->shared[BULLET_ART_STREAK];
    if (own.tex.handle == 0 || own.width <= 0.0f) return;
    Vec2 path = vec2_sub(t->to, t->from);
    float length = vec2_length(path);
    float left = 1.0f - clampf((t->age + alpha) / t->life, 0.0f, 1.0f);
    uint8_t a = (uint8_t)(BULLET_ALPHA * left);
    draw_sprite(own, t->from, vec2(0, 0), vec2(length / own.width, 1.0f), atan2f(path.y, path.x), (Rgba){255, 255, 255, a});
}

void bullets_draw(const BulletArt *b, const Bullet *bullets, float alpha, double seconds)
{
    if (!b->loaded) return;
    for (int i = 0; i < b->tracer_count; i++) tracer_draw(b, &b->tracers[i], alpha);
    for (int i = 0; i < MAX_BULLETS; i++)
        if (bullets[i].active) bullet_draw(b, &bullets[i], alpha, seconds);
}
