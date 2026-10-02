#include "resources/map.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------------
// Reading. Out-of-range reads yield zeroes, like the original loader.

typedef struct Reader {
    const uint8_t *data;
    size_t size;
    size_t pos;
} Reader;

static void take(Reader *r, void *dst, size_t n)
{
    if (r->pos + n <= r->size) memcpy(dst, r->data + r->pos, n);
    else memset(dst, 0, n);
    r->pos += n;
}

static void skip(Reader *r, size_t n) { r->pos += n; }

static uint8_t take_u8(Reader *r)
{
    uint8_t v;
    take(r, &v, 1);
    return v;
}

static uint16_t take_u16(Reader *r)
{
    uint8_t b[2];
    take(r, b, 2);
    return (uint16_t)(b[0] | b[1] << 8);
}

static int32_t take_i32(Reader *r)
{
    uint8_t b[4];
    take(r, b, 4);
    return (int32_t)((uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24);
}

static float take_f32(Reader *r)
{
    int32_t bits = take_i32(r);
    float v;
    memcpy(&v, &bits, sizeof(v));
    return v;
}

static Vec2 take_vec2(Reader *r)
{
    float x = take_f32(r);
    float y = take_f32(r);
    return (Vec2){x, y};
}

static Rgba take_color(Reader *r)
{
    uint8_t bgra[4];
    take(r, bgra, 4);
    return rgba_from_bgra(bgra);
}

// A fixed-size field holding a length byte and up to max_size characters.
static void take_string(Reader *r, char *out, size_t max_size)
{
    size_t n = take_u8(r);
    out[0] = '\0';
    if (n > max_size || r->pos + max_size > r->size) {
        r->pos += max_size;
        return;
    }
    const char *field = (const char *)r->data + r->pos;
    size_t len = 0;
    while (len < n && field[len] != '\0') len++;
    memcpy(out, field, len);
    out[len] = '\0';
    r->pos += max_size;
}

// calloc that never returns NULL for a zero count, so NULL always means failure.
static void *alloc_array(size_t count, size_t size)
{
    return calloc(count ? count : 1, size);
}

// ---------------------------------------------------------------------------------
// Loading

static MapError load_polys(Map *m, Reader *r)
{
    int count = take_i32(r);
    if (count < 0 || count > MAX_POLYS) return MAP_TOO_MANY_POLYS;

    m->polys = alloc_array((size_t)count, sizeof(Polygon));
    m->back_polys = alloc_array((size_t)count, sizeof(uint16_t));
    if (!m->polys || !m->back_polys) return MAP_OUT_OF_MEMORY;
    m->poly_count = count;

    for (int i = 0; i < count; i++) {
        Polygon *p = &m->polys[i];
        for (int k = 0; k < 3; k++) {
            p->verts[k] = take_vec2(r);
            skip(r, 8); // z, rhw
            p->colors[k] = take_color(r);
            p->uvs[k] = take_vec2(r);
        }
        for (int k = 0; k < 3; k++) {
            Vec2 n = take_vec2(r);
            skip(r, 4); // z
            if (k == 2) p->bounciness = vec2_length(n); // encoded in the third normal's length
            p->perp[k] = vec2_normalize(n);
        }
        p->type = take_u8(r);
        if (p->type == POLY_BACKGROUND || p->type == POLY_BACKGROUND_TRANSITION) {
            m->back_polys[m->back_poly_count++] = (uint16_t)i;
        }
    }
    return MAP_OK;
}

static MapError load_sectors(Map *m, Reader *r)
{
    m->sectors_division = take_i32(r);
    m->sectors_num = take_i32(r);
    if (m->sectors_num < 0 || m->sectors_num > MAX_SECTOR || m->sectors_division <= 0) return MAP_BAD_SECTORS;

    int side = 2 * m->sectors_num + 1;
    m->sectors = alloc_array((size_t)(side * side), sizeof(PolySector));
    if (!m->sectors) return MAP_OUT_OF_MEMORY;
    m->sector_count = side * side;

    for (int s = 0; s < m->sector_count; s++) {
        int count = take_u16(r);
        if (count > MAX_POLYS) return MAP_BAD_SECTORS;

        uint16_t *polys = alloc_array((size_t)count, sizeof(uint16_t));
        if (!polys) return MAP_OUT_OF_MEMORY;
        m->sectors[s].polys = polys;

        int n = 0;
        for (int i = 0; i < count; i++) {
            int index = take_u16(r) - 1; // file indices are 1-based
            if (index >= 0 && index < m->poly_count) polys[n++] = (uint16_t)index;
        }
        m->sectors[s].count = n;
    }
    return MAP_OK;
}

static MapError load_props_and_scenery(Map *m, Reader *r)
{
    int count = take_i32(r);
    if (count < 0 || count > MAX_PROPS) return MAP_TOO_MANY_PROPS;
    m->props = alloc_array((size_t)count, sizeof(MapProp));
    if (!m->props) return MAP_OUT_OF_MEMORY;

    for (int i = 0; i < count; i++) {
        MapProp p = {0};
        bool active = take_u8(r) != 0;
        skip(r, 1);
        p.style = take_u16(r);
        p.width = take_i32(r);
        p.height = take_i32(r);
        p.pos = take_vec2(r);
        p.rotation = take_f32(r);
        p.scale = take_vec2(r);
        p.alpha = take_u8(r);
        skip(r, 3);
        p.color = take_color(r);
        p.level = take_u8(r);
        skip(r, 3);
        // The original hides inactive props, anything above level 2, and styles that
        // name no scenery entry.
        if (active && p.level <= 2 && p.style > 0) m->props[m->prop_count++] = p;
    }

    int scenery = take_i32(r);
    if (scenery < 0 || scenery > MAX_PROPS) return MAP_TOO_MANY_PROPS;
    m->scenery = alloc_array((size_t)scenery, sizeof(*m->scenery));
    if (!m->scenery) return MAP_OUT_OF_MEMORY;
    m->scenery_count = scenery;

    for (int i = 0; i < scenery; i++) {
        take_string(r, m->scenery[i], MAP_SCENERY_NAME_SIZE);
        skip(r, 4); // timestamp
    }
    for (int i = 0; i < m->prop_count; i++) {
        if (m->props[i].style > scenery) m->props[i].style = 0;
    }
    return MAP_OK;
}

static MapError load_colliders(Map *m, Reader *r)
{
    int count = take_i32(r);
    if (count < 0 || count > MAX_COLLIDERS) return MAP_TOO_MANY_COLLIDERS;
    m->colliders = alloc_array((size_t)count, sizeof(MapCollider));
    if (!m->colliders) return MAP_OUT_OF_MEMORY;
    m->collider_count = count;

    for (int i = 0; i < count; i++) {
        MapCollider *c = &m->colliders[i];
        c->active = take_u8(r) != 0;
        skip(r, 3);
        c->pos = take_vec2(r);
        c->radius = take_f32(r);
    }
    return MAP_OK;
}

static MapError load_spawnpoints(Map *m, Reader *r)
{
    int count = take_i32(r);
    if (count < 0 || count > MAX_SPAWNPOINTS) return MAP_TOO_MANY_SPAWNPOINTS;
    m->spawnpoints = alloc_array((size_t)count, sizeof(Spawnpoint));
    if (!m->spawnpoints) return MAP_OUT_OF_MEMORY;
    m->spawnpoint_count = count;

    for (int i = 0; i < count; i++) {
        Spawnpoint *s = &m->spawnpoints[i];
        s->active = take_u8(r) != 0;
        skip(r, 3);
        int32_t x = take_i32(r);
        int32_t y = take_i32(r);
        s->team = take_i32(r);
        s->pos = (Vec2){(float)x, (float)y};
        if (abs(x) >= 2000000 || abs(y) >= 2000000) s->active = false;
    }
    return MAP_OK;
}

// The waypoints the bots walk, numbered from 1 as their own connections refer to them.
static MapError load_waypoints(Map *m, Reader *r)
{
    int count = take_i32(r);
    if (count < 0 || count > MAX_WAYPOINTS) return MAP_TOO_MANY_WAYPOINTS;
    m->waypoints = alloc_array((size_t)count + 1, sizeof(Waypoint));
    if (!m->waypoints) return MAP_OUT_OF_MEMORY;
    m->waypoint_count = count + 1;

    for (int i = 1; i <= count; i++) {
        Waypoint *p = &m->waypoints[i];
        p->active = take_u8(r) != 0;
        skip(r, 3);
        skip(r, 4); // the editor's own numbering, which nothing reads
        int32_t x = take_i32(r);
        int32_t y = take_i32(r);
        p->pos = (Vec2){(float)x, (float)y};
        p->left = take_u8(r) != 0;
        p->right = take_u8(r) != 0;
        p->up = take_u8(r) != 0;
        p->down = take_u8(r) != 0;
        p->jet = take_u8(r) != 0;
        p->path = take_u8(r);
        p->action = take_u8(r);
        skip(r, 5);
        p->count = clampi(take_i32(r), 0, MAX_CONNECTIONS);
        for (int c = 0; c < MAX_CONNECTIONS; c++) {
            int32_t to = take_i32(r);
            if (to < 0 || to > count) to = 0; // a connection to nowhere
            p->connections[c] = to;
        }
    }
    return MAP_OK;
}

MapError map_load(Map *m, const uint8_t *data, size_t size)
{
    memset(m, 0, sizeof(*m));
    Reader r = {data, size, 0};

    skip(&r, 4); // version
    take_string(&r, m->name, MAP_NAME_SIZE);
    take_string(&r, m->texture, MAP_TEXTURE_SIZE);
    m->bg_top = take_color(&r);
    m->bg_bottom = take_color(&r);
    m->start_jet = 119 * take_i32(&r) / 100; // the original's "quickfix" scaling
    m->grenade_packs = take_u8(&r);
    m->medikits = take_u8(&r);
    m->weather = take_u8(&r);
    m->steps = take_u8(&r);
    skip(&r, 4); // random id

    MapError err = load_polys(m, &r);
    if (!err) err = load_sectors(m, &r);
    if (!err) err = load_props_and_scenery(m, &r);
    if (!err) err = load_colliders(m, &r);
    if (!err) err = load_spawnpoints(m, &r);
    if (!err) err = load_waypoints(m, &r);

    if (err) map_destroy(m);
    return err;
}

bool map_load_file(Map *m, const char *base_dir, const char *map_name)
{
    char file[256];
    char path[512];
    snprintf(file, sizeof(file), "%s.pms", map_name);
    path_join(path, sizeof(path), base_dir, "maps", file);

    size_t size = 0;
    uint8_t *data = file_read_all(path, &size);
    if (!data) {
        fprintf(stderr, "failed to read %s\n", path);
        memset(m, 0, sizeof(*m));
        return false;
    }

    MapError err = map_load(m, data, size);
    free(data);
    if (err) {
        fprintf(stderr, "failed to load map %s: %s\n", path, map_error_name(err));
        return false;
    }
    return true;
}

void map_destroy(Map *m)
{
    if (m->sectors) {
        for (int i = 0; i < m->sector_count; i++) free((void *)m->sectors[i].polys);
    }
    free(m->sectors);
    free(m->polys);
    free(m->back_polys);
    free(m->spawnpoints);
    free(m->waypoints);
    free(m->colliders);
    free(m->props);
    free(m->scenery);
    memset(m, 0, sizeof(*m));
}

const char *map_error_name(MapError err)
{
    switch (err) {
    case MAP_OK: return "ok";
    case MAP_TOO_MANY_POLYS: return "too many polygons";
    case MAP_BAD_SECTORS: return "bad sectors";
    case MAP_TOO_MANY_PROPS: return "too many props";
    case MAP_TOO_MANY_COLLIDERS: return "too many colliders";
    case MAP_TOO_MANY_SPAWNPOINTS: return "too many spawn points";
    case MAP_TOO_MANY_WAYPOINTS: return "too many waypoints";
    case MAP_OUT_OF_MEMORY: return "out of memory";
    }
    return "unknown error";
}

// ---------------------------------------------------------------------------------
// Queries

PolySector map_sector_at(const Map *m, int sx, int sy)
{
    int n = m->sectors_num;
    if (sx < -n || sx > n || sy < -n || sy > n) return (PolySector){0};
    return m->sectors[(sx + n) * (2 * n + 1) + (sy + n)];
}

PolySector map_sector_polys(const Map *m, Vec2 pos)
{
    int sx = round_half_even(pos.x / (float)m->sectors_division);
    int sy = round_half_even(pos.y / (float)m->sectors_division);
    int n = m->sectors_num;
    if (sx > -n && sx < n && sy > -n && sy < n) return map_sector_at(m, sx, sy);
    return (PolySector){0};
}

bool point_in_poly(Vec2 p, const Polygon *poly)
{
    Vec2 a = poly->verts[0], b = poly->verts[1], c = poly->verts[2];
    Vec2 ap = vec2_sub(p, a);
    bool p_ab = (b.x - a.x) * ap.y - (b.y - a.y) * ap.x > 0.0f;
    bool p_ac = (c.x - a.x) * ap.y - (c.y - a.y) * ap.x > 0.0f;
    if (p_ac == p_ab) return false;
    bool p_bc = (c.x - b.x) * (p.y - b.y) - (c.y - b.y) * (p.x - b.x) > 0.0f;
    return p_bc == p_ab;
}

bool point_in_poly_edges(Vec2 p, const Polygon *poly)
{
    for (int k = 0; k < 3; k++) {
        if (vec2_dot(poly->perp[k], vec2_sub(p, poly->verts[k])) < 0.0f) return false;
    }
    return true;
}

Vec2 closest_perpendicular(const Polygon *poly, Vec2 pos, float *dist, int *edge)
{
    const Vec2 *v = poly->verts;
    float d1 = point_line_distance(v[0], v[1], pos);
    float d2 = point_line_distance(v[1], v[2], pos);
    float d3 = point_line_distance(v[2], v[0], pos);

    int e = 0;
    float d = d1;
    if (d2 < d1) {
        e = 1;
        d = d2;
    }
    if (d3 < d2 && d3 < d1) {
        e = 2;
        d = d3;
    }
    if (dist) *dist = d;
    if (edge) *edge = e;
    return poly->perp[e];
}

bool line_in_poly(Vec2 a, Vec2 b, const Polygon *poly, Vec2 *hit)
{
    for (int i = 0; i < 3; i++) {
        Vec2 p = poly->verts[i];
        Vec2 q = poly->verts[(i + 1) % 3];
        Vec2 h;

        if (b.x == a.x && q.x == p.x) continue;
        if (b.x == a.x) {
            float bk = (q.y - p.y) / (q.x - p.x);
            float bm = p.y - bk * p.x;
            h = (Vec2){a.x, bk * a.x + bm};
            if (h.x > minf(p.x, q.x) && h.x < maxf(p.x, q.x) && h.y > minf(a.y, b.y) && h.y < maxf(a.y, b.y)) {
                *hit = h;
                return true;
            }
        } else if (q.x == p.x) {
            float ak = (b.y - a.y) / (b.x - a.x);
            float am = a.y - ak * a.x;
            h = (Vec2){p.x, ak * p.x + am};
            if (h.y > minf(p.y, q.y) && h.y < maxf(p.y, q.y) && h.x > minf(a.x, b.x) && h.x < maxf(a.x, b.x)) {
                *hit = h;
                return true;
            }
        } else {
            float ak = (b.y - a.y) / (b.x - a.x);
            float bk = (q.y - p.y) / (q.x - p.x);
            if (ak == bk) continue;
            float am = a.y - ak * a.x;
            float bm = p.y - bk * p.x;
            h.x = (bm - am) / (ak - bk);
            h.y = ak * h.x + am;
            if (h.x > minf(p.x, q.x) && h.x < maxf(p.x, q.x) && h.x > minf(a.x, b.x) && h.x < maxf(a.x, b.x)) {
                *hit = h;
                return true;
            }
        }
    }
    return false;
}

static bool ray_poly_collides(PolyType t, RayFilter f)
{
    switch (t) {
    case POLY_RED_BULLETS: return f.team == TEAM_ALPHA && f.bullet;
    case POLY_RED_PLAYER: return f.team == TEAM_ALPHA && f.player;
    case POLY_BLUE_BULLETS: return f.team == TEAM_BRAVO && f.bullet;
    case POLY_BLUE_PLAYER: return f.team == TEAM_BRAVO && f.player;
    case POLY_YELLOW_BULLETS: return f.team == TEAM_CHARLIE && f.bullet;
    case POLY_YELLOW_PLAYER: return f.team == TEAM_CHARLIE && f.player;
    case POLY_GREEN_BULLETS: return f.team == TEAM_DELTA && f.bullet;
    case POLY_GREEN_PLAYER: return f.team == TEAM_DELTA && f.player;
    case POLY_ONLY_FLAGGERS: return f.flag && f.player;
    case POLY_NOT_FLAGGERS: return !f.flag && f.player;
    case POLY_NON_FLAGGER_COLLIDES: return f.flag && f.player && f.bullet;
    case POLY_ONLY_BULLETS: return f.bullet;
    case POLY_ONLY_PLAYER: return f.player;
    case POLY_DOESNT:
    case POLY_BACKGROUND:
    case POLY_BACKGROUND_TRANSITION: return false;
    default: return true;
    }
}

bool map_ray_cast_hit(const Map *m, Vec2 a, Vec2 b, float max_dist, RayFilter filter, float *dist, Vec2 *hit,
                      int *poly_index)
{
    float d = vec2_length(vec2_sub(a, b));
    if (dist) *dist = d;
    if (d > max_dist) {
        if (dist) *dist = 9999999.0f;
        return true;
    }

    float div = (float)m->sectors_division;
    int ax = round_half_even(minf(a.x, b.x) / div);
    int ay = round_half_even(minf(a.y, b.y) / div);
    int bx = round_half_even(maxf(a.x, b.x) / div);
    int by = round_half_even(maxf(a.y, b.y) / div);
    if (ax > MAX_SECTORZ || bx < MIN_SECTORZ || ay > MAX_SECTORZ || by < MIN_SECTORZ) return false;
    ax = maxi(MIN_SECTORZ, ax);
    ay = maxi(MIN_SECTORZ, ay);
    bx = mini(MAX_SECTORZ, bx);
    by = mini(MAX_SECTORZ, by);

    for (int sx = ax; sx <= bx; sx++) {
        for (int sy = ay; sy <= by; sy++) {
            PolySector sector = map_sector_at(m, sx, sy);
            for (int i = 0; i < sector.count; i++) {
                const Polygon *poly = &m->polys[sector.polys[i]];
                if (!ray_poly_collides((PolyType)poly->type, filter)) continue;
                if (point_in_poly(a, poly)) {
                    if (dist) *dist = 0.0f;
                    if (hit) *hit = a;
                    if (poly_index) *poly_index = sector.polys[i];
                    return true;
                }
                Vec2 p;
                if (line_in_poly(a, b, poly, &p)) {
                    if (dist) *dist = vec2_length(vec2_sub(p, a));
                    if (hit) *hit = p;
                    if (poly_index) *poly_index = sector.polys[i];
                    return true;
                }
            }
        }
    }

    if (filter.check_collider) {
        // A segment crossing a collider circle counts as blocked.
        float e = a.y - b.y;
        float f = b.x - a.x;
        float g = a.x * b.y - a.y * b.x;
        float h = sqrtf(e * e + f * f);
        Vec2 ab = vec2_sub(a, b);
        float ab2 = vec2_dot(ab, ab);
        for (int i = 0; i < m->collider_count; i++) {
            const MapCollider *c = &m->colliders[i];
            if (!c->active) continue;
            if (fabsf(e * c->pos.x + f * c->pos.y + g) / h <= c->radius) {
                float r = ab2 + c->radius * c->radius;
                Vec2 ac = vec2_sub(a, c->pos), bc = vec2_sub(b, c->pos);
                if (vec2_dot(ac, ac) <= r && vec2_dot(bc, bc) <= r) return false;
            }
        }
    }
    return false;
}

bool map_ray_cast(const Map *m, Vec2 a, Vec2 b, float max_dist, RayFilter filter, float *dist)
{
    return map_ray_cast_hit(m, a, b, max_dist, filter, dist, NULL, NULL);
}

bool map_collision_test(const Map *m, Vec2 pos, bool is_flag, Vec2 *push)
{
    PolySector sector = map_sector_polys(m, pos);
    for (int i = 0; i < sector.count; i++) {
        const Polygon *poly = &m->polys[sector.polys[i]];
        switch (poly->type) {
        case POLY_ONLY_BULLETS:
        case POLY_ONLY_PLAYER:
        case POLY_DOESNT:
        case POLY_RED_PLAYER:
        case POLY_BLUE_PLAYER:
        case POLY_YELLOW_PLAYER:
        case POLY_GREEN_PLAYER:
        case POLY_BACKGROUND:
        case POLY_BACKGROUND_TRANSITION:
            continue;
        case POLY_ONLY_FLAGGERS:
        case POLY_NOT_FLAGGERS:
        case POLY_NON_FLAGGER_COLLIDES:
            if (!is_flag) continue;
            break;
        default:
            break;
        }
        if (point_in_poly(pos, poly)) {
            float dist;
            Vec2 normal = closest_perpendicular(poly, pos, &dist, NULL);
            if (push) *push = vec2_scale(normal, 1.5f * dist);
            return true;
        }
    }
    return false;
}

bool bullet_team_collides(PolyType t, Team team)
{
    switch (t) {
    case POLY_RED_BULLETS:
    case POLY_RED_PLAYER: return t == POLY_RED_BULLETS && team == TEAM_ALPHA;
    case POLY_BLUE_BULLETS:
    case POLY_BLUE_PLAYER: return t == POLY_BLUE_BULLETS && team == TEAM_BRAVO;
    case POLY_YELLOW_BULLETS:
    case POLY_YELLOW_PLAYER: return t == POLY_YELLOW_BULLETS && team == TEAM_CHARLIE;
    case POLY_GREEN_BULLETS:
    case POLY_GREEN_PLAYER: return t == POLY_GREEN_BULLETS && team == TEAM_DELTA;
    case POLY_NON_FLAGGER_COLLIDES: return false;
    default: return true;
    }
}

bool team_collides(PolyType t, Team team)
{
    switch (t) {
    case POLY_RED_BULLETS:
    case POLY_RED_PLAYER:
        if ((t == POLY_RED_BULLETS && team == TEAM_ALPHA) || team != TEAM_ALPHA) return false;
        break;
    case POLY_BLUE_BULLETS:
    case POLY_BLUE_PLAYER:
        if ((t == POLY_BLUE_BULLETS && team == TEAM_BRAVO) || team != TEAM_BRAVO) return false;
        break;
    case POLY_YELLOW_BULLETS:
    case POLY_YELLOW_PLAYER:
        if ((t == POLY_YELLOW_BULLETS && team == TEAM_CHARLIE) || team != TEAM_CHARLIE) return false;
        break;
    case POLY_GREEN_BULLETS:
    case POLY_GREEN_PLAYER:
        if ((t == POLY_GREEN_BULLETS && team == TEAM_DELTA) || team != TEAM_DELTA) return false;
        break;
    case POLY_NON_FLAGGER_COLLIDES: return false;
    default: break;
    }
    return true;
}
