// The corpses: falling, coming apart, blown about, shot, under a parachute; on ctf_Ash.

#include "test.h"

// A wound straight from `shooter`, applied at once.
static void wound(Game *g, int target, int shooter, float amount, uint8_t part)
{
    Hit hit = {.shooter = (uint8_t)shooter, .target = (uint8_t)target, .amount = amount, .part = part, .pos = g->world.soldiers[target].pos};
    damage_apply(&g->ctx, &g->world, hit, &g->events);
}

static int cuts(const Ragdoll *r)
{
    int n = 0;
    for (uint32_t torn = r->torn; torn; torn &= torn - 1) n++;
    return n;
}

static void falls_and_rests(void)
{
    Game *g = scene("ctf_Ash", 200, WEAPON_AK74, WEAPON_AK74);
    settle(g);
    Soldier *s = &g->world.soldiers[1];
    const Ragdoll *r = &g->world.ragdolls[1];
    s->vel = vec2(3.0f, -2.0f);
    wound(g, 1, 0, 200.0f, 9);
    run(g, 1, press_nothing);
    CHECK(s->dead && r->active, "a dead soldier becomes a body");

    run(g, 130, press_nothing);
    Vec2 head = r->pos[11];
    run(g, 20, press_nothing);
    // a body at rest still trembles a few tenths of a pixel: the map and the
    // constraints nudge each other every tick
    CHECK(vec2_length(vec2_sub(r->pos[11], head)) < 1.0f, "which falls and comes to rest");
    CHECK(vec2_length(vec2_sub(s->pos, r->pos[11])) < 0.001f, "the soldier is where its head is");
    CHECK(r->torn == 0, "a plain death leaves the body whole");

    run(g, 40, press_nothing);
    CHECK(!s->dead && !r->active, "and it is placed again after the respawn time");
    scene_free(g);
}

static void comes_apart(void)
{
    Game *g = scene("ctf_Ash", 200, WEAPON_AK74, WEAPON_AK74);
    settle(g);
    wound(g, 1, 0, DEFAULT_HEALTH + 100.0f, 12); // to -100, by the head
    run(g, 1, press_nothing);
    CHECK(g->world.ragdolls[1].torn == 1u << 19, "a head-chop cuts the neck and nothing else");
    wound(g, 1, 0, 400.0f, 5);
    run(g, 1, press_nothing);
    CHECK(cuts(&g->world.ragdolls[1]) == 5, "and a body shot to pieces comes apart");
    scene_free(g);

    g = scene("ctf_Ash", 200, WEAPON_AK74, WEAPON_AK74);
    settle(g);
    g->world.soldiers[0].bonus = BONUS_BERSERKER;
    wound(g, 1, 0, 40.0f, 9); // fourfold: a plain death, but for the berserker
    run(g, 1, press_nothing);
    CHECK(g->world.soldiers[1].dead && cuts(&g->world.ragdolls[1]) == 5, "a berserker's kill tears the body apart");
    scene_free(g);
}

static void blown_about(void)
{
    Game *g = scene("ctf_Ash", 60, WEAPON_AK74, WEAPON_AK74);
    settle(g);
    wound(g, 1, 0, 200.0f, 9);
    run(g, 60, press_nothing);
    const Ragdoll *r = &g->world.ragdolls[1];
    Vec2 head = r->pos[11];
    // a grenade on its last tick beside the head
    g->world.bullets[0] = (Bullet){.active = true, .style = BULLET_FRAG_GRENADE, .weapon = WEAPON_FRAG, .timeout = 1, .hit_body = -1,
                                   .pos = vec2(head.x - 10.0f, head.y + 5.0f)};
    g->world.bullets[0].old_pos = g->world.bullets[0].pos;
    run(g, 10, press_nothing);
    CHECK(vec2_length(vec2_sub(r->pos[11], head)) > 2.0f, "a blast throws a body");
    CHECK(g->world.soldiers[1].health < -50.0f, "and wounds it");
    scene_free(g);
}

static void shot(void)
{
    Game *g = scene("ctf_Ash", 100, WEAPON_AK74, WEAPON_AK74);
    settle(g);
    wound(g, 1, 0, 200.0f, 9);
    run(g, 90, press_nothing);
    Vec2 chest = g->world.ragdolls[1].pos[9];
    Command cmds[MAX_PLAYERS] = {0};
    float health = g->world.soldiers[1].health;
    for (int t = 0; t < 60; t++) {
        cmds[0] = (Command){.seq = g->world.tick + 1, .buttons = BUTTON_FIRE, .aim = chest};
        game_tick(g, cmds);
    }
    CHECK(g->world.soldiers[1].health < health, "a body can be shot");
    scene_free(g);
}

static void parachute(void)
{
    Game *g = scene("ctf_Ash", 200, WEAPON_AK74, WEAPON_AK74);
    settle(g);
    Soldier *s = &g->world.soldiers[1];
    place(s, open_sky(g, 1));
    parachute_deploy(&g->ctx, &g->world, 1);
    int para = s->held - 1;
    run(g, 20, press_nothing);
    wound(g, 1, 0, 200.0f, 9);
    run(g, 30, press_nothing);
    CHECK(para >= 0 && g->world.things[para].holder == 2 && s->held == para + 1, "a body keeps its parachute");
    scene_free(g);
}

static void determinism(void)
{
    Game *a = scene("ctf_Ash", 60, WEAPON_M79, WEAPON_AK74), *b = scene("ctf_Ash", 60, WEAPON_M79, WEAPON_AK74);
    settle(a);
    settle(b);
    run(a, 400, press_fire);
    run(b, 400, press_fire);
    CHECK(same_world(&a->world, &b->world), "the things and the corpses end the same on two worlds");
    scene_free(a);
    scene_free(b);
}

void corpse_tests(void)
{
    falls_and_rests();
    comes_apart();
    blown_about();
    shot();
    parachute();
    determinism();
}
