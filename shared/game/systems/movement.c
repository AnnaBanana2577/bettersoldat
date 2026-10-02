// The control state machines: input -> animation state -> forces.
//
// A soldier is driven by two coupled state machines, both using AnimId as the state:
// the legs (locomotion: Stand, Run, Jump, Crouch, Prone, Roll, Fall...) and the body
// (pose and weapon handling: Stand, Aim, Recoil, Reload, Change, Throw, Roll...).
// anim_apply is the raw transition; legs_apply the guarded one (lying down blocks leg
// transitions until Get_Up runs). Rolls force the two into lockstep, and finished
// one-shot body animations fall back to the pose for the stance. The stance itself is
// derived from the legs: they are the machine of record for posture.
//
// Transitions fire in a fixed order each tick, inherited from the original's
// ControlSoldier; the feel depends on that order:
//
//   resolve_left_right -> jets_control -> combat_control -> prone_control
//   -> combat_after_prone -> animation_slowdown -> cover_check -> movement_control
//   -> combat_reload_animation -> roll_control -> body_pose_control
//
// Ported from OpenSoldat Sprites.pas / Control.pas by way of soldat-odin.
//
// The constants are folded in double and narrowed once, as Odin folds its untyped
// constants, so the forces come out bit-identical.

#include "game/systems/systems.h"

#define RUNSPEED ((float)0.118)
#define RUNSPEEDUP ((float)(0.118 / 6))
#define FLYSPEED ((float)0.03)
#define JUMPSPEED ((float)0.66)
#define CROUCHRUNSPEED ((float)(0.118 / 0.6))
#define PRONESPEED ((float)(0.118 * 4.0))
#define ROLLSPEED ((float)(0.118 / 1.2))
#define JUMPDIRSPEED ((float)0.30)
#define JETSPEED ((float)0.10)
#define SPRITE_RADIUS 16.0f // a crouched teammate counts as cover within this distance

// One control tick's input after left+right conflict resolution. `prone` is a press
// latch: prone_control consumes it when the go-prone transition fires.
typedef struct ControlInput {
    bool left, right, up, down, jet, prone;
    bool pressed_left_right; // both held this tick, resolved to one
} ControlInput;

void legs_apply(const Anims *anims, Soldier *s, AnimId id, int32_t frame)
{
    if (s->legs.id == ANIM_PRONE || s->legs.id == ANIM_PRONE_MOVE) return;
    anim_apply(anims, &s->legs, id, frame);
}

// Left+Right held together: keep the direction while jumping, else switch to the new
// one. Mutates s->controls so everything downstream agrees.
static ControlInput resolve_left_right(Soldier *s)
{
    ControlInput input = {0};
    if ((s->controls & BUTTON_LEFT) && (s->controls & BUTTON_RIGHT)) {
        input.pressed_left_right = true;
        if (s->was_jumping == s->was_running_left) s->controls &= (Buttons)~BUTTON_RIGHT;
        else s->controls &= (Buttons)~BUTTON_LEFT;
    } else {
        s->was_running_left = (s->controls & BUTTON_LEFT) != 0;
        s->was_jumping = (s->controls & BUTTON_JUMP) != 0;
    }
    input.left = (s->controls & BUTTON_LEFT) != 0;
    input.right = (s->controls & BUTTON_RIGHT) != 0;
    input.up = (s->controls & BUTTON_JUMP) != 0;
    input.down = (s->controls & BUTTON_CROUCH) != 0;
    input.jet = (s->controls & BUTTON_JET) != 0;
    input.prone = (s->controls & BUTTON_PRONE) != 0;
    return input;
}

// Jets. Jetting against a side jump's direction is a backflip; otherwise thrust.
static void jets_control(const Context *ctx, const World *w, Soldier *s, ControlInput input)
{
    const Anims *anims = ctx->anims;
    Anim *legs = &s->legs, *body = &s->body;

    bool against_side_jump = legs->id == ANIM_JUMP_SIDE &&
                             ((s->direction == -1 && input.right) || (s->direction == 1 && input.left) ||
                              input.pressed_left_right);
    bool backflip = input.jet && (against_side_jump || (legs->id == ANIM_ROLL_BACK && input.up));

    if (backflip) {
        anim_apply(anims, body, ANIM_ROLL_BACK, 1);
        legs_apply(anims, s, ANIM_ROLL_BACK, 1);
    } else if (input.jet && s->jets > 0) {
        float jet_force = w->gravity > 0.05f ? JETSPEED : w->gravity * 2.0f;
        if (s->on_ground) s->forces.y = -2.5f * jet_force;
        else if (s->stance != STANCE_PRONE) s->forces.y -= jet_force;
        else s->forces.x += (float)s->direction * jet_force / 2.0f;

        if (legs->id != ANIM_GET_UP && body->id != ANIM_ROLL && body->id != ANIM_ROLL_BACK) {
            legs_apply(anims, s, ANIM_FALL, 1);
        }
        s->jets--;
        if (s->jets == 1) s->jets = 0; // the last unit is spent outright while the key is held
    }
}

static bool body_busy_with_weapon(AnimId id)
{
    return id == ANIM_RELOAD || id == ANIM_CHANGE || id == ANIM_THROW_WEAPON;
}

// Prone entry and exit: the get-up doubles as a jump wind-up near its end.
static void prone_control(const Context *ctx, Soldier *s, ControlInput *input)
{
    const Anims *anims = ctx->anims;
    Anim *legs = &s->legs, *body = &s->body;

    if (input->prone && legs->id != ANIM_GET_UP && legs->id != ANIM_PRONE && legs->id != ANIM_PRONE_MOVE) {
        legs_apply(anims, s, ANIM_PRONE, 1);
        if (!body_busy_with_weapon(body->id)) anim_apply(anims, body, ANIM_PRONE, 1);
        s->old_direction = s->direction;
        input->prone = false;
    }

    // Get up: pressing prone again, or turning around.
    if (s->stance == STANCE_PRONE && (input->prone || s->direction != s->old_direction) &&
        ((legs->id == ANIM_PRONE && legs->frame > 23) || legs->id == ANIM_PRONE_MOVE)) {
        if (legs->id != ANIM_GET_UP) anim_set(anims, legs, ANIM_GET_UP, 9);
        if (!body_busy_with_weapon(body->id)) anim_apply(anims, body, ANIM_GET_UP, 9);
    }

    bool unprone = false;
    if (legs->id == ANIM_GET_UP && legs->frame > 20 && s->on_ground && input->up) {
        if (input->left || input->right) legs_apply(anims, s, ANIM_JUMP_SIDE, legs->frame - 20);
        else legs_apply(anims, s, ANIM_JUMP, legs->frame - 15);
        unprone = true;
    } else if (legs->id == ANIM_GET_UP && legs->frame > 23) {
        if (input->left || input->right) {
            legs_apply(anims, s, (s->direction == 1) != input->left ? ANIM_RUN : ANIM_RUN_BACK, 1);
        } else if (!s->on_ground && input->up) {
            legs_apply(anims, s, ANIM_RUN, 1);
        } else {
            legs_apply(anims, s, ANIM_STAND, 1);
        }
        unprone = true;
    }
    if (unprone) {
        s->stance = STANCE_STAND;
        if (!body_busy_with_weapon(body->id)) anim_apply(anims, body, ANIM_STAND, 1);
    }
}

// Every 10 ticks: how close the muzzle is to cover (Control.pas). Probes 8 units along
// the arm from 5 units above the head, against map colliders and, in team games,
// crouched teammates.
static void cover_check(const Context *ctx, World *w, uint8_t index)
{
    if (w->tick % 10 != 0) return;
    Soldier *s = &w->soldiers[index];
    s->collider_distance = 255;

    Pose pose = soldier_pose(ctx->anims, s, s->pos);
    Vec2 arm = vec2_scale(vec2_normalize(vec2_sub(pose.p[14], pose.p[15])), 8.0f);
    Vec2 probe = vec2_add(vec2_sub(pose.p[11], vec2(0.0f, 5.0f)), arm);

    const Map *map = ctx->map;
    for (int i = 0; i < map->collider_count; i++) {
        const MapCollider *c = &map->colliders[i];
        if (!c->active) continue;
        float d = vec2_length(vec2_sub(probe, c->pos));
        if (d < c->radius) {
            s->collider_distance = (uint8_t)round_half_even(minf(d, 253.0f));
            break;
        }
    }

    if (s->team == TEAM_NONE || s->team == TEAM_SPECTATOR) return;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        const Soldier *other = &w->soldiers[i];
        if (i == index || !other->active || other->team != s->team || other->stance != STANCE_CROUCH) continue;
        float d = vec2_length(vec2_sub(probe, other->pos));
        if (d < SPRITE_RADIUS) {
            s->collider_distance = (uint8_t)round_half_even(minf(d, 253.0f));
            break;
        }
    }
}

// Slow down movement while an animation runs at more than normal speed.
static void animation_slowdown(Soldier *s)
{
    Anim *legs = &s->legs;
    if (legs->speed <= 1) return;

    switch (legs->id) {
    case ANIM_JUMP:
    case ANIM_JUMP_SIDE:
    case ANIM_ROLL:
    case ANIM_ROLL_BACK:
    case ANIM_PRONE:
    case ANIM_RUN:
    case ANIM_RUN_BACK:
        s->vel = vec2_div(s->vel, (float)legs->speed);
        break;
    default:
        break;
    }
    if (legs->speed > 2 && (legs->id == ANIM_PRONE_MOVE || legs->id == ANIM_CROUCH_RUN)) {
        s->vel = vec2_div(s->vel, (float)legs->speed);
    }
}

// Rolling: the roll pushes along the way it faces; a backward roll can be jumped out of.
static void move_rolling(Soldier *s, ControlInput input)
{
    Anim *legs = &s->legs;
    float dir = (float)s->direction;

    if (legs->id == ANIM_ROLL) {
        s->forces.x = s->on_ground ? dir * ROLLSPEED : dir * 2.0f * FLYSPEED;
    } else if (legs->id == ANIM_ROLL_BACK) {
        s->forces.x = s->on_ground ? -dir * ROLLSPEED : -dir * 2.0f * FLYSPEED;
        if (legs->frame > 1 && legs->frame < 8 && input.up) {
            s->forces.y -= (float)(0.30 * 1.5); // JUMPDIRSPEED * 1.5
            s->forces.x *= 0.5f;
            s->vel.x *= 0.8f;
        }
    }
}

// Crouching while running: a roll from a run or a fall, a crouch-run otherwise.
static void move_crouch_run(const Anims *anims, Soldier *s, ControlInput input)
{
    Anim *legs = &s->legs, *body = &s->body;
    if (!s->on_ground) return;

    float sign = input.right ? 1.0f : -1.0f;
    bool facing_move = (s->direction == 1) == input.right;
    bool can_roll = legs->id == ANIM_RUN || legs->id == ANIM_RUN_BACK || legs->id == ANIM_FALL ||
                    legs->id == ANIM_PRONE_MOVE || (legs->id == ANIM_PRONE && legs->frame >= 24);

    if (can_roll) {
        if (legs->id == ANIM_PRONE_MOVE || (legs->id == ANIM_PRONE && legs->frame == anim_frames(anims, ANIM_PRONE))) {
            s->stance = STANCE_STAND;
        }
        AnimId roll = facing_move ? ANIM_ROLL : ANIM_ROLL_BACK;
        anim_apply(anims, body, roll, 1);
        anim_set(anims, legs, roll, 1);
    } else {
        legs_apply(anims, s, facing_move ? ANIM_CROUCH_RUN : ANIM_CROUCH_RUN_BACK, 1);
    }

    if (legs->id == ANIM_CROUCH_RUN || legs->id == ANIM_CROUCH_RUN_BACK) {
        s->forces.x = sign * CROUCHRUNSPEED;
    } else if (legs->id == ANIM_ROLL || legs->id == ANIM_ROLL_BACK) {
        s->forces.x = sign * 2.0f * CROUCHRUNSPEED;
    }
}

// Lying down: crawling, or holding still at the end of the go-prone animation.
static void move_prone(const Anims *anims, Soldier *s, ControlInput input)
{
    Anim *legs = &s->legs, *body = &s->body;
    if (!s->on_ground) return;
    if (!((legs->id == ANIM_PRONE && legs->frame > 25) || legs->id == ANIM_PRONE_MOVE)) return;

    if (input.left || input.right) {
        if (legs->frame < 4 || legs->frame > 14) s->forces.x = input.left ? -PRONESPEED : PRONESPEED;
        legs_apply(anims, s, ANIM_PRONE_MOVE, 1);
        switch (body->id) {
        case ANIM_CLIP_IN:
        case ANIM_CLIP_OUT:
        case ANIM_SLIDE_BACK:
        case ANIM_RELOAD:
        case ANIM_CHANGE:
        case ANIM_THROW:
        case ANIM_THROW_WEAPON:
            break;
        default:
            anim_apply(anims, body, ANIM_PRONE_MOVE, 1);
            break;
        }
        if (legs->id != ANIM_PRONE_MOVE) anim_set(anims, legs, ANIM_PRONE_MOVE, 1);
    } else {
        if (legs->id != ANIM_PRONE) anim_set(anims, legs, ANIM_PRONE, 1);
        legs->frame = 26;
    }
}

static void move_side_jump(const Anims *anims, Soldier *s, ControlInput input)
{
    Anim *legs = &s->legs;
    float sign = input.right ? 1.0f : -1.0f;

    if (s->on_ground) {
        switch (legs->id) {
        case ANIM_RUN:
        case ANIM_RUN_BACK:
        case ANIM_STAND:
        case ANIM_CROUCH:
        case ANIM_CROUCH_RUN:
        case ANIM_CROUCH_RUN_BACK:
            legs_apply(anims, s, ANIM_JUMP_SIDE, 1);
            break;
        default:
            break;
        }
        if (legs->frame == anim_frames(anims, legs->id)) legs_apply(anims, s, ANIM_RUN, 1);
    } else if (legs->id == ANIM_ROLL || legs->id == ANIM_ROLL_BACK) {
        legs_apply(anims, s, (s->direction == 1) == input.right ? ANIM_RUN : ANIM_RUN_BACK, 1);
    }

    if (legs->id == ANIM_JUMP && legs->frame < 10) legs_apply(anims, s, ANIM_JUMP_SIDE, 1);
    if (legs->id == ANIM_JUMP_SIDE && legs->frame > 3 && legs->frame < 11) {
        s->forces = vec2(sign * JUMPDIRSPEED, (float)(-0.30 / 1.2)); // -JUMPDIRSPEED / 1.2
    }
}

static void move_jump(const Anims *anims, Soldier *s)
{
    Anim *legs = &s->legs;
    if (s->on_ground) {
        legs_apply(anims, s, ANIM_JUMP, 1);
        if (legs->frame == anim_frames(anims, legs->id)) legs_apply(anims, s, ANIM_STAND, 1);
    }
    if (legs->id == ANIM_JUMP) {
        if (legs->frame > 8 && legs->frame < 15) s->forces.y = -JUMPSPEED;
        if (legs->frame == anim_frames(anims, ANIM_JUMP)) legs_apply(anims, s, ANIM_FALL, 1);
    }
}

static void move_run(const Anims *anims, Soldier *s, ControlInput input)
{
    float sign = input.right ? 1.0f : -1.0f;
    legs_apply(anims, s, (s->direction == 1) == input.right ? ANIM_RUN : ANIM_RUN_BACK, 1);
    if (s->on_ground) s->forces = vec2(sign * RUNSPEED, -RUNSPEEDUP);
    else s->forces.x = sign * FLYSPEED;
}

// Locomotion: one situation wins per tick, in priority order: rolling, crouch-slide,
// prone crawl, side jump, jump, crouch, run, idle. Some body poses freeze it.
static void movement_control(const Context *ctx, Soldier *s, ControlInput input)
{
    const Anims *anims = ctx->anims;
    Anim *legs = &s->legs, *body = &s->body;

    switch (body->id) {
    case ANIM_TAKE_OFF:
    case ANIM_PISS:
    case ANIM_MERCY:
    case ANIM_MERCY2:
    case ANIM_VICTORY:
    case ANIM_OWN:
        return;
    default:
        break;
    }

    bool sideways = input.left || input.right;
    bool lying = legs->id == ANIM_PRONE || legs->id == ANIM_PRONE_MOVE ||
                 (legs->id == ANIM_GET_UP && body->id != ANIM_THROW && body->id != ANIM_PUNCH);

    if (body->id == ANIM_ROLL || body->id == ANIM_ROLL_BACK) {
        move_rolling(s, input);
    } else if (sideways && input.down) {
        move_crouch_run(anims, s, input);
    } else if (lying) {
        move_prone(anims, s, input);
    } else if (sideways && input.up) {
        move_side_jump(anims, s, input);
    } else if (input.up) {
        move_jump(anims, s);
    } else if (input.down) {
        if (s->on_ground) legs_apply(anims, s, ANIM_CROUCH, 1);
    } else if (sideways) {
        move_run(anims, s, input);
    } else {
        legs_apply(anims, s, s->on_ground ? ANIM_STAND : ANIM_FALL, 1);
    }
}

// Rolls run on both machines in lockstep; whichever started, the other follows.
static void roll_control(const Context *ctx, Soldier *s, ControlInput input)
{
    const Anims *anims = ctx->anims;
    Anim *legs = &s->legs, *body = &s->body;

    if (legs->id == ANIM_ROLL && body->id != ANIM_ROLL) anim_apply(anims, body, ANIM_ROLL, 1);
    if (body->id == ANIM_ROLL && legs->id != ANIM_ROLL) legs_apply(anims, s, ANIM_ROLL, 1);
    if (legs->id == ANIM_ROLL_BACK && body->id != ANIM_ROLL_BACK) anim_apply(anims, body, ANIM_ROLL_BACK, 1);
    if (body->id == ANIM_ROLL_BACK && legs->id != ANIM_ROLL_BACK) legs_apply(anims, s, ANIM_ROLL_BACK, 1);

    bool rolling = body->id == ANIM_ROLL || body->id == ANIM_ROLL_BACK;
    if (rolling && legs->frame != body->frame) {
        if (legs->frame > body->frame) body->frame = legs->frame;
        else legs->frame = body->frame;
    }

    if (rolling && body->frame == anim_frames(anims, body->id)) {
        bool backflip = !s->on_ground && body->id == ANIM_ROLL_BACK && input.up;
        if (backflip) {
            if (input.left || input.right) {
                legs_apply(anims, s, (s->direction == 1) != input.left ? ANIM_RUN : ANIM_RUN_BACK, 1);
            } else {
                legs_apply(anims, s, ANIM_FALL, 1);
            }
        } else if (input.down) {
            if (input.left || input.right) {
                legs_apply(anims, s, body->id == ANIM_ROLL ? ANIM_CROUCH_RUN : ANIM_CROUCH_RUN_BACK, 1);
            } else {
                legs_apply(anims, s, ANIM_CROUCH, 15);
            }
        }
        anim_apply(anims, body, ANIM_STAND, 1);
    }
}

// Body animations that the stance pose may replace at any frame.
static bool body_idle_animation(AnimId id)
{
    switch (id) {
    case ANIM_RECOIL:
    case ANIM_SMALL_RECOIL:
    case ANIM_AIM_RECOIL:
    case ANIM_HANDS_UP_RECOIL:
    case ANIM_SHOTGUN:
    case ANIM_BARRET:
    case ANIM_CHANGE:
    case ANIM_THROW_WEAPON:
    case ANIM_WEAPON_NONE:
    case ANIM_PUNCH:
    case ANIM_ROLL:
    case ANIM_ROLL_BACK:
    case ANIM_RELOAD_BOW:
    case ANIM_CIGAR:
    case ANIM_MATCH:
    case ANIM_SMOKE:
    case ANIM_WIPE:
    case ANIM_TAKE_OFF:
    case ANIM_GROIN:
    case ANIM_PISS:
    case ANIM_MERCY:
    case ANIM_MERCY2:
    case ANIM_VICTORY:
    case ANIM_OWN:
    case ANIM_RELOAD:
    case ANIM_PRONE:
    case ANIM_GET_UP:
    case ANIM_PRONE_MOVE:
    case ANIM_MELEE:
        return false;
    default:
        return true;
    }
}

// One-shot body animations fall back to the pose for the stance when they finish (or
// at once for the replaceable ones); the stance itself follows the legs.
static void body_pose_control(const Context *ctx, Soldier *s)
{
    const Anims *anims = ctx->anims;
    Anim *legs = &s->legs, *body = &s->body;

    bool returns_to_stance = (!(s->controls & BUTTON_THROW) && body_idle_animation(body->id)) ||
                             (body->frame == anim_frames(anims, body->id) && body->id != ANIM_PRONE) ||
                             (s->weapon.fire_count == 0 && body->id == ANIM_BARRET);

    if (s->weapon.ammo > 0 && returns_to_stance) {
        switch (s->stance) {
        case STANCE_STAND:
            anim_apply(anims, body, ANIM_STAND, 1);
            break;
        case STANCE_CROUCH:
            // Near cover the gun comes up over it; out of a recoil it resumes partway in.
            if (s->collider_distance < 255) {
                anim_apply(anims, body, ANIM_HANDS_UP_AIM, body->id == ANIM_HANDS_UP_RECOIL ? 11 : 1);
            } else {
                anim_apply(anims, body, ANIM_AIM, body->id == ANIM_AIM_RECOIL ? 6 : 1);
            }
            break;
        case STANCE_PRONE:
            anim_apply(anims, body, ANIM_PRONE, 26);
            break;
        }
    }

    switch (legs->id) {
    case ANIM_CROUCH:
    case ANIM_CROUCH_RUN:
    case ANIM_CROUCH_RUN_BACK:
        s->stance = STANCE_CROUCH;
        break;
    case ANIM_PRONE:
    case ANIM_PRONE_MOVE:
        s->stance = STANCE_PRONE;
        break;
    default:
        s->stance = STANCE_STAND;
        break;
    }
}

// The sniper view (Control.pas): with the Barrett ready, from a crouch or prone, aiming
// near the view's edge draws the camera out toward the aim, a step a tick, further
// prone than crouching; aiming back near the body lets it return. Anything else, the
// gun fired or the stance left, snaps it back.
static void sniper_view(Soldier *s)
{
    bool scoping = s->weapon.id == WEAPON_BARRETT && s->weapon.fire_count == 0 && (s->body.id == ANIM_PRONE || s->body.id == ANIM_AIM);
    if (!scoping) {
        s->aim_dist = DEFAULT_AIM_DIST;
        return;
    }
    float dx = fabsf(s->aim.x - s->pos.x), dy = fabsf(s->aim.y - s->pos.y);
    if (dx >= 640.0f / 1.035f || dy >= 480.0f / 1.035f) {
        if (s->body.id == ANIM_PRONE && s->aim_dist > SNIPER_AIM_DIST) s->aim_dist -= AIM_DIST_STEP;
        if (s->body.id == ANIM_AIM && s->aim_dist > CROUCH_AIM_DIST) s->aim_dist -= 2 * AIM_DIST_STEP;
    }
    if (dx < 640.0f / 1.5f && dy < 480.0f / 1.5f && s->aim_dist < DEFAULT_AIM_DIST) {
        s->aim_dist += AIM_DIST_STEP;
        if (s->aim_dist > DEFAULT_AIM_DIST - AIM_DIST_STEP / 2) s->aim_dist = DEFAULT_AIM_DIST;
    }
}

void soldier_control(const Context *ctx, World *w, uint8_t index, Events *events, bool armed)
{
    Soldier *s = &w->soldiers[index];
    if (s->legs.speed < 1) s->legs.speed = 1;
    if (s->body.speed < 1) s->body.speed = 1;
    s->fired = false; // set again by the weapon if a shot goes off this tick

    ControlInput input = resolve_left_right(s);
    // the jets, or the grappling hook on their key; it takes nothing else away
    if (s->gear == GEAR_HOOK && w->rules.hook) hook_control(ctx, w, index);
    else jets_control(ctx, w, s, input);
    if (armed) combat_control(ctx, w, index, events);
    prone_control(ctx, s, &input);
    if (armed) combat_after_prone(ctx, w, s);
    animation_slowdown(s);
    cover_check(ctx, w, index);
    movement_control(ctx, s, input);
    if (armed) combat_reload_animation(ctx, s);
    roll_control(ctx, s, input);
    body_pose_control(ctx, s);
    sniper_view(s);
}
