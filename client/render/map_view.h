#pragma once

// One map, ready to draw: its polygons as meshes, the texture they wear, and the images
// its props name. Nothing here knows about a game: the renderer keeps one and slots the
// soldiers between the layers; an editor could own one and draw it whole. Ported from
// soldat-odin's client/render/map_view.odin. The window must be open.

#include <raylib.h>

#include "render/camera.h"
#include "resources/map.h"

// The map's polygons as two static meshes built once: the background polys, drawn
// first, and the solid terrain, drawn after the players so it occludes them (the
// original's order). Both carry the map texture with per-vertex colour.
typedef struct MapMeshes {
    Mesh background;
    Mesh terrain;
    Material material;
    bool built;
} MapMeshes;

typedef struct MapView {
    const Map *map;     // borrowed; the owner outlives the view
    Texture2D texture;  // id 0 draws the polygons untextured
    Texture2D *scenery; // one per Map.scenery entry, id 0 where it failed to load
    MapMeshes meshes;
} MapView;

// The pieces of a map, so a caller can leave some out.
typedef enum MapPart {
    MAP_PART_BACKGROUND = 1 << 0, // the sky gradient
    MAP_PART_POLYGONS = 1 << 1,   // the terrain meshes
    MAP_PART_SCENERY = 1 << 2,    // the props
    MAP_PART_WIREFRAME = 1 << 3,  // the polygon edges, over everything
} MapPart;

#define MAP_PARTS_ALL (MAP_PART_BACKGROUND | MAP_PART_POLYGONS | MAP_PART_SCENERY)

// Read a map's art and build its meshes.
void map_view_load(MapView *v, const char *base, const Map *map);
void map_view_unload(MapView *v);

// The map and nothing else, in the original's layer order. Between BeginMode2D and
// EndMode2D.
void map_view_draw(const MapView *v, const GameCamera *camera, unsigned parts);

// The box the map's polygons fill, which is what a view frames when a map opens.
void map_view_bounds(const Map *map, Vec2 *low, Vec2 *high);

// The layers one at a time, for the renderer to slot the living between. Between
// BeginMode2D and EndMode2D, with back-face culling off (the map's triangles wind
// either way).
void map_draw_background(const Map *map, const GameCamera *camera);
void map_draw_background_polys(const MapView *v);
void map_draw_terrain(const MapView *v);
void map_draw_scenery(const MapView *v, uint8_t layer); // 0 behind the map, 1 in front, 2 in front of the players
void map_draw_wireframe(const Map *map);
