// The skeleton pose: where each of the gostek's points is for a soldier, from its legs
// and body animation frames, its stance and its aim. Gameplay only moves the body
// particle; the pose is derived on demand for hit tests, hands and drawing. Ported from
// soldat-odin.

#include "game/systems/systems.h"

// The points the legs animation places; the body animation places the rest.
static bool is_leg_point(int i)
{
    return (i >= 0 && i <= 5) || i == 16 || i == 17;
}

// Body animations in which the arms do their own thing rather than follow the aim.
static bool arms_animated(AnimId id)
{
    switch (id) {
    case ANIM_RELOAD:
    case ANIM_RELOAD_BOW:
    case ANIM_CLIP_IN:
    case ANIM_CLIP_OUT:
    case ANIM_SLIDE_BACK:
    case ANIM_CHANGE:
    case ANIM_THROW_WEAPON:
    case ANIM_WEAPON_NONE:
    case ANIM_PUNCH:
    case ANIM_ROLL:
    case ANIM_ROLL_BACK:
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
    case ANIM_MELEE:
        return true;
    default:
        return false;
    }
}

Pose soldier_pose(const Anims *anims, const Soldier *s, Vec2 pos)
{
    Pose pose;
    Vec2 *p = pose.p;
    float dir = (float)s->direction;
    const Vec2 *legs = anim_frame(anims, s->legs);
    const Vec2 *body = anim_frame(anims, s->body);

    float body_y = 0.0f;
    switch (s->stance) {
    case STANCE_STAND:
        body_y = 8.0f;
        break;
    case STANCE_CROUCH:
        body_y = 9.0f;
        break;
    case STANCE_PRONE:
        body_y = 9.0f;
        if (s->body.id == ANIM_PRONE) body_y = s->body.frame > 9 ? -2.0f : 14.0f - (float)s->body.frame;
        if (s->body.id == ANIM_PRONE_MOVE) body_y = 0.0f;
        break;
    }
    if (s->body.id == ANIM_GET_UP) body_y = s->body.frame > 18 ? 8.0f : 4.0f;

    for (int i = 0; i < POSE_POINTS; i++) {
        if (is_leg_point(i)) p[i] = vec2_add(pos, vec2(dir * legs[i].x, legs[i].y));
    }
    float hip_y = p[5].y;
    for (int i = 0; i < POSE_POINTS; i++) {
        if (!is_leg_point(i)) p[i] = vec2(pos.x + dir * body[i].x, hip_y + body_y + body[i].y);
    }

    // The head and arms turn toward the aim point.
    Vec2 head = vec2_normalize(vec2_sub(p[11], s->aim));
    p[11] = vec2_add(p[8], vec2_scale(vec2_scale(vec2(-head.y, head.x), dir), 0.1f));

    if (!arms_animated(s->body.id)) {
        bool throwing = s->body.id == ANIM_THROW;
        p[14] = vec2_add(p[15], vec2_scale(vec2_normalize(vec2_sub(p[14], s->aim)), throwing ? -5.0f : -7.0f));
        p[18] = vec2_add(vec2_add(p[15], vec2(0.0f, -4.0f)),
                         vec2_scale(vec2_normalize(vec2_sub(p[18], s->aim)), throwing ? -6.0f : -8.0f));
    }
    return pose;
}

// ---- the swing: the chain and the hair ----

// The gostek skeleton's settings for its loose points (Anims.pas: GostekSkeleton).
#define SWING_GRAVITY 1.06f
#define SWING_DAMPING 0.997f

// The chain's and the hair's swing (TSprite.UpdatePose, and the two DoVerletTimeStepFor
// at the end of a live soldier's update). The neck (21) goes where the pose has it, a
// step ahead by the body's speed; the head's top (23) sits out past the head along the
// way it is turned (the head point is 0.1 along that way, this one 5). The pendant (22)
// and the dreadlocks' end (24) fall after them under the skeleton's own gravity and
// damping, then their constraints (gostek.po's last two) pull each pair half way
// together, so the anchors end a little off too, and that is what is drawn.
void soldier_swing(const Context *ctx, const World *w, Soldier *s)
{
    const ParticleObject *sk = &ctx->skeletons->gostek;
    Pose pose = soldier_pose(ctx->anims, s, s->pos);
    s->swing[0] = vec2_add(pose.p[8], s->vel);
    s->swing[2] = vec2_add(pose.p[8], vec2_scale(vec2_sub(pose.p[11], pose.p[8]), 50.0f));
    for (int k = 0; k < 2; k++) {
        Vec2 *anchor = &s->swing[2 * k], *end = &s->swing[2 * k + 1], *old = &s->swing_old[k];
        Vec2 was = *end;
        *end = vec2_add(vec2_sub(vec2_scale(was, 1.0f + SWING_DAMPING), vec2_scale(*old, SWING_DAMPING)), vec2(0, SWING_GRAVITY * w->gravity));
        *old = was;
        int c = sk->constraint_count - 2 + k; // 22 to 21, then 24 to 23
        if (c < 0) continue;
        float rest = vec2_length(vec2_sub(sk->points[sk->constraints[c][1]], sk->points[sk->constraints[c][0]]));
        Vec2 d = vec2_sub(*anchor, *end);
        float len = vec2_length(d);
        float diff = len != 0.0f ? (len - rest) / len : 0.0f;
        *end = vec2_add(*end, vec2_scale(d, 0.5f * diff));
        *anchor = vec2_sub(*anchor, vec2_scale(d, 0.5f * diff));
    }
}
