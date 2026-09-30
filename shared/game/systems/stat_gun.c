// The stationary gun (M2): a standing soldier in reach mans it, swings the barrel with
// the aim and fires it on a fixed beat instead of its own gun, hotter with every shot
// until it overheats; a jump or the jets leave it. Ported from OpenSoldat Things.pas
// (TThing.CheckStationaryGunCollision) and Control.pas (the gunner's side).

#include "game/systems/systems.h"

#define STAT_RADIUS 15.0f
#define M2_OVERHEAT 18 // shots on end before it stops
#define M2_OVERAIM 4   // shots on end before it starts to wander

void stat_gun_update(const Context *ctx, World *w, int index, Events *events)
{
    Thing *t = &w->things[index];
    if (t->timeout > 0) return; // still settling

    if (t->interest > M2_OVERHEAT + 1) t->interest = 0;
    if (t->interest > 0 && w->tick % 8 == 0) t->interest--;

    Vec2 base = t->pos[0];
    for (int i = 0; i < MAX_PLAYERS; i++) {
        Soldier *s = &w->soldiers[i];
        if (!s->active || s->dead || s->team == TEAM_SPECTATOR || s->stat != index + 1) continue;
        if (vec2_length(vec2_sub(base, s->pos)) >= STAT_RADIUS) {
            s->stat = 0;
            t->is_static = false;
            return;
        }

        t->is_static = true;
        Pose pose = soldier_pose(ctx->anims, s, s->pos);
        Vec2 aim = vec2_scale(vec2_normalize(vec2_sub(s->aim, pose.p[14])), 3.0f);
        aim.x = -aim.x;
        t->old_pos[3] = t->pos[3];
        t->pos[3] = vec2_add(t->pos[0], aim);
        t->interest = s->use_time;

        const WeaponStats *m2 = &ctx->weapons.info[WEAPON_M2].stats;
        if (!(s->controls & BUTTON_FIRE) || s->legs.id != ANIM_STAND || w->tick % (uint32_t)m2->fire_interval != 0) return;
        if (s->use_time > M2_OVERHEAT) return;

        int wander = 0;
        if (s->use_time > M2_OVERAIM) {
            wander = s->use_time / 11;
            wander = -wander + rand_int(&w->rng, 2 * wander);
        }
        Vec2 vel = vec2_scale(vec2_normalize(vec2_sub(t->pos[3], t->pos[0])), m2->speed);
        vel.x = -vel.x + (float)wander;
        vel.y += (float)wander;
        Vec2 muzzle = vec2(t->pos[3].x + 4.0f, t->pos[3].y - 10.0f);
        if (w->authority) bullet_spawn(ctx, w, muzzle, vel, WEAPON_M2, (uint8_t)i, m2->damage, events);
        event_emit(events, (Event){.type = EVENT_FIRE, .fire = {.player = (uint8_t)i, .weapon = WEAPON_M2, .pos = muzzle, .vel = vel}});
        s->use_time++;
        return;
    }

    // nobody on it: a standing soldier in reach takes it
    if (!w->authority || t->is_static) return;
    for (int j = 0; j < MAX_PLAYERS; j++) {
        Soldier *s = &w->soldiers[j];
        if (!s->active || s->dead || s->team == TEAM_SPECTATOR) continue;
        if (vec2_length(vec2_sub(base, s->pos)) < STAT_RADIUS) {
            if (s->legs.id == ANIM_STAND) {
                t->is_static = true;
                s->stat = (uint8_t)(index + 1);
            }
            return;
        }
        if (s->stat == index + 1) s->stat = 0;
        t->is_static = false;
    }
}

void stat_gun_leave(World *w, Soldier *s)
{
    if (!s->stat || !(s->controls & (BUTTON_JUMP | BUTTON_JET))) return;
    w->things[s->stat - 1].is_static = false;
    s->stat = 0;
}

void stat_gun_cool(const World *w, Soldier *s)
{
    if (s->controls & BUTTON_FIRE) return;
    if (s->use_time > M2_OVERHEAT + 1) s->use_time = 0;
    if (s->use_time > 0 && w->tick % 8 == 0) s->use_time--;
}
