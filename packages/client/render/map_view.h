#pragma once

// One map, ready to draw: its polygons in a vertex buffer, the texture they wear, and
// the images its props name. Nothing here knows about a game: the renderer keeps one
// and slots the soldiers between the layers; an editor could own one and draw it whole.
// Ported from soldat-odin's client/render/map_view.odin. The context must be up.

#include "gfx/gfx.h"
#include "render/camera.h"
#include "resources/map.h"
#include "mod.h"

// The map's polygons uploaded once, as the original's MapGraphics: the background polys
// first, drawn before everything, then the solid terrain, drawn after the players so it
// occludes them. Both wear the map texture with a colour per vertex.
typedef struct MapPolys {
    GfxBuffer buffer;
    int background_count; // vertices, from 0
    int terrain_count;    // vertices, from background_count
} MapPolys;

// The map in small, for the corner of the screen: the original's MapGfx.Minimap. The
// image spans 260 interface units of width plus height; the scale and offset map a
// world point onto it.
typedef struct Minimap {
    GfxTexture tex;      // handle 0 until built
    float width, height; // in interface units
    float u1, v1;        // the image's share of the texture
    float scale;         // world units to interface units
    Vec2 offset;         // the world's top-left
} Minimap;

typedef struct MapView {
    const Map *map;      // borrowed; the owner outlives the view
    // The sky's colours forced (r_forcebg): the player's own top and bottom instead of
    // the map's, in the sky, the clear and the minimap alike.
    bool force_bg;
    Rgba forced_top, forced_bottom;
    GfxTexture texture;  // handle 0 draws the polygons untextured
    GfxTexture *scenery; // one per Map.scenery entry, handle 0 where it failed to load
    MapPolys polys;
    Minimap minimap;
} MapView;

// The pieces of a map, so a caller can leave some out.
typedef enum MapPart {
    MAP_PART_BACKGROUND = 1 << 0, // the sky gradient
    MAP_PART_POLYGONS = 1 << 1,   // the terrain
    MAP_PART_SCENERY = 1 << 2,    // the props
    MAP_PART_WIREFRAME = 1 << 3,  // the polygon edges, over everything
} MapPart;

#define MAP_PARTS_ALL (MAP_PART_BACKGROUND | MAP_PART_POLYGONS | MAP_PART_SCENERY)

// Read a map's art and upload its polygons.
void map_view_load(MapView *v, const Mod *mod, const Map *map);
void map_view_unload(MapView *v);

// The sky's colours as the player wants them: the map's own, or with `force` the two
// given, top and bottom. True if that changed, which calls for the minimap to be built
// again. Survives a map change.
bool map_view_force_background(MapView *v, bool force, Rgba top, Rgba bottom);
// The sky's colours in use, top and bottom, opaque.
void map_view_background(const MapView *v, Rgba *top, Rgba *bottom);

// The map and nothing else, in the original's layer order. Under the camera's
// transform.
void map_view_draw(const MapView *v, const GameCamera *camera, unsigned parts);

// Draws the map into the minimap's texture, sized for a window `render_height` pixels
// tall (again when that changes). Leaves the window as the target, with no viewport or
// transform set.
void map_view_build_minimap(MapView *v, float render_height);

// A world point on the minimap, from its top-left.
Vec2 map_view_to_minimap(const MapView *v, Vec2 world);

// The box the map's polygons fill, which is what a view frames when a map opens.
void map_view_bounds(const Map *map, Vec2 *low, Vec2 *high);

// The layers one at a time, for the renderer to slot the living between. Under the
// camera's transform.
void map_draw_background(const MapView *v, const GameCamera *camera);
void map_draw_background_polys(const MapView *v);
void map_draw_terrain(const MapView *v);
void map_draw_scenery(const MapView *v, uint8_t layer); // 0 behind the map, 1 in front, 2 in front of the players
void map_draw_wireframe(const Map *map, const GameCamera *camera);
