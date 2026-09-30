// The bullet pool: spawning, the tick (collisions, then the timeout and the damage
// falling off with distance), the flight, ending. Ported from OpenSoldat Bullets.pas
// (CreateBullet, TBullet.DoUpdate) and Parts.pas (the Euler step) by way of soldat-odin.
// What a bullet does to a soldier is a Hit event (bullet_collision.c); this file never
// wounds anyone.
//
// As in the original's loop, every bullet runs its tick, a bullet made during the
// pass runs its own if its slot comes later, and then every bullet flies.

#include "game/systems/systems.h"

#define BULLET_DAMPING 0.99f

int bullet_spawn(const Context *ctx, World *w, Vec2 pos, Vec2 vel, WeaponId weapon, uint8_t owner, float damage, Events *events)
{
    for (int i = 0; i < MAX_BULLETS; i++) {
        Bullet *b = &w->bullets[i];
        if (b->active) continue;

        const WeaponInfo *info = &ctx->weapons.info[weapon];
        Soldier *s = &w->soldiers[owner];
        s->shot_count++;
        *b = (Bullet){
            .active = true,
            .style = info->stats.style,
            .weapon = weapon,
            .owner = owner,
            .lag = s->view_lag,
            .spawn_cmd = s->cmd_seq,
            .shot_id = s->shot_count,
            .pos = pos,
            .old_pos = pos,
            .vel = vel,
            .initial = pos,
            .timeout = info->timeout,
            .hit_multiply = damage,
            .hit_body = -1,
        };
        event_emit(events, (Event){
            .type = EVENT_BULLET_SPAWN,
            .bullet_spawn = {.id = (uint16_t)i, .player = owner, .weapon = weapon, .pos = pos, .vel = vel, .damage = damage},
        });
        return i;
    }
    return -1;
}

void bullet_end(Bullet *b, uint16_t index, Events *events, const Vec2 *impact)
{
    if (!b->active) return;
    b->active = false;
    EventBulletEnd e = {.id = index, .owner = b->owner, .shot = b->shot_id, .weapon = b->weapon, .pos = b->pos};
    if (impact) {
        e.pos = *impact;
        e.impact = true;
    }
    event_emit(events, (Event){.type = EVENT_BULLET_END, .bullet_end = e});
}

// One tick of one bullet, all but its flight: the map's edge, its collisions, its
// timeout, the damage falling off.
static void bullet_update(const Context *ctx, World *w, Bullet *b, uint16_t index, Events *events)
{
    const Map *map = ctx->map;
    float bound = (float)(map->sectors_num * map->sectors_division - 10);
    if (fabsf(b->pos.x) > bound || fabsf(b->pos.y) > bound) {
        bullet_end(b, index, events, NULL);
        return;
    }

    bullet_collide(ctx, w, b, index, events);
    if (!b->active) return;

    b->timeout--;
    if (b->timeout == 0) {
        switch (b->style) {
        case BULLET_FRAG_GRENADE:
        case BULLET_M79:
        case BULLET_FLAME_ARROW:
        case BULLET_LAW:
            explode(ctx, w, b, index, EXPLOSION_FRAG, -1, -1, events); // the M79 too: a spent round goes off as a frag
            break;
        case BULLET_CLUSTER:
        case BULLET_M2: // the M2's flak
            explode(ctx, w, b, index, EXPLOSION_CLUSTER, -1, -1, events);
            break;
        default:
            break;
        }
        bullet_end(b, index, events, NULL);
        return;
    }

    // the damage falls off with distance travelled
    if (b->timeout % 6 == 0 && b->weapon != WEAPON_BARRETT && b->weapon != WEAPON_M79 && b->weapon != WEAPON_KNIFE &&
        b->weapon != WEAPON_LAW) {
        float dist = vec2_length(vec2_sub(b->initial, b->pos));
        if ((b->degrade_count == 0 && dist > 500.0f) || (b->degrade_count == 1 && dist > 900.0f)) {
            b->hit_multiply *= 0.5f;
            b->degrade_count++;
        }
    }
    if (b->style == BULLET_FLAME) b->forces.y -= 0.15f;
}

static void bullet_integrate(const World *w, Bullet *b)
{
    b->forces.y += w->gravity * BULLET_GRAVITY;
    Vec2 prev = b->pos;
    b->vel = vec2_add(b->vel, b->forces);
    b->pos = vec2_add(b->pos, b->vel);
    b->vel = vec2_scale(b->vel, BULLET_DAMPING);
    b->old_pos = prev;
    b->forces = (Vec2){0};
}

void bullets_update(const Context *ctx, World *w, Events *events)
{
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (w->bullets[i].active) bullet_update(ctx, w, &w->bullets[i], (uint16_t)i, events);
    }
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (w->bullets[i].active) bullet_integrate(w, &w->bullets[i]);
    }
}
