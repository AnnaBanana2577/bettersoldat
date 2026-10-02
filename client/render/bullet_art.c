#include "render/bullet_art.h"

#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <stdio.h>

#include "game/systems/systems.h"
#include "render/textures.h"

#define BULLET_TRAIL 13.0f
#define BULLET_LENGTH 21.0f // BULLETLENGTH: a shot run forward trails at most its run over this, in its art's scale
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

static bool load_stem(Sprite *s, const char *dir, const char *stem, const Rgba *key, bool colorizable)
{
    char name[128], path[512];
    *s = (Sprite){0};
    if (!stem) return true;
    snprintf(name, sizeof name, "%s.png", stem);
    if (!find_image(dir, name, path, sizeof path)) return false;
    if (colorizable) return sprite_load_colorizable(s, path);
    return sprite_load(s, path, key);
}

void bullet_art_load(BulletArt *b, const char *base)
{
    char dir[512];
    snprintf(dir, sizeof dir, "%s/weapons-gfx", base);
    int missing = 0;
    for (int id = 0; id < WEAPON_COUNT; id++) missing += !load_stem(&b->weapons[id], dir, BULLET_STEMS[id], NULL, false);
    for (int k = 0; k < BULLET_ART_SHARED_COUNT; k++) {
        bool colorizable = k == BULLET_ART_FRAG_GRENADE || k == BULLET_ART_CLUSTER_GRENADE || k == BULLET_ART_CLUSTER;
        missing += !load_stem(&b->shared[k], dir, SHARED_STEMS[k], NULL, colorizable);
    }
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

static void draw_one_colorized(Sprite sprite, Vec2 at, Vec2 scale, float angle, Rgba color)
{
    if (sprite.tex.handle == 0) return;
    draw_sprite_colorized(sprite, at, vec2(0, 0), scale, angle, color);
}

static void bullet_draw(const BulletArt *b, const Bullet *bullet, float alpha, Rgba grenade_color, double seconds)
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
        draw_one_colorized(b->shared[BULLET_ART_FRAG_GRENADE], vec2_sub(pos, vec2(1, 4)), vec2(1, 1), 0, grenade_color);
        break;
    case BULLET_CLUSTER_NADE: {
        float turn = timeout * -5.0f * (float)M_PI / 180.0f * (vel.x < 0 ? -1.0f : 1.0f);
        draw_one_colorized(b->shared[BULLET_ART_CLUSTER_GRENADE], vec2_sub(pos, vec2(0, 3)), vec2(1, 1), turn, grenade_color);
        break;
    }
    case BULLET_CLUSTER:
        draw_one_colorized(b->shared[BULLET_ART_CLUSTER], vec2_sub(pos, vec2(0, 2)), vec2(1, 1), 0, grenade_color);
        break;
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
        if (bullet->ping_add < 1) draw_streak(own, vec2_add(pos, vel), vec2(stretch, 1), heading, (Rgba){255, 255, 255, (uint8_t)a});
        // A shot heard and run forward (TBullet.Render, PingAdd): instead of the round, its
        // own art stretched back from it toward where it was fired, over the distance it
        // skipped, shrinking as the run's ticks count down; fainter still once it is gone.
        if (bullet->ping_add > 0) {
            float run = vec2_length(vec2_sub(pos, bullet->initial));
            float length = run * minf(1.0f / BULLET_LENGTH, ((float)(bullet->ping_add + 2) / (float)bullet->ping_add_start) / BULLET_TRAIL);
            float faint = bullet->active ? a / 6.0f : a / 4.0f;
            draw_streak(own, vec2_add(pos, vel), vec2(length, 1), heading, (Rgba){255, 255, 255, (uint8_t)roundf(faint)});
        }
        // the trail is the weapon's own art at half alpha; a round that hit someone trails pink
        if (timeout < BULLET_TIMEOUT - 7) {
            if (bullet->hit_body >= 0) draw_streak(own, pos, vec2(speed / 4, 1), heading, (Rgba){255, 222, 222, half});
            else draw_streak(own, pos, vec2(speed / 3.5f, 1), heading, (Rgba){255, 255, 255, half});
        }
        break;
    }
    }
}

void bullets_draw(const BulletArt *b, const Bullet *bullets, float alpha, Rgba grenade_color, double seconds)
{
    if (!b->loaded) return;
    // a shot run forward is drawn on while its trail lasts, gone or not (GameRendering.pas)
    for (int i = 0; i < MAX_BULLETS; i++)
        if (bullets[i].active || bullets[i].ping_add > 0) bullet_draw(b, &bullets[i], alpha, grenade_color, seconds);
}
