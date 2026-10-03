// The flags: in base or not, grabbed, returned by a touch, captured, thrown. The
// carrying and the timeout are the pool's (thing.c). Ported from OpenSoldat Things.pas
// (TThing.Update's base and touchdown checks, CheckSpriteCollision's flag case) and
// Sprites.pas (TSprite.ThrowFlag), capture the flag's rules alone for now.
//
// A capture scores for the carrier's tally here; the team's score is the match's, from
// the event.

#include "game/systems/systems.h"

#define BASE_RADIUS 75.0f      // how far from its spawn a flag still counts as in base
#define TOUCHDOWN_RADIUS 28.0f // how close to the other flag a carrier must bring it
#define FLAG_THROW_POWER 4.225f
#define FLAG_GRAB_COOLDOWN (60 / 4)

static Team flag_team(ThingStyle style) { return style == THING_ALPHA_FLAG ? TEAM_ALPHA : TEAM_BRAVO; }

Vec2 flag_base(const Map *m, ThingStyle style)
{
    int32_t kind = style == THING_ALPHA_FLAG ? SPAWN_ALPHA_FLAG : SPAWN_BRAVO_FLAG;
    for (int i = 0; i < m->spawnpoint_count; i++)
        if (m->spawnpoints[i].active && m->spawnpoints[i].team == kind) return m->spawnpoints[i].pos;
    return (Vec2){0};
}

// A carrier home with the other flag in its base scores.
static void touchdown(const Context *ctx, World *w, int index, Events *events)
{
    Thing *t = &w->things[index];
    Soldier *carrier = &w->soldiers[t->holder - 1];
    if (carrier->team == flag_team(t->style)) return;

    for (int i = 0; i < MAX_THINGS; i++) {
        const Thing *other = &w->things[i];
        if (i == index || other->style == THING_NONE || !other->in_base || other->holder != 0) continue;
        if (vec2_length(vec2_sub(t->pos[0], other->pos[0])) >= TOUCHDOWN_RADIUS) continue;
        carrier->flags++;
        event_emit(events, (Event){.type = EVENT_FLAG_SCORE, .flag_score = {.player = (uint8_t)(t->holder - 1), .flag = t->style, .pos = t->pos[0]}});
        thing_respawn(ctx, w, index);
        return;
    }
}

void flag_update(const Context *ctx, World *w, int index, Events *events)
{
    Thing *t = &w->things[index];
    t->in_base = vec2_length(vec2_sub(t->pos[0], flag_base(ctx->map, t->style))) < BASE_RADIUS;
    if (t->in_base) {
        t->timeout = FLAG_TIMEOUT;
        t->interest = FLAG_INTEREST_TIME;
        // its own team brought it home
        if (w->authority && t->holder && w->soldiers[t->holder - 1].team == flag_team(t->style)) {
            thing_respawn(ctx, w, index);
            return;
        }
    }
    if (w->authority && t->holder) touchdown(ctx, w, index, events);
}

void flag_touch(const Context *ctx, World *w, int index, uint8_t soldier, Events *events)
{
    Thing *t = &w->things[index];
    Soldier *s = &w->soldiers[soldier];
    t->is_static = false;
    t->timeout = FLAG_TIMEOUT;
    t->interest = FLAG_INTEREST_TIME;
    if (s->team == flag_team(t->style) && t->in_base) return;
    if (t->holder != 0 || s->flag_grab_cooldown >= 1) return;

    if (s->team == flag_team(t->style)) {
        // its own team's flag, away from home: back it goes
        Vec2 at = t->pos[0];
        ThingStyle style = t->style;
        thing_respawn(ctx, w, index);
        event_emit(events, (Event){.type = EVENT_FLAG_RETURN, .flag_return = {.player = soldier, .flag = style, .pos = at}});
        return;
    }
    t->holder = (uint8_t)(soldier + 1);
    event_emit(events, (Event){.type = EVENT_FLAG_GRAB, .flag_grab = {.player = soldier, .thing = (uint8_t)index, .flag = t->style, .pos = t->pos[0]}});
}

void flag_throw(const Context *ctx, World *w, uint8_t soldier)
{
    Soldier *s = &w->soldiers[soldier];
    if (!w->authority) return;

    for (int i = 0; i < MAX_THINGS; i++) {
        Thing *t = &w->things[i];
        if (t->holder != soldier + 1 || !thing_is_flag(t->style)) continue;

        Pose pose = soldier_pose(ctx->anims, s, s->pos);
        Vec2 dir = vec2_scale(vec2_normalize(vec2_sub(s->aim, pose.p[14])), FLAG_THROW_POWER);
        Vec2 offset = vec2_scale(dir, 5.0f); // off the thrower, so it isn't grabbed again at once
        Vec2 vel = vec2_add(dir, s->vel);
        Vec2 ahead = vec2_add(offset, vel);

        // not if it would be in a wall next tick
        RayFilter filter = {.flag = true};
        Vec2 pole = vec2_add(t->pos[0], ahead);
        bool blocked = false;
        for (int k = 1; k < 4; k++) blocked = blocked || map_ray_cast(ctx->map, pose.p[14], vec2_add(t->pos[k], ahead), 200.0f, filter, NULL);
        Vec2 corners[4] = {{-10, -8}, {10, -8}, {-10, 8}, {10, 8}};
        for (int k = 0; k < 4; k++) blocked = blocked || map_collision_test(ctx->map, vec2_add(pole, corners[k]), true, NULL);
        if (blocked) continue;

        for (int k = 0; k < 4; k++) {
            t->pos[k] = vec2_add(vec2_add(t->pos[k], offset), vel);
            t->old_pos[k] = vec2_sub(t->pos[k], vel);
        }
        // a little spin, for the look of it
        Vec2 spin = vec2_scale(vec2_normalize(vec2(-vel.y, vel.x)), (float)s->direction);
        t->pos[0] = vec2_sub(t->pos[0], spin);
        t->pos[1] = vec2_add(t->pos[1], spin);

        t->holder = 0;
        s->held = 0;
        s->flag_grab_cooldown = FLAG_GRAB_COOLDOWN;
        t->bg = (BackgroundState){.status = BACKGROUND_TRANSITION, .poly = BACKGROUND_POLY_UNKNOWN};
        t->is_static = false;
    }
}
