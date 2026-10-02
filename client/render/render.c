#include "render/render.h"

#include <stdio.h>

#define BONE_THICKNESS 1.2f // world units, as the original's debug bones

// Teeworlds' hook art: its head and a link of its chain, cut from its game.png
// (CC BY-SA 3.0, assets/NOTICE.md). Missing, the hook goes undrawn.
static void hook_art_load(Render *r, const char *base)
{
    char path[512];
    snprintf(path, sizeof path, "%s/hook-gfx/hook-head.png", base);
    sprite_load(&r->hook_head, path, NULL);
    snprintf(path, sizeof path, "%s/hook-gfx/hook-chain.png", base);
    sprite_load(&r->hook_chain, path, NULL);
}

void render_init(Render *r, const char *base, const Context *ctx)
{
    *r = (Render){.bones = &ctx->skeletons->gostek};
    gostek_load(&r->gostek, base);
    bullet_art_load(&r->bullet_art, base);
    things_art_load(&r->things_art, base);
    sparks_load(&r->sparks, base);
    hook_art_load(r, base);
    map_view_load(&r->map_view, base, ctx->map);
}

void render_destroy(Render *r)
{
    map_view_unload(&r->map_view);
    sprite_unload(&r->hook_chain);
    sprite_unload(&r->hook_head);
    sparks_unload(&r->sparks);
    things_art_unload(&r->things_art);
    bullet_art_unload(&r->bullet_art);
    gostek_unload(&r->gostek);
}

void render_tick(Render *r, const Context *ctx, const World *w, const Events *events)
{
    sparks_tick(&r->sparks, ctx, w, events);
}

static void draw_soldiers(const Render *r, const RenderState *state, Rgba grenade_color)
{
    for (int i = 0; i < MAX_PLAYERS; i++) {
        const RenderSoldier *s = &state->soldiers[i];
        if (s->active && s->team != TEAM_SPECTATOR) gostek_draw(&r->gostek, s, s->corpse, grenade_color);
    }
}

// Teeworlds' hook art, drawn at Teeworlds' sizes brought to this world as the hook's
// lengths are (game.h, HookTuning): its 24 by 16 head and 16 by 16 links.
#define HOOK_ART_SCALE 0.55f
#define HOOK_HEAD_W (24.0f * HOOK_ART_SCALE)
#define HOOK_HEAD_H (16.0f * HOOK_ART_SCALE)
#define HOOK_LINK (16.0f * HOOK_ART_SCALE)

// `sprite` as a `w` by `h` quad centred on `at`, its right turned to `angle`.
static void draw_centred(Sprite sprite, Vec2 at, float w, float h, float angle)
{
    sprite.width = w;
    sprite.height = h;
    draw_sprite(sprite, at, vec2(w / 2, h / 2), vec2(1, 1), angle, RGBA_WHITE);
}

// The grappling hooks, as Teeworlds' RenderHook draws them (players.cpp): the head
// at the hook, turned away from its owner, and a link every link's length back toward
// the hand that holds it. Drawn behind the soldiers, whose sprites cover that hand.
static void draw_hooks(const Render *r, const RenderState *state)
{
    for (int i = 0; i < MAX_PLAYERS; i++) {
        const RenderSoldier *s = &state->soldiers[i];
        if (!s->active || s->dead || s->hook < HOOK_RETRACT_1) continue;
        Vec2 hand = s->pose.p[14], head = s->hook_pos;
        Vec2 back = vec2_sub(hand, head);
        float distance = vec2_length(back);
        if (distance < 0.001f) continue;
        back = vec2_scale(back, 1.0f / distance);
        float angle = atan2f(-back.y, -back.x); // the way it flew: from the hand out to the head
        draw_centred(r->hook_head, head, HOOK_HEAD_W, HOOK_HEAD_H, angle);
        for (float f = HOOK_LINK; f < distance; f += HOOK_LINK)
            draw_centred(r->hook_chain, vec2_add(head, vec2_scale(back, f)), HOOK_LINK, HOOK_LINK, angle);
    }
}

// The skeleton under the sprites: the gostek's own constraints are the bones.
static void draw_bones(const Render *r, const RenderState *state)
{
    const Rgba lime = {0, 158, 47, 255};
    for (int i = 0; i < MAX_PLAYERS; i++) {
        const RenderSoldier *s = &state->soldiers[i];
        if (!s->active) continue;
        for (int c = 0; c < r->bones->constraint_count; c++) {
            int a = r->bones->constraints[c][0], b = r->bones->constraints[c][1];
            if (a < 0 || b < 0 || a >= POSE_POINTS || b >= POSE_POINTS) continue;
            gfx_draw_line(s->pose.p[a], s->pose.p[b], BONE_THICKNESS, lime);
        }
    }
}

static Rgba poly_debug_color(PolyType type)
{
    switch (type) {
    case POLY_DEADLY:
    case POLY_BLOODY_DEADLY:
    case POLY_EXPLODES: return (Rgba){230, 41, 55, 255};
    case POLY_HURTS:
    case POLY_LAVA: return (Rgba){255, 161, 0, 255};
    case POLY_ICE: return (Rgba){102, 191, 255, 255};
    case POLY_BOUNCY: return (Rgba){255, 0, 255, 255};
    case POLY_ONLY_BULLETS: return (Rgba){0, 121, 241, 255};
    case POLY_ONLY_PLAYER: return (Rgba){0, 228, 48, 255};
    case POLY_BACKGROUND:
    case POLY_BACKGROUND_TRANSITION: return (Rgba){0, 82, 172, 255};
    default: return (Rgba){253, 249, 0, 255}; // team and flagger polys
    }
}

static void draw_cross(Vec2 at, float size, float thickness, Rgba color)
{
    gfx_draw_line(vec2_add(at, vec2(-size, -size)), vec2_add(at, vec2(size, size)), thickness, color);
    gfx_draw_line(vec2_add(at, vec2(-size, size)), vec2_add(at, vec2(size, -size)), thickness, color);
}

static void draw_circle(Vec2 center, float radius, float thickness, Rgba color)
{
    const int segments = 36;
    Vec2 prev = vec2_add(center, vec2(radius, 0));
    for (int i = 1; i <= segments; i++) {
        float a = (float)i / segments * 6.2831853f;
        Vec2 p = vec2_add(center, vec2(radius * cosf(a), radius * sinf(a)));
        gfx_draw_line(prev, p, thickness, color);
        prev = p;
    }
}

// What the map is made of that the picture doesn't show: the special polys, the
// colliders, the spawn points by team (then the flags', kits' and stat guns').
static void draw_map_debug(const Map *map, const GameCamera *camera)
{
    float px = 1.0f / pixels_per_unit(camera); // one pixel, in world units
    for (int i = 0; i < map->poly_count; i++) {
        const Polygon *poly = &map->polys[i];
        if (poly->type == POLY_NORMAL) continue;
        Rgba c = poly_debug_color((PolyType)poly->type);
        for (int k = 0; k < 3; k++) gfx_draw_line(poly->verts[k], poly->verts[(k + 1) % 3], px, c);
    }

    for (int i = 0; i < map->collider_count; i++) {
        if (map->colliders[i].active) draw_circle(map->colliders[i].pos, map->colliders[i].radius, px, RGBA_WHITE);
    }

    const Rgba team_colors[] = {
        {255, 255, 255, 255}, {230, 41, 55, 255}, {0, 121, 241, 255}, {253, 249, 0, 255}, {0, 228, 48, 255},
    };
    const Rgba purple = {200, 122, 255, 255};
    for (int i = 0; i < map->spawnpoint_count; i++) {
        const Spawnpoint *s = &map->spawnpoints[i];
        if (s->active) draw_cross(s->pos, 5.0f, px, s->team >= 0 && s->team <= 4 ? team_colors[s->team] : purple);
    }
}

void render_draw(const Render *r, const RenderState *state, const GameCamera *camera, RenderOptions options, Rgba grenade_color,
                 double seconds)
{
    const MapView *v = &r->map_view;
    if (!v->map) {
        gfx_clear((Rgba){0, 0, 0, 255});
        return;
    }

    Rgba sky_top, sky_bottom;
    map_view_background(v, &sky_top, &sky_bottom);
    gfx_clear(camera->pos.y > 0 ? sky_bottom : sky_top); // the original's choice
    gfx_transform(camera_transform(camera));

    // the original's RenderFrame order: the bullets behind the soldiers, the things'
    // sprites just in front of them, the sparks (explosions among them) under the middle
    // scenery, the things' quads (cloth, kits) over it, and the terrain and the front
    // scenery over everything
    map_draw_background(v, camera);
    map_draw_background_polys(v);
    if (options.scenery) map_draw_scenery(v, 0);
    bullets_draw(&r->bullet_art, state->bullets, state->alpha, grenade_color, seconds);
    draw_hooks(r, state);
    draw_soldiers(r, state, grenade_color);
    things_draw(&r->things_art, THINGS_SPRITES, state->things, state->soldiers, state->alpha, seconds);
    sparks_draw(&r->sparks, state->alpha);
    if (options.scenery) map_draw_scenery(v, 1);
    things_draw(&r->things_art, THINGS_QUADS, state->things, state->soldiers, state->alpha, seconds);
    map_draw_terrain(v);
    if (options.scenery) map_draw_scenery(v, 2);

    if (options.wireframe) map_draw_wireframe(v->map, camera);
    if (options.debug) {
        draw_map_debug(v->map, camera);
        draw_bones(r, state);
    }
}
