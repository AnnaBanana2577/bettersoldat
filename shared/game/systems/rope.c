// The rope that replaces the jets: a Worms/Liero line flung at the aim until it holds
// a poly the player can stand on. The throw is a rocket's flight: fast at first, the
// LAW's pace, losing its speed each tick until gravity has its way and it drops; past
// its reach or off the map, the rope gives up. Held, the rope cannot pass through the
// polys between its anchor and its owner: it catches on the first corner its line
// crosses, the part wound around it held there while the rest hangs. From the last
// corner it hangs the soldier from a rod with the rope's give: taut, nothing moves
// along it, but pulled past its length it stretches — up to a part of itself — and
// the stretch pulls back; the up key climbs it to the anchor, where the rope lets go;
// left and right swing on it, the soldier rolled up in the classic cannonball, the
// momentum gathered — held with up or down, the sideways push joins the climb or the
// payout, the diagonal; and the rope key pressed again cuts it, the swing kept. The same
// key as the jets, played the same way: held to throw, pressed to cut.
//
// The rope's state lives in the soldier's owned half (phase, tip, length, the corners
// it is wound around, the key's press edge), so it travels with the rest of what its
// client decides and every machine sees the same rope; computed locally instead, a
// corner is caught from a stale position and pins the rope elsewhere, and a press
// re-read on the cut state throws a phantom rope. The cut needs no authority: bullets
// fly identically on every machine, and every machine's bullets pass computes the cut
// itself (rope_cut); the EVENT_ROPE_CUT it emits is for the sparks alone and never
// leaves the machine that made it.

#include "game/systems/systems.h"

#define ROPE_MAX 800.0f // a throw flies this far at most; past it, the rope drops
#define ROPE_DRAG 0.97f // the throw's loss of speed a tick: fast at first, falling at last
#define ROPE_SWING 0.088f // the push a tick of holding left or right gives the swing
#define ROPE_ROLL_SLOW 2 // the swing's roll plays this many times slower than its pace
#define ROPE_CATCH 1.0f // a pull along the rope faster than this stretches it
#define ROPE_SPRING 0.05f // the stretch's pull back a tick, ROPE_SPRING of itself
#define ROPE_DAMP 0.9f // the part of the bounce kept a tick: how the stretch settles
// The climb's pace, as parts of the rope's speed: a tenth at first, gaining a
// hundredth a tick of holding up, a fifth at most.
#define ROPE_CLIMB_START 0.10f
#define ROPE_CLIMB_GAIN 0.01f
#define ROPE_CLIMB_MAX 0.20f
#define ROPE_REACH 8.0f // how near the anchor the climb comes before it snaps it

// The throw's flying end: flung at the aim at the rope's pace, losing its speed each
// tick as the LAW's rocket does, and falling under gravity at last. It holds on the
// first poly the player can stand on (the parachute's filter) it crosses; past the
// rope's reach, or off the map, the rope drops.
static void rope_throw(const Context *ctx, const World *w, Soldier *s)
{
    Vec2 prev = s->rope_tip;
    s->rope_tip_vel.y += w->gravity * BULLET_GRAVITY;
    s->rope_tip_vel = vec2_scale(s->rope_tip_vel, ROPE_DRAG);
    s->rope_tip = vec2_add(s->rope_tip, s->rope_tip_vel);
    float dist = 0.0f;
    RayFilter filter = {.player = true, .team = s->team};
    if (map_ray_cast(ctx->map, prev, s->rope_tip, vec2_length(vec2_sub(s->rope_tip, prev)) + 1.0f, filter, &dist)) {
        s->rope_tip = vec2_add(prev, vec2_scale(vec2_normalize(vec2_sub(s->rope_tip, prev)), dist));
        s->rope_len = vec2_length(vec2_sub(s->rope_tip, s->pos));
        s->rope_grab = s->rope_len; // the down key feeds it back out only this far
        s->rope = ROPE_ATTACHED;
        return;
    }
    s->rope_len = vec2_length(vec2_sub(s->rope_tip, s->pos));
    float bound = (float)(ctx->map->sectors_num * ctx->map->sectors_division - 10);
    if (s->rope_len > ROPE_MAX || fabsf(s->rope_tip.x) > bound || fabsf(s->rope_tip.y) > bound) s->rope = ROPE_NONE;
}

// The rope's running end: where the physics holds it — the last corner it is caught
// around, or the anchor itself.
static Vec2 rope_end(const Soldier *s)
{
    return s->rope_wraps_count ? s->rope_wraps[s->rope_wraps_count - 1] : s->rope_tip;
}

// The part of the rope wound around the corners it is caught on.
static float rope_wrapped(const Soldier *s)
{
    float wrapped = 0.0f;
    Vec2 prev = s->rope_tip;
    for (int i = 0; i < s->rope_wraps_count; i++) {
        wrapped += vec2_length(vec2_sub(s->rope_wraps[i], prev));
        prev = s->rope_wraps[i];
    }
    return wrapped;
}

// The rope and the map: held to a poly, the rope cannot pass through the polys
// between its anchor and its owner — it catches on the one its line crosses,
// pinned on the edge it meets and the edge it leaves by, the wound part held
// around the solid while the rest hangs. The pins sit a hair on the rope's own
// side of each edge, so every free stretch stays clear of the map; swung back,
// the corner lets go when the straight line from the point before it is clear
// again and the rope reaches.
static void rope_wrap(const Context *ctx, Soldier *s)
{
    RayFilter filter = {.player = true, .team = s->team};
    Vec2 end = rope_end(s);
    Vec2 d = vec2_sub(s->pos, end);
    float len = vec2_length(d);
    if (len < 0.001f) return;

    if (s->rope_wraps_count < ROPE_WRAPS) {
        float dist = 0.0f;
        int poly_index = -1;
        if (map_ray_cast_hit(ctx->map, end, s->pos, len, filter, &dist, NULL, &poly_index) && poly_index >= 0) {
            const Polygon *poly = &ctx->map->polys[poly_index];
            Vec2 dir = vec2_div(d, len);
            // the crossings of the rope's line with the poly's edges: where it meets
            // the solid, and where it leaves it again
            Vec2 in = end, out = end;
            int n = 0;
            for (int e = 0; e < 3; e++) {
                Vec2 p;
                if (!segments_cross(end, s->pos, poly->verts[e], poly->verts[(e + 1) % 3], &p)) continue;
                if (n > 0 && vec2_length(vec2_sub(p, in)) < 0.01f) continue; // the same vertex, grazing it
                if (n == 0) in = p;
                else out = p;
                n++;
            }
            if (n == 2 && vec2_length(vec2_sub(in, end)) > vec2_length(vec2_sub(out, end))) {
                Vec2 t = in;
                in = out;
                out = t;
            }
            float in_d = vec2_length(vec2_sub(in, end));
            float out_d = vec2_length(vec2_sub(out, end));
            int room = ROPE_WRAPS - (int)s->rope_wraps_count;
            if (n >= 1 && (in_d > 6.0f || s->rope_wraps_count == 0)) {
                // in_d <= 6 with wraps already on: the running end is a pin on
                // this very poly — the rope is already wound around it. The bare
                // anchor may sit as close: the throw lands the tip on the surface
                // of the poly it holds, and that poly catches the rope all the same.
                if (n == 2 && out_d < len - 6.0f && room >= 2) {
                    s->rope_wraps[s->rope_wraps_count++] = vec2_sub(in, vec2_scale(dir, 0.5f));
                    s->rope_wraps[s->rope_wraps_count++] = vec2_add(out, vec2_scale(dir, 0.5f));
                    return;
                }
                if (room >= 1 && (n == 1 ? in_d : out_d) < len - 6.0f) {
                    s->rope_wraps[s->rope_wraps_count++] = vec2_add(n == 1 ? in : out, vec2_scale(dir, 0.5f));
                    return;
                }
            }
        }
    }
    if (s->rope_wraps_count > 0) {
        Vec2 before = s->rope_wraps_count > 1 ? s->rope_wraps[s->rope_wraps_count - 2] : s->rope_tip;
        float rest = s->rope_len - (rope_wrapped(s) - vec2_length(vec2_sub(s->rope_wraps[s->rope_wraps_count - 1], before)));
        float dist = 0.0f;
        if (!map_ray_cast(ctx->map, before, s->pos, len, filter, &dist) &&
            vec2_length(vec2_sub(s->pos, before)) <= rest + 0.01f)
            s->rope_wraps_count--;
    }
}

// The hang: a rod with the rope's give, held from the rope's running end. At its
// length — and anywhere nearer the end — the rod holds: nothing moves along it, so
// gravity swings the soldier without stretching the rope. Pulled past its length,
// the rope gives: the stretch pulls back — ROPE_SPRING of itself a tick, the bounce
// damping a part of it — and never past ROPE_GIVE of the length, so a hard pull
// bounces instead of snapping.
static void rope_hold(Soldier *s)
{
    Vec2 end = rope_end(s);
    float hold_len = s->rope_len - rope_wrapped(s); // what's left past the wound part
    if (hold_len < 0.0f) hold_len = 0.0f; // wound up to its owner: held at the last corner
    Vec2 d = vec2_sub(s->pos, end);
    float len = vec2_length(d);
    if (len < 0.001f) { // right at the anchor: hang straight down
        s->pos = vec2_add(end, vec2(0.0f, hold_len));
        d = vec2(0.0f, hold_len);
        len = hold_len;
    }
    Vec2 n = vec2_div(d, len);
    float radial = vec2_dot(s->vel, n); // along the rope: + away from the anchor
    if (len > hold_len && (radial > ROPE_CATCH || len > hold_len + ROPE_CATCH)) {
        // the give: the stretch pulls back, harder the farther it goes, and the
        // bounce damps as it settles, never past ROPE_GIVE of the length
        float stretch = len - hold_len;
        s->vel = vec2_sub(s->vel, vec2_scale(n, stretch * ROPE_SPRING + radial * (1.0f - ROPE_DAMP)));
        float max_len = hold_len * (1.0f + ROPE_GIVE);
        if (len > max_len) s->pos = vec2_add(end, vec2_scale(n, max_len));
    } else {
        // taut: the rod holds the length, nothing moves along it
        s->pos = vec2_add(end, vec2_scale(n, hold_len));
        s->vel = vec2_sub(s->vel, vec2_scale(n, radial));
    }
}

// The climb: the up key reels the soldier to the anchor, the rope shortening as it
// goes — the wound part slips around its corners as the owner rises past them. The
// pace is the rope's law of climbing: a tenth of the rope's speed at first, gaining
// a hundredth of it every tick the key is held, a fifth of it at most — a slow pull
// that builds. The pull is the pace itself, travelled by the body, the sideways
// momentum carried, so the climb's momentum is the soldier's own — a cut made
// mid-climb, or the rope let go at the top, leaves them flying on it. The rope snaps
// when the body is within the climb's reach of its end, or when the rope is reeled
// down to it — the anchor's poly pushes the body back, so the reeled length is what
// tells the hands have arrived — and lets its owner go, stood where it held.
static void rope_climb(Soldier *s)
{
    s->rope_climb = fminf(s->rope_climb + ROPE_SPEED * ROPE_CLIMB_GAIN, ROPE_SPEED * ROPE_CLIMB_MAX);
    Vec2 end = rope_end(s);
    Vec2 d = vec2_sub(end, s->pos);
    float len = vec2_length(d);
    Vec2 n = len > 0.001f ? vec2_div(d, len) : vec2(0.0f, 1.0f);
    // the pace along the rope; what crosses it — the swing's push, gathered — stays
    float radial = vec2_dot(s->vel, n);
    s->vel = vec2_add(vec2_scale(n, s->rope_climb - radial), s->vel);
    if (len <= s->rope_climb + ROPE_REACH || s->rope_len <= ROPE_REACH) {
        s->pos = end;
        s->rope = ROPE_NONE;
        s->rope_wraps_count = 0;
        return;
    }
    s->rope_len -= s->rope_climb;
}

// The payout: the down key feeds the rope out, the pace the climb reels it in — the
// one ramp serves both, so holding down runs out exactly as fast as holding up hauls
// back — and only as far as the rope was when it grabbed: the climb's slack back out,
// never more rope than the throw made.
static void rope_payout(Soldier *s)
{
    s->rope_climb = fminf(s->rope_climb + ROPE_SPEED * ROPE_CLIMB_GAIN, ROPE_SPEED * ROPE_CLIMB_MAX);
    if (s->rope_len < s->rope_grab) s->rope_len = fminf(s->rope_len + s->rope_climb, s->rope_grab);
    rope_hold(s);
}

// The swing: holding left or right pushes the hang along its arc, at right angles to
// the rope, and rolls the soldier up — the classic cannonball, its spin slowed so
// the roll reads, the momentum gathered for the cut. The gathered momentum is cut at
// ROPE_SWING_MAX: past it the push in that direction adds nothing — a swing built
// faster than the sim resolves runs its owner through polygons — while the push
// against the momentum always works: the cut is on the gain, not the turn.
static void rope_swing(const Anims *anims, Soldier *s, bool left, bool right)
{
    Vec2 d = vec2_sub(s->pos, rope_end(s));
    float len = vec2_length(d);
    if (len < 0.001f) return;
    Vec2 n = vec2_div(d, len);
    Vec2 tang = vec2(n.y, -n.x);
    float tang_vel = vec2_dot(s->vel, tang); // the momentum already gathered along the arc
    if (right && tang_vel < ROPE_SWING_MAX) s->vel = vec2_add(s->vel, vec2_scale(tang, ROPE_SWING));
    if (left && tang_vel > -ROPE_SWING_MAX) s->vel = vec2_sub(s->vel, vec2_scale(tang, ROPE_SWING));
    anim_apply(anims, &s->body, ANIM_ROLL, 1);
    legs_apply(anims, s, ANIM_ROLL, 1);
    s->body.speed = anims->data[ANIM_ROLL].speed * ROPE_ROLL_SLOW;
    s->legs.speed = anims->data[ANIM_ROLL].speed * ROPE_ROLL_SLOW;
}

void rope_control(const Context *ctx, World *w, uint8_t index, Events *events)
{
    Soldier *s = &w->soldiers[index];
    bool jet = (s->controls & BUTTON_JET) != 0;
    bool press = jet && !s->was_jet;
    bool up = (s->controls & BUTTON_JUMP) != 0;
    bool down = (s->controls & BUTTON_CROUCH) != 0;
    bool left = (s->controls & BUTTON_LEFT) != 0;
    bool right = (s->controls & BUTTON_RIGHT) != 0;
    switch (s->rope) {
    case ROPE_NONE:
        if (press && w->rules.rope) { // a game without the rope (sv_rope 0) throws nothing
            if (s->stance == STANCE_PRONE) { // up off the ground first
                anim_set(ctx->anims, &s->legs, ANIM_STAND, 1);
                s->stance = STANCE_STAND;
            }
            s->rope_tip = soldier_pose(ctx->anims, s, s->pos).p[14]; // the rope leaves the hand
            if (map_collision_test(ctx->map, s->rope_tip, false, NULL)) s->rope_tip = s->pos; // a hand in a wall throws from the body
            Vec2 dir = vec2_normalize(vec2_sub(s->aim, s->pos));
            if (dir.x == 0.0f && dir.y == 0.0f) dir = (Vec2){(float)s->direction, 0.0f}; // aiming at oneself
            s->rope_tip_vel = vec2_scale(dir, ROPE_SPEED);
            s->rope_climb = ROPE_SPEED * ROPE_CLIMB_START; // the fresh rope climbs from its first pace
            s->rope_wraps_count = 0;
            s->rope = ROPE_THROWING;
        }
        break;
    case ROPE_THROWING:
        if (jet) rope_throw(ctx, w, s);
        else s->rope = ROPE_NONE; // let go of before it held: it retracts
        break;
    case ROPE_ATTACHED:
        if (press) { // the rope key pressed again cuts it, the swing's momentum kept
            s->rope = ROPE_NONE;
            s->rope_wraps_count = 0;
            event_emit(events, (Event){.type = EVENT_ROPE_CUT, .rope_cut = {.player = index, .pos = s->pos}});
        } else {
            rope_wrap(ctx, s); // the polys between it and its owner catch it
            if (up) {
                rope_climb(s);
            } else if (down) {
                rope_payout(s); // and the down key feeds it out again, the same pace
            } else {
                s->rope_climb = ROPE_SPEED * ROPE_CLIMB_START; // a new climb starts from the first pace again
                rope_hold(s);
            }
            // left and right with up or down: the diagonal — the climb or the payout
            // carries the swing's sideways push, the roll on
            if (s->rope == ROPE_ATTACHED && (left || right)) rope_swing(ctx->anims, s, left, right);
        }
        break;
    case ROPE_PHASE_COUNT:
        break;
    }
    s->was_jet = jet;

    // A rope out takes the locomotion away, so the legs stay as the hang poses them —
    // the swing's roll excepted.
    if (s->rope != ROPE_NONE && s->body.id != ANIM_ROLL && s->body.id != ANIM_ROLL_BACK)
        legs_apply(ctx->anims, s, s->on_ground ? ANIM_STAND : ANIM_FALL, 1);

    // The rope gone, the roll's slowed pace leaves with it: back on the fall before
    // the animation's slowdown could rob the momentum the cut kept. A plain roll of
    // the soldier's own plays at its own speed and stays.
    if (s->rope == ROPE_NONE && s->legs.speed > 1 && (s->legs.id == ANIM_ROLL || s->legs.id == ANIM_ROLL_BACK))
        legs_apply(ctx->anims, s, s->on_ground ? ANIM_STAND : ANIM_FALL, 1);
}

// Does the segment a-b cross the rope — the hanging part or the part wound around
// its corners? Gives the crossing point. The rope's end is in the owner's hand, as
// drawn.
bool rope_crosses(const Context *ctx, const Soldier *s, Vec2 a, Vec2 b, Vec2 *point)
{
    Vec2 prev = soldier_pose(ctx->anims, s, s->pos).p[14];
    for (int i = (int)s->rope_wraps_count - 1; i >= -1; i--) {
        Vec2 next = i < 0 ? s->rope_tip : s->rope_wraps[i];
        if (segments_cross(a, b, prev, next, point)) return true;
        prev = next;
    }
    return false;
}

void rope_cut(World *w, uint8_t player, Vec2 at, Events *events)
{
    Soldier *s = &w->soldiers[player];
    if (!s->active || s->rope == ROPE_NONE) return;
    s->rope = ROPE_NONE;
    s->rope_wraps_count = 0;
    event_emit(events, (Event){.type = EVENT_ROPE_CUT, .rope_cut = {.player = player, .pos = at}});
}
