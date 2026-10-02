// The grappling hook, Teeworlds' (CCharacterCore::Tick) brought to this world: fired at
// the aim it flies straight at its pace and gives up at its length, hangs three ticks
// and is put away, and fires again only once let go; it holds the first poly it meets;
// holding, it pulls its owner toward it, three times harder up than down, harder the
// way they move, only up to its pace and never nearer than its near distance; let go,
// it is put away. Through the game's tick: it takes the jets' place and their fuel is
// left alone, and in a game without it the key is the jets again.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "test.h"

#define NEAR(a, b) (fabsf((a) - (b)) < 0.001f)

// The hook's game: the map, soldier 0 wearing the hook, in a game that allows it
// (sv_hook 1: the hook is off by default).
static Game *hook_scene(void)
{
    Game *g = scene("Arena", 60.0f, WEAPON_AK74, WEAPON_AK74);
    g->match.settings.hook = true;
    g->world.rules.hook = true;
    g->world.soldiers[0].gear = GEAR_HOOK;
    return g;
}

// A spot with nothing a player stands on within `reach` above, left and right, and
// the ground `below_min` to `below_max` beneath it. The map is the same every run, so
// the spot found is too.
static bool find_spot(const Context *ctx, float reach, float below_min, float below_max, Vec2 *out, Vec2 *ground)
{
    RayFilter filter = {.player = true, .team = TEAM_ALPHA};
    for (float y = -900.0f; y <= 900.0f; y += 10.0f) {
        for (float x = -600.0f; x <= 600.0f; x += 10.0f) {
            Vec2 at = vec2(x, y);
            if (map_ray_cast(ctx->map, at, vec2(x, y - reach), reach + 1.0f, filter, NULL)) continue;
            if (map_ray_cast(ctx->map, at, vec2(x - reach, y), reach + 1.0f, filter, NULL)) continue;
            if (map_ray_cast(ctx->map, at, vec2(x + reach, y), reach + 1.0f, filter, NULL)) continue;
            Vec2 hit;
            if (!map_ray_cast_hit(ctx->map, at, vec2(x, y + below_max), below_max + 1.0f, filter, NULL, &hit, NULL)) continue;
            if (hit.y - y < below_min) continue;
            *out = at;
            *ground = hit;
            return true;
        }
    }
    return false;
}

// One step of the hook alone, soldier 0 holding `buttons` and aiming at `aim`.
static void step(Game *g, Buttons buttons, Vec2 aim)
{
    Soldier *s = &g->world.soldiers[0];
    s->controls = buttons;
    s->aim = aim;
    hook_control(&g->ctx, &g->world, 0);
}

static void flies_and_gives_up(void)
{
    Game *g = hook_scene();
    Soldier *s = &g->world.soldiers[0];
    const HookTuning *t = &g->world.rules.hook_tuning;
    Vec2 at, ground;
    if (!find_spot(&g->ctx, t->length + 20.0f, 50.0f, 400.0f, &at, &ground)) {
        CHECK(false, "a spot in open air to fire the hook from");
        scene_free(g);
        return;
    }
    place(s, at);
    at = s->pos; // where the soldier stands placed: the hook is fired from here
    Vec2 right = vec2(at.x + 500.0f, at.y);

    step(g, BUTTON_JET, right);
    CHECK(s->hook == HOOK_FLYING && NEAR(s->hook_pos.x, at.x + HOOK_START + t->fire_speed) && NEAR(s->hook_pos.y, at.y),
          "pressed, it leaves the hand at the aim and flies a tick's pace at once (%.2f)", s->hook_pos.x - at.x);
    int ticks = 1;
    while (s->hook == HOOK_FLYING && ticks < 100) {
        step(g, BUTTON_JET, right);
        ticks++;
    }
    // past its length it gives up, where it last flew to: a hook no longer flying is not
    // moved on, as Teeworlds' is not (gamecore.cpp)
    int expected = (int)ceilf((t->length - HOOK_START) / t->fire_speed);
    float last = HOOK_START + t->fire_speed * (float)(expected - 1);
    CHECK(s->hook == HOOK_RETRACT_1 && NEAR(s->hook_pos.x - at.x, last) && ticks == expected,
          "past its length it gives up after %d ticks, where it last flew to (%d, %.2f out of %.2f)", expected, ticks,
          s->hook_pos.x - at.x, last);
    step(g, BUTTON_JET, right);
    step(g, BUTTON_JET, right);
    CHECK(s->hook == HOOK_RETRACT_3 && NEAR(s->hook_pos.x - at.x, last), "it hangs there three ticks");
    step(g, BUTTON_JET, right);
    for (int i = 0; i < 20; i++) step(g, BUTTON_JET, right);
    CHECK(s->hook == HOOK_RETRACTED, "and is put away, firing nothing however long the key is held");
    step(g, 0, right);
    CHECK(s->hook == HOOK_IDLE && NEAR(s->hook_pos.x, s->pos.x), "let go, it is idle in the hand");
    step(g, BUTTON_JET, right);
    CHECK(s->hook == HOOK_FLYING, "and pressed again, it fires again");
    scene_free(g);
}

static void grabs_the_ground(void)
{
    Game *g = hook_scene();
    Soldier *s = &g->world.soldiers[0];
    const HookTuning *t = &g->world.rules.hook_tuning;
    Vec2 at, ground;
    if (!find_spot(&g->ctx, 60.0f, 80.0f, t->length - 20.0f, &at, &ground)) {
        CHECK(false, "a spot with the ground within the hook's reach below");
        scene_free(g);
        return;
    }
    place(s, at);
    at = s->pos;
    Vec2 down = vec2(at.x, at.y + 500.0f);
    for (int i = 0; i < 20 && s->hook != HOOK_GRABBED; i++) step(g, BUTTON_JET, down);
    CHECK(s->hook == HOOK_GRABBED && vec2_length(vec2_sub(s->hook_pos, ground)) < 1.0f,
          "fired at the ground, it holds where the ground is ((%.1f,%.1f), the ground at (%.1f,%.1f))", s->hook_pos.x,
          s->hook_pos.y, ground.x, ground.y);
    step(g, 0, down);
    CHECK(s->hook == HOOK_IDLE, "let go, it is put away");
    scene_free(g);
}

// The pull, from rest with the head set where each case wants it.
static Vec2 pull(Game *g, Vec2 head_off, Vec2 vel, Buttons buttons)
{
    Soldier *s = &g->world.soldiers[0];
    s->hook = HOOK_GRABBED;
    s->hook_pos = vec2_add(s->pos, head_off);
    s->vel = vel;
    step(g, BUTTON_JET | buttons, vec2_add(s->pos, head_off));
    return s->vel;
}

static void pulls_as_teeworlds(void)
{
    Game *g = hook_scene();
    Soldier *s = &g->world.soldiers[0];
    place(s, vec2(0.0f, -2000.0f)); // far above everything: nothing for the head to hit
    const HookTuning *t = &g->world.rules.hook_tuning;
    float a = t->drag_accel;
    Vec2 v = pull(g, vec2(0, -100), vec2(0, 0), 0);
    CHECK(NEAR(v.x, 0) && NEAR(v.y, -a), "held above, it pulls up at its whole pull (%.4f)", v.y);
    v = pull(g, vec2(0, 100), vec2(0, 0), 0);
    CHECK(NEAR(v.y, a * 0.3f), "held below, at three tenths of it (%.4f)", v.y);
    v = pull(g, vec2(100, 0), vec2(0, 0), 0);
    CHECK(NEAR(v.x, a * 0.75f), "held to the side, standing still, at three quarters (%.4f)", v.x);
    v = pull(g, vec2(100, 0), vec2(0, 0), BUTTON_RIGHT);
    CHECK(NEAR(v.x, a * 0.95f), "and moving its way, at nineteen twentieths (%.4f)", v.x);
    v = pull(g, vec2(100, 0), vec2(0, 0), BUTTON_LEFT);
    CHECK(NEAR(v.x, a * 0.75f), "but moving away, three quarters again (%.4f)", v.x);
    v = pull(g, vec2(100, 0), vec2(t->drag_speed, 0), 0);
    CHECK(NEAR(v.x, t->drag_speed), "it adds no speed past its pace (%.4f)", v.x);
    v = pull(g, vec2(100, 0), vec2(-10.0f, 0), 0);
    CHECK(NEAR(v.x, -10.0f + a * 0.75f), "but slows one faster going the other way (%.4f)", v.x);
    v = pull(g, vec2(HOOK_NEAR - 1.0f, 0), vec2(0, 0), 0);
    CHECK(NEAR(v.x, 0) && NEAR(v.y, 0), "and nearer than its near distance, it pulls no more");
    scene_free(g);
}

static void in_the_game(void)
{
    // the hook takes the jets' place: their fuel stays, the soldier walks on it
    Game *g = hook_scene();
    settle(g);
    Soldier *s = &g->world.soldiers[0];
    int32_t fuel = s->jets;
    Vec2 at = s->pos;
    Command cmds[MAX_PLAYERS] = {0};
    cmds[0] = (Command){.seq = 1, .buttons = BUTTON_JET, .aim = vec2(at.x, at.y - 300.0f)};
    cmds[1] = (Command){.seq = 1, .aim = g->world.soldiers[1].pos};
    game_tick(g, cmds);
    CHECK(s->hook != HOOK_IDLE && s->jets == fuel, "on the jets' key the hook fires, and the jets' fuel is left alone");
    scene_free(g);

    // in a game without it (sv_hook 0), the key is the jets again
    g = hook_scene();
    settle(g);
    g->match.settings.hook = false;
    s = &g->world.soldiers[0];
    at = s->pos;
    for (int i = 0; i < 30; i++) {
        cmds[0] = (Command){.seq = (uint32_t)(i + 2), .buttons = BUTTON_JET, .aim = vec2(at.x, at.y - 300.0f)};
        cmds[1] = (Command){.seq = (uint32_t)(i + 2), .aim = g->world.soldiers[1].pos};
        game_tick(g, cmds);
    }
    CHECK(s->hook == HOOK_IDLE && s->pos.y < at.y - 20.0f, "with the hook off, the key lifts on the jets and fires nothing");
    scene_free(g);

    // and the jets' own wearer still flies
    g = scene("Arena", 60.0f, WEAPON_AK74, WEAPON_AK74);
    settle(g);
    s = &g->world.soldiers[0];
    at = s->pos;
    for (int i = 0; i < 30; i++) {
        cmds[0] = (Command){.seq = (uint32_t)(i + 2), .buttons = BUTTON_JET, .aim = vec2(at.x, at.y - 300.0f)};
        cmds[1] = (Command){.seq = (uint32_t)(i + 2), .aim = g->world.soldiers[1].pos};
        game_tick(g, cmds);
    }
    CHECK(s->pos.y < at.y - 20.0f && s->hook == HOOK_IDLE, "the jets still lift their wearer, and leave the hook alone");
    scene_free(g);
}

void hook_tests(void)
{
    flies_and_gives_up();
    grabs_the_ground();
    pulls_as_teeworlds();
    in_the_game();
}
