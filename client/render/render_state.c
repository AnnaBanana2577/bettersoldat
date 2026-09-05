#include "render/render_state.h"

#include <string.h>

#include "game/systems/systems.h"

void tick_snapshot_capture(TickSnapshot *snap, const World *w)
{
    snap->tick = w->tick;
    memcpy(snap->soldiers, w->soldiers, sizeof(snap->soldiers));
}

// Whether the soldier moved continuously between the two ticks, so blending the two
// positions shows something that happened. A new life is a jump, not a journey.
static bool continuous(const Soldier *from, const Soldier *to)
{
    return from->active && to->active && from->life == to->life;
}

static Vec2 lerp(Vec2 a, Vec2 b, float t)
{
    return vec2_add(a, vec2_scale(vec2_sub(b, a), t));
}

static RenderSoldier soldier_state(const Context *ctx, const Soldier *from, const Soldier *to, float alpha)
{
    RenderSoldier out = {.active = to->active};
    if (!to->active) return out;

    out.pos = continuous(from, to) ? lerp(from->pos, to->pos, alpha) : to->pos;
    // The dead hold their last pose until the corpses (ragdoll.odin) are ported and
    // take over.
    out.pose = soldier_pose(ctx->anims, to, out.pos);

    out.dead = to->dead;
    out.team = to->team;
    out.facing_left = to->direction != 1;
    out.weapon = to->weapon.id;
    out.secondary = to->secondary.id;
    out.body_anim = to->body.id;
    out.grenades = to->grenades;
    out.health = to->health;
    out.vest = to->vest;
    out.jetting = (to->controls & BUTTON_JET) && to->jets > 0;
    out.fired = to->fired;
    out.spawn_protected = to->cease_fire_counter >= 0;
    return out;
}

void build_render_state(RenderState *out, const Context *ctx, const TickSnapshot *from, const TickSnapshot *to,
                        float alpha, int me)
{
    out->alpha = clampf(alpha, 0.0f, 1.0f);
    for (int i = 0; i < MAX_PLAYERS; i++) {
        out->soldiers[i] = soldier_state(ctx, &from->soldiers[i], &to->soldiers[i], out->alpha);
    }
    out->focus = out->soldiers[me].pos;
}
