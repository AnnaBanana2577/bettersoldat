// The grappling hook that replaces the jets: Teeworlds' hook (CCharacterCore::Tick,
// gamecore.cpp), its steps in its order, its numbers brought to this world (game.h,
// HookTuning). The key held fires the hook at the aim; it flies straight and fast,
// and the first poly its owner would stand on holds it. Past its length it gives up,
// hangs there three ticks, and is put away, and the key must be let go before it fires
// again. Holding, it pulls its owner toward its head until they are near it: three
// times harder up than down, so a wall is climbed more easily than a drop is taken,
// and a little harder the way the owner is moving, so they steer it; and only up to
// its pace, or to whatever speed they already had. Let go, it is put away at once.
// It takes nothing else away: the soldier walks, jumps and shoots on it.
//
// Teeworlds' hook also catches players and drags them; this one holds polys alone.
// Its state rides the soldier's owned half (hook, hook_pos, hook_dir), as its owner's
// client plays it and every machine draws it.

#include "game/systems/systems.h"

// The input's direction, Teeworlds' m_Direction: -1 left, 1 right, 0 neither or both.
static int input_direction(const Soldier *s)
{
    bool left = s->controls & BUTTON_LEFT, right = s->controls & BUTTON_RIGHT;
    return left == right ? 0 : left ? -1 : 1;
}

void hook_control(const Context *ctx, World *w, uint8_t index)
{
    Soldier *s = &w->soldiers[index];
    const HookTuning *t = &w->rules.hook_tuning;
    Vec2 pos = s->pos;

    // handle hook: fired at the aim from the idle, put away when let go
    if (s->controls & BUTTON_JET) {
        if (s->hook == HOOK_IDLE) {
            Vec2 aim = vec2_sub(s->aim, pos);
            Vec2 dir = vec2_length(aim) > 0.0f ? vec2_normalize(aim) : vec2((float)s->direction, 0.0f);
            s->hook = HOOK_FLYING;
            s->hook_pos = vec2_add(pos, vec2_scale(dir, HOOK_START));
            s->hook_dir = dir;
        }
    } else {
        s->hook = HOOK_IDLE;
        s->hook_pos = pos;
    }

    // do hook
    if (s->hook == HOOK_IDLE) {
        s->hook_pos = pos;
    } else if (s->hook >= HOOK_RETRACT_1 && s->hook < HOOK_RETRACT_3) {
        s->hook++;
    } else if (s->hook == HOOK_RETRACT_3) {
        s->hook = HOOK_RETRACTED;
    } else if (s->hook == HOOK_FLYING) {
        Vec2 next = vec2_add(s->hook_pos, vec2_scale(s->hook_dir, t->fire_speed));
        if (vec2_length(vec2_sub(pos, next)) > t->length) {
            s->hook = HOOK_RETRACT_1;
            next = vec2_add(pos, vec2_scale(vec2_normalize(vec2_sub(next, pos)), t->length));
        }
        // make sure that the hook doesn't go through the ground: what a player stands on
        RayFilter filter = {.player = true, .team = s->team};
        Vec2 hit;
        float reach = vec2_length(vec2_sub(next, s->hook_pos)) + 1.0f;
        bool ground = map_ray_cast_hit(ctx->map, s->hook_pos, next, reach, filter, NULL, &hit, NULL);
        if (ground) next = hit;
        if (s->hook == HOOK_FLYING) { // gave up for its length, it stays where it was
            if (ground) s->hook = HOOK_GRABBED;
            s->hook_pos = next;
        }
    }

    // holding: the pull toward its head, while not near it
    if (s->hook == HOOK_GRABBED && vec2_length(vec2_sub(s->hook_pos, pos)) > HOOK_NEAR) {
        Vec2 pull = vec2_scale(vec2_normalize(vec2_sub(s->hook_pos, pos)), t->drag_accel);
        // more power to drag up than down: it makes climbing easier
        if (pull.y > 0.0f) pull.y *= 0.3f;
        // more power the way the player wants to move, otherwise a little damped
        int dir = input_direction(s);
        if ((pull.x < 0.0f && dir < 0) || (pull.x > 0.0f && dir > 0)) pull.x *= 0.95f;
        else pull.x *= 0.75f;
        Vec2 vel = vec2_add(s->vel, pull);
        // only under the hook's pace, or slower than the speed already had
        if (vec2_length(vel) < t->drag_speed || vec2_length(vel) < vec2_length(s->vel)) s->vel = vel;
    }
}
