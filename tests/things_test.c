// The things: flags, kits, dropped guns, parachutes and the stationary gun, on ctf_Ash.

#include "test.h"

static Buttons tap_drop(int tick) { return tick < 3 ? BUTTON_DROP : 0; }
static Buttons hold_drop(int tick) { return tick < 30 ? BUTTON_DROP : 0; }
static Buttons tap_jump(int tick) { return tick < 3 ? BUTTON_JUMP : 0; }

static void round_start(void)
{
    Game *g = scene("ctf_Ash", 120, WEAPON_AK74, WEAPON_AK74);
    int alpha = find_thing(g, THING_ALPHA_FLAG), bravo = find_thing(g, THING_BRAVO_FLAG);
    CHECK(alpha >= 0 && bravo >= 0, "both flags are placed");
    int kits = 0;
    for (int i = 0; i < MAX_THINGS; i++) {
        ThingStyle style = g->world.things[i].style;
        kits += style == THING_MEDICAL_KIT || style == THING_GRENADE_KIT;
    }
    CHECK(kits == g->ctx.map->medikits + g->ctx.map->grenade_packs, "and the map's kits (%d)", kits);
    settle(g);
    const Thing *flag = &g->world.things[alpha];
    CHECK(flag->is_static && flag->in_base, "a flag settles in its base");
    scene_free(g);
}

static void dropped_gun(void)
{
    Game *g = scene("ctf_Ash", 200, WEAPON_AK74, WEAPON_AK74);
    settle(g);
    Soldier *s = &g->world.soldiers[0];
    run(g, 30, tap_drop);
    int gun = find_thing(g, THING_WEAPON);
    CHECK(gun >= 0 && s->weapon.id == WEAPON_NONE, "a gun thrown away lies on the ground");
    if (gun >= 0) {
        CHECK(g->world.things[gun].ammo == g->ctx.weapons.info[WEAPON_AK74].stats.ammo, "with its ammo");
        run(g, 120, press_nothing);
        CHECK(g->world.things[gun].is_static, "and settles");
        place(s, g->world.things[gun].pos[0]);
        run(g, 3, press_nothing);
        CHECK(s->weapon.id == WEAPON_AK74, "and is taken back by empty hands");
    }
    scene_free(g);
}

static void medikit(void)
{
    Game *g = scene("ctf_Ash", 200, WEAPON_AK74, WEAPON_AK74);
    settle(g);
    int kit = find_thing(g, THING_MEDICAL_KIT);
    Soldier *s = &g->world.soldiers[0];
    s->health = 50;
    Vec2 was = g->world.things[kit].pos[0];
    place(s, was);
    run(g, 2, press_nothing);
    CHECK(s->health == DEFAULT_HEALTH, "a medikit heals");
    CHECK(g->world.things[kit].style == THING_MEDICAL_KIT && vec2_length(vec2_sub(g->world.things[kit].pos[0], was)) > 1.0f,
          "and comes up again at another of its spawn points");
    scene_free(g);
}

static void capture(void)
{
    Game *g = scene("ctf_Ash", 200, WEAPON_AK74, WEAPON_AK74);
    settle(g);
    int bravo = find_thing(g, THING_BRAVO_FLAG), alpha = find_thing(g, THING_ALPHA_FLAG);
    Soldier *s = &g->world.soldiers[0];
    place(s, g->world.things[bravo].pos[0]);
    run(g, 2, press_nothing);
    CHECK(g->world.things[bravo].holder == 1 && s->held == bravo + 1, "alpha grabs the bravo flag");
    place(s, g->world.things[alpha].pos[0]);
    run(g, 2, press_nothing);
    CHECK(g->match.scores[TEAM_ALPHA] == 1 && s->flags == 1, "and captures it at its own");
    CHECK(g->world.things[bravo].holder == 0 && s->held == 0, "which sends it home");
    scene_free(g);
}

static void parachute(void)
{
    Game *g = scene("ctf_Ash", 200, WEAPON_AK74, WEAPON_AK74);
    settle(g);
    Soldier *s = &g->world.soldiers[0];
    place(s, open_sky(g, 0));
    s->cease_fire_counter = DEFAULT_CEASE_FIRE;
    parachute_deploy(&g->ctx, &g->world, 0);
    CHECK(s->held && g->world.things[s->held - 1].style == THING_PARACHUTE, "a soldier high up gets a parachute");
    float y = s->pos.y;
    run(g, 30, press_nothing);
    CHECK(s->pos.y - y < 20.0f, "and floats down (%.1f in 30 ticks)", s->pos.y - y);
    run(g, 1500, press_nothing);
    CHECK(!s->held, "and lets it go on the ground");
    scene_free(g);
}

static void knife_lands(void)
{
    Game *g = scene("ctf_Ash", 150, WEAPON_KNIFE, WEAPON_AK74);
    settle(g);
    run(g, 200, hold_drop);
    int knife = find_thing(g, THING_WEAPON);
    CHECK(knife >= 0 && g->world.things[knife].weapon == WEAPON_KNIFE, "a thrown knife lands as a knife to pick up");
    scene_free(g);
}

static void stationary_gun(void)
{
    Game *g = scene("ctf_Ash", 300, WEAPON_AK74, WEAPON_AK74);
    settle(g);
    Soldier *s = &g->world.soldiers[0];
    int gun = thing_create(&g->ctx, &g->world, THING_STAT_GUN, vec2(s->pos.x + 5.0f, s->pos.y - 10.0f), WEAPON_NONE, 0, -1);
    run(g, 90, press_nothing);
    CHECK(s->stat == gun + 1, "a standing soldier by a stationary gun mans it");
    Tally t = run(g, 60, press_fire);
    CHECK(t.spawned[WEAPON_M2] == 6 && t.spawned[WEAPON_AK74] == 0, "and fires it on its beat instead of its own gun (%d)",
          t.spawned[WEAPON_M2]);
    run(g, 2, tap_jump);
    CHECK(s->stat == 0, "and leaves it with a jump");
    run(g, 120, press_nothing);
    CHECK(s->stat == gun + 1, "and standing by it again mans it again");
    scene_free(g);
}

void thing_tests(void)
{
    round_start();
    dropped_gun();
    medikit();
    capture();
    parachute();
    knife_lands();
    stationary_gun();
}
