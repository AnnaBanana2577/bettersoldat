// The parachute of a spawn high over the map: hung from the soldier's head, slowing its
// fall, let go of on the ground (or with the jets) once the spawn protection has worn
// down a little, and then left to lie. Ported from OpenSoldat Sprites.pas
// (TSprite.Parachute, the parachuter in TSprite.Update) and Things.pas (TThing.Update's
// parachute).
//
// The parachute is the things'. It is deployed and let go of in the things pass, which
// reads its holder (alive: on the ground or jetting; a corpse: landed); the soldier's
// side, in its own step, only reads the parachute back: its lift, and the tick its
// canopy turned over, which catches the fall.

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

// The line to the holder is cut: the parachute lies where it is a while.
static void let_go(Thing *t, Soldier *holder)
{
    t->holder = 0;
    t->cut++;
    t->timeout = PARA_LANDED_TIMEOUT;
    holder->held = 0;
}

void parachute_update(const Context *ctx, World *w, int index)
{
    Thing *t = &w->things[index];
    t->flipped = false;
    if (!t->holder) return;

    Soldier *holder = &w->soldiers[t->holder - 1];
    const Ragdoll *body = &w->ragdolls[t->holder - 1];

    // let go of: by a living holder on the ground or jetting once the spawn protection
    // has worn down a little, or by a corpse that has landed
    if (holder->dead) {
        if (body->active && body->on_ground) let_go(t, holder);
    } else if (holder->cease_fire_counter < DEFAULT_CEASE_FIRE - 30 && (holder->on_ground || (holder->controls & BUTTON_JET))) {
        let_go(t, holder);
    }
    if (!t->holder) return;

    // the lines meet at the head, the living one's or the corpse's
    t->pos[3] = holder->dead && body->active ? body->pos[11] : soldier_pose(ctx->anims, holder, holder->pos).p[11];
    t->forces[0].y = -holder->vel.y;
    holder->held = (uint8_t)(index + 1);

    // the canopy turned over: the lines swap, and the fall catches for a tick
    if (t->pos[2].x < t->pos[3].x) {
        Vec2 head = t->pos[3];
        t->pos[3] = t->old_pos[3] = t->pos[2];
        t->pos[2] = t->old_pos[2] = head;
        t->flipped = true;
    }
}

// Whether the soldier hangs from a parachute, and which.
static Thing *parachute_of(World *w, const Soldier *s)
{
    if (!s->held) return NULL;
    Thing *t = &w->things[s->held - 1];
    return t->style == THING_PARACHUTE ? t : NULL;
}

void parachute_catch(World *w, Soldier *s)
{
    Thing *t = parachute_of(w, s);
    if (t && t->flipped) s->forces.y = w->gravity;
}

void parachute_carry(World *w, Soldier *s)
{
    if (parachute_of(w, s)) s->forces.y = PARA_SPEED;
}
