#include "render/map_view.h"

#include <float.h>
#include <raymath.h>
#include <rlgl.h>
#include <stdlib.h>
#include <string.h>

#include "render/textures.h"

static bool is_background(const Polygon *poly)
{
    return poly->type == POLY_BACKGROUND || poly->type == POLY_BACKGROUND_TRANSITION;
}

static Mesh build_poly_mesh(const Map *map, bool background)
{
    Mesh mesh = {0};
    int count = 0;
    for (int i = 0; i < map->poly_count; i++) {
        if (is_background(&map->polys[i]) == background) count++;
    }
    if (count == 0) return mesh;

    // raylib frees these with its own allocator on UnloadMesh
    mesh.vertexCount = count * 3;
    mesh.triangleCount = count;
    mesh.vertices = MemAlloc((unsigned)(count * 3 * 3) * sizeof(float));
    mesh.texcoords = MemAlloc((unsigned)(count * 3 * 2) * sizeof(float));
    mesh.colors = MemAlloc((unsigned)(count * 3 * 4));

    int v = 0;
    for (int i = 0; i < map->poly_count; i++) {
        const Polygon *poly = &map->polys[i];
        if (is_background(poly) != background) continue;
        for (int k = 0; k < 3; k++, v++) {
            mesh.vertices[v * 3 + 0] = poly->verts[k].x;
            mesh.vertices[v * 3 + 1] = poly->verts[k].y;
            mesh.vertices[v * 3 + 2] = 0.0f;
            mesh.texcoords[v * 2 + 0] = poly->uvs[k].x;
            mesh.texcoords[v * 2 + 1] = poly->uvs[k].y;
            mesh.colors[v * 4 + 0] = poly->colors[k].r;
            mesh.colors[v * 4 + 1] = poly->colors[k].g;
            mesh.colors[v * 4 + 2] = poly->colors[k].b;
            mesh.colors[v * 4 + 3] = poly->colors[k].a;
        }
    }
    UploadMesh(&mesh, false);
    return mesh;
}

static void meshes_build(MapMeshes *m, const Map *map, Texture2D texture)
{
    m->background = build_poly_mesh(map, true);
    m->terrain = build_poly_mesh(map, false);
    m->material = LoadMaterialDefault();
    if (texture.id != 0) SetMaterialTexture(&m->material, MATERIAL_MAP_ALBEDO, texture);
    m->built = true;
}

static void meshes_unload(MapMeshes *m)
{
    if (!m->built) return;
    if (m->background.vertexCount) UnloadMesh(m->background);
    if (m->terrain.vertexCount) UnloadMesh(m->terrain);
    // The material's texture belongs to the view, so only its shader-less shell goes.
    RL_FREE(m->material.maps);
    m->built = false;
}

void map_view_load(MapView *v, const char *base, const Map *map)
{
    map_view_unload(v);
    v->map = map;
    v->texture = map_texture_load(base, map->texture);
    v->scenery = scenery_load(base, map);
    meshes_build(&v->meshes, map, v->texture);
}

void map_view_unload(MapView *v)
{
    if (!v->map) return;
    meshes_unload(&v->meshes);
    scenery_unload(v->scenery, v->map->scenery_count);
    if (v->texture.id != 0) UnloadTexture(v->texture);
    memset(v, 0, sizeof(*v));
}

// A mesh draws at once while everything else waits in the batch, so the batch is
// flushed first or the mesh ends up underneath what was pushed before it.
static void draw_mesh_now(Mesh mesh, Material material)
{
    if (mesh.vertexCount == 0) return;
    rlDrawRenderBatchActive();
    DrawMesh(mesh, material, MatrixIdentity());
}

void map_draw_background_polys(const MapView *v)
{
    if (v->meshes.built) draw_mesh_now(v->meshes.background, v->meshes.material);
}

void map_draw_terrain(const MapView *v)
{
    if (v->meshes.built) draw_mesh_now(v->meshes.terrain, v->meshes.material);
}

// The sky gradient. The original anchors it in world space vertically, spanning +/-d
// about the origin, and stretches it across the view, so it scrolls with the camera.
void map_draw_background(const Map *map, const GameCamera *camera)
{
    float d = (float)MAX_SECTOR * fmaxf((float)map->sectors_division, ceilf(0.5f * GAME_HEIGHT / (float)MAX_SECTOR));
    float half_width = camera_view_size(camera).x / 2;
    float x0 = camera->pos.x - half_width, x1 = camera->pos.x + half_width;
    Rgba top = map->bg_top, bottom = map->bg_bottom;

    rlSetTexture(0);
    rlBegin(RL_QUADS);
    rlColor4ub(top.r, top.g, top.b, top.a);
    rlVertex2f(x0, -d);
    rlColor4ub(bottom.r, bottom.g, bottom.b, bottom.a);
    rlVertex2f(x0, d);
    rlVertex2f(x1, d);
    rlColor4ub(top.r, top.g, top.b, top.a);
    rlVertex2f(x1, -d);
    rlEnd();
}

// The quad reproduces the original's GfxMat3Transform: the map's position is the prop's
// top-left, its size is the map's width and height times the scale (not the image's own
// size), rotated about a pivot one unit below the anchor.
static void prop_corners(const MapProp *prop, Vec2 out[4])
{
    float angle = -prop->rotation;
    float c = cosf(angle), s = sinf(angle);
    float cx = 0.0f, cy = 1.0f;
    float sx = prop->scale.x, sy = prop->scale.y;
    float m0 = c * sx, m3 = -s * sy;
    float m1 = s * sx, m4 = c * sy;
    float m6 = prop->pos.x + cy * s - c * cx + cx;
    float m7 = prop->pos.y - cx * s - c * cy + cy;
    float w = (float)prop->width, h = (float)prop->height;
    const Vec2 local[4] = {{0, 0}, {w, 0}, {w, h}, {0, h}};
    for (int i = 0; i < 4; i++) {
        out[i] = (Vec2){m0 * local[i].x + m3 * local[i].y + m6, m1 * local[i].x + m4 * local[i].y + m7};
    }
}

void map_draw_scenery(const MapView *v, uint8_t layer)
{
    const Map *map = v->map;
    for (int i = 0; i < map->prop_count; i++) {
        const MapProp *prop = &map->props[i];
        if (prop->level != layer || prop->style == 0) continue;
        Texture2D tex = v->scenery[prop->style - 1];
        if (tex.id == 0) continue;

        Vec2 p[4];
        prop_corners(prop, p);
        Rgba c = prop->color;
        c.a = (uint8_t)((float)c.a * (float)prop->alpha / 255.0f);

        rlSetTexture(tex.id);
        rlBegin(RL_QUADS);
        rlColor4ub(c.r, c.g, c.b, c.a);
        rlTexCoord2f(0, 0);
        rlVertex2f(p[0].x, p[0].y);
        rlTexCoord2f(0, 1);
        rlVertex2f(p[3].x, p[3].y);
        rlTexCoord2f(1, 1);
        rlVertex2f(p[2].x, p[2].y);
        rlTexCoord2f(1, 0);
        rlVertex2f(p[1].x, p[1].y);
        rlEnd();
    }
    rlSetTexture(0);
}

void map_draw_wireframe(const Map *map)
{
    for (int i = 0; i < map->poly_count; i++) {
        const Polygon *poly = &map->polys[i];
        for (int k = 0; k < 3; k++) {
            Vec2 a = poly->verts[k], b = poly->verts[(k + 1) % 3];
            DrawLineV((Vector2){a.x, a.y}, (Vector2){b.x, b.y}, WHITE);
        }
    }
}

void map_view_draw(const MapView *v, const GameCamera *camera, unsigned parts)
{
    if (!v->map) return;
    // raylib culls back faces by default, and a map's triangles wind either way.
    rlDisableBackfaceCulling();

    if (parts & MAP_PART_BACKGROUND) map_draw_background(v->map, camera);
    if (parts & MAP_PART_POLYGONS) map_draw_background_polys(v);
    if (parts & MAP_PART_SCENERY) {
        map_draw_scenery(v, 0);
        map_draw_scenery(v, 1);
    }
    if (parts & MAP_PART_POLYGONS) map_draw_terrain(v);
    if (parts & MAP_PART_SCENERY) map_draw_scenery(v, 2);
    if (parts & MAP_PART_WIREFRAME) map_draw_wireframe(v->map);
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
