// Bink (hit_spray): a hit disturbs the aim once, whichever of its two words comes first,
// the bullet flown here or the server's damage; one the bullet here missed binks all
// the same; it goes with the life, and with the gun put away.

#include "test.h"

static Buttons press_change(int tick)
{
    (void)tick;
    return BUTTON_CHANGE;
}

void bink_tests(void)
{
    // soldier 0 holds the Barrett, which binks its holder (65); soldier 1 shoots it
    Game *g = scene("Arena", 100.0f, WEAPON_BARRETT, WEAPON_AK74);
    settle(g);
    World *w = &g->world;
    Soldier *me = &w->soldiers[0];
    uint16_t one = calculate_bink(0, g->ctx.weapons.info[WEAPON_BARRETT].stats.bink);
    CHECK(one > 0, "the Barrett binks its holder (%u)", one);

    me->hit_spray = 0;
    hit_spray(&g->ctx, w, 0, 1, BINK_FLOWN);
    hit_spray(&g->ctx, w, 0, 1, BINK_TOLD);
    CHECK(me->hit_spray == one, "flown, then told: one bink (%u, %u)", me->hit_spray, one);

    w->tick += 100; // long after
    me->hit_spray = 0;
    hit_spray(&g->ctx, w, 0, 1, BINK_TOLD);
    CHECK(me->hit_spray == one, "told first: the bink comes with the server's word, flown or not (%u)", me->hit_spray);
    w->tick += 3;
    hit_spray(&g->ctx, w, 0, 1, BINK_FLOWN);
    CHECK(me->hit_spray == one, "and the bullet flown here after it adds nothing (%u)", me->hit_spray);

    w->tick += 100;
    me->hit_spray = 0;
    hit_spray(&g->ctx, w, 0, 1, BINK_FLOWN);
    hit_spray(&g->ctx, w, 0, 1, BINK_FLOWN);
    hit_spray(&g->ctx, w, 0, 1, BINK_TOLD);
    hit_spray(&g->ctx, w, 0, 1, BINK_TOLD);
    CHECK(me->hit_spray == calculate_bink(one, g->ctx.weapons.info[WEAPON_BARRETT].stats.bink),
          "two bullets, each flown and told: two binks (%u)", me->hit_spray);

    w->tick += 100;
    me->hit_spray = 0;
    hit_spray(&g->ctx, w, 0, 1, BINK_FLOWN);
    w->tick += BINK_MATCH_TICKS + 1;
    hit_spray(&g->ctx, w, 0, 1, BINK_TOLD);
    CHECK(me->hit_spray == calculate_bink(one, g->ctx.weapons.info[WEAPON_BARRETT].stats.bink),
          "a word that comes too late is a hit of its own (%u)", me->hit_spray);

    CHECK(weapon_binks(WEAPON_AK74) && weapon_binks(WEAPON_FRAG) && !weapon_binks(WEAPON_FLAMER) && !weapon_binks(WEAPON_NONE),
          "a bullet's or a blast's wound binks, a flame's or a fall's does not");

    // the gun put away takes its bink with it
    w->tick += 100;
    me->hit_spray = 200;
    WeaponId held = me->weapon.id;
    int ticks = 0;
    while (me->weapon.id == held && ticks < 120) {
        run(g, 1, press_change);
        ticks++;
    }
    CHECK(me->weapon.id != held && ticks < 120, "the change swaps the guns (%d ticks)", ticks);
    CHECK(me->hit_spray == 0, "and the other comes up steady (%u)", me->hit_spray);

    // the dead have none, and gain none
    me->hit_spray = 200;
    me->dead = true;
    me->respawn_counter = 1000; // and staying dead a while
    run(g, 1, press_nothing);
    CHECK(me->hit_spray == 0, "a death takes the bink with it (%u)", me->hit_spray);
    hit_spray(&g->ctx, w, 0, 1, BINK_TOLD);
    CHECK(me->hit_spray == 0, "and a corpse is binked no more");
    scene_free(g);
}
