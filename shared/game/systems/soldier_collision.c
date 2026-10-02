// The soldier against the map, in the original's order: the head's two points, the
// legs' two points (only the second if the first missed), the swept circle, the
// corners. What a special poly does to the soldier is a Hit or a PolyEffect event,
// never a wound applied here. Ported from OpenSoldat Sprites.pas by way of soldat-odin.

#include "game/systems/systems.h"

#define SURFACECOEFX 0.970f
#define SURFACECOEFY 0.970f
#define CROUCHMOVESURFACECOEFX 0.850f
#define CROUCHMOVESURFACECOEFY 0.970f
#define STANDSURFACECOEFX 0.000f
#define STANDSURFACECOEFY 0.000f

#define SPRITE_COL_RADIUS 3.0f
#define SLIDELIMIT 0.2f

// The hit location the original reports for poly damage; not a real skeleton part.
#define POLY_HIT_PART 12

static void self_hit(World *w, uint8_t index, float amount, Events *events)
{
    Soldier *s = &w->soldiers[index];
    event_emit(events, (Event){
        .type = EVENT_HIT,
        .hit = {.shooter = index, .target = index, .weapon = WEAPON_NONE, .amount = amount, .part = POLY_HIT_PART, .pos = s->pos,
                .impact = s->vel},
    });
}

static void poly_effect(Events *events, uint8_t index, PolyType type, Vec2 pos, bool spark)
{
    event_emit(events, (Event){
        .type = EVENT_POLY_EFFECT,
        .poly_effect = {.target = index, .type = type, .pos = pos, .spark = spark},
    });
}

// What a special poly does when a soldier touches it (HandleSpecialPolyTypes). The
// soldier reports its own wound as a Hit on itself.
static void handle_special_poly(const Context *ctx, World *w, uint8_t index, PolyType t, Vec2 pos, Events *events)
{
    Soldier *s = &w->soldiers[index];

    switch (t) {
    case POLY_DEADLY:
        self_hit(w, index, 50.0f + s->health, events); // lands the soldier on exactly -50
        break;

    case POLY_BLOODY_DEADLY:
        self_hit(w, index, 450.0f + s->health, events); // past BRUTAL_DEATH_HEALTH, so it gibs
        break;

    case POLY_HURTS:
    case POLY_LAVA:
        if (!s->dead) {
            if (rand_int(&w->rng, 10) == 0) {
                self_hit(w, index, 5.0f, events);
                poly_effect(events, index, t, pos, false);
            }
            if (s->health < 1.0f) self_hit(w, index, 10.0f, events);
        }
        if (t == POLY_LAVA && rand_int(&w->rng, 3) == 0) {
            Vec2 spark = vec2_sub(pos, vec2(0.0f, 3.0f));
            poly_effect(events, index, POLY_LAVA, spark, true);
            // the map's own bullets are asked for where the world decides, and told from there
            if (w->authority && rand_int(&w->rng, 3) == 0) {
                soldier_shoot(w, index, WEAPON_FLAMER, spark, vec2_scale(s->vel, -1.0f), ctx->weapons.info[WEAPON_FLAMER].stats.damage,
                              events);
            }
        }
        break;

    case POLY_REGENERATES:
        if (s->health < DEFAULT_HEALTH && w->tick % 12 == 0) {
            self_hit(w, index, -2.0f, events); // negative damage heals
            poly_effect(events, index, t, pos, false);
        }
        break;

    case POLY_EXPLODES:
        if (!s->dead) {
            Vec2 origin = vec2_sub(pos, vec2(0.0f, 3.0f));
            poly_effect(events, index, t, origin, false);
            if (w->authority) {
                soldier_shoot(w, index, WEAPON_M79, origin, (Vec2){0}, ctx->weapons.info[WEAPON_M79].stats.damage, events);
            }
            self_hit(w, index, 4000.0f, events);
        }
        break;

    case POLY_HURTS_FLAGGERS:
        if (!s->dead && s->held && thing_is_flag(w->things[s->held - 1].style) && rand_int(&w->rng, 10) == 0) {
            self_hit(w, index, 10.0f, events);
            poly_effect(events, index, t, pos, false);
        }
        if (s->health < 1.0f) self_hit(w, index, 10.0f, events);
        break;

    default:
        break;
    }
}

static void apply_ground_friction(const World *w, Soldier *s, const Polygon *poly, Vec2 normal)
{
    const Vec2 stand = {STANDSURFACECOEFX, STANDSURFACECOEFY};
    const Vec2 surface = {SURFACECOEFX, SURFACECOEFY};

    switch (s->legs.id) {
    case ANIM_STAND:
    case ANIM_CROUCH:
    case ANIM_PRONE:
    case ANIM_PRONE_MOVE:
    case ANIM_GET_UP:
    case ANIM_FALL:
    case ANIM_MERCY:
    case ANIM_MERCY2:
    case ANIM_OWN:
        // Standing still on a walkable slope: cancel gravity so you don't slide.
        if (s->vel.x < SLIDELIMIT && s->vel.x > -SLIDELIMIT && normal.y > SLIDELIMIT) {
            s->pos = s->old_pos;
            s->forces.y -= w->gravity;
        }
        if (normal.y > SLIDELIMIT && poly->type != POLY_ICE && poly->type != POLY_BOUNCY) {
            switch (s->legs.id) {
            case ANIM_STAND:
            case ANIM_FALL:
            case ANIM_CROUCH:
                s->vel = vec2_mul(s->vel, stand);
                s->forces.x -= s->vel.x;
                break;
            case ANIM_PRONE:
                if (s->legs.frame > 24) {
                    bool moving = (s->controls & BUTTON_CROUCH) && (s->controls & (BUTTON_LEFT | BUTTON_RIGHT));
                    if (!moving) {
                        s->vel = vec2_mul(s->vel, stand);
                        s->forces.x -= s->vel.x;
                    }
                } else {
                    s->vel = vec2_mul(s->vel, surface);
                }
                break;
            case ANIM_GET_UP:
                s->vel = vec2_mul(s->vel, surface);
                break;
            case ANIM_PRONE_MOVE:
                s->vel = vec2_mul(s->vel, stand);
                break;
            default:
                break;
            }
        }
        break;

    case ANIM_CROUCH_RUN:
    case ANIM_CROUCH_RUN_BACK:
        s->vel = vec2_mul(s->vel, (Vec2){CROUCHMOVESURFACECOEFX, CROUCHMOVESURFACECOEFY});
        break;

    default:
        s->vel = vec2_mul(s->vel, surface);
        break;
    }
}

bool check_map_collision(const Context *ctx, World *w, uint8_t index, Vec2 at, int area, Events *events)
{
    const Map *map = ctx->map;
    Soldier *s = &w->soldiers[index];
    Vec2 pos = vec2_add(at, s->vel);

    PolySector sector = map_sector_polys(map, pos);
    if (sector.count == 0) return false;
    bg_test_big_poly_center(map, &s->bg, pos);

    for (int i = 0; i < sector.count; i++) {
        uint16_t idx = sector.polys[i];
        const Polygon *poly = &map->polys[idx];
        if (!soldier_collides_with(s, (PolyType)poly->type) || !point_in_poly(pos, poly)) continue;
        if (bg_test(map, &s->bg, idx)) continue;

        handle_special_poly(ctx, w, index, (PolyType)poly->type, pos, events);

        float dist;
        Vec2 normal = closest_perpendicular(poly, pos, &dist, NULL);
        Vec2 push = vec2_scale(normal, dist);
        float speed = vec2_length(s->vel);
        if (vec2_length(push) > speed) push = vec2_scale(vec2_normalize(push), speed);

        if (area == 0 || (area == 1 && (s->vel.y < 0.0f || s->vel.x > SLIDELIMIT || s->vel.x < -SLIDELIMIT))) {
            s->old_pos = s->pos;
            s->pos = vec2_sub(s->pos, push);
            if (poly->type == POLY_BOUNCY) {
                push = vec2_scale(vec2_normalize(push), poly->bounciness * speed);
                if (vec2_length(push) > 1.0f) poly_effect(events, index, POLY_BOUNCY, pos, false); // the thud
            }
            s->vel = vec2_sub(s->vel, push);
        }
        if (area == 0) apply_ground_friction(w, s, poly, normal);
        return true;
    }
    return false;
}

// Swept circle along the velocity, catching thin polys at high speed.
static bool check_radius_map_collision(const Context *ctx, World *w, uint8_t index, Vec2 at, bool has_collided, Events *events)
{
    const Map *map = ctx->map;
    Soldier *s = &w->soldiers[index];
    Vec2 spos = vec2_add(at, vec2(0.0f, -3.0f));
    int steps = (int)vec2_length(s->vel);
    if (steps == 0) steps = 1;
    Vec2 step = vec2_scale(s->vel, 1.0f / (float)steps);

    for (int n = 0; n < steps; n++) {
        spos = vec2_add(spos, step);
        PolySector sector = map_sector_polys(map, spos);
        for (int i = 0; i < sector.count; i++) {
            uint16_t idx = sector.polys[i];
            const Polygon *poly = &map->polys[idx];
            PolyType t = (PolyType)poly->type;

            bool collides = team_collides(t, s->team);
            if ((!s->held && t == POLY_ONLY_FLAGGERS) || (s->held && t == POLY_NOT_FLAGGERS)) collides = false;
            if (!collides || t == POLY_DOESNT || t == POLY_ONLY_BULLETS) continue;

            for (int k = 0; k < 3; k++) {
                Vec2 probe = vec2_sub(spos, vec2_scale(poly->perp[k], SPRITE_COL_RADIUS));
                if (!point_in_poly_edges(probe, poly)) continue;
                if (bg_test(map, &s->bg, idx)) continue;
                if (!has_collided) handle_special_poly(ctx, w, index, t, probe, events);

                int edge;
                Vec2 normal = closest_perpendicular(poly, spos, NULL, &edge);
                float dist = point_line_distance(poly->verts[edge], poly->verts[(edge + 1) % 3], probe);
                s->pos = s->old_pos;
                s->vel = vec2_sub(s->forces, vec2_scale(normal, dist));
                return true;
            }
        }
    }
    return false;
}

// Pushes the soldier away from poly corners within radius r.
static bool check_map_vertices_collision(const Context *ctx, World *w, uint8_t index, Vec2 pos, float r, bool has_collided, Events *events)
{
    const Map *map = ctx->map;
    Soldier *s = &w->soldiers[index];

    PolySector sector = map_sector_polys(map, pos);
    for (int i = 0; i < sector.count; i++) {
        uint16_t idx = sector.polys[i];
        const Polygon *poly = &map->polys[idx];
        if (!soldier_collides_with(s, (PolyType)poly->type)) continue;

        for (int v = 0; v < 3; v++) {
            Vec2 vert = poly->verts[v];
            if (vec2_length(vec2_sub(vert, pos)) >= r) continue;
            if (bg_test(map, &s->bg, idx)) continue;
            if (!has_collided) handle_special_poly(ctx, w, index, (PolyType)poly->type, pos, events);
            s->pos = vec2_add(s->pos, vec2_normalize(vec2_sub(pos, vert)));
            return true;
        }
    }
    return false;
}

void soldier_collide(const Context *ctx, World *w, uint8_t index, Events *events)
{
    const Map *map = ctx->map;
    Soldier *s = &w->soldiers[index];
    s->on_ground = false;
    bg_test_prepare(&s->bg);

    // head
    check_map_collision(ctx, w, index, vec2_add(s->pos, vec2(-3.5f, -12.0f)), 1, events);
    check_map_collision(ctx, w, index, vec2_add(s->pos, vec2(3.5f, -12.0f)), 1, events);

    // Lift the trailing leg slightly so walking doesn't catch on slopes.
    float body_y = 0.0f, arm_s = 0.0f;
    bool left = (s->controls & BUTTON_LEFT) != 0, right = (s->controls & BUTTON_RIGHT) != 0;
    if (left != right) {
        if (left != (s->direction == 1)) arm_s = 0.25f;
        else body_y = 0.25f;
    }
    if (body_y == 0.0f) {
        Vec2 p = vec2_add(s->pos, vec2(2.0f, 1.9f));
        if (map_ray_cast(map, p, p, 10.0f, RAY_FILTER_DEFAULT, NULL)) body_y = 0.25f;
    }
    if (arm_s == 0.0f) {
        Vec2 p = vec2_add(s->pos, vec2(-2.0f, 1.9f));
        if (map_ray_cast(map, p, p, 10.0f, RAY_FILTER_DEFAULT, NULL)) arm_s = 0.25f;
    }

    // legs: only the second side if the first didn't collide
    s->on_ground = check_map_collision(ctx, w, index, vec2_add(s->pos, vec2(2.0f, 2.0f - body_y)), 0, events) ||
                   check_map_collision(ctx, w, index, vec2_add(s->pos, vec2(-2.0f, 2.0f - arm_s)), 0, events);

    s->on_ground_for_law = check_radius_map_collision(ctx, w, index, vec2_add(s->pos, vec2(0.0f, -1.0f)), s->on_ground, events);
    bool corners = check_map_vertices_collision(ctx, w, index, s->pos, 3.0f, s->on_ground || s->on_ground_for_law, events);
    s->on_ground = corners || s->on_ground;

    // Debounced ground state: only changes after two identical ticks.
    if (s->on_ground == s->on_ground_last) s->on_ground_permanent = s->on_ground;
    s->on_ground_last = s->on_ground;

    bg_test_reset(&s->bg);

    // A rope soldier flies by its own rules: the travel along the rope and the momentum
    // a cut keeps run past the walking clamp — but only as far as the swing's own cut
    // (ROPE_SWING_MAX), what the collision's push-out is built to resolve.
    float max_vel = s->gear == GEAR_ROPE ? ROPE_SWING_MAX : MAX_VELOCITY;
    s->vel.x = clampf(s->vel.x, -max_vel, max_vel);
    s->vel.y = clampf(s->vel.y, -max_vel, max_vel);
}

bool soldier_collides_with(const Soldier *s, PolyType t)
{
    if (t == POLY_ONLY_FLAGGERS) return s->held != 0;
    if (t == POLY_NOT_FLAGGERS) return s->held == 0;
    return t != POLY_DOESNT && t != POLY_ONLY_BULLETS && team_collides(t, s->team);
}

bool bg_test(const Map *m, BackgroundState *bg, uint16_t poly)
{
    switch (m->polys[poly].type) {
    case POLY_BACKGROUND:
        if (bg->status == BACKGROUND_TRANSITION) {
            bg->test_result = true;
            bg->poly = (int16_t)poly;
            return true;
        }
        break;
    case POLY_BACKGROUND_TRANSITION:
        bg->test_result = true;
        if (bg->status == BACKGROUND_NORMAL) bg->status = BACKGROUND_TRANSITION;
        return true;
    default:
        break;
    }
    return false;
}

void bg_test_big_poly_center(const Map *m, BackgroundState *bg, Vec2 pos)
{
    if (bg->status != BACKGROUND_TRANSITION) return;

    if (bg->poly == BACKGROUND_POLY_UNKNOWN) {
        bg->poly = BACKGROUND_POLY_NONE;
        for (int i = 0; i < m->back_poly_count; i++) {
            uint16_t idx = m->back_polys[i];
            if (point_in_poly(pos, &m->polys[idx])) {
                bg->poly = (int16_t)idx;
                bg->test_result = true;
                break;
            }
        }
    } else if (bg->poly != BACKGROUND_POLY_NONE && point_in_poly(pos, &m->polys[bg->poly])) {
        bg->test_result = true;
    }
}

void bg_test_prepare(BackgroundState *bg) { bg->test_result = false; }

void bg_test_reset(BackgroundState *bg)
{
    if (bg->test_result) return;
    bg->status = BACKGROUND_NORMAL;
    bg->poly = BACKGROUND_POLY_NONE;
}
