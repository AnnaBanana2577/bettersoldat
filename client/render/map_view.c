#include "render/map_view.h"

#include <float.h>
#include <stdlib.h>
#include <string.h>

#include "render/textures.h"

static bool is_background(const Polygon *poly)
{
    return poly->type == POLY_BACKGROUND || poly->type == POLY_BACKGROUND_TRANSITION;
}

// The polygons that are (or aren't) background, three vertices each, appended at `out`.
// Returns how many vertices went out.
static int poly_vertices(const Map *map, bool background, GfxVertex *out)
{
    int n = 0;
    for (int i = 0; i < map->poly_count; i++) {
        const Polygon *poly = &map->polys[i];
        if (is_background(poly) != background) continue;
        for (int k = 0; k < 3; k++) {
            out[n++] = gfx_vertex(poly->verts[k].x, poly->verts[k].y, poly->uvs[k].x, poly->uvs[k].y, poly->colors[k]);
        }
    }
    return n;
}

static void polys_build(MapPolys *p, const Map *map)
{
    *p = (MapPolys){0};
    if (map->poly_count == 0) return;
    GfxVertex *v = malloc((size_t)map->poly_count * 3 * sizeof(GfxVertex));
    if (!v) return;
    p->background_count = poly_vertices(map, true, v);
    p->terrain_count = poly_vertices(map, false, v + p->background_count);
    p->buffer = gfx_buffer_create(v, p->background_count + p->terrain_count);
    free(v);
}

void map_view_load(MapView *v, const char *base, const Map *map)
{
    map_view_unload(v);
    v->map = map;
    v->texture = map_texture_load(base, map->texture);
    v->scenery = scenery_load(base, map);
    polys_build(&v->polys, map);
}

void map_view_unload(MapView *v)
{
    if (!v->map) return;
    gfx_texture_delete(&v->minimap.tex);
    gfx_buffer_delete(&v->polys.buffer);
    scenery_unload(v->scenery, v->map->scenery_count);
    gfx_texture_delete(&v->texture);
    memset(v, 0, sizeof(*v));
}

void map_draw_background_polys(const MapView *v)
{
    gfx_draw_buffer(v->polys.buffer, v->texture, 0, v->polys.background_count);
}

void map_draw_terrain(const MapView *v)
{
    gfx_draw_buffer(v->polys.buffer, v->texture, v->polys.background_count, v->polys.terrain_count);
}

static bool same_color(Rgba a, Rgba b) { return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a; }

bool map_view_force_background(MapView *v, bool force, Rgba top, Rgba bottom)
{
    top.a = bottom.a = 255;
    bool same = v->force_bg == force && (!force || (same_color(v->forced_top, top) && same_color(v->forced_bottom, bottom)));
    v->force_bg = force;
    v->forced_top = top;
    v->forced_bottom = bottom;
    return !same;
}

void map_view_background(const MapView *v, Rgba *top, Rgba *bottom)
{
    if (v->force_bg) {
        *top = v->forced_top;
        *bottom = v->forced_bottom;
    } else {
        *top = v->map ? v->map->bg_top : (Rgba){0, 0, 0, 255};
        *bottom = v->map ? v->map->bg_bottom : (Rgba){0, 0, 0, 255};
    }
    top->a = bottom->a = 255; // as the original forces them
}

// The sky gradient. The original anchors it in world space vertically, spanning +/-d
// about the origin, and stretches it across the view, so it scrolls with the camera.
void map_draw_background(const MapView *v, const GameCamera *camera)
{
    const Map *map = v->map;
    float d = (float)MAX_SECTOR * fmaxf((float)map->sectors_division, ceilf(0.5f * GAME_HEIGHT / (float)MAX_SECTOR));
    float half_width = camera_view_size(camera).x / 2;
    float x0 = camera->pos.x - half_width, x1 = camera->pos.x + half_width;
    Rgba top, bottom;
    map_view_background(v, &top, &bottom);

    GfxVertex quad[4] = {
        gfx_vertex(x0, -d, 0, 0, top),
        gfx_vertex(x1, -d, 0, 0, top),
        gfx_vertex(x1, d, 0, 0, bottom),
        gfx_vertex(x0, d, 0, 0, bottom),
    };
    gfx_draw_quad(gfx_white(), quad);
}

// The quad reproduces the original's GfxMat3Transform: the map's position is the prop's
// top-left, its size is the map's width and height times the scale (not the image's own
// size), rotated about a pivot one unit below the anchor.
static void prop_corners(const MapProp *prop, Vec2 out[4])
{
    Mat3 m = mat3_transform(prop->pos.x, prop->pos.y, prop->scale.x, prop->scale.y, 0.0f, 1.0f, -prop->rotation);
    float w = (float)prop->width, h = (float)prop->height;
    const Vec2 local[4] = {{0, 0}, {w, 0}, {w, h}, {0, h}};
    for (int i = 0; i < 4; i++) out[i] = mat3_apply(m, local[i]);
}

void map_draw_scenery(const MapView *v, uint8_t layer)
{
    const Map *map = v->map;
    for (int i = 0; i < map->prop_count; i++) {
        const MapProp *prop = &map->props[i];
        if (prop->level != layer || prop->style == 0) continue;
        GfxTexture tex = v->scenery[prop->style - 1];
        if (tex.handle == 0) continue;

        Vec2 p[4];
        prop_corners(prop, p);
        Rgba c = prop->color;
        c.a = prop->alpha; // the original's: the prop's own alpha, whatever its colour's says

        GfxVertex quad[4] = {
            gfx_vertex(p[0].x, p[0].y, 0, 0, c),
            gfx_vertex(p[1].x, p[1].y, 1, 0, c),
            gfx_vertex(p[2].x, p[2].y, 1, 1, c),
            gfx_vertex(p[3].x, p[3].y, 0, 1, c),
        };
        gfx_draw_quad(tex, quad);
    }
}

void map_draw_wireframe(const Map *map, const GameCamera *camera)
{
    float thickness = 1.0f / pixels_per_unit(camera);
    for (int i = 0; i < map->poly_count; i++) {
        const Polygon *poly = &map->polys[i];
        for (int k = 0; k < 3; k++) gfx_draw_line(poly->verts[k], poly->verts[(k + 1) % 3], thickness, RGBA_WHITE);
    }
}

void map_view_draw(const MapView *v, const GameCamera *camera, unsigned parts)
{
    if (!v->map) return;
    if (parts & MAP_PART_BACKGROUND) map_draw_background(v, camera);
    if (parts & MAP_PART_POLYGONS) map_draw_background_polys(v);
    if (parts & MAP_PART_SCENERY) {
        map_draw_scenery(v, 0);
        map_draw_scenery(v, 1);
    }
    if (parts & MAP_PART_POLYGONS) map_draw_terrain(v);
    if (parts & MAP_PART_SCENERY) map_draw_scenery(v, 2);
    if (parts & MAP_PART_WIREFRAME) map_draw_wireframe(v->map, camera);
}

void map_view_bounds(const Map *map, Vec2 *low, Vec2 *high)
{
    if (!map || map->poly_count == 0) {
        *low = (Vec2){-640, -480};
        *high = (Vec2){640, 480};
        return;
    }
    *low = (Vec2){FLT_MAX, FLT_MAX};
    *high = (Vec2){-FLT_MAX, -FLT_MAX};
    for (int i = 0; i < map->poly_count; i++) {
        for (int k = 0; k < 3; k++) {
            Vec2 p = map->polys[i].verts[k];
            low->x = fminf(low->x, p.x);
            low->y = fminf(low->y, p.y);
            high->x = fmaxf(high->x, p.x);
            high->y = fmaxf(high->y, p.y);
        }
    }
}

// --- the minimap -------------------------------------------------------------------

static int npot(int x)
{
    int n = 1;
    while (n < x) n *= 2;
    return n;
}

// The original's minimap: the sky and every polygon drawn four times over into a
// target, then that shrunk into the texture the interface shows, so the edges come out
// smooth.
void map_view_build_minimap(MapView *v, float render_height)
{
    Minimap *m = &v->minimap;
    gfx_texture_delete(&m->tex);
    if (!v->map) return;
    const Map *map = v->map;

    Vec2 low, high;
    map_view_bounds(map, &low, &high);
    float bw = high.x - low.x, bh = high.y - low.y;
    if (bw <= 0 || bh <= 0) return;

    // 260 interface units of width plus height, in pixels at this window
    float unit = render_height / GAME_HEIGHT; // pixels per interface unit
    float w = 260.0f * unit;
    float sx = w / (bw + bh);
    int i = (int)roundf(sx * bw), j = (int)roundf(sx * bh);
    if (i <= 0 || j <= 0) return;

    m->scale = sx / unit;
    m->offset = low;
    m->width = (float)i / unit;
    m->height = (float)j / unit;

    // four times over: the sky above and below the map, the gradient, the polygons
    const int n = 4;
    GfxTexture big = gfx_render_target_create(npot(n * i), npot(n * j));
    if (big.handle == 0) return;
    gfx_target(&big);
    gfx_viewport(0, 0, n * i, n * j);
    gfx_clear((Rgba){0, 0, 0, 0});

    float d = (float)MAX_SECTOR * fmaxf((float)map->sectors_division, ceilf(0.5f * GAME_HEIGHT / (float)MAX_SECTOR));
    Rgba top, bottom;
    map_view_background(v, &top, &bottom);
    gfx_transform(mat3_ortho(0, 1, low.y, high.y));
    GfxVertex above[4] = {
        gfx_vertex(0, fminf(-d, low.y), 0, 0, top), gfx_vertex(1, fminf(-d, low.y), 0, 0, top),
        gfx_vertex(1, low.y, 0, 0, top),            gfx_vertex(0, low.y, 0, 0, top),
    };
    GfxVertex below[4] = {
        gfx_vertex(0, high.y, 0, 0, bottom),           gfx_vertex(1, high.y, 0, 0, bottom),
        gfx_vertex(1, fmaxf(d, high.y), 0, 0, bottom), gfx_vertex(0, fmaxf(d, high.y), 0, 0, bottom),
    };
    GfxVertex sky[4] = {
        gfx_vertex(0, -d, 0, 0, top), gfx_vertex(1, -d, 0, 0, top),
        gfx_vertex(1, d, 0, 0, bottom), gfx_vertex(0, d, 0, 0, bottom),
    };
    gfx_draw_quad(gfx_white(), above);
    gfx_draw_quad(gfx_white(), below);
    gfx_draw_quad(gfx_white(), sky);

    gfx_transform(mat3_ortho(low.x, high.x, low.y, high.y));
    gfx_draw_buffer(v->polys.buffer, v->texture, 0, v->polys.background_count + v->polys.terrain_count);

    // shrunk into the texture the interface draws
    m->tex = gfx_render_target_create(npot(i), npot(j));
    if (m->tex.handle == 0) {
        gfx_target(NULL);
        gfx_texture_delete(&big);
        return;
    }
    gfx_target(&m->tex);
    gfx_viewport(0, 0, i, j);
    gfx_clear((Rgba){0, 0, 0, 0});
    gfx_transform(mat3_ortho(0, 1, 0, 1));
    float u = (float)(n * i) / (float)big.width, vv = (float)(n * j) / (float)big.height;
    GfxVertex quad[4] = {
        gfx_vertex(0, 0, 0, 0, RGBA_WHITE), gfx_vertex(1, 0, u, 0, RGBA_WHITE),
        gfx_vertex(1, 1, u, vv, RGBA_WHITE), gfx_vertex(0, 1, 0, vv, RGBA_WHITE),
    };
    gfx_draw_quad(big, quad);
    gfx_target(NULL);
    gfx_texture_delete(&big);

    m->u1 = (float)i / (float)m->tex.width;
    m->v1 = (float)j / (float)m->tex.height;
    gfx_texture_filter(m->tex, true);
}

Vec2 map_view_to_minimap(const MapView *v, Vec2 world)
{
    const Minimap *m = &v->minimap;
    return (Vec2){(world.x - m->offset.x) * m->scale, (world.y - m->offset.y) * m->scale};
}
