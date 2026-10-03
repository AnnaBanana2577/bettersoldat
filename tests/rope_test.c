// The rope: flung at the aim like a rocket until it holds a poly, retracted by
// letting go early, hung from as a rod with a rope's give, swung on with left and
// right (the rolling cannonball), climbed with up to the anchor where the rope lets
// go, and cut with the rope key pressed, the momentum kept. The cutters (knife,
// thrown knife, LAW, M79, Barrett) cut a rope their bullet crosses; every other
// bullet passes it by.

#include <stdio.h>
#include <stdlib.h>

#include "test.h"

// A game like scene(), but soldier 0 wears the rope, in a game that allows it (sv_rope 1:
// the rope is off by default).
static Game *rope_scene(void)
{
    Game *g = calloc(1, sizeof(Game));
    if (!g || !context_load(&g->ctx, TEST_ASSETS, "Arena")) {
        printf("could not load map 'Arena' from assets/: the tests run from the project directory\n");
        exit(2);
    }
    MatchSettings settings = match_settings_for_map(g->ctx.map);
    settings.rope = true;
    game_init(g, 1, settings);
    g->world.authority = true;

    uint64_t rng = 7;
    Vec2 at = spawn_point(g->ctx.map, TEAM_ALPHA, &rng);
    soldier_spawn(&g->ctx, &g->world.soldiers[0], at, TEAM_ALPHA, GEAR_ROPE, WEAPON_AK74, WEAPON_COLT);
    soldier_spawn(&g->ctx, &g->world.soldiers[1], vec2(at.x + 60.0f, at.y), TEAM_BRAVO, GEAR_JETS, WEAPON_AK74, WEAPON_COLT);
    return g;
}

// One tick: `player` presses `buttons` aiming at `aim`; the rest stand with their
// sights on themselves.
static void tick_cmd(Game *g, int player, Buttons buttons, Vec2 aim)
{
    Command cmds[MAX_PLAYERS] = {0};
    for (int i = 0; i < MAX_PLAYERS; i++) {
        const Soldier *s = &g->world.soldiers[i];
        cmds[i] = (Command){.seq = g->world.tick + 1, .buttons = i == player ? buttons : 0,
                            .aim = i == player ? aim : s->pos};
    }
    game_tick(g, cmds);
}

static bool cut_event(const Game *g)
{
    for (int i = 0; i < g->events.count; i++)
        if (g->events.items[i].type == EVENT_ROPE_CUT) return true;
    return false;
}

// A spot a long rope can hang from: at least 350 of open air below, and a clear lane
// for the test's bullet across the rope's top. The map is the same every run, so the
// spot found is too.
static Vec2 rope_air(const Context *ctx)
{
    RayFilter filter = {.player = true, .team = TEAM_ALPHA};
    for (float y = -900.0f; y <= 900.0f; y += 25.0f) {
        for (float x = -500.0f; x <= 500.0f; x += 25.0f) {
            Vec2 at = vec2(x, y);
            if (map_ray_cast(ctx->map, at, vec2(x, y + 350.0f), 360.0f, filter, NULL)) continue;
            if (map_ray_cast(ctx->map, vec2(x - 60.0f, y + 40.0f), vec2(x + 60.0f, y + 40.0f), 130.0f, filter, NULL))
                continue;
            return at;
        }
    }
    return vec2(0.0f, -300.0f);
}

// Held until the rope holds, however far the throw flies.
static void throw_until_held(Game *g, Vec2 aim)
{
    tick_cmd(g, 0, BUTTON_JET, aim);
    for (int t = 0; t < 60 && g->world.soldiers[0].rope == ROPE_THROWING; t++) tick_cmd(g, 0, BUTTON_JET, aim);
}

// The presses with the same key as the jets: hold to throw, let go to retract, hold
// and it holds, climb with up to the anchor where it lets go, press to cut.
static void throw_retract_climb_cut(void)
{
    Game *g = rope_scene();
    settle(g);
    Soldier *s0 = &g->world.soldiers[0];
    Vec2 at = s0->pos;
    Vec2 down = vec2(at.x, at.y + 200.0f);

    // one press and a quick let-go retracts it before it holds
    Soldier aimed = *s0;
    aimed.aim = down; // the arms turn toward the aim: the hand the rope leaves from
    Pose pose = soldier_pose(g->ctx.anims, &aimed, s0->pos);
    tick_cmd(g, 0, BUTTON_JET, down);
    CHECK(s0->rope == ROPE_THROWING, "a press starts the throw");
    CHECK(vec2_length(vec2_sub(s0->rope_tip, pose.p[14])) < 0.5f, "and the rope leaves the hand");
    tick_cmd(g, 0, 0, down);
    CHECK(s0->rope == ROPE_NONE, "letting go before it holds retracts it");

    // held until it reaches the ground: attached, the rod at the length it flew
    tick_cmd(g, 0, BUTTON_JET, down);
    throw_until_held(g, down);
    CHECK(s0->rope == ROPE_ATTACHED, "held to the ground, the rope holds");
    CHECK(s0->rope_len > 0.0f && s0->rope_len <= 100.0f, "a short throw holds at its own length (%f)", s0->rope_len);
    Vec2 tip = s0->rope_tip;

    // the up key climbs it to the anchor, and there the rope lets go
    tick_cmd(g, 0, 0, down);
    for (int t = 0; t < 60 && s0->rope != ROPE_NONE; t++) tick_cmd(g, 0, BUTTON_JUMP, down);
    CHECK(s0->rope == ROPE_NONE, "climbed to the anchor, the rope lets go");
    CHECK(vec2_length(vec2_sub(s0->pos, tip)) < 20.0f && fabsf(s0->vel.y) < 10.0f,
          "and stands its owner where it held");

    // and a press on the hold cuts it
    tick_cmd(g, 0, BUTTON_JET, down);
    throw_until_held(g, down);
    CHECK(s0->rope == ROPE_ATTACHED, "thrown again, it holds again");
    tick_cmd(g, 0, 0, down);
    tick_cmd(g, 0, BUTTON_JET, down);
    CHECK(s0->rope == ROPE_NONE, "a press on the hold cuts the rope");
    scene_free(g);
}

// The long hang: flung from open air it holds far below, and the rod keeps the
// soldier its length away, swinging, its give never stretched past; left and right
// swing it, the soldier rolled up, and the climb up the rope at the rope's pace,
// then the cut.
static void long_hang_swing_climb_cut(void)
{
    Game *g = rope_scene();
    Soldier *s0 = &g->world.soldiers[0];
    Vec2 sky = rope_air(&g->ctx);
    place(s0, sky);
    Vec2 aim = vec2(sky.x, sky.y + 200.0f);

    throw_until_held(g, aim);
    CHECK(s0->rope == ROPE_ATTACHED, "flung from open air, the rope holds far below");
    CHECK(s0->rope_len > 350.0f, "it flew far to get there (%f)", s0->rope_len);
    Vec2 tip = s0->rope_tip;

    float rope_len = s0->rope_len;
    for (int t = 0; t < 30; t++) {
        tick_cmd(g, 0, 0, aim);
        float len = vec2_length(vec2_sub(s0->pos, s0->rope_tip));
        CHECK(len <= rope_len * (1.0f + ROPE_GIVE) + 0.001f, "the rope never gives past its stretch (%.3f of %.3f)",
              len, rope_len);
    }
    CHECK(s0->rope == ROPE_ATTACHED, "it hangs on");

    // a yank away from the anchor stretches it — the rope's give, a part of its
    // length at most — and the stretch settles back to the hang
    s0->vel.y = -10.0f;
    for (int t = 0; t < 10; t++) tick_cmd(g, 0, 0, aim);
    float stretched = vec2_length(vec2_sub(s0->pos, s0->rope_tip));
    CHECK(stretched > rope_len && stretched <= rope_len * (1.0f + ROPE_GIVE) + 0.001f,
          "a yank stretches the rope, a part of its length at most (%.1f of %.0f)", stretched, rope_len);
    for (int t = 0; t < 60; t++) tick_cmd(g, 0, 0, aim);
    CHECK(vec2_length(vec2_sub(s0->pos, s0->rope_tip)) <= rope_len + 0.01f,
          "and the rope settles back to the hang (%.2f)", vec2_length(vec2_sub(s0->pos, s0->rope_tip)));

    // the down key feeds the rope out: the pace the climb reels it in, the one ramp
    // both ways — and only as far as the rope was when it grabbed: the climb's
    // slack back out, never more rope than the throw made
    float rope_len_grab = s0->rope_len;
    tick_cmd(g, 0, BUTTON_CROUCH, aim);
    CHECK(fabsf(s0->rope_len - rope_len_grab) < 0.01f,
          "at the grab length, the down key feeds nothing out — no more rope than the throw made");
    tick_cmd(g, 0, BUTTON_JUMP, aim);
    tick_cmd(g, 0, BUTTON_JUMP, aim);
    tick_cmd(g, 0, BUTTON_JUMP, aim);
    CHECK(s0->rope_len < rope_len_grab - 5.0f, "the up key reeled some in (%f)", rope_len_grab - s0->rope_len);
    float rope_len_reeled = s0->rope_len;
    tick_cmd(g, 0, BUTTON_CROUCH, aim);
    float paid1 = s0->rope_len - rope_len_reeled;
    CHECK(paid1 >= ROPE_SPEED * 0.10f - 0.01f && paid1 <= ROPE_SPEED * 0.20f + 0.01f,
          "a tick of the down key feeds a tenth of the rope's pace at least (%.2f)", paid1);
    for (int t = 0; t < 20; t++) tick_cmd(g, 0, BUTTON_CROUCH, aim);
    CHECK(fabsf(s0->rope_len - rope_len_grab) < 0.01f,
          "and the rope runs back out — to the grab length, never past it (%.2f)", s0->rope_len);
    CHECK(vec2_length(vec2_sub(s0->pos, s0->rope_tip)) <= rope_len_grab * (1.0f + ROPE_GIVE) + 0.01f,
          "the hang back down to it");

    // up or down with left and right: the diagonal — climbing or paying out with the
    // swing's sideways push, the roll on
    float rope_len_diag = s0->rope_len;
    Vec2 diag_at = s0->pos;
    for (int t = 0; t < 5; t++) tick_cmd(g, 0, BUTTON_JUMP | BUTTON_RIGHT, aim);
    CHECK(s0->rope_len < rope_len_diag - 5.0f, "up and right climb the rope (%f)", rope_len_diag - s0->rope_len);
    CHECK(fabsf(s0->pos.x - diag_at.x) > 0.5f, "with the swing's sideways push on the way (%.2f)",
          s0->pos.x - diag_at.x);
    CHECK(s0->body.id == ANIM_ROLL, "rolled up all the same");
    rope_len_diag = s0->rope_len;
    diag_at = s0->pos;
    for (int t = 0; t < 5; t++) tick_cmd(g, 0, BUTTON_CROUCH | BUTTON_LEFT, aim);
    CHECK(s0->rope_len > rope_len_diag + 5.0f, "down and left feed the rope out (%f)", s0->rope_len - rope_len_diag);
    CHECK(fabsf(s0->pos.x - diag_at.x) > 0.5f, "the sideways push still on the way down (%.2f)",
          s0->pos.x - diag_at.x);

    // left and right swing it: the soldier rolls up, the cannonball, and the swing
    // carries it off the rope's line, the rope holding
    float max_swing = 0.0f;
    for (int t = 0; t < 30; t++) {
        tick_cmd(g, 0, BUTTON_RIGHT, aim);
        max_swing = fmaxf(max_swing, fabsf(s0->pos.x - tip.x));
    }
    CHECK(s0->body.id == ANIM_ROLL, "swinging rolls the soldier up, the cannonball");
    CHECK(s0->body.speed > 1, "the roll plays slow, not its own hurried pace");
    CHECK(max_swing > 2.0f, "the swing carries it wide of the rope's line (%.1f)", max_swing);
    CHECK(vec2_length(vec2_sub(s0->pos, s0->rope_tip)) <= rope_len * (1.0f + ROPE_GIVE) + 0.001f,
          "and the rope holds through the swing");

    // the up key climbs it: a slow pull that builds, a tenth of the rope's pace at
    // first, gaining a hundredth of it every tick held, a fifth of it at most
    float rope_len_before = s0->rope_len;
    tick_cmd(g, 0, BUTTON_JUMP, aim);
    float reel1 = rope_len_before - s0->rope_len;
    CHECK(s0->rope == ROPE_ATTACHED && reel1 >= ROPE_SPEED * 0.10f - 0.01f && reel1 <= ROPE_SPEED * 0.20f + 0.01f,
          "a tick of the up key reels a tenth of the rope's pace at least (%.2f)", reel1);
    float rope_len_mid = s0->rope_len;
    tick_cmd(g, 0, BUTTON_JUMP, aim);
    float reel2 = rope_len_mid - s0->rope_len;
    CHECK(fabsf(reel2 - reel1 - ROPE_SPEED * 0.01f) < 0.01f, "and each tick held gains a hundredth of the pace (%.2f to %.2f)",
          reel1, reel2);

    // the climb's pull is the soldier's own: a cut mid-climb leaves them flying on
    // the pace they built
    for (int t = 0; t < 3; t++) tick_cmd(g, 0, BUTTON_JUMP, aim);
    tick_cmd(g, 0, BUTTON_JET, aim);
    CHECK(s0->rope == ROPE_NONE, "the rope key pressed mid-climb cuts it");
    CHECK(vec2_length(s0->vel) > 2.0f, "and the climb's pull is kept: its momentum (%.2f)", vec2_length(s0->vel));

    // thrown again, the climb goes on to the anchor, where the rope lets go and
    // stands its owner where it held
    tick_cmd(g, 0, 0, aim); // the cut's key released, the next press throws fresh
    throw_until_held(g, aim);
    CHECK(s0->rope == ROPE_ATTACHED, "thrown again mid-air, it holds again");
    Vec2 tip2 = s0->rope_tip;
    for (int t = 0; t < 200 && s0->rope != ROPE_NONE; t++) tick_cmd(g, 0, BUTTON_JUMP, aim);
    CHECK(s0->rope == ROPE_NONE, "climbed to the anchor, the rope lets go");
    CHECK(vec2_length(vec2_sub(s0->pos, tip2)) < 20.0f && fabsf(s0->vel.y) < 10.0f,
          "and stands its owner where it held");

    // thrown again from the anchor, a press on the hold cuts it, the swing's momentum
    // kept
    Vec2 below = vec2(tip.x, tip.y + 200.0f);
    tick_cmd(g, 0, BUTTON_JET, below);
    throw_until_held(g, below);
    CHECK(s0->rope == ROPE_ATTACHED, "thrown again, it holds again");

    tick_cmd(g, 0, 0, below);
    tick_cmd(g, 0, BUTTON_JET, below);
    CHECK(s0->rope == ROPE_NONE, "a press on the hold cuts the rope");
    scene_free(g);
}

// A spot with a clear circle around it, for the swing to loop free: nothing within
// `radius` of the anchor in any of eight ways. The map is the same every run, so
// the spot found is too.
static Vec2 rope_disc(const Context *ctx, float radius)
{
    RayFilter filter = {.player = true, .team = TEAM_ALPHA};
    for (float y = -900.0f; y <= 900.0f; y += 25.0f) {
        for (float x = -500.0f; x <= 500.0f; x += 25.0f) {
            Vec2 at = vec2(x, y);
            bool clear = true;
            for (int k = 0; k < 8 && clear; k++) {
                float a = (float)k * 3.14159265f / 4.0f;
                Vec2 out = vec2(x + cosf(a) * radius, y + sinf(a) * radius);
                if (map_ray_cast(ctx->map, at, out, radius + 5.0f, filter, NULL)) clear = false;
            }
            if (clear) return at;
        }
    }
    return vec2(0.0f, -300.0f);
}

// The swing's momentum is cut: held in one direction however long, the gathered
// speed stops at ROPE_SWING_MAX — what the collision's push-out is built to resolve,
// where the swing once ran to ROPE_SPEED and its owner through polygons — and the
// push against the momentum still works: the turn is free, only slower. The world's
// gravity is off and the anchor sits in a clear circle; the down key holds the rope
// paid out at its length, so the hang turns in place, and the push and the momentum
// share one line: the cut reads exactly.
static void swing_momentum_cut(void)
{
    Game *g = rope_scene();
    g->world.gravity = 0.0f;
    Soldier *s0 = &g->world.soldiers[0];

    // The cut itself, hung at the swing's diagonal where the momentum runs faster
    // than any axis alone: the push keeps adding past the cut without it — the cut
    // holds it, whatever the direction.
    Vec2 sky = rope_disc(&g->ctx, 310.0f);
    Vec2 aim = vec2(sky.x, sky.y + 600.0f);
    place(s0, vec2(sky.x + 212.0f, sky.y + 212.0f));
    s0->rope = ROPE_ATTACHED;
    s0->rope_tip = sky;
    s0->rope_len = 300.0f;
    s0->rope_grab = 300.0f;
    s0->rope_wraps_count = 0;
    s0->rope_climb = 0.0f;
    s0->vel = vec2(11.0f, -11.0f);
    for (int t = 0; t < 20; t++) tick_cmd(g, 0, BUTTON_RIGHT | BUTTON_CROUCH, aim);
    Vec2 dd = vec2_sub(s0->pos, s0->rope_tip);
    Vec2 nn = vec2_div(dd, vec2_length(dd));
    float tang_vel = vec2_dot(s0->vel, vec2(nn.y, -nn.x));
    CHECK(s0->rope == ROPE_ATTACHED && tang_vel <= 12.5f && tang_vel > 3.0f,
          "the push never gathers momentum past the cut (%.2f)", tang_vel);

    // The speed cut: a soldier flying faster than the cut is held to it — the part
    // of the swing that ran its owner through polygons.
    place(s0, vec2(sky.x, sky.y + 300.0f));
    s0->rope = ROPE_ATTACHED;
    s0->rope_tip = sky;
    s0->rope_len = 300.0f;
    s0->rope_grab = 300.0f;
    s0->rope_wraps_count = 0;
    s0->rope_climb = 0.0f;
    s0->vel = vec2(20.0f, 0.0f);
    tick_cmd(g, 0, 0, aim);
    CHECK(vec2_length(s0->vel) <= ROPE_SWING_MAX * 1.4142f + 0.5f,
          "a soldier flying past the cut is held to it (%.2f)", vec2_length(s0->vel));

    // The turn: the push against the momentum still works — swung the other way
    // from the cut, the momentum gathers again (a cut that froze the swing instead
    // would leave it at the cut).
    place(s0, vec2(sky.x, sky.y + 300.0f));
    s0->rope = ROPE_ATTACHED;
    s0->rope_tip = sky;
    s0->rope_len = 300.0f;
    s0->rope_grab = 300.0f;
    s0->rope_wraps_count = 0;
    s0->rope_climb = 0.0f;
    s0->vel = vec2(ROPE_SWING_MAX, 0.0f);
    for (int t = 0; t < 50; t++) tick_cmd(g, 0, BUTTON_LEFT | BUTTON_CROUCH, aim);
    dd = vec2_sub(s0->pos, s0->rope_tip);
    nn = vec2_div(dd, vec2_length(dd));
    tang_vel = vec2_dot(s0->vel, vec2(nn.y, -nn.x));
    CHECK(tang_vel < 5.0f, "the push against the momentum turns the swing (%.2f)", tang_vel);
    scene_free(g);
}

// A rope hung over a corner wraps it: the line from the anchor to its owner
// crossing a poly pins both its crossings, the wound part held around the solid
// while the hang keeps its full length — and swung back clear of the corner, the
// corner lets the rope go, plain again.
static void wrap_and_unwrap(void)
{
    Game *g = rope_scene();
    Soldier *s0 = &g->world.soldiers[0];

    // the geometry the map gives: an anchor in open air, the hang below-right of
    // it, its line crossing one poly 48 along
    Vec2 T = vec2(485.0f, -315.0f);
    float rope_len = 120.0f;
    Vec2 S = vec2(369.1f, -283.9f);
    Vec2 aim = vec2(S.x, S.y + 50.0f);

    // the rope attached at T with the soldier hung at its length on the far side
    // of the corner: one tick, and the rope is caught around it
    place(s0, S);
    s0->rope = ROPE_ATTACHED;
    s0->rope_tip = T;
    s0->rope_len = rope_len;
    s0->rope_wraps_count = 0;
    s0->rope_climb = 0.0f;
    tick_cmd(g, 0, 0, aim);
    CHECK(s0->rope == ROPE_ATTACHED && s0->rope_wraps_count == 2,
          "the rope's line crossing a poly wraps it, pinned at both its crossings");
    CHECK(vec2_length(vec2_sub(s0->pos, S)) < 0.5f, "and the hang keeps its full length around the corner");
    Vec2 w0 = s0->rope_wraps[0];
    Vec2 w1 = s0->rope_wraps[1];
    CHECK(vec2_length(vec2_sub(w0, T)) > 40.0f && vec2_length(vec2_sub(w0, T)) < 55.0f,
          "the first pin sits just this side of the corner (%f)", vec2_length(vec2_sub(w0, T)));
    CHECK(vec2_length(vec2_sub(w1, w0)) > 20.0f && vec2_length(vec2_sub(w1, w0)) < 30.0f,
          "and the second past it — the wound part held around the solid (%f)", vec2_length(vec2_sub(w1, w0)));
    RayFilter filter = {.player = true, .team = s0->team};
    float dist = 0.0f;
    CHECK(!map_ray_cast(g->ctx.map, w1, s0->pos, vec2_length(vec2_sub(s0->pos, w1)), filter, &dist),
          "the stretch from the corner to the soldier stays clear of the map");

    // held, the wrap holds: the wound part and the hang keep the rope's full
    // length as the soldier swings from the corner
    float wrapped = vec2_length(vec2_sub(w0, T)) + vec2_length(vec2_sub(w1, w0));
    for (int t = 0; t < 30; t++) tick_cmd(g, 0, 0, aim);
    CHECK(s0->rope == ROPE_ATTACHED && s0->rope_wraps_count == 2, "it hangs on, wound around the corner");
    CHECK(vec2_length(vec2_sub(s0->pos, w1)) + wrapped <= rope_len * (1.0f + ROPE_GIVE) + 0.01f &&
              vec2_length(vec2_sub(s0->pos, w1)) + wrapped > rope_len - 1.0f,
          "the wound part and the hang keep the rope's full length as it swings (%.1f)",
          vec2_length(vec2_sub(s0->pos, w1)) + wrapped);

    // swung back to the anchor's side, clear of the corner, the corner lets go,
    // and the plain hang holds its length again
    place(s0, vec2(500.0f, -310.0f));
    tick_cmd(g, 0, 0, aim);
    tick_cmd(g, 0, 0, aim);
    tick_cmd(g, 0, 0, aim);
    CHECK(s0->rope == ROPE_ATTACHED && s0->rope_wraps_count == 0,
          "swung back clear of the corner, the corner lets the rope go");
    for (int t = 0; t < 5; t++) {
        tick_cmd(g, 0, 0, aim);
        CHECK(s0->rope == ROPE_ATTACHED &&
                  vec2_length(vec2_sub(s0->pos, T)) <= rope_len * (1.0f + ROPE_GIVE) + 1.0f,
              "the plain hang keeps its length (%.1f)", vec2_length(vec2_sub(s0->pos, T)));
    }
    scene_free(g);
}

// The cutters cut a rope their bullet crosses; other bullets pass it by. The bullet
// is fired across the long rope from open air, so nothing stops it first.
static void cut_and_pass_through(void)
{
    const struct {
        WeaponId weapon;
        bool cuts;
    } cases[] = {
        {WEAPON_KNIFE, true},        {WEAPON_THROWN_KNIFE, true}, {WEAPON_LAW, true},
        {WEAPON_M79, true},          {WEAPON_BARRETT, true},
        {WEAPON_AK74, false},        {WEAPON_M249, false},
    };
    for (int c = 0; c < (int)(sizeof cases / sizeof cases[0]); c++) {
        Game *g = rope_scene();
        Soldier *s0 = &g->world.soldiers[0];
        Vec2 sky = rope_air(&g->ctx);
        Vec2 aim = vec2(sky.x, sky.y + 200.0f);

        Buttons buttons = 0;
        Vec2 stab_at = vec2(sky.x, s0->pos.y + 40.0f); // the rope's line at the stabbing height
        if (cases[c].weapon == WEAPON_KNIFE) {
            // The stab is a single tick along the punch's fixed line, so the throw's
            // swing moves the rope faster than the stab can follow: the knife gets a
            // rope hung to rest — set straight down from its tip, it cannot swing —
            // and the stabber, once the first stab has shown where the sweep goes,
            // is stood so the next one crosses it.
            place(s0, vec2(sky.x, sky.y + 300.0f));
            s0->rope = ROPE_ATTACHED;
            s0->rope_tip = sky;
            s0->rope_len = 300.0f;
            s0->rope_wraps_count = 0;
            s0->rope_climb = 0.0f;
            tick_cmd(g, 0, 0, aim);
            CHECK(s0->rope == ROPE_ATTACHED && s0->rope_wraps_count == 0,
                  "weapon %d: the rope hangs to rest for the stab", cases[c].weapon);
            s0->body.speed = 1000; // the hang's pose held: the rope's end stays put

            Soldier *stabber = &g->world.soldiers[1];
            place(stabber, vec2(sky.x - 60.0f, sky.y + 40.0f));
            stabber->weapon = weapon_state(&g->ctx, WEAPON_KNIFE);
            stabber->cease_fire_counter = -1; // no spawn protection here
            buttons = BUTTON_FIRE;

            Vec2 start_off = {0}, end_off = {0};
            bool measured = false, cut = false;
            for (int t = 0; t < 30 && !cut; t++) {
                tick_cmd(g, 1, buttons, stab_at);
                cut = cut_event(g);
                if (cut) break;
                if (stabber->body.id == ANIM_PUNCH && stabber->body.frame == 13 && !measured) {
                    // the stab that just went out, on the pose the collision saw: the
                    // sweep's start from the body now, its end from the bullet's spawn
                    Pose p13 = soldier_pose(g->ctx.anims, stabber, stabber->pos);
                    Vec2 hd = hands_aim_direction(&p13);
                    start_off = vec2_sub(vec2_add(p13.p[14], vec2_scale(hd, 4.0f)), stabber->pos);
                    Anim body = stabber->body;
                    stabber->body.frame = 11;
                    Pose p11 = soldier_pose(g->ctx.anims, stabber, stabber->pos);
                    end_off = vec2_sub(vec2_add(p11.p[15], vec2(2.1f * (float)stabber->direction, 3.0f)), stabber->pos);
                    stabber->body = body;
                    measured = true;
                }
                if (measured && stabber->body.id == ANIM_PUNCH && stabber->body.frame == 11) {
                    // the stab goes out this coming tick: stand its sweep over the rope
                    Vec2 hand = soldier_pose(g->ctx.anims, s0, s0->pos).p[14];
                    float sweep_y = stabber->pos.y + (start_off.y + end_off.y) / 2.0f;
                    float tt = (sweep_y - hand.y) / (s0->rope_tip.y - hand.y);
                    float rope_x = hand.x + (s0->rope_tip.x - hand.x) * tt;
                    stabber->pos.x = rope_x - (start_off.x + end_off.x) / 2.0f;
                    stabber->old_pos.x = stabber->pos.x;
                    stabber->vel.x = 0.0f;
                }
            }
            CHECK(cut && s0->rope == ROPE_NONE, "weapon %d cuts the rope it crosses", cases[c].weapon);
            scene_free(g);
            continue;
        }

        place(s0, sky);
        throw_until_held(g, aim);
        CHECK(s0->rope == ROPE_ATTACHED && s0->rope_len > 350.0f, "weapon %d: the rope hangs for the shot", cases[c].weapon);

        float speed = cases[c].weapon == WEAPON_M79 ? 10.7f : 23.0f;
        Vec2 fire = vec2(sky.x - 60.0f, s0->pos.y + 40.0f);
        bullet_spawn(&g->ctx, &g->world, fire, vec2(speed, 0.0f), cases[c].weapon, 1, 1.0f, &g->events);

        bool cut = false;
        for (int t = 0; t < 30 && !cut; t++) {
            tick_cmd(g, 1, buttons, stab_at);
            cut = cut_event(g);
        }
        if (cases[c].cuts) {
            CHECK(cut && s0->rope == ROPE_NONE, "weapon %d cuts the rope it crosses", cases[c].weapon);
        } else {
            CHECK(!cut && s0->rope == ROPE_ATTACHED, "weapon %d passes the rope by", cases[c].weapon);
        }
        scene_free(g);
    }
}

// The jets still fly: the rope did not take their key away.
static void jets_regression(void)
{
    Game *g = scene("Arena", 60.0f, WEAPON_AK74, WEAPON_AK74);
    settle(g);
    Vec2 at = g->world.soldiers[0].pos;
    for (int t = 0; t < 30; t++) tick_cmd(g, 0, BUTTON_JET, vec2(at.x, at.y - 200.0f));
    CHECK(g->world.soldiers[0].pos.y < at.y - 20.0f, "the jets still lift their wearer");
    CHECK(g->world.soldiers[0].rope == ROPE_NONE, "and the jets leave the rope alone");
    scene_free(g);
}

// The rope switched off (sv_rope 0): the rope key throws nothing, held however long.
static void rope_off(void)
{
    Game *g = rope_scene();
    settle(g);
    g->match.settings.rope = false; // as a game hosted with sv_rope 0
    Soldier *s0 = &g->world.soldiers[0];
    Vec2 aim = vec2(s0->pos.x, s0->pos.y + 200.0f);
    for (int t = 0; t < 30; t++) {
        tick_cmd(g, 0, BUTTON_JET, aim);
        CHECK(s0->rope == ROPE_NONE, "with the rope off, the rope key throws nothing");
    }
    scene_free(g);
}

void rope_tests(void)
{
    throw_retract_climb_cut();
    long_hang_swing_climb_cut();
    swing_momentum_cut();
    wrap_and_unwrap();
    cut_and_pass_through();
    jets_regression();
    rope_off();
}
