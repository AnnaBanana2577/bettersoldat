#include "render/things_art.h"

#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <stdio.h>

#include "game/systems/systems.h"
#include "render/textures.h"

// The loose art of dropped guns: the held art, except the pistols and the bow, which
// have a version without the hand.
static const char *const GUN_STEMS[WEAPON_COUNT] = {
    [WEAPON_EAGLE] = "n-deserteagle", [WEAPON_MP5] = "mp5",       [WEAPON_AK74] = "ak74",       [WEAPON_STEYR] = "steyraug",
    [WEAPON_SPAS] = "spas12",         [WEAPON_RUGER] = "ruger77", [WEAPON_M79] = "m79",         [WEAPON_BARRETT] = "barretm82",
    [WEAPON_M249] = "m249",           [WEAPON_MINIGUN] = "minigun", [WEAPON_COLT] = "n-colt1911", [WEAPON_KNIFE] = "knife",
    [WEAPON_CHAINSAW] = "chainsaw",   [WEAPON_LAW] = "law",       [WEAPON_BOW] = "n-bow",       [WEAPON_BOW2] = "n-bow",
    [WEAPON_FLAMER] = "flamer",
};

static const char *const KIT_STEMS[THING_STYLE_COUNT] = {
    [THING_MEDICAL_KIT] = "medikit",   [THING_GRENADE_KIT] = "grenadekit", [THING_FLAMER_KIT] = "flamerkit",
    [THING_PREDATOR_KIT] = "predatorkit", [THING_VEST_KIT] = "vestkit",    [THING_BERSERK_KIT] = "berserkerkit",
    [THING_CLUSTER_KIT] = "clusterkit",
};

// The cloth tints: base, top, low, so it reads as lit from one side.
static const Rgba ALPHA_TINT[3] = {{173, 20, 20, 255}, {181, 20, 20, 255}, {148, 20, 20, 255}};
static const Rgba BRAVO_TINT[3] = {{5, 15, 173, 255}, {5, 15, 181, 255}, {5, 15, 148, 255}};

static const Rgba GREEN = {0, 255, 0, 255};

static void load_at(Sprite *s, const char *base, const char *rel)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", base, rel);
    if (!sprite_load(s, path, &GREEN)) *s = (Sprite){0};
}

static void load_found(Sprite *s, const char *dir, const char *stem, const char *suffix)
{
    char name[128], path[512];
    *s = (Sprite){0};
    snprintf(name, sizeof name, "%s%s.png", stem, suffix);
    if (find_image(dir, name, path, sizeof path)) sprite_load(s, path, &GREEN);
}

void things_art_load(ThingsArt *a, const char *base)
{
    char dir[512];
    load_at(&a->cloth, base, "textures/objects/flag.bmp");
    for (int style = 0; style < THING_STYLE_COUNT; style++) {
        a->kits[style] = (Sprite){0};
        if (!KIT_STEMS[style]) continue;
        char rel[128];
        snprintf(rel, sizeof rel, "textures/objects/%s.png", KIT_STEMS[style]);
        load_at(&a->kits[style], base, rel);
    }
    load_at(&a->handle, base, "objects-gfx/flag.png");
    load_at(&a->glow, base, "objects-gfx/ilum.png");
    snprintf(dir, sizeof dir, "%s/weapons-gfx", base);
    for (int id = 0; id < WEAPON_COUNT; id++) {
        a->guns[id][0] = a->guns[id][1] = (Sprite){0};
        if (!GUN_STEMS[id]) continue;
        load_found(&a->guns[id][0], dir, GUN_STEMS[id], "");
        load_found(&a->guns[id][1], dir, GUN_STEMS[id], "-2");
    }
    load_at(&a->para[0], base, "gostek-gfx/para.png");
    load_at(&a->para[1], base, "gostek-gfx/para2.png");
    load_at(&a->rope, base, "gostek-gfx/para-rope.png");
    load_at(&a->m2_base, base, "weapons-gfx/m2-stat.png");
    load_at(&a->m2[0], base, "weapons-gfx/m2.png");
    load_at(&a->m2[1], base, "weapons-gfx/m2-2.png");
    a->loaded = true;
}

void things_art_unload(ThingsArt *a)
{
    sprite_unload(&a->cloth);
    for (int style = 0; style < THING_STYLE_COUNT; style++) sprite_unload(&a->kits[style]);
    sprite_unload(&a->handle);
    sprite_unload(&a->glow);
    for (int id = 0; id < WEAPON_COUNT; id++) {
        sprite_unload(&a->guns[id][0]);
        sprite_unload(&a->guns[id][1]);
    }
    sprite_unload(&a->para[0]);
    sprite_unload(&a->para[1]);
    sprite_unload(&a->rope);
    sprite_unload(&a->m2_base);
    sprite_unload(&a->m2[0]);
    sprite_unload(&a->m2[1]);
    *a = (ThingsArt){0};
}

static float angle_to(Vec2 from, Vec2 to)
{
    return atan2f(to.y - from.y, to.x - from.x);
}

static void draw_if(Sprite s, Vec2 at, Vec2 center, Vec2 scale, float angle, Rgba color)
{
    if (s.tex.handle) draw_sprite(s, at, center, scale, angle, color);
}

// The pole's half-way point, where the cloth's lifted corner hangs and the glow sits.
static Vec2 pole_half(const Vec2 p[4]) { return vec2_add(p[0], vec2_scale(vec2_sub(p[1], p[0]), 0.5f)); }

// The flag's sprites (TThing.Render): the handle along the pole, from the base toward
// the tip, and the pulsing glow in base.
static void draw_flag_pole(const ThingsArt *a, const Thing *t, const Vec2 p[4], double seconds)
{
    draw_if(a->handle, p[0], vec2(0, 0), vec2(1, 1), angle_to(p[0], p[1]), RGBA_WHITE);
    if (t->in_base) {
        float glow = fabsf(5.0f + 20.0f * sinf(5.1f * (float)seconds));
        draw_if(a->glow, vec2_sub(pole_half(p), vec2(12.5f, 12.5f)), vec2(0, 0), vec2(1, 1), 0, (Rgba){255, 255, 255, alpha8(glow)});
    }
}

// The cloth (TThing.PolygonsRender) hangs from the upper half of the pole: p2 the tip,
// half the lifted handle corner, p4 and p3 the free edge, with the original's UV
// assignment.
static void draw_flag_cloth(const ThingsArt *a, const Thing *t, const Vec2 p[4])
{
    if (!a->cloth.tex.handle) return;
    const Rgba *tint = t->style == THING_ALPHA_FLAG ? ALPHA_TINT : BRAVO_TINT;
    Vec2 corners[4] = {p[1], pole_half(p), p[3], p[2]};
    Vec2 uv[4] = {{0, 0}, {0, 1}, {1, 1}, {1, 0}};
    Rgba colors[4] = {tint[0], tint[1], tint[0], tint[2]};
    draw_quad(a->cloth.tex, corners, uv, colors);
}

// Three ropes from the harness to the canopy corners, then the canopy in the owner's
// shirt colour.
static void draw_parachute(const ThingsArt *a, const Thing *t, const Vec2 p[4], const RenderSoldier *soldiers)
{
    Vec2 targets[3] = {p[1], p[2], p[0]};
    for (int i = 0; i < 3; i++) {
        float angle = angle_to(p[3], targets[i]);
        if (i == 1) angle -= 5.0f * (float)M_PI / 180.0f;
        draw_if(a->rope, vec2_sub(p[3], vec2(0, 0.55f)), vec2(0, a->rope.height / 2), vec2(1, 1), angle, RGBA_WHITE);
    }
    float span = vec2_length(vec2_sub(p[1], p[2])) / 45.83f;
    if (span > 2.0f) return;
    Rgba color = RGBA_WHITE;
    if (t->owner > 0 && t->owner <= MAX_PLAYERS) {
        color = soldiers[t->owner - 1].look.shirt;
        color.a = 255;
    }
    draw_if(a->para[1], p[2], vec2(0, 0), vec2(span, span), angle_to(p[2], p[0]), color);
    draw_if(a->para[0], p[0], vec2(0, 0), vec2(span, span), angle_to(p[0], p[1]), color);
}

void things_draw(const ThingsArt *a, ThingsPass pass, const Thing *things, const RenderSoldier *soldiers, float alpha, double seconds)
{
    if (!a->loaded) return;
    for (int i = 0; i < MAX_THINGS; i++) {
        const Thing *t = &things[i];
        if (t->style == THING_NONE) continue;
        // A flag or a dropped gun about to go flashes its last five seconds (TThing.Render,
        // PolygonsRender). Nothing else does: a kit lies on past its timeout, which runs
        // down to -1000 and stays, and a negative count would never show again.
        bool flag = t->style == THING_ALPHA_FLAG || t->style == THING_BRAVO_FLAG;
        if ((flag || t->style == THING_WEAPON) && t->timeout < 300 && t->timeout % 6 < 3) continue;
        Vec2 p[4];
        for (int k = 0; k < 4; k++) p[k] = vec2_add(t->old_pos[k], vec2_scale(vec2_sub(t->pos[k], t->old_pos[k]), alpha));
        if (pass == THINGS_QUADS) {
            if (flag) draw_flag_cloth(a, t, p);
            else if (thing_is_kit(t->style)) {
                // kit.po numbers its corners bottom first, so the box is drawn upright from the top pair
                Sprite kit = a->kits[t->style];
                if (kit.tex.handle == 0) continue;
                Vec2 corners[4] = {p[2], p[3], p[0], p[1]};
                Vec2 uv[4] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
                Rgba colors[4] = {RGBA_WHITE, RGBA_WHITE, RGBA_WHITE, RGBA_WHITE};
                draw_quad(kit.tex, corners, uv, colors);
            }
            continue;
        }
        switch (t->style) {
        case THING_ALPHA_FLAG:
        case THING_BRAVO_FLAG: draw_flag_pole(a, t, p, seconds); break;
        case THING_WEAPON: {
            Sprite art = a->guns[t->weapon][t->flip ? 1 : 0];
            if (art.tex.handle == 0) art = a->guns[t->weapon][0];
            draw_if(art, vec2_sub(p[0], vec2(0, 1)), vec2(0, 2), vec2(1, 1), angle_to(p[0], p[1]), RGBA_WHITE);
            break;
        }
        case THING_PARACHUTE: draw_parachute(a, t, p, soldiers); break;
        case THING_STAT_GUN: {
            draw_if(a->m2_base, vec2_sub(p[2], vec2(0, 20)), vec2(0, 0), vec2(1, 1), angle_to(p[2], p[1]), RGBA_WHITE);
            float heat = (float)t->interest;
            Rgba tint = {255, alpha8(255 - 10 * heat), alpha8(255 - 13 * heat), 255};
            Sprite gun = a->m2[p[3].x >= p[0].x ? 1 : 0];
            draw_if(gun, vec2_sub(p[0], vec2(0, 13)), vec2(5, 4), vec2(1, 1), -angle_to(p[3], p[0]), tint);
            break;
        }
        default: break; // a kit is a quad, drawn in the other pass
        }
    }
}
