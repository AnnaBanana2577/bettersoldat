#pragma once

// The map as the game reads it: polygons in sectors, colliders, spawn points,
// waypoints, and the props and scenery names the client draws. A .pms read from disk,
// the fields taken out of it, and the queries the game makes of it: sectors, ray
// casts, the polygon tests, which polygons a team passes. Ported from OpenSoldat
// MapFile.pas / PolyMap.pas by way of soldat-odin's shared/polymap.

#include "utils/utils.h"

// The teams as the map knows them: whose a team polygon is, whose a spawn point. The
// game's soldiers wear this same enum.
typedef enum Team {
    TEAM_NONE,
    TEAM_ALPHA,
    TEAM_BRAVO,
    TEAM_CHARLIE,
    TEAM_DELTA,
    TEAM_SPECTATOR,
    TEAM_COUNT
} Team;

#define MAX_POLYS 5000
#define MAX_SECTOR 25
#define MIN_SECTORZ (-35)
#define MAX_SECTORZ 35
#define MAX_PROPS 500
#define MAX_SPAWNPOINTS 255
#define MAX_COLLIDERS 128
#define MAX_WAYPOINTS 5000
#define MAX_CONNECTIONS 20

#define MAP_NAME_SIZE 38
#define MAP_TEXTURE_SIZE 24
#define MAP_SCENERY_NAME_SIZE 50

typedef enum PolyType {
    POLY_NORMAL = 0,
    POLY_ONLY_BULLETS = 1,
    POLY_ONLY_PLAYER = 2,
    POLY_DOESNT = 3,
    POLY_ICE = 4,
    POLY_DEADLY = 5,
    POLY_BLOODY_DEADLY = 6,
    POLY_HURTS = 7,
    POLY_REGENERATES = 8,
    POLY_LAVA = 9,
    POLY_RED_BULLETS = 10,
    POLY_RED_PLAYER = 11,
    POLY_BLUE_BULLETS = 12,
    POLY_BLUE_PLAYER = 13,
    POLY_YELLOW_BULLETS = 14,
    POLY_YELLOW_PLAYER = 15,
    POLY_GREEN_BULLETS = 16,
    POLY_GREEN_PLAYER = 17,
    POLY_BOUNCY = 18,
    POLY_EXPLODES = 19,
    POLY_HURTS_FLAGGERS = 20,
    POLY_ONLY_FLAGGERS = 21,
    POLY_NOT_FLAGGERS = 22,
    POLY_NON_FLAGGER_COLLIDES = 23,
    POLY_BACKGROUND = 24,
    POLY_BACKGROUND_TRANSITION = 25,
} PolyType;

typedef struct Polygon {
    Vec2 verts[3];
    Rgba colors[3];
    Vec2 uvs[3];
    Vec2 perp[3]; // normalized edge normals; perp[k] belongs to edge k -> k+1
    float bounciness;
    uint8_t type; // PolyType
} Polygon;

typedef struct Spawnpoint {
    bool active;
    Vec2 pos;
    int32_t team; // 0 general, 1 alpha, 2 bravo, 3 charlie, 4 delta, then the flag and kit spawns
} Spawnpoint;

typedef struct MapCollider {
    bool active;
    Vec2 pos;
    float radius;
} MapCollider;

// A piece of scenery placed by the map author. Purely decorative: nothing collides.
typedef struct MapProp {
    uint16_t style; // 1-based index into Map.scenery, 0 = none
    int32_t width, height;
    Vec2 pos;
    float rotation;
    Vec2 scale;
    uint8_t alpha;
    Rgba color;
    uint8_t level; // 0 behind the map, 1 in front of it, 2 in front of the players
} MapProp;

// One point of the path net the map author laid for the bots. The numbering is the
// file's own, 1-based with 0 for none, because that is what the connections hold;
// waypoints[0] is a blank one.
typedef struct Waypoint {
    bool active;
    Vec2 pos;
    bool left, right, up, down, jet;
    uint8_t path;   // the team whose bots follow it, 0 any
    uint8_t action; // 0 none, 1 stop and camp, 2-6 wait 1, 5, 10, 15 or 20 seconds
    int32_t connections[MAX_CONNECTIONS];
    int count;
} Waypoint;

// The polygon indices in one sector.
typedef struct PolySector {
    const uint16_t *polys;
    int count;
} PolySector;

typedef struct Map {
    char name[MAP_NAME_SIZE + 1];
    char texture[MAP_TEXTURE_SIZE + 1];
    Rgba bg_top;
    Rgba bg_bottom;
    int32_t start_jet;
    uint8_t grenade_packs;
    uint8_t medikits;
    uint8_t weather;
    uint8_t steps;

    Polygon *polys;
    int poly_count;
    uint16_t *back_polys; // indices of the Background / Background_Transition polys
    int back_poly_count;

    int32_t sectors_division;
    int32_t sectors_num;
    PolySector *sectors; // (2n+1)^2 grid of poly indices, see map_sector_at
    int sector_count;

    Spawnpoint *spawnpoints;
    int spawnpoint_count;
    Waypoint *waypoints; // the bots' paths; 1-based, [0] a blank
    int waypoint_count;  // including the blank
    MapCollider *colliders;
    int collider_count;
    MapProp *props;
    int prop_count;
    char (*scenery)[MAP_SCENERY_NAME_SIZE + 1]; // image names props refer to by 1-based style
    int scenery_count;
} Map;

typedef enum MapError {
    MAP_OK,
    MAP_TOO_MANY_POLYS,
    MAP_BAD_SECTORS,
    MAP_TOO_MANY_PROPS,
    MAP_TOO_MANY_COLLIDERS,
    MAP_TOO_MANY_SPAWNPOINTS,
    MAP_TOO_MANY_WAYPOINTS,
    MAP_OUT_OF_MEMORY,
} MapError;

// --- loading -----------------------------------------------------------------------

// Parses .pms bytes into an empty map. On error the map is left empty.
MapError map_load(Map *m, const uint8_t *data, size_t size);

// A map from the data folder by name, <base_dir>/maps/<name>.pms. Reports
// failures on stderr.
bool map_load_file(Map *m, const char *base_dir, const char *map_name);

void map_destroy(Map *m);
const char *map_error_name(MapError err);

// --- queries -----------------------------------------------------------------------

// Polys in sector (sx, sy); empty outside the map's sector grid.
PolySector map_sector_at(const Map *m, int sx, int sy);

// The sector lookup soldier collision uses: excludes the outermost ring. Empty outside
// the grid, and an empty sector reads the same: callers treat both as "no polys here".
PolySector map_sector_polys(const Map *m, Vec2 pos);

bool point_in_poly(Vec2 p, const Polygon *poly);
bool point_in_poly_edges(Vec2 p, const Polygon *poly);

// Normal of the edge closest to pos; the distance to it and the edge index (0..2).
// Either out pointer may be NULL.
Vec2 closest_perpendicular(const Polygon *poly, Vec2 pos, float *dist, int *edge);

// Intersection of segment a-b with any edge of the polygon.
bool line_in_poly(Vec2 a, Vec2 b, const Polygon *poly, Vec2 *hit);

typedef struct RayFilter {
    bool player;
    bool flag;
    bool bullet;
    bool check_collider;
    Team team;
} RayFilter;

#define RAY_FILTER_DEFAULT ((RayFilter){.bullet = true})

// Whether segment a-b is blocked; *dist is the distance to the first blocking poly
// (may be NULL). Rays longer than max_dist report a hit at a huge distance.
bool map_ray_cast(const Map *m, Vec2 a, Vec2 b, float max_dist, RayFilter filter, float *dist);

// As map_ray_cast, also telling where the ray met the solid and which poly it was
// (either may be NULL); left unset when nothing is hit.
bool map_ray_cast_hit(const Map *m, Vec2 a, Vec2 b, float max_dist, RayFilter filter, float *dist, Vec2 *hit,
                      int *poly_index);

// Point-in-solid test for spawn checks (muzzle, grenade release point); *push is the
// push-out vector of the containing poly (may be NULL).
bool map_collision_test(const Map *m, Vec2 pos, bool is_flag, Vec2 *push);

// Whether a bullet fired by `team` collides with a poly of this type.
bool bullet_team_collides(PolyType t, Team team);

// Whether a (non-bullet) object on `team` collides with a poly of this type.
bool team_collides(PolyType t, Team team);
