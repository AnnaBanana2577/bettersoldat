#include "render/render.h"

#include <rlgl.h>

#define BONE_THICKNESS 1.2f

static Vector2 rv(Vec2 v) { return (Vector2){v.x, v.y}; }

void render_init(Render *r, const char *base, const Context *ctx)
{
    *r = (Render){.bones = &ctx->skeletons->gostek};
    gostek_load(&r->gostek, base);
    map_view_load(&r->map_view, base, ctx->map);
}

void render_destroy(Render *r)
{
    map_view_unload(&r->map_view);
    gostek_unload(&r->gostek);
}

static void draw_soldiers(const Render *r, const RenderState *state)
{
    for (int i = 0; i < MAX_PLAYERS; i++) {
        const RenderSoldier *s = &state->soldiers[i];
        if (s->active) gostek_draw(&r->gostek, s, false);
    }
}

// The skeleton under the sprites: the gostek's own constraints are the bones.
static void draw_bones(const Render *r, const RenderState *state)
{
    for (int i = 0; i < MAX_PLAYERS; i++) {
        const RenderSoldier *s = &state->soldiers[i];
        if (!s->active) continue;
        for (int c = 0; c < r->bones->constraint_count; c++) {
            int a = r->bones->constraints[c][0], b = r->bones->constraints[c][1];
            if (a < 0 || b < 0 || a >= POSE_POINTS || b >= POSE_POINTS) continue;
            DrawLineEx(rv(s->pose.p[a]), rv(s->pose.p[b]), BONE_THICKNESS, LIME);
        }
    }
}

static Color poly_debug_color(PolyType type)
{
    switch (type) {
    case POLY_DEADLY:
    case POLY_BLOODY_DEADLY:
    case POLY_EXPLODES: return RED;
    case POLY_HURTS:
    case POLY_LAVA: return ORANGE;
    case POLY_ICE: return SKYBLUE;
    case POLY_BOUNCY: return MAGENTA;
    case POLY_ONLY_BULLETS: return BLUE;
    case POLY_ONLY_PLAYER: return GREEN;
    case POLY_BACKGROUND:
    case POLY_BACKGROUND_TRANSITION: return DARKBLUE;
    default: return YELLOW; // team and flagger polys
    }
}

static void draw_cross(Vec2 at, float size, Color color)
{
    DrawLineV(rv(vec2_add(at, vec2(-size, -size))), rv(vec2_add(at, vec2(size, size))), color);
    DrawLineV(rv(vec2_add(at, vec2(-size, size))), rv(vec2_add(at, vec2(size, -size))), color);
}

// What the map is made of that the picture doesn't show: the special polys, the
// colliders, the spawn points by team (then the flags', kits' and stat guns').
static void draw_map_debug(const Map *map)
{
    for (int i = 0; i < map->poly_count; i++) {
        const Polygon *poly = &map->polys[i];
        if (poly->type == POLY_NORMAL) continue;
        Color c = poly_debug_color((PolyType)poly->type);
        for (int k = 0; k < 3; k++) DrawLineV(rv(poly->verts[k]), rv(poly->verts[(k + 1) % 3]), c);
    }

    for (int i = 0; i < map->collider_count; i++) {
        if (map->colliders[i].active) DrawCircleLinesV(rv(map->colliders[i].pos), map->colliders[i].radius, WHITE);
    }

    const Color team_colors[] = {WHITE, RED, BLUE, YELLOW, GREEN};
    for (int i = 0; i < map->spawnpoint_count; i++) {
        const Spawnpoint *s = &map->spawnpoints[i];
        if (s->active) draw_cross(s->pos, 5.0f, s->team >= 0 && s->team <= 4 ? team_colors[s->team] : PURPLE);
    }
}

void render_draw(const Render *r, const RenderState *state, const GameCamera *camera, RenderOptions options)
{
    const MapView *v = &r->map_view;
    if (!v->map) {
        ClearBackground(BLACK);
        return;
    }

    Rgba bg = v->map->bg_bottom;
    ClearBackground((Color){bg.r, bg.g, bg.b, bg.a});
    BeginMode2D(camera_rl(camera));
    rlDisableBackfaceCulling(); // the map's triangles wind either way

    map_draw_background(v->map, camera);
    map_draw_background_polys(v);
    map_draw_scenery(v, 0);
    draw_soldiers(r, state);
    map_draw_scenery(v, 1);
    map_draw_terrain(v);
    map_draw_scenery(v, 2);

    if (options.wireframe) map_draw_wireframe(v->map);
    if (options.debug) {
        draw_map_debug(v->map);
        draw_bones(r, state);
    }
    EndMode2D();
}
