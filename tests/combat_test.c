// The weapons, the bullets and the explosions, on Arena.

#include "test.h"

static Buttons hold_throw(int tick) { return tick < 30 ? BUTTON_THROW : 0; }
static Buttons hold_drop(int tick) { return tick < 30 ? BUTTON_DROP : 0; }
static Buttons tap_drop(int tick) { return tick < 3 ? BUTTON_DROP : 0; }
static Buttons tap_suicide(int tick) { return tick == 0 ? BUTTON_SUICIDE : 0; }

static void rifle_kills(void)
{
    Game *g = scene("Arena", 120, WEAPON_AK74, WEAPON_AK74);
    settle(g);
    CHECK(g->world.soldiers[0].on_ground && g->world.soldiers[1].on_ground, "both soldiers stand on the ground");
    Tally t = run(g, 240, press_fire);
    CHECK(t.spawned[WEAPON_AK74] == 24, "the AK fires every 10 ticks (%d bullets in 240)", t.spawned[WEAPON_AK74]);
    CHECK(t.hits > 0 && t.damage > 0, "its bullets hit");
    CHECK(t.kills == 1 && g->world.soldiers[1].dead, "and kill");
    scene_free(g);
}

// A soldier with no team, a deathmatch player, is hit as any other: no team is not a team.
static void no_team_is_hit(void)
{
    Game *g = scene("Arena", 120, WEAPON_AK74, WEAPON_AK74);
    g->world.soldiers[0].team = TEAM_NONE;
    g->world.soldiers[1].team = TEAM_NONE;
    settle(g);
    Tally t = run(g, 240, press_fire);
    CHECK(t.hits > 0 && t.damage > 0 && g->world.soldiers[1].dead, "a soldier with no team is hit and killed like any other (%d hits)", t.hits);
    scene_free(g);
}

static void shotgun_and_eagles(void)
{
    Game *g = scene("Arena", 120, WEAPON_SPAS, WEAPON_AK74);
    settle(g);
    Tally t = run(g, 3, press_fire);
    CHECK(t.spawned[WEAPON_SPAS] == 6, "the shotgun fires six pellets (%d)", t.spawned[WEAPON_SPAS]);
    scene_free(g);

    g = scene("Arena", 120, WEAPON_EAGLE, WEAPON_AK74);
    settle(g);
    t = run(g, 3, press_fire);
    CHECK(t.spawned[WEAPON_EAGLE] == 2, "the Eagles fire two (%d)", t.spawned[WEAPON_EAGLE]);
    scene_free(g);
}

static void grenade(void)
{
    Game *g = scene("Arena", 150, WEAPON_AK74, WEAPON_AK74);
    settle(g);
    g->world.soldiers[0].grenades = 3;
    Tally t = run(g, 300, hold_throw);
    CHECK(t.spawned[WEAPON_FRAG] == 1, "one grenade is thrown per release, of three (%d)", t.spawned[WEAPON_FRAG]);
    CHECK(t.explosions >= 1, "and goes off");
    CHECK(t.damage > 0, "and hurts");
    CHECK(g->world.soldiers[0].grenades == 2, "and is counted off (%d left)", g->world.soldiers[0].grenades);
    scene_free(g);
}

static void knife(void)
{
    Game *g = scene("Arena", 150, WEAPON_KNIFE, WEAPON_AK74);
    settle(g);
    Tally t = run(g, 60, hold_drop);
    CHECK(t.spawned[WEAPON_THROWN_KNIFE] == 1, "the knife is thrown spinning, not dropped (%d)", t.spawned[WEAPON_THROWN_KNIFE]);
    CHECK(g->world.soldiers[0].weapon.id == WEAPON_NONE, "and leaves the hands empty");
    scene_free(g);

    g = scene("Arena", 150, WEAPON_BOW, WEAPON_AK74);
    settle(g);
    run(g, 60, tap_drop);
    CHECK(g->world.soldiers[0].weapon.id == WEAPON_BOW, "the bow cannot be thrown away");
    scene_free(g);
}

static void suicide(void)
{
    Game *g = scene("Arena", 150, WEAPON_AK74, WEAPON_AK74);
    settle(g);
    Tally t = run(g, 2, tap_suicide);
    CHECK(t.kills == 1 && g->world.soldiers[0].dead, "suicide kills");
    scene_free(g);
}

static void m79(void)
{
    Game *g = scene("Arena", 40, WEAPON_M79, WEAPON_AK74);
    settle(g);
    Tally t = run(g, 400, press_fire);
    CHECK(t.spawned[WEAPON_M79] >= 1 && t.explosions >= 1, "the M79 reloads, fires and goes off");
    CHECK(t.kills >= 1, "and a direct hit kills");
    scene_free(g);
}

// The sniper view: crouched with the Barrett, aiming far, the camera's lead shortens toward the
// crouch's distance, a step a tick; a shot snaps it back.
static void sniper_view(void)
{
    Game *g = scene("Arena", 150, WEAPON_BARRETT, WEAPON_AK74);
    settle(g);
    Soldier *s = &g->world.soldiers[0];
    for (int t = 0; t < 120; t++) {
        Command cmds[MAX_PLAYERS] = {0};
        cmds[0] = (Command){.seq = g->world.tick + 1, .buttons = BUTTON_CROUCH, .aim = vec2(s->pos.x + 1000.0f, s->pos.y)};
        cmds[1] = (Command){.seq = g->world.tick + 1};
        game_tick(g, cmds);
    }
    CHECK(s->body.id == ANIM_AIM && s->aim_dist < DEFAULT_AIM_DIST - 0.5f && s->aim_dist >= CROUCH_AIM_DIST - 0.01f,
          "crouched with the Barrett, aiming far, the sniper view draws the camera out (aim distance %.2f, body %d)", s->aim_dist, s->body.id);
    for (int t = 0; t < 40; t++) { // past the Barrett's wind-up
        Command cmds[MAX_PLAYERS] = {0};
        cmds[0] = (Command){.seq = g->world.tick + 1, .buttons = BUTTON_CROUCH | BUTTON_FIRE, .aim = vec2(s->pos.x + 1000.0f, s->pos.y)};
        cmds[1] = (Command){.seq = g->world.tick + 1};
        game_tick(g, cmds);
    }
    CHECK(s->aim_dist == DEFAULT_AIM_DIST, "and a shot snaps it back (%.2f)", s->aim_dist);
    scene_free(g);
}

static void determinism(void)
{
    Game *a = scene("Arena", 120, WEAPON_MINIGUN, WEAPON_SPAS), *b = scene("Arena", 120, WEAPON_MINIGUN, WEAPON_SPAS);
    settle(a);
    settle(b);
    run(a, 300, press_fire);
    run(b, 300, press_fire);
    CHECK(same_world(&a->world, &b->world), "the same world from the same start ends the same");
    scene_free(a);
    scene_free(b);
}

// The knockback is felt where the bullet is flown, authority or not: a client's world
// shoves the soldier a bullet meets, as the original writes its NextPush wherever the
// bullet is simulated, while the wound stays the server's.
static void knockback_without_authority(void)
{
    Game *g = scene("Arena", 120, WEAPON_AK74, WEAPON_AK74);
    settle(g);
    g->world.authority = false;
    Soldier *victim = &g->world.soldiers[1];
    float health = victim->health;
    float pushed = 0.0f;
    int hits = 0;
    for (int i = 0; i < 120; i++) {
        hits += run(g, 1, press_fire).hits;
        if (victim->next_push.x > pushed) pushed = victim->next_push.x;
    }
    CHECK(hits > 0, "the AK's bullets meet the other soldier (%d hits)", hits);
    CHECK(pushed > 0.0f, "and shove it along their way on a world without authority (%.3f)", pushed);
    CHECK(victim->health == health, "without wounding it there: the wound is the server's");
    scene_free(g);
}

// The M79 boost: a grenade at one's own feet throws one upward, on a client's world as
// on the server's, since a blast's shove is felt wherever it is flown.
static void m79_boost(void)
{
    Game *g = scene("Arena", 120, WEAPON_M79, WEAPON_AK74);
    settle(g);
    g->world.authority = false;
    Soldier *s = &g->world.soldiers[0];
    float stood = s->pos.y, highest = s->pos.y, lift = 0.0f;
    Command cmds[MAX_PLAYERS] = {0};
    for (int i = 0; i < 400; i++) { // the trigger held: the M79 reloads first, then fires at the feet
        cmds[0] = (Command){.seq = g->world.tick + 1, .buttons = BUTTON_FIRE, .aim = vec2(s->pos.x + 6.0f, s->pos.y + 12.0f)};
        cmds[1] = (Command){.seq = g->world.tick + 1, .aim = s->pos};
        game_tick(g, cmds);
        if (-s->next_push.y > lift) lift = -s->next_push.y;
        if (s->pos.y < highest) highest = s->pos.y;
    }
    CHECK(lift > 0.0f, "the blast at the feet shoves the shooter upward (%.3f)", lift);
    CHECK(highest < stood - 5.0f, "and lifts it off the ground (%.1f up)", stood - highest);
    scene_free(g);
}

void combat_tests(void)
{
    rifle_kills();
    no_team_is_hit();
    shotgun_and_eagles();
    grenade();
    knife();
    suicide();
    m79();
    knockback_without_authority();
    m79_boost();
    sniper_view();
    determinism();
}
