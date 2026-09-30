// The parachute of a spawn high over the map: hung from the soldier's head, slowing its
// fall, let go of on the ground (or with the jets) once the spawn protection has worn
// down a little, and then left to lie. Ported from OpenSoldat Sprites.pas
// (TSprite.Parachute, the parachuter in TSprite.Update) and Things.pas (TThing.Update's
// parachute).

#include "game/systems/systems.h"

#define PARA_DISTANCE 500.0f      // a spawn with no ground this far below gets one
#define PARA_SPEED (-0.5f * 0.06f) // the lift against gravity
#define PARA_LANDED_TIMEOUT (3 * 60)

void parachute_deploy(const Context *ctx, World *w, uint8_t soldier)
{
    Soldier *s = &w->soldiers[soldier];
    if (s->held || s->team == TEAM_SPECTATOR) return;
    for (int i = 0; i < MAX_THINGS; i++)
        if (w->things[i].holder == soldier + 1) thing_kill(&w->things[i]);

    Vec2 below = vec2(s->pos.x, s->pos.y + PARA_DISTANCE);
    float dist;
    RayFilter filter = {.player = true, .team = s->team};
    if (map_ray_cast(ctx->map, s->pos, below, PARA_DISTANCE + 50.0f, filter, &dist) || dist <= PARA_DISTANCE - 10.0f) return;

    int k = thing_create(ctx, w, THING_PARACHUTE, vec2(s->pos.x, s->pos.y + 70.0f), WEAPON_NONE, (uint8_t)(soldier + 1), -1);
    if (k < 0) return;
    w->things[k].holder = (uint8_t)(soldier + 1);
    s->held = (uint8_t)(k + 1);
}

void parachute_update(const Context *ctx, World *w, int index)
{
    Thing *t = &w->things[index];
    if (!t->holder) return;

    Soldier *holder = &w->soldiers[t->holder - 1];
    const Ragdoll *body = &w->ragdolls[t->holder - 1];
    // the lines meet at the head, the living one's or the corpse's
    t->pos[3] = holder->dead && body->active ? body->pos[11] : soldier_pose(ctx->anims, holder, holder->pos).p[11];
    t->forces[0].y = -holder->vel.y;
    holder->held = (uint8_t)(index + 1);

    // the canopy turned over: the lines swap, and the fall catches for a tick
    if (t->pos[2].x < t->pos[3].x) {
        Vec2 head = t->pos[3];
        t->pos[3] = t->old_pos[3] = t->pos[2];
        t->pos[2] = t->old_pos[2] = head;
        holder->forces.y = w->gravity;
    }
}

void parachute_carry(World *w, Soldier *s)
{
    if (!s->held) return;
    Thing *t = &w->things[s->held - 1];
    if (t->style != THING_PARACHUTE) return;

    s->forces.y = PARA_SPEED;
    if (s->cease_fire_counter < DEFAULT_CEASE_FIRE - 30 && (s->on_ground || (s->controls & BUTTON_JET))) {
        t->holder = 0;
        t->cut++; // the line to the soldier
        t->timeout = PARA_LANDED_TIMEOUT;
        s->held = 0;
    }
}
