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
    Tally t = run(g, 300, hold_throw);
    CHECK(t.spawned[WEAPON_FRAG] == 1, "a grenade is thrown");
    CHECK(t.explosions >= 1, "and goes off");
    CHECK(t.damage > 0, "and hurts");
    CHECK(g->world.soldiers[0].grenades == 0, "and is counted off");
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

void combat_tests(void)
{
    rifle_kills();
    shotgun_and_eagles();
    grenade();
    knife();
    suicide();
    m79();
    determinism();
}
