// A shot's end, the server's word on it (EventShotEnd): a grenade going off on the
// server says where, for the clients; a client's own grenade, flying elsewhere, goes off
// where the server's did when the word comes, and once only.

#include "network/wire.h"
#include "test.h"

static const Event *find(const Events *events, EventType type)
{
    for (int i = 0; i < events->count; i++)
        if (events->items[i].type == type) return &events->items[i];
    return NULL;
}

void shot_end_tests(void)
{
    // the server: a grenade goes off, and says where
    Game *g = scene("Arena", 100.0f, WEAPON_AK74, WEAPON_AK74);
    Vec2 sky = open_sky(g, 0);
    int k = bullet_spawn(&g->ctx, &g->world, sky, vec2(0, 0), WEAPON_FRAG, 0, 1.0f, &g->events);
    CHECK(k >= 0, "a grenade is made");
    if (k < 0) return;
    uint32_t shot = g->world.bullets[k].shot_id;
    explode(&g->ctx, &g->world, &g->world.bullets[k], (uint16_t)k, EXPLOSION_FRAG, -1, -1, &g->events);
    const Event *told = find(&g->events, EVENT_SHOT_END);
    CHECK(told && told->shot_end.owner == 0 && told->shot_end.shot == shot && told->shot_end.blast == EXPLOSION_FRAG + 1 &&
              told->shot_end.pos.x == sky.x && told->shot_end.pos.y == sky.y,
          "the server says where the grenade went off, and as what");
    CHECK(wire_side(EVENT_SHOT_END) == WIRE_SERVER, "and the word goes to the clients");
    scene_free(g);

    // a client: its own flight of the grenade elsewhere; the word puts it there and it goes off
    Game *c = scene("Arena", 100.0f, WEAPON_AK74, WEAPON_AK74);
    c->world.authority = false;
    Vec2 here = open_sky(c, 0), there = vec2(here.x + 150.0f, here.y);
    k = bullet_spawn(&c->ctx, &c->world, here, vec2(0, 0), WEAPON_FRAG, 0, 1.0f, &c->events);
    if (k < 0) return;
    shot = c->world.bullets[k].shot_id;
    Event word = {.type = EVENT_SHOT_END, .tick = 1, .shot_end = {.owner = 0, .shot = shot, .weapon = WEAPON_FRAG, .pos = there, .blast = EXPLOSION_FRAG + 1}};
    game_hear(c, word);
    Command cmds[MAX_PLAYERS] = {0};
    game_tick(c, cmds);
    const Event *blast = find(&c->events, EVENT_EXPLOSION);
    CHECK(!c->world.bullets[k].active, "the client's grenade is gone when the word comes");
    CHECK(blast && blast->explosion.pos.x == there.x && blast->explosion.pos.y == there.y,
          "it went off where the server's did, not where it flew here");
    game_hear(c, word); // heard again: it is gone, and nothing goes off twice
    game_tick(c, cmds);
    CHECK(!find(&c->events, EVENT_EXPLOSION), "a word heard again sets nothing off");
    CHECK(!find(&c->events, EVENT_SHOT_END) || find(&c->events, EVENT_SHOT_END)->tick != 0, "and a client tells nobody");
    scene_free(c);
}
